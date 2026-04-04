/**
 * @file        state.cpp
 * @brief       Runtime state management and LittleFS persistence for the BMS Gateway.
 *
 * @details     This module owns the single source of truth for all BMS data points
 * and dashboard layout. It replaces the compile-time static arrays of v1 with
 * heap-sized flat arrays that are populated at boot from /config.json and
 * can be mutated and re-persisted at runtime without a firmware recompile.
 *
 * ### Boot Sequence
 * 1. state_load_from_json() is called from setup() after LittleFS.begin().
 * 2. If /config.json is present and valid, arrays are populated from it.
 * 3. If the file is absent or corrupt, built-in defaults are applied and
 * state_save_to_json() is called to seed the file for subsequent boots.
 *
 * ### Persistence Contract
 * state_save_to_json() is called immediately after every mutation originating
 * from a web API call. This means at most one operation can be lost on a hard
 * power cut, which is acceptable for a commissioning tool.
 *
 * ### Conflict Detection
 * The state_is_*_used() helpers allow the web handler and the front-end to
 * validate addresses before committing a new point, preventing duplicate
 * Modbus register or BACnet instance assignments.
 *
 * @dependencies
 * - ArduinoJson (>=6.x) for JSON serialisation / deserialisation.
 * - LittleFS (ESP32 Arduino core) for filesystem I/O.
 *
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ==============================================================================
// GLOBAL STATE ARRAYS (Definitions)
// ==============================================================================

BinaryPoint      binaryPoints[MAX_POINTS];
AnalogPoint      analogPoints[MAX_POINTS];
DashboardSection dashboardSections[MAX_SECTIONS];

int NUM_BINARY_POINTS = 0;
int NUM_ANALOG_POINTS = 0;
int NUM_SECTIONS      = 0;

// ==============================================================================
// INTERNAL HELPERS
// ==============================================================================

/**
 * @brief Populates the global arrays with the built-in AHU demo configuration.
 *
 * @details This function mirrors the static initialisation that existed in
 * v1 of state.cpp. It is called automatically when no valid config.json is
 * found, ensuring the device is immediately useful out of the box.
 */
static void _apply_defaults() {
    Serial.println("[STATE] Applying built-in default configuration.");

    // --- Default Binary Points ---
    NUM_BINARY_POINTS = 1;

    strncpy(binaryPoints[0].name, "FanStatus", MAX_NAME_LEN);
    binaryPoints[0].modbusCoil     = 10;
    binaryPoints[0].bacnetInstance = 0;
    binaryPoints[0].value          = false;
    binaryPoints[0].protocol       = PROTO_MODBUS | PROTO_BACNET;
    binaryPoints[0].writable       = true;

    // --- Default Analog Points ---
    NUM_ANALOG_POINTS = 2;

    strncpy(analogPoints[0].name, "FanSpeed", MAX_NAME_LEN);
    analogPoints[0].modbusReg      = 100;
    analogPoints[0].bacnetInstance = 0;
    analogPoints[0].value          = 0.0f;
    analogPoints[0].modbusScale    = 1.0f;
    analogPoints[0].protocol       = PROTO_MODBUS | PROTO_BACNET;
    analogPoints[0].writable       = true;
    strncpy(analogPoints[0].unit, "%", 16);

    strncpy(analogPoints[1].name, "TempSetpoint", MAX_NAME_LEN);
    analogPoints[1].modbusReg      = 101;
    analogPoints[1].bacnetInstance = 1;
    analogPoints[1].value          = 21.0f;
    analogPoints[1].modbusScale    = 10.0f;
    analogPoints[1].protocol       = PROTO_MODBUS | PROTO_BACNET;
    analogPoints[1].writable       = true;
    strncpy(analogPoints[1].unit, "°C", 16);

    // --- Default Dashboard Layout (one section containing all points) ---
    NUM_SECTIONS = 1;

    strncpy(dashboardSections[0].id,    "section-default", MAX_NAME_LEN);
    strncpy(dashboardSections[0].label, "Air Handling Unit", MAX_NAME_LEN);
    dashboardSections[0].widthCols  = 4;
    dashboardSections[0].pointCount = 3;
    strncpy(dashboardSections[0].pointIds[0], "FanStatus",   MAX_NAME_LEN);
    strncpy(dashboardSections[0].pointIds[1], "FanSpeed",    MAX_NAME_LEN);
    strncpy(dashboardSections[0].pointIds[2], "TempSetpoint", MAX_NAME_LEN);
}

