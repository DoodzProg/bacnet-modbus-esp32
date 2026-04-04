/**
 * @file        state.h
 * @brief       Shared state declarations for the BMS Gateway.
 *
 * @details     Defines the core data structures for BMS data points (binary and analog),
 * as well as the card/section layout model used by the web dashboard. All protocol
 * handlers (BACnet, Modbus, Web) share a single instance of these structures,
 * ensuring a consistent single source of truth across the system.
 *
 * Dynamic point management (add/remove at runtime) is supported via a flat
 * heap-allocated array pattern. The authoritative configuration is persisted in
 * LittleFS as /config.json and loaded at boot via state_load_from_json().
 *
 * @note        Thread safety is the responsibility of the caller. In a multi-task
 * (FreeRTOS) environment, acquire the shared mutex before accessing these arrays.
 *
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ==============================================================================
// CONSTANTS
// ==============================================================================

/** @brief Maximum number of BMS data points supported at runtime. */
#define MAX_POINTS 64

/** @brief Maximum length for a point or section name string (incl. null terminator). */
#define MAX_NAME_LEN 32

/** @brief Maximum number of dashboard sections (cards) allowed. */
#define MAX_SECTIONS 16

/** @brief Maximum number of points that can be assigned to a single section. */
#define MAX_POINTS_PER_SECTION 32

/** @brief File path for persisting the dynamic configuration in LittleFS. */
#define CONFIG_FILE_PATH "/config.json"

// ==============================================================================
// PROTOCOL BITMASKS
// ==============================================================================

#define PROTO_MODBUS 0x01
#define PROTO_BACNET 0x02

// ==============================================================================
// DATA STRUCTURES
// ==============================================================================

/**
 * @brief Represents a single boolean data point (Coil / Binary Value).
 */
struct BinaryPoint {
    char     name[MAX_NAME_LEN];
    uint16_t modbusCoil;
    uint16_t bacnetInstance;
    bool     value;
    uint8_t  protocol; // Bitmask (PROTO_MODBUS | PROTO_BACNET)
    bool     writable;
};

/**
 * @brief Represents a single floating-point data point (Holding Register / Analog Value).
 */
struct AnalogPoint {
    char     name[MAX_NAME_LEN];
    uint16_t modbusReg;
    uint16_t bacnetInstance;
    float    value;
    float    modbusScale; // Multiplier for Modbus (e.g., 10.0 for 1 decimal place)
    uint8_t  protocol;    // Bitmask (PROTO_MODBUS | PROTO_BACNET)
    bool     writable;
    char     unit[16];    // Engineering unit (e.g., "°C", "%", "Pa")
};

/**
 * @brief Represents a logical grouping of points for the dashboard UI.
 */
struct DashboardSection {
    char    id[MAX_NAME_LEN];
    char    label[MAX_NAME_LEN];
    uint8_t widthCols; // Dashboard grid width (1 to 4)
    int     pointCount;
    char    pointIds[MAX_POINTS_PER_SECTION][MAX_NAME_LEN];
};

// ==============================================================================
// GLOBAL STATE EXPORTS
// ==============================================================================

extern BinaryPoint      binaryPoints[MAX_POINTS];
extern AnalogPoint      analogPoints[MAX_POINTS];
extern DashboardSection dashboardSections[MAX_SECTIONS];

extern int NUM_BINARY_POINTS;
extern int NUM_ANALOG_POINTS;
extern int NUM_SECTIONS;

// ==============================================================================
// STATE MANAGEMENT & PERSISTENCE (Declarations)
// ==============================================================================

/**
 * @brief Loads the full gateway configuration from LittleFS.
 * @details If /config.json does not exist or is corrupted, default values are
 * seeded and automatically saved.
 * @return true Configuration loaded successfully or defaults seeded.
 * @return false Fatal error reading the filesystem.
 */
bool state_load_from_json();

/**
 * @brief Persists the current RAM state arrays to /config.json in LittleFS.
 * @note This function performs a complete overwrite of the file.
 * @return true Write successful.
 * @return false Write failed (filesystem full, mount error, etc.).
 */
bool state_save_to_json();

/**
 * @brief Checks whether a Modbus coil address is already in use.
 * @param coil      The coil address to test.
 * @param excludeIdx Index of the binary point to exclude from the check (-1 for none).
 * @return true if the address is already assigned to another point.
 */
bool state_is_coil_used(uint16_t coil, int excludeIdx = -1);

/**
 * @brief Checks whether a Modbus holding register address is already in use.
 * @param reg        The register address to test.
 * @param excludeIdx Index of the analog point to exclude from the check (-1 for none).
 * @return true if the address is already assigned to another point.
 */
bool state_is_reg_used(uint16_t reg, int excludeIdx = -1);

/**
 * @brief Checks whether a BACnet Binary Value instance number is already in use.
 * @param instance   The BV instance to test.
 * @param excludeIdx Index of the binary point to exclude from the check (-1 for none).
 * @return true if the instance is already assigned to another point.
 */
bool state_is_bv_used(uint16_t instance, int excludeIdx = -1);

/**
 * @brief Checks whether a BACnet Analog Value instance number is already in use.
 * @param instance   The AV instance to test.
 * @param excludeIdx Index of the analog point to exclude from the check (-1 for none).
 * @return true if the instance is already assigned to another point.
 */
bool state_is_av_used(uint16_t instance, int excludeIdx = -1);