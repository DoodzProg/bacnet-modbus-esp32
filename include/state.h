/**
 * @file state.h
 * @brief Central data structure (Data-Driven Architecture).
 * @details Internal in-memory database acting as the bridge between Modbus, BACnet, and the Web UI.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#pragma once
#include <Arduino.h>

// ==============================================================================
// DATA STRUCTURES
// ==============================================================================

/**
 * @brief Structure representing a digital point (ON/OFF).
 */
struct BinaryPoint {
    const char* name;         /**< Object name (e.g., "FanStatus") */
    uint16_t mb_coil;         /**< Modbus Coil Register ID */
    uint32_t bac_instance;    /**< BACnet Binary Value Instance ID */
    bool value;               /**< Current operational state */
};

/**
 * @brief Structure representing an analog point.
 */
struct AnalogPoint {
    const char* name;         /**< Object name (e.g., "TempSetpoint") */
    uint16_t mb_hreg;         /**< Modbus Holding Register ID */
    uint32_t bac_instance;    /**< BACnet Analog Value Instance ID */
    float value;              /**< Current real value (e.g., 21.0) */
    float mb_scale;           /**< Modbus scale factor (e.g., 10.0 to send 210 for 21.0) */
};

// ==============================================================================
// GLOBAL STATE ARRAYS (Declarations)
// ==============================================================================

extern BinaryPoint binaryPoints[];
extern AnalogPoint analogPoints[];

/** @brief Helper variable defining the total number of binary points. */
extern const int NUM_BINARY_POINTS;

/** @brief Helper variable defining the total number of analog points. */
extern const int NUM_ANALOG_POINTS;