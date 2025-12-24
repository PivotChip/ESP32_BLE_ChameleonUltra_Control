#include "BleComm.h"

// Define Globals for Comm
NimBLERemoteCharacteristic* pRemoteCharacteristicRX = nullptr;
NimBLERemoteCharacteristic* pRemoteCharacteristicTX = nullptr;

// Define UUIDs
NimBLEUUID serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
NimBLEUUID charUUID_RX("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
NimBLEUUID charUUID_TX("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");

// Protocol Constants
#define CHAMELEON_SOF 0x11

// Commands
#define CMD_GET_VERSION     1000
#define CMD_CHANGE_MODE     1001
#define CMD_SCAN_14443A     2000
#define CMD_SCAN_125K       3000

// Status Codes
#define STATUS_SUCCESS      0x0000
#define STATUS_GEN_ERR      0x0001 
#define STATUS_LF_OK        0x0040 
#define STATUS_LF_ERR_1     0x0041 
#define STATUS_LF_ERR_2     0x0042 
#define STATUS_HF_ERR       0x0065 
#define STATUS_MODE_ERR     0x0066 
#define STATUS_OK_CUSTOM    0x0068 

// Parser Globals
uint8_t rxBuffer[512];
uint16_t rxIndex = 0;

// Helper to format hex strings efficiently
String formatHex(const uint8_t* data, size_t len) {
    String s = "";
    s.reserve(len * 3); // Pre-allocate memory
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) s += "0";
        s += String(data[i], HEX);
        if (i < len - 1) s += " ";
    }
    return s;
}

static void notifyCB(NimBLERemoteCharacteristic* c, uint8_t* data, size_t len, bool isNotify) {
  // 1. Buffer Management
  if (rxIndex + len > 512) {
      logOutput("!! RX Buffer Overflow. Resetting.");
      rxIndex = 0;
  }
  memcpy(&rxBuffer[rxIndex], data, len);
  rxIndex += len;

  // 2. Prepare Atomic Log Message
  // We build the string first to prevent Serial interleaving from other tasks
  String logMsg = "<< [RX Raw]: " + formatHex(data, len);

  // 3. Parse Frame
  // [SOF] [LRC1] [CMD_H] [CMD_L] [STAT_H] [STAT_L] [LEN_H] [LEN_L] [LRC2] + [DATA] + [LRC3]
  if (rxIndex >= 9) {
      if (rxBuffer[0] != CHAMELEON_SOF) {
           rxIndex = 0; // Reset garbage
           return; // Abort (logMsg is discarded or we can print it if debugging low level)
      }
      
      // Header Fields (Big Endian)
      uint16_t cmd = (rxBuffer[2] << 8) | rxBuffer[3];
      uint16_t status = (rxBuffer[4] << 8) | rxBuffer[5];
      uint16_t payloadLen = (rxBuffer[6] << 8) | rxBuffer[7];
      
      // Check for full frame
      uint16_t totalExpected = 9 + payloadLen + 1;
      
      if (rxIndex >= totalExpected) {
           // We have a full frame. Append parsed info to logMsg.
           
           // Map Status Code
           String statMsg = "Unknown";
           if (status == STATUS_SUCCESS) statMsg = "Success";
           else if (status == STATUS_OK_CUSTOM) statMsg = "Success";
           else if (status == STATUS_LF_OK) statMsg = "Success";
           else if (status == STATUS_MODE_ERR) statMsg = "Mode Error (Set Reader)";
           else if (status == STATUS_HF_ERR || status == STATUS_LF_ERR_1 || status == STATUS_LF_ERR_2 || status == STATUS_GEN_ERR) {
               statMsg = "No card detected";
           }
           
           logMsg += "\n<< [RX] Cmd: " + String(cmd) + " Status: 0x" + String(status, HEX) + " (" + statMsg + ") Len: " + String(payloadLen);

           // Parse Payload if Success
           bool isSuccess = (status == STATUS_SUCCESS || status == STATUS_OK_CUSTOM || status == STATUS_LF_OK);

           if (isSuccess && payloadLen > 0) {
               uint8_t* payload = &rxBuffer[9];
               
               switch(cmd) {
                   case CMD_GET_VERSION: {
                       if (payloadLen >= 2) {
                           logMsg += "\n   -> Version: " + String(payload[0]) + "." + String(payload[1]);
                       }
                       break;
                   }
                   
                   case CMD_SCAN_14443A: {
                       // Struct: uidLen(1) + UID(...) + ATQA(2) + SAK(1)
                       if (payloadLen >= 1) {
                           uint8_t uidLen = payload[0];
                           if (uidLen == 4 || uidLen == 7 || uidLen == 10) {
                               if (payloadLen >= 1 + uidLen) {
                                   String uid = formatHex(&payload[1], uidLen);
                                   logMsg += "\n   -> HF TAG FOUND!";
                                   logMsg += "\n      UID:  " + uid;
                                   
                                   if (payloadLen >= 1 + uidLen + 2) {
                                       String atqa = formatHex(&payload[1+uidLen], 2);
                                       logMsg += "\n      ATQA: " + atqa;
                                   }
                                   if (payloadLen >= 1 + uidLen + 2 + 1) {
                                       uint8_t sak = payload[1+uidLen+2];
                                       char sakHex[10];
                                       sprintf(sakHex, "0x%02X", sak);
                                       logMsg += "\n      SAK:  " + String(sakHex);
                                   }
                               }
                           } else {
                               logMsg += "\n   -> Malformed HF Response (Invalid UID Len: " + String(uidLen) + ")";
                           }
                       }
                       break;
                   }

                   case CMD_SCAN_125K: {
                       if (payloadLen > 0) {
                           String dataStr = formatHex(payload, payloadLen);
                           logMsg += "\n   -> LF TAG FOUND!";
                           logMsg += "\n      Data: " + dataStr;
                       }
                       break;
                   }
               }
           }
           
           // Clear buffer after processing
           rxIndex = 0; 
      }
  }

  // ATOMIC OUTPUT
  logOutput(logMsg);
}

