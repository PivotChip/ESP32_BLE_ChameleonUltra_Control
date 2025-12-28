/*
 * Chameleon Ultra - Bluetooth BLE control from ESP32 device
 * by PivotChip Security
 */

#include "BlePairing.h"

Preferences preferences;
NimBLEAddress storedAddress; 
bool hasStoredAddress = false;
static String targetNameCache = ""; 

// --- PIN GLOBALS ---
uint32_t userBLEPin = 123456;
bool pinPairingEnabled = false;

static void scanCompleteCB(NimBLEScanResults results) {
  logOutput("\nID | RSSI | Con | Address           | Name");
  for (int i = 0; i < results.getCount(); i++) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    // Mark Connectable status: [ ] = Yes, [X] = No
    const char* connStr = d->isConnectable() ? "[ ]" : "[X]";
    char line[128];
    snprintf(line, sizeof(line), "%2d | %4d | %s | %s | %s", 
              i, d->getRSSI(), connStr, d->getAddress().toString().c_str(), d->getName().c_str());
    logOutput(String(line));
  }
  logOutput("--- Scan Complete ---");
  currentState = ST_IDLE;
}

class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    String name = advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : "Unknown";
    String addrStr = advertisedDevice->getAddress().toString().c_str();
    bool connectable = advertisedDevice->isConnectable();
    int rssi = advertisedDevice->getRSSI();

    if (currentState == ST_SCANNING) {
      // List ALL connectable devices
      //if (!connectable) return; 

      // Calculate Index (Count includes this new one)
      //int index = NimBLEDevice::getScan()->getResults().getCount() - 1;

      // Check for Chameleon characteristics for highlighting
      //bool isChameleon = (name.indexOf("Chameleon") >= 0) || (name.indexOf("Ultra") >= 0) ||
      //             (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID));
      
      //String prefix = isChameleon ? "[-->] " : "[   ] ";
      //String saved = (hasStoredAddress && advertisedDevice->getAddress() == storedAddress) ? " [SAVED]" : "";

      //logOutput(prefix + String(index) + ": " + name + " (" + addrStr + ") " + String(rssi) + "dBm" + saved);
    } else if (currentState == ST_RESCAN_TARGET) {
      bool match = false;
      if (hasStoredAddress && advertisedDevice->getAddress() == storedAddress) match = true;
      else if (targetNameCache.length() > 0 && name.indexOf(targetNameCache) >= 0) match = true;
      else if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) match = true;

      if (match) {
        logOutput(" *** TARGET RE-ACQUIRED: " + addrStr + " (RSSI: " + String(rssi) + ") ***", true);
        
        if (!connectable) {
           logOutput("     WARNING: Target says NOT CONNECTABLE. Ignoring.");
           return; 
        }

        if (targetDevice) delete targetDevice;
        targetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        
        NimBLEDevice::getScan()->stop();
        currentState = ST_CONNECT_ATTEMPT;
        stateTimer = millis(); 
      }
    }
  }
};

class MyClientCallback : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pclient) override {
    logOutput(" -> [CB] Connected.", true);
  }

  void onDisconnect(NimBLEClient* pclient, int reason) override {
    logOutput(" -> [CB] Disconnected. Reason: " + String(reason));
    authInProgress = false;
    
    if (currentState >= ST_CONNECTED_PENDING) {
        currentState = ST_CONNECT_COOLDOWN; 
        stateTimer = millis();
        retryCount = 0; 
    } else {
        currentState = ST_IDLE;
    }
  }

  void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
    logOutput(" -> [SEC] PIN Requested. Injecting PIN: " + String(userBLEPin), true);
    NimBLEDevice::injectPassKey(connInfo, userBLEPin);
  }

  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pass_key) override {
    logOutput(" -> [SEC] Confirm Passkey: " + String(pass_key), true);
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    authInProgress = false; 
    lastSecurityTime = millis();
    if (connInfo.isEncrypted()) {
      logOutput(" -> [SEC] Encrypted/Bonded!");
    } else {
      logOutput(" -> [SEC] Auth Failed.");
      logOutput(" -> Clearing local bonds to recover...");
      NimBLEDevice::deleteAllBonds();
      clearPairedDevice();
      pClient->disconnect();
    }
  }
};

static MyScanCallbacks scanCallbacks;
static MyClientCallback clientCallbacks;

void savePairedDevice(const NimBLEAddress& addr) {
  if (!hasStoredAddress || storedAddress != addr) {
    storedAddress = addr;
    hasStoredAddress = true;
    
    preferences.begin("chameleon", false);
    preferences.putString("bonded_addr", String(addr.toString().c_str()));
    preferences.putUChar("bonded_type", addr.getType());
    preferences.end();
    
    logOutput(" -> [NVS] Paired device saved: " + String(addr.toString().c_str()));
  }
}

