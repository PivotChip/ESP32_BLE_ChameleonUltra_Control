#ifndef BLE_PAIRING_H
#define BLE_PAIRING_H

#include "Shared.h"

extern NimBLEAddress storedAddress; 
extern bool hasStoredAddress;

void initBLE();
void startScan();
void triggerReScan();
void startPair();
void savePairedDevice(const NimBLEAddress& addr);
void clearPairedDevice();

#endif