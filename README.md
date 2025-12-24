# Use ESP32 to control Chameleon devices over BLE (Bluethooth)

A modularized, asynchronous firmware for ESP32 designed to interface with the **Chameleon Ultra** RFID research tool via BLE. This project provides a robust serial-to-BLE bridge, implementing the Chameleon Ultra's binary protocol for remote card scanning and device management.

## Features

* **Asynchronous State Machine**: Handles discovery, connection, and security without blocking the main execution loop.
* **Secure Pairing**: Implements "Just Works" bonding with automatic reconnection to saved devices using ESP32 NVS (Preferences).
* **Binary Protocol Engine**: Full implementation of the Chameleon Ultra frame format, including:
    * SOF (0x11) validation.
    * Multi-stage LRC checksum calculation.
    * Big-Endian command and status parsing.
* **Card Scanning & Identification**:
    * **HF (13.56MHz)**: Parses ISO14443A responses including UID length, UID, ATQA, and SAK.
    * **LF (125kHz)**: Parses EM410x and other low-frequency tag IDs.
* **Atomic Logging**: Prevents serial output mangling by pre-building log strings before transmission, essential for multi-tasking BLE environments.

## Hardware Requirements

* **ESP32 Development Board** (WROOM, S3, C3, etc.)
* **Chameleon Ultra** (or Chameleon Lite) device.

## Software Dependencies

* **Arduino IDE** (or PlatformIO)
* **NimBLE-Arduino v2.3.7**: This specific version is recommended for stability with the pairing logic used in this firmware.

## Installation

1. Clone this repository.
2. Install the `NimBLE-Arduino` library (v2.3.7) via the Arduino Library Manager.
3. Open `FrostChameleon.ino` in your IDE.
4. Select your ESP32 board and upload the code.

## Serial Commands

The firmware accepts the following commands via the Serial Monitor (115200 baud):

| Command | Description |
| :--- | :--- |
| `discover` | Scans for nearby Chameleon Ultra devices. |
| `pair` | Initiates connection and bonding with the discovered or saved device. |
| `pin 123456` | This command would enable pin 123456 on reset Chameleon. |
| `forget` | Clears the bonded device address from NVS and deletes local bonds. |
| `info` | Requests device firmware version. |
| `scan` | Triggers both High Frequency and Low Frequency tag search. |
| `scan hf` | Triggers a High Frequency (13.56MHz) tag search. |
| `scan lf` | Triggers a Low Frequency (125kHz) tag search. |
| `mode reader` | Switches the Chameleon Ultra into Reader mode. |
| `mode tag` | Switches the Chameleon Ultra into Tag Emulation mode. |
| `drop` | Disconnects the current BLE link. |
| `send <txt>` | Sends a raw text command to the device. |
| `clear bonds` | Reset bluetooth devices paired with Chamaleon. |

## Project Structure

* `FrostChameleon.ino`: Main async state machine and serial command processor.
* `Shared.h`: Global enums, state definitions, and external variable declarations.
* `BlePairing.h/cpp`: Logic for BLE scanning, connection callbacks, and security/bonding.
* `BleComm.h/cpp`: Binary protocol implementation, frame construction, and tag data parsing.

## Protocol Details

The implementation follows the Chameleon Ultra binary frame structure:
`[SOF] [LRC1] [CMD_H] [CMD_L] [STAT_H] [STAT_L] [LEN_H] [LEN_L] [LRC2] + [DATA] + [LRC3]`

Responses are automatically parsed into human-readable formats in the serial logs, providing instant feedback on tag UIDs and hardware status codes.

## About PivotChip

Visit PivotChip Security's website for a wide selection of pentesting devices for cybersecurity professionals.

https://pivotchip.ca

## License

This project is intended for research and educational purposes in the field of RFID security. Please use responsibly.
