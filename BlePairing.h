/*
 * Chameleon Ultra - Bluetooth BLE control from ESP32 device
 * by PivotChip Security
 */

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
void connectToScannedDevice(int index);
void savePinConfig(uint32_t pin, bool enable);
void updateSecuritySettings(); 

#endif