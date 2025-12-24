/*
 * TARGET LIBRARY: NimBLE-Arduino v2.3.7
 * NOTE: Ensure this specific version is used.
 *
 * HISTORY:
 * - v171.0: Removed Bonding Wait. Auto-Setup logic.
 * - Flow: Connect -> Subscribe -> Ready -> Auto-Info & Mode Switch.
 */

#ifndef SHARED_H
#define SHARED_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

// --- STATE MACHINE ENUMS ---
enum AppState {
    ST_IDLE,
    ST_SCANNING,          
    ST_RESCAN_TARGET,     
    ST_CONNECT_ATTEMPT,   
    ST_CONNECT_COOLDOWN,  
    ST_CONNECTED_PENDING, 
    ST_SECURING,          
    ST_SECURITY_SETTLE,   
    ST_DISCOVERING,
    ST_SUBSCRIBING,       
    ST_READY
};

// --- GLOBAL EXTERNALS ---
extern volatile AppState currentState;
extern unsigned long stateTimer;
extern int retryCount;
extern const int MAX_RETRIES;
extern volatile bool authInProgress;
extern volatile unsigned long lastSecurityTime;

extern NimBLEClient* pClient;
extern NimBLEAdvertisedDevice* targetDevice;

extern NimBLEUUID serviceUUID;
extern NimBLEUUID charUUID_RX;
extern NimBLEUUID charUUID_TX;

void logOutput(const String& msg);

#endif