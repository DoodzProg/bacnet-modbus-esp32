/**
 * @file config.h
 * @brief Hardware configuration and global system definitions.
 * @details Contains pin mappings, network parameters, and protocol register/instance maps.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#pragma once

// ==============================================================================
// HARDWARE CONFIGURATION
// ==============================================================================

/**
 * @brief Built-in LED pin.
 * @note Set to 16 for the YD-ESP32-S3 N16R8 board used in this project.
 * Update this value to match your specific hardware variant if different.
 */
#define LED_PIN 16

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