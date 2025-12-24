/*
 * Chameleon Ultra - ASYNC STATE MACHINE (Modularized v172.0 - Payload Fix)
 *
 * FIXES:
 * - COMMAND: setDeviceMode now sends 1 byte payload.
 * - This fixes 0x60 status error.
 * - Enabies mode switching -> Enables Scanning.
 */

#include "Shared.h"
#include "BlePairing.h"
#include "BleComm.h"

// --- DEFINE MAIN GLOBALS ---
NimBLEClient* pClient = nullptr;
NimBLEAdvertisedDevice* targetDevice = nullptr;
volatile AppState currentState = ST_IDLE;
unsigned long stateTimer = 0;
int retryCount = 0;
const int MAX_RETRIES = 15;
volatile bool authInProgress = false; 
volatile unsigned long lastSecurityTime = 0;
// --- UTILITIES ---

void logOutput(const String& msg, bool debug_bypass) { 
  if (debug_bypass == false || DEBUG_MODE) {
    Serial.print("[");
    Serial.print(millis());
    Serial.print("] ");
    Serial.println(msg);
  }
}

// --- MAIN LOOP & COMMANDS ---

void processCommand(String cmd) {
  cmd.trim();
  if (cmd == "discover") {
    startScan();
  } else if (cmd == "pair") {
    startPair();
  } else if (cmd == "forget") {
    clearPairedDevice();
    NimBLEDevice::deleteAllBonds();
  } else if (cmd == "clear bonds") { 
    clearChameleonBonds();
  } else if (cmd.startsWith("pin_enable ")) {
    String pinStr = cmd.substring(11);
    uint32_t pin = pinStr.toInt();
    if (pinStr.length() != 6 || pin < 0 || pin > 999999) {
        logOutput("Error: PIN must be exactly 6 digits (000000-999999).");
    } else {
        logOutput("Configuring PIN: " + String(pin) + " and Enabling Security...");
        
        // 1. Save to NVS & Global State
        savePinConfig(pin, true);
        
        // 2. Configure Chameleon (if connected)
        if (pClient && pClient->isConnected() && currentState == ST_READY) {
            logOutput(" -> Device Connected. Syncing settings...");
            setChameleonPIN(pin);
            delay(200);
            enableChameleonPairing(true);
            delay(200);
            
            // 3. Save Settings to Flash
            saveSettings(); 
            // FIX: Increased delay to 2000ms. Flash writes can pause the CPU on the target.
            delay(2000); 

            // Clear Chameleon's existing bonds disabled, it messes with settings
            //clearChameleonBonds();
            // FIX: Increased delay to 1500ms to ensure packet transmission before disconnect.
            //delay(1500);
            
            //logOutput(" -> Settings Synced & Bonds Cleared. Disconnecting...");
            
            // 5. Clean up local state
            //clearPairedDevice();          
            //NimBLEDevice::deleteAllBonds(); 
            //pClient->disconnect();        
        } else {
            logOutput(" -> Device NOT Connected. Settings saved to NVS.");
            logOutput(" -> Please pair normally. Security will be applied on next connect.");
            updateSecuritySettings(); // Apply to stack now
        }
    }

  } else if (cmd.startsWith("send ")) {
    sendText(cmd.substring(5));
  } else if (cmd == "scan hf") {
    logOutput("Command: Scan High Frequency");
    sendText("hf search\r\n");
  } else if (cmd == "scan lf") {
    logOutput("Command: Scan Low Frequency");
    sendText("lf search\r\n");
  }  else if (cmd == "scan") {
    logOutput("Discovering the device type");
    logOutput("testing low frequency");
    sendText("lf search\r\n");
    delay(2000); 
    logOutput("testing high frequency");
    sendText("hf search\r\n"); 
  } else if (cmd == "info") {
    logOutput("Command: Get Device Info");
    sendText("info");
  } else if (cmd == "mode reader") {
    sendText("mode reader");
  } else if (cmd == "drop") {
    logOutput("Dropping.");
    if (pClient) pClient->disconnect();
    if (targetDevice) { delete targetDevice;
    targetDevice = nullptr; }
    currentState = ST_IDLE;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  initBLE();
  logOutput("Ready. Commands: discover | pair | forget | scan | scan hf | scan lf | info | mode reader | drop | pin_enable 123456 | clear bonds");
  
  
  if (hasStoredAddress && DEBUG_MODE) {
    logOutput("Boot: Triggering auto-connect scan...", true);
    triggerReScan();
  }
}

void loop() {
  if (Serial.available()) processCommand(Serial.readStringUntil('\n'));

  switch (currentState) {
    
    case ST_CONNECT_ATTEMPT: {
      logOutput("Step 2: Connect Attempt " + String(retryCount + 1), true);
      if (targetDevice == nullptr) {
        logOutput(" -> Error: Target device lost or not found during scan.");
        currentState = ST_IDLE;
        break;
      }

      NimBLEScan* scan = NimBLEDevice::getScan();
      scan->stop();
      unsigned long stopWait = millis();
      while(scan->isScanning() && (millis() - stopWait < 2000)) { delay(10); }

      logOutput("     Debug: Waiting 250ms for radio idle...", true);
      delay(250); 

      if (pClient->isConnected()) {
        logOutput("     Debug: Client reports ALREADY CONNECTED.", true);
        logOutput(" -> Connect Accepted (Pre-existing). Settling...");
        currentState = ST_CONNECTED_PENDING;
        stateTimer = millis();
        break;
      }

      String addr = targetDevice->getAddress().toString().c_str();
      logOutput("     Debug: Connecting to " + addr + " (Object Pointer)...", true);
      // Pointer-based Connect
      bool connected = pClient->connect(targetDevice, false);
      if (!connected && pClient->isConnected()) {
         logOutput("     WARNING: Connect returned false, but Link is UP.", true);
         connected = true;
      }

      if (connected) {
        logOutput(" -> Connect Accepted. Settling...", true);
        currentState = ST_CONNECTED_PENDING;
        stateTimer = millis();
      } else {
        logOutput(" -> Connect Failed. Error: " + String(pClient->getLastError()), true);
        currentState = ST_CONNECT_COOLDOWN;
        stateTimer = millis();
      }
      break;
    }

    case ST_CONNECT_COOLDOWN: {
      if (millis() - stateTimer >= 2000) {
        retryCount++;
        if (retryCount < MAX_RETRIES) {
          logOutput(" -> Retrying...", true);
          triggerReScan();
        } else {
          logOutput(" -> All Retries Failed.");
          NimBLEDevice::getScan()->clearResults();
          currentState = ST_IDLE;
        }
      }
      break;
    }

    case ST_CONNECTED_PENDING: {
      if (millis() - stateTimer >= 3000) { 
        if (pClient && pClient->isConnected()) {
          
          if (pClient->getConnInfo().isEncrypted()) {
             logOutput(" -> Security Auto-Established (Bonded).");
             currentState = ST_SECURITY_SETTLE; 
             stateTimer = millis();
          } 
          else {
             logOutput("Step 3: Moving to Discovery (Lazy Security)...", true);
             currentState = ST_DISCOVERING;
             stateTimer = millis();
          }
        } else {
          logOutput(" -> Link Lost while Pending.", true);
          currentState = ST_CONNECT_COOLDOWN;
          stateTimer = millis();
        }
      }
      break;
    }

    case ST_SECURING: {
      if (authInProgress) {
         if (millis() - stateTimer > 20000) {
            logOutput(" -> Security Timeout (Callback missing).", true);
            currentState = ST_READY; 
         }
         break;
      }

      if (pClient->isConnected() && pClient->getConnInfo().isEncrypted()) {
         logOutput(" -> Encryption Verified. Saving Pairing...");
         savePairedDevice(pClient->getPeerAddress());
         currentState = ST_READY; 
         break; 
      }
      break;
    }

    case ST_SECURITY_SETTLE: {
        if (millis() - stateTimer < 3000) return;
        logOutput(" -> Security Settled. Moving to Discovery.", true);
        currentState = ST_DISCOVERING;
        stateTimer = millis();
        break;
    }

    case ST_DISCOVERING: {
      if (!pClient || !pClient->isConnected()) {
        currentState = ST_CONNECT_COOLDOWN;
        stateTimer = millis();
        break;
      }

      if (setupService()) {
        logOutput("Step 5: Enabling Notifications...", true);
        logOutput("     Debug: Waiting 1000ms before Subscribe...", true);
        retryCount = 0; 
        currentState = ST_SUBSCRIBING;
        stateTimer = millis();
        authInProgress = false;
        lastSecurityTime = millis(); 
      } else {
        pClient->disconnect();
        currentState = ST_CONNECT_COOLDOWN;
      }
      break;
    }

    case ST_SUBSCRIBING: {
      if (millis() - stateTimer < 2000) return;
      if (millis() - lastSecurityTime < 3000) {
        return;
      }

      stateTimer = millis();
      retryCount++;
      
      logOutput(" -> Attempting Subscribe (" + String(retryCount) + ")...", true);
      bool subOk = false;
      
      bool result = enableNotifications(subOk);
      
      if (result && subOk) {
        logOutput(" -> Notifications ENABLED. Comm Link Open.", true);
        
        // --- FIX: Save Pairing on ANY Success ---
        // This ensures "Just Works" connections are also saved to NVS.
        savePairedDevice(pClient->getPeerAddress());
        // ----------------------------------------

        currentState = ST_READY;
        stateTimer = millis();
        retryCount = 0;
         
        NimBLEDevice::getScan()->clearResults();
         
        if (DEBUG_MODE){
          logOutput(" -> Auto-testing INFO command...", true);
          sendText("info");
          delay(500);
        }
        
        logOutput(" -> Auto-switching to READER mode...", true);
        sendText("mode reader");
        logOutput("Connected...");
 
      } else {
         logOutput(" -> Subscribe failed.", true);
         if (retryCount >= MAX_RETRIES) {
           logOutput(" -> CRITICAL: Subscribe failed after max attempts. Resetting.");
           pClient->disconnect();
           currentState = ST_CONNECT_COOLDOWN;
         }
      }
      break;
    }

    case ST_READY: {
      if (!pClient || !pClient->isConnected()) {
        logOutput(" -> Link dropped.",true);
        currentState = ST_IDLE;
      }
      break;
    }

    case ST_SCANNING:
    case ST_RESCAN_TARGET:
    case ST_IDLE:
    default:
      break;
  }
}