// Standard LRC: 2's complement of sum
uint8_t calcLRC(const uint8_t* data, uint16_t len) {
  uint8_t sum = 0;
  for (uint16_t i = 0; i < len; i++) {
    sum += data[i]; 
  }
  return (uint8_t)(-sum); 
}

void sendUltraCommand(uint16_t cmd, const uint8_t* payload, uint16_t payloadLen) {
  if (currentState != ST_READY) {
     if (!pClient || !pClient->isConnected() || !pRemoteCharacteristicRX) {
       logOutput("Not ready/connected.");
       return;
     }
  }

  // Frame: [SOF] [LRC1] [CMD_H] [CMD_L] [STAT_H] [STAT_L] [LEN_H] [LEN_L] [LRC2] + [DATA...] [LRC3]
  uint16_t headerSize = 9; 
  uint16_t totalLen = headerSize + payloadLen + 1; // +1 for LRC3

  uint8_t* frame = new uint8_t[totalLen];
  uint16_t status = 0x0000; 

  frame[0] = CHAMELEON_SOF; // 0x11
  frame[1] = 0xEF;          // LRC1

  // BIG ENDIAN
  frame[2] = (cmd >> 8) & 0xFF;   
  frame[3] = cmd & 0xFF;          
  
  frame[4] = (status >> 8) & 0xFF;       
  frame[5] = status & 0xFF;
  
  frame[6] = (payloadLen >> 8) & 0xFF;   
  frame[7] = payloadLen & 0xFF;

  // LRC2: Covers bytes 2..7
  frame[8] = calcLRC(&frame[2], 6);

  if (payloadLen > 0) {
    memcpy(&frame[9], payload, payloadLen);
    // LRC3: Covers DATA
    frame[totalLen - 1] = calcLRC(&frame[9], payloadLen);
  } else {
    frame[totalLen - 1] = 0x00;
  }

  bool res = pRemoteCharacteristicRX->writeValue(frame, totalLen, true);
  
  // ATOMIC OUTPUT FOR TX
  String logMsg = ">> [TX Cmd " + String(cmd) + "]: " + formatHex(frame, totalLen);
  logMsg += res ? " (OK)" : " (Fail)";
  logOutput(logMsg);

  delete[] frame;
}

void setDeviceMode(uint8_t mode) {
    logOutput("Command: Set Device Mode to " + String(mode == MODE_READER ? "READER" : "TAG"));
    uint8_t data[] = {mode}; 
    sendUltraCommand(CMD_CHANGE_MODE, data, 1);
}

void sendText(const String& s) {
  if (s.indexOf("hf search") >= 0) {
    logOutput("Mapping 'hf search' to Binary CMD_SCAN_14443A...");
    sendUltraCommand(CMD_SCAN_14443A, nullptr, 0);
    return;
  }
  if (s.indexOf("lf search") >= 0) {
    logOutput("Mapping 'lf search' to Binary CMD_SCAN_125K...");
    sendUltraCommand(CMD_SCAN_125K, nullptr, 0);
    return;
  }
  if (s.indexOf("info") >= 0) {
    logOutput("Mapping 'info' to Binary CMD_GET_VERSION...");
    sendUltraCommand(CMD_GET_VERSION, nullptr, 0);
    return;
  }
  if (s.indexOf("mode reader") >= 0) {
    setDeviceMode(MODE_READER);
    return;
  }
  if (s.indexOf("mode tag") >= 0) {
    setDeviceMode(MODE_TAG);
    return;
  }

  if (currentState != ST_READY || !pClient || !pClient->isConnected() || !pRemoteCharacteristicRX) {
    logOutput("Not ready/connected.");
    return;
  }
  pRemoteCharacteristicRX->writeValue((uint8_t*)s.c_str(), s.length(), false);
  logOutput(">> sent text (raw)");
}

