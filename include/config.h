/**
 * @file        config.h
 * @brief       Hardware configuration and global system definitions.
 * @details     Contains pin mappings, network parameters, and protocol register/instance maps.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.1
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#pragma once

// ==============================================================================
// HARDWARE NOTES (YD-ESP32-S3 N16R8)
// ==============================================================================
//
// LED layout (hardware-wired, not software-controllable):
//   - Red  LED : power indicator (always on when board is powered)
//   - Blue LED : USB activity / flashing indicator
//   - Green LED: GPIO activity (faint blink on GPIO transitions)
//   There is no general-purpose LED controllable from firmware without
//   hardware modifications. Status is observable via Serial and the Web UI.
//
// BOOT button (GPIO 0):
//   - Available on the board, confirmed functional (reads LOW when pressed,
//     pull-up internal). Reserved for future use (e.g., factory reset trigger).
//   - Not used in v1.0.1.

// ==============================================================================
// DEFAULT IDENTITY & CREDENTIALS
// ==============================================================================
// These values are used on first boot (nothing stored in NVS yet).
// All of them can be changed at runtime via the web UI without reflashing.

#define DEFAULT_DEVICE_NAME ""          // Empty = use "bms-gateway.local" (legacy behaviour)
#define DEFAULT_WEB_USER    "admin"
#define DEFAULT_WEB_PASS    "admin1234"

// ==============================================================================
// NETWORK & BACNET CONFIGURATION
// ==============================================================================

#define BACNET_PORT        47808
#define BACNET_DEVICE_ID   1234
#define BACNET_DEVICE_NAME "ESP32-AHU-Sim" // Air Handling Unit Simulator

// ==============================================================================
// MODBUS TCP REGISTERS MAP
// ==============================================================================

#define MB_COIL_FAN_STATUS 10
#define MB_HREG_FAN_SPEED  100
#define MB_HREG_TEMP_SET   101

// ==============================================================================
// BACNET OBJECT INSTANCES MAP
// ==============================================================================

#define BAC_BV_FAN_STATUS  0
#define BAC_AV_FAN_SPEED   0
#define BAC_AV_TEMP_SET    1