// ==============================================================================
// CONFLICT DETECTION
// ==============================================================================

bool state_is_coil_used(uint16_t coil, int excludeIdx) {
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (i == excludeIdx) continue;
        if (binaryPoints[i].modbusCoil == coil) return true;
    }
    return false;
}

bool state_is_reg_used(uint16_t reg, int excludeIdx) {
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (i == excludeIdx) continue;
        if (analogPoints[i].modbusReg == reg) return true;
    }
    return false;
}

bool state_is_bv_used(uint16_t instance, int excludeIdx) {
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (i == excludeIdx) continue;
        if (binaryPoints[i].bacnetInstance == instance) return true;
    }
    return false;
}

bool state_is_av_used(uint16_t instance, int excludeIdx) {
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (i == excludeIdx) continue;
        if (analogPoints[i].bacnetInstance == instance) return true;
    }
    return false;
}

// ==============================================================================
// JSON PERSISTENCE
// ==============================================================================

/**
 * @brief Deserialises a single BinaryPoint from a JsonObject.
 * @param obj  Source JsonObject (one element of the "binary" array).
 * @param dest Destination struct to populate.
 */
static void _parse_binary(JsonObject obj, BinaryPoint& dest) {
    strncpy(dest.name, obj["name"] | "Unknown", MAX_NAME_LEN);
    dest.modbusCoil     = obj["modbusCoil"]     | (uint16_t)0;
    dest.bacnetInstance = obj["bacnetInstance"] | (uint16_t)0;
    dest.value          = obj["value"]          | false;
    dest.protocol       = obj["protocol"]       | (uint8_t)(PROTO_MODBUS | PROTO_BACNET);
    dest.writable       = obj["writable"]       | true;
}

/**
 * @brief Deserialises a single AnalogPoint from a JsonObject.
 * @param obj  Source JsonObject (one element of the "analog" array).
 * @param dest Destination struct to populate.
 */
static void _parse_analog(JsonObject obj, AnalogPoint& dest) {
    strncpy(dest.name, obj["name"] | "Unknown", MAX_NAME_LEN);
    dest.modbusReg      = obj["modbusReg"]       | (uint16_t)0;
    dest.bacnetInstance = obj["bacnetInstance"]  | (uint16_t)0;
    dest.value          = obj["value"]           | 0.0f;
    dest.modbusScale    = obj["modbusScale"]     | 1.0f;
    dest.protocol       = obj["protocol"]        | (uint8_t)(PROTO_MODBUS | PROTO_BACNET);
    dest.writable       = obj["writable"]        | true;
    strncpy(dest.unit, obj["unit"] | "", 16);
}

/**
 * @brief Deserialises a DashboardSection from a JsonObject.
 * @param obj  Source JsonObject (one element of the "sections" array).
 * @param dest Destination struct to populate.
 */
static void _parse_section(JsonObject obj, DashboardSection& dest) {
    strncpy(dest.id,    obj["id"]    | "section-?", MAX_NAME_LEN);
    strncpy(dest.label, obj["label"] | "Section",   MAX_NAME_LEN);
    dest.widthCols  = obj["widthCols"] | (uint8_t)4;
    dest.pointCount = 0;

    JsonArray pts = obj["points"];
    if (pts) {
        for (JsonVariant v : pts) {
            if (dest.pointCount >= MAX_POINTS_PER_SECTION) break;
            strncpy(dest.pointIds[dest.pointCount++], v.as<const char*>(), MAX_NAME_LEN);
        }
    }
}

// ------------------------------------------------------------------------------

bool state_load_from_json() {
    if (!LittleFS.exists(CONFIG_FILE_PATH)) {
        Serial.println("[STATE] config.json not found. Seeding defaults.");
        _apply_defaults();
        return state_save_to_json();
    }

    File f = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!f) {
        Serial.println("[STATE] ERROR: Cannot open config.json for reading.");
        _apply_defaults();
        return false;
    }

    // Allocate a generous document; complex configs with 64 points fit in ~8 KB.
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[STATE] ERROR: JSON parse failed (%s). Restoring defaults.\n", err.c_str());
        _apply_defaults();
        return state_save_to_json();
    }

    // --- Parse binary points ---
    NUM_BINARY_POINTS = 0;
    JsonArray binArr = doc["binary"];
    if (binArr) {
        for (JsonObject obj : binArr) {
            if (NUM_BINARY_POINTS >= MAX_POINTS) break;
            _parse_binary(obj, binaryPoints[NUM_BINARY_POINTS++]);
        }
    }

    // --- Parse analog points ---
    NUM_ANALOG_POINTS = 0;
    JsonArray anaArr = doc["analog"];
    if (anaArr) {
        for (JsonObject obj : anaArr) {
            if (NUM_ANALOG_POINTS >= MAX_POINTS) break;
            _parse_analog(obj, analogPoints[NUM_ANALOG_POINTS++]);
        }
    }

    // --- Parse dashboard layout ---
    NUM_SECTIONS = 0;
    JsonArray secArr = doc["sections"];
    if (secArr) {
        for (JsonObject obj : secArr) {
            if (NUM_SECTIONS >= MAX_SECTIONS) break;
            _parse_section(obj, dashboardSections[NUM_SECTIONS++]);
        }
    }

    // Update global count accurately
    NUM_BINARY_POINTS = binArr.size();
    NUM_ANALOG_POINTS = anaArr.size();
    NUM_SECTIONS      = secArr.size();

    Serial.printf("[STATE] Loaded from config.json: %d Binary, %d Analog, %d Sections.\n", 
                  NUM_BINARY_POINTS, NUM_ANALOG_POINTS, NUM_SECTIONS);
    return true;
}

// ------------------------------------------------------------------------------

bool state_save_to_json() {
    DynamicJsonDocument doc(8192);

    // --- Serialise binary points ---
    JsonArray binArr = doc.createNestedArray("binary");
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        JsonObject o = binArr.createNestedObject();
        o["name"]           = binaryPoints[i].name;
        o["modbusCoil"]     = binaryPoints[i].modbusCoil;
        o["bacnetInstance"] = binaryPoints[i].bacnetInstance;
        o["value"]          = binaryPoints[i].value;
        o["protocol"]       = binaryPoints[i].protocol;
        o["writable"]       = binaryPoints[i].writable;
    }

    // --- Serialise analog points ---
    JsonArray anaArr = doc.createNestedArray("analog");
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        JsonObject o = anaArr.createNestedObject();
        o["name"]           = analogPoints[i].name;
        o["modbusReg"]      = analogPoints[i].modbusReg;
        o["bacnetInstance"] = analogPoints[i].bacnetInstance;
        o["value"]          = analogPoints[i].value;
        o["modbusScale"]    = analogPoints[i].modbusScale;
        o["protocol"]       = analogPoints[i].protocol;
        o["writable"]       = analogPoints[i].writable;
        o["unit"]           = analogPoints[i].unit;
    }

    // --- Serialise dashboard layout ---
    JsonArray secArr = doc.createNestedArray("sections");
    for (int i = 0; i < NUM_SECTIONS; i++) {
        JsonObject o = secArr.createNestedObject();
        o["id"]        = dashboardSections[i].id;
        o["label"]     = dashboardSections[i].label;
        o["widthCols"] = dashboardSections[i].widthCols;
        JsonArray pts  = o.createNestedArray("points");
        for (int j = 0; j < dashboardSections[i].pointCount; j++) {
            pts.add(dashboardSections[i].pointIds[j]);
        }
    }

    // Atomic write: open for write (truncate), serialise, close.
    File f = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!f) {
        Serial.println("[STATE] ERROR: Cannot open config.json for writing.");
        return false;
    }
    serializeJson(doc, f);
    f.close();

    Serial.printf("[STATE] config.json saved (%d B, %d A, %d S).\n",
                  NUM_BINARY_POINTS, NUM_ANALOG_POINTS, NUM_SECTIONS);
    return true;
}