bool setupService() {
  logOutput("Step 4: Discovering Services...");
  NimBLERemoteService* svc = pClient->getService(serviceUUID);
  if (!svc) {
    logOutput(" -> Service not found.");
    return false;
  }

  pRemoteCharacteristicRX = svc->getCharacteristic(charUUID_RX);
  pRemoteCharacteristicTX = svc->getCharacteristic(charUUID_TX);

  if (!pRemoteCharacteristicRX || !pRemoteCharacteristicTX) {
    logOutput(" -> RX/TX missing.");
    return false;
  }
  
  String props = "";
  if (pRemoteCharacteristicTX->canNotify()) props += "Notify ";
  logOutput(" -> TX Props: " + props);

  if (!pRemoteCharacteristicTX->canNotify()) {
      logOutput(" -> ERROR: TX char does not support Notify.");
      return false;
  }
  
  return true;
}

bool enableNotifications(bool& subOk) {
    NimBLERemoteDescriptor* pDesc = pRemoteCharacteristicTX->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (!pDesc) {
        logOutput("     Debug: Error - CCCD Descriptor not found!");
        subOk = false;
        return false; 
    }

    String mtu = String(pClient->getMTU());
    bool isEnc = pClient->getConnInfo().isEncrypted();
    bool isBond = pClient->getConnInfo().isBonded();
    logOutput("     Debug: MTU=" + mtu + " Enc=" + String(isEnc) + " Bond=" + String(isBond));

    // CHECK STATE
    std::string val = pDesc->readValue();
    uint16_t currentCCCD = 0;
    if (val.length() >= 2) {
        currentCCCD = (uint8_t)val[0] | ((uint8_t)val[1] << 8);
        logOutput("     Debug: Current CCCD: " + String(currentCCCD));
    }

    if (currentCCCD == 1 || currentCCCD == 2) {
       logOutput("     Debug: Already Enabled. Linking callback...");
       pRemoteCharacteristicTX->subscribe(true, notifyCB, false); 
       subOk = true;
       return true;
    }

    // ATTEMPT
    logOutput("     Debug: Attempting Standard Subscribe...");
    
    if (pRemoteCharacteristicTX->subscribe(true, notifyCB, true)) {
        logOutput("     Debug: Subscribe Success (API)!");
        
        delay(200);
        val = pDesc->readValue();
        if (val.length() >= 2) {
            uint16_t verifyCCCD = (uint8_t)val[0] | ((uint8_t)val[1] << 8);
            if (verifyCCCD == 1 || verifyCCCD == 2) {
                logOutput("     Debug: [VERIFY] CCCD is enabled (" + String(verifyCCCD) + "). Success.");
                pRemoteCharacteristicTX->subscribe(true, notifyCB, false);
                subOk = true;
                return true;
            }
        }
    }
    
    logOutput("     Debug: Subscribe Failed. Stack Error: " + String(pClient->getLastError()));
    
    // MANUAL WRITE FALLBACK
    logOutput("     Debug: Trying Manual Descriptor Write (01 00, Resp)...");
    uint8_t enableVal[] = {0x01, 0x00};
    
    if (pDesc->writeValue(enableVal, 2, true)) {
        logOutput("     Debug: Manual Write Success! Linking callback...");
        pRemoteCharacteristicTX->subscribe(true, notifyCB, false); 
        subOk = true;
        return true;
    }

    logOutput("     Debug: Manual Write Failed. Verifying if it stuck...");
    delay(500);
    
    val = pDesc->readValue();
    if (val.length() >= 2) {
        uint16_t verifyCCCD = (uint8_t)val[0] | ((uint8_t)val[1] << 8);
        if (verifyCCCD == 1 || verifyCCCD == 2) {
            logOutput("     Debug: OVERRIDE! CCCD is enabled (" + String(verifyCCCD) + "). Success.");
            pRemoteCharacteristicTX->subscribe(true, notifyCB, false);
            subOk = true;
            return true;
        }
    }
    
    logOutput("     Debug: Subscribe truly failed. Retrying...");
    subOk = false;
    return false; 
}