void clearPairedDevice() {
  if (hasStoredAddress) {
    hasStoredAddress = false;
    storedAddress = NimBLEAddress();
    
    preferences.begin("chameleon", false);
    preferences.remove("bonded_addr");
    preferences.remove("bonded_type");
    preferences.end();
    
    logOutput(" -> [NVS] Paired device forgotten.");
  } else {
    logOutput(" -> No saved device to forget.");
  }
}

// --- NEW PIN MANAGEMENT ---
void savePinConfig(uint32_t pin, bool enable) {
    preferences.begin("chameleon", false);
    preferences.putUInt("ble_pin", pin);
    preferences.putBool("pin_mode", enable);
    preferences.end();
    
    userBLEPin = pin;
    pinPairingEnabled = enable;
    
    logOutput(" -> [NVS] PIN Config Saved: " + String(pin) + " (Enabled: " + String(enable) + ")");
}

void updateSecuritySettings() {
    // Dynamic Reconfiguration of Security
    if (pinPairingEnabled) {
        logOutput(" -> Security Mode: PIN (MITM + Keyboard Only)");
        NimBLEDevice::setSecurityAuth(true, true, true); 
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);
    } else {
        logOutput(" -> Security Mode: Just Works (No MITM + No I/O)");
        NimBLEDevice::setSecurityAuth(true, false, true); 
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    }
}

void initBLE() {
  preferences.begin("chameleon", false);
  
  // Load Address
  String savedStr = preferences.getString("bonded_addr", "");
  if (savedStr.length() > 0) {
    uint8_t savedType = preferences.getUChar("bonded_type", 1); 
    storedAddress = NimBLEAddress(savedStr.c_str(), savedType);
    hasStoredAddress = true;
    logOutput("Boot: Found saved paired device [" + savedStr + "]", true);
  } else {
    logOutput("Boot: No saved paired device found.", true);
  }
  
  // Load PIN Settings
  userBLEPin = preferences.getUInt("ble_pin", 123456);
  pinPairingEnabled = preferences.getBool("pin_mode", false);
  
  preferences.end();

  NimBLEDevice::init("ESP32_Chameleon");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 

  // Apply Security Logic
  updateSecuritySettings();

  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallbacks, false);
  pClient->setConnectTimeout(20);
  pClient->setConnectionParams(100, 200, 0, 800);
  
  if (pinPairingEnabled) logOutput("Boot: PIN Pairing ENABLED [" + String(userBLEPin) + "]", true);
  else logOutput("Boot: PIN Pairing DISABLED (Just Works)", true);
}

void triggerReScan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  scan->setScanCallbacks(&scanCallbacks, false); 
  
  // Passive Scan
  scan->setActiveScan(false); 
  scan->setInterval(80); 
  scan->setWindow(40);   
  
  currentState = ST_RESCAN_TARGET;
  scan->start(5000, scanCompleteCB, false);
}

void startScan() {
  logOutput("Scanning...");
  if (targetDevice) { delete targetDevice; targetDevice = nullptr; targetNameCache = ""; }

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->stop();
  scan->clearResults();
  scan->setScanCallbacks(&scanCallbacks, false); 
  
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(100);

  currentState = ST_SCANNING;
  scan->start(10000, false);

  while (scan->isScanning()) {
      delay(100);
  }
  
  NimBLEScanResults results = scan->getResults();
  scanCompleteCB(results);
}

void startPair() {
  if (!targetDevice && !hasStoredAddress) { 
    logOutput("Error: Run 'discover' first or no saved device."); 
    return; 
  }
  logOutput("--- Starting Pair/Connect Sequence ---");
  retryCount = 0;
  triggerReScan();
  currentState = ST_RESCAN_TARGET;
}

void connectToScannedDevice(int index) {
    NimBLEScanResults results = NimBLEDevice::getScan()->getResults();
    if (index < 0 || index >= results.getCount()) {
        logOutput("Error: Invalid device index.");
        return;
    }

    const NimBLEAdvertisedDevice* dev = results.getDevice(index);
    logOutput("Selected: " + String(dev->getAddress().toString().c_str()));
    
    if (targetDevice) delete targetDevice;
    targetDevice = new NimBLEAdvertisedDevice(*dev);
    
    // Stop any ongoing scan
    NimBLEDevice::getScan()->stop();
    
    logOutput("--- Initiating Direct Connection ---");
    retryCount = 0;
    currentState = ST_CONNECT_ATTEMPT;
    stateTimer = millis();
}