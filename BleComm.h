/*
 * Chameleon Ultra - Bluetooth BLE control from ESP32 device
 * by PivotChip Security
 */
 
#ifndef BLE_COMM_H
#define BLE_COMM_H

#include "Shared.h"

// Characteristic Pointers
extern NimBLERemoteCharacteristic* pRemoteCharacteristicRX;
extern NimBLERemoteCharacteristic* pRemoteCharacteristicTX;

// Protocol Constants
#define CHAMELEON_SOF 0x11

// Commands
#define CMD_GET_VERSION     1000
#define CMD_CHANGE_MODE     1001
#define CMD_SCAN_14443A     2000
#define CMD_SCAN_125K       3000

// PIN COMMANDS
#define CMD_BLE_SET_PAIRING_KEY     1030
#define CMD_BLE_GET_PAIRING_KEY     1031
#define CMD_BLE_SET_PAIRING_ENABLE  1037
#define CMD_BLE_GET_PAIRING_ENABLE  1036
#define CMD_BLE_DELETE_ALL_BONDS    1032
#define CMD_FACTORY_RESET           1020
#define CMD_SAVE_SETTINGS           1013

// Status Codes
#define STATUS_SUCCESS      0x0000
#define STATUS_GEN_ERR      0x0001 
#define STATUS_LF_OK        0x0040 
#define STATUS_LF_ERR_1     0x0041 
#define STATUS_LF_ERR_2     0x0042 
#define STATUS_HF_ERR       0x0065 
#define STATUS_MODE_ERR     0x0066 
#define STATUS_OK_CUSTOM    0x0068 

// Device Modes
#define MODE_TAG    0x00
#define MODE_READER 0x01

// Functions
void sendText(const String& s);
bool setupService(); 
bool enableNotifications(bool& subOk);
bool triggerSecurityViaRead();
void sendUltraCommand(uint16_t cmd, const uint8_t* payload = nullptr, uint16_t payloadLen = 0);
void setDeviceMode(uint8_t mode);

// PIN HELPERS
void setChameleonPIN(uint32_t pin);
void enableChameleonPairing(bool enable);
void clearChameleonBonds();
void saveSettings();

// Parser State
extern uint8_t rxBuffer[512];
extern uint16_t rxIndex;
extern int identifyingStage;

#endif