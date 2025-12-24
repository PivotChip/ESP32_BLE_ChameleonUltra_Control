#include "BlePairing.h"

Preferences preferences;
NimBLEAddress storedAddress; 
bool hasStoredAddress = false;
static String targetNameCache = ""; 

static void scanCompleteCB(NimBLEScanResults results) {
  (void)results;
  if (currentState == ST_SCANNING) {
    logOutput("--- Scan Timeout ---");
    currentState = ST_IDLE;
  } 
  else if (currentState == ST_RESCAN_TARGET) {
    if (targetDevice == nullptr) {
       logOutput("--- Auto-scan timeout: Device not found ---");
       currentState = ST_IDLE;
    } else {
       currentState = ST_IDLE; 
    }
  }
}

class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    String name = advertisedDevice->haveName() ? advertisedDevice->getName().c_str() : "";
    String addrStr = advertisedDevice->getAddress().toString().c_str();
    bool connectable = advertisedDevice->isConnectable();
    int rssi = advertisedDevice->getRSSI();

    if (currentState == ST_SCANNING) {
      if (!connectable) return; 

      if (hasStoredAddress && advertisedDevice->getAddress() == storedAddress) {
        logOutput(" *** FOUND SAVED DEVICE: " + addrStr + " (RSSI: " + String(rssi) + ") ***");
        logOutput(" -> Auto-connecting to paired device...");
        
        if (targetDevice) delete targetDevice;
        targetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        
        NimBLEDevice::getScan()->stop();
        currentState = ST_CONNECT_ATTEMPT;
        stateTimer = millis();
        return;
      }

      bool match = (name.indexOf("Chameleon") >= 0) || (name.indexOf("Ultra") >= 0) ||
                   (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID));

      if (match) {
        logOutput(" *** FOUND: " + (name.isEmpty() ? String("Device") : name) + 
                  " [" + addrStr + "] (RSSI: " + String(rssi) + ") ***");

        if (targetDevice) delete targetDevice;
        targetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
        targetNameCache = name; 
        
        NimBLEDevice::getScan()->stop();
        currentState = ST_IDLE;
        logOutput(" -> Ready. Type 'pair' to connect.");
      }
    }
    else if (currentState == ST_RESCAN_TARGET) {
      bool match = false;
      if (hasStoredAddress && advertisedDevice->getAddress() == storedAddress) match = true;
      else if (targetNameCache.length() > 0 && name.indexOf(targetNameCache) >= 0) match = true;
      else if (advertisedDevice->haveServiceUUID() && advertisedDevice->isAdvertisingService(serviceUUID)) match = true;

      if (match) {
        logOutput(" *** TARGET RE-ACQUIRED: " + addrStr + " (RSSI: " + String(rssi) + ") ***");
        
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
    logOutput(" -> [CB] Connected.");
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
    logOutput(" -> [SEC] Passkey Requested. Injecting: 123456");
    NimBLEDevice::injectPassKey(connInfo, 123456);
  }

  void onConfirmPasskey(NimBLEConnInfo& connInfo, uint32_t pass_key) override {
    logOutput(" -> [SEC] Confirm Passkey: " + String(pass_key));
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

void initBLE() {
  preferences.begin("chameleon", false);
  String savedStr = preferences.getString("bonded_addr", "");
  if (savedStr.length() > 0) {
    uint8_t savedType = preferences.getUChar("bonded_type", 1); 
    storedAddress = NimBLEAddress(savedStr.c_str(), savedType);
    hasStoredAddress = true;
    logOutput("Boot: Found saved paired device [" + savedStr + "]");
  } else {
    logOutput("Boot: No saved paired device found.");
  }
  preferences.end();

  NimBLEDevice::init("ESP32_Chameleon");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 

  // JUST WORKS: Bond=True, MITM=False, SC=True
  NimBLEDevice::setSecurityAuth(true, false, true); 
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(&clientCallbacks, false);
  pClient->setConnectTimeout(20);
  pClient->setConnectionParams(100, 200, 0, 800);
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
  scan->start(10000, scanCompleteCB, false);
}

void startPair() {
  if (!targetDevice && !hasStoredAddress) { 
    logOutput("Error: Run 'discover' first or no saved device."); 
    return; 
  }
  logOutput("--- Starting Pair/Connect Sequence ---");
  retryCount = 0;
  triggerReScan();
}