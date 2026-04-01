/**
 * @file state.cpp
 * @brief Central data structure definitions.
 * @details Instantiates the global arrays containing the telemetry points mapped 
 * across Modbus and BACnet protocols.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#include "state.h"
#include "config.h"

// ==============================================================================
// GLOBAL STATE ARRAYS (Definitions)
// ==============================================================================

BinaryPoint binaryPoints[] = {
    // Name             Modbus Coil           BACnet Instance      Initial Value
    {"FanStatus",       MB_COIL_FAN_STATUS,   BAC_BV_FAN_STATUS,   false}
};

AnalogPoint analogPoints[] = {
    // Name             Modbus Hreg           BACnet Instance      Initial Value  Modbus Scale
    {"FanSpeed",        MB_HREG_FAN_SPEED,    BAC_AV_FAN_SPEED,    0.0f,          1.0f},
    {"TempSetpoint",    MB_HREG_TEMP_SET,     BAC_AV_TEMP_SET,     21.0f,         10.0f}
};

// ==============================================================================
// ARRAY SIZES (Calculated automatically at compile time)
// ==============================================================================

const int NUM_BINARY_POINTS = sizeof(binaryPoints) / sizeof(binaryPoints[0]);
const int NUM_ANALOG_POINTS = sizeof(analogPoints) / sizeof(analogPoints[0]);