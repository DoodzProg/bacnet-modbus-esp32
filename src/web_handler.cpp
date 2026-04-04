/**
 * @file        web_handler.cpp
 * @brief       REST API server and embedded web interface for the BMS Gateway.
 *
 * @details     Serves the front-end SPA (Single-Page Application) from LittleFS
 * and exposes a JSON REST API consumed by the dashboard. All state mutations
 * (point creation, deletion, value writes, layout changes) are immediately
 * persisted to /config.json via state_save_to_json() to survive hard resets.
 *
 * ### API Surface (v1.0.0)
 *
 * | Method | Endpoint              | Description                                      |
 * |--------|-----------------------|--------------------------------------------------|
 * | GET    | /api/data             | Live telemetry values + RAM stats                |
 * | GET    | /api/system           | Network configuration and current IP addresses   |
 * | GET    | /api/config           | Full point definitions and dashboard layout      |
 * | POST   | /api/config/layout    | Persist dashboard section order / sizes          |
 * | POST   | /api/point/add        | Create a new binary or analog point              |
 * | POST   | /api/point/update     | Update metadata for an existing point            |
 * | POST   | /api/point/delete     | Remove a point from state and all sections       |
 * | POST   | /api/point/write      | Write a live value to a point                    |
 * | GET    | /api/point/check      | Validate address availability before committing  |
 * | GET    | /api/scan             | Scan surrounding Wi-Fi networks                  |
 * | GET    | /api/switch_network   | Persist network credentials and reboot           |
 *
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#include "web_handler.h"
#include "state.h"
#include "secrets.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// ==============================================================================
// PROTOCOL SYNCHRONIZATION EXTERNS
// ==============================================================================

#ifdef ENABLE_MODBUS
  extern void modbus_write_binary(int index);
  extern void modbus_write_analog(int index);
#endif

#ifdef ENABLE_BACNET
  extern "C" {
      #include "bacnet/bacdef.h"
      #include "bacnet/bacstr.h"
      #include "bacnet/config.h" 
      
      #define BACnetHostOctetString BACnet_Octet_String
      #define BACnetHostCharacterString BACnet_Character_String

      #include "bacnet/basic/object/av.h"
      #include "bacnet/basic/object/bv.h"
  }
#endif

// ==============================================================================
// GLOBAL VARIABLES
// ==============================================================================

extern const char* AP_SSID;
extern const char* AP_PASS;

WebServer server(80);

/** * @brief Flag to trigger a safe reboot in the main loop.
 * @details Prevents watchdog crashes by avoiding ESP.restart() inside async web requests. 
 */
volatile bool pendingReboot = false;

// ==============================================================================
// INTERNAL UTILITIES
// ==============================================================================

static void _send_error(int code, const char* message) {
    String body = "{\"error\":\"";
    body += message;
    body += "\"}";
    server.send(code, "application/json", body);
}

static bool _parse_body(DynamicJsonDocument& doc, size_t maxBytes = 4096) {
    if (!server.hasArg("plain")) {
        _send_error(400, "Missing JSON body");
        return false;
    }
    const String& body = server.arg("plain");
    if (body.length() > maxBytes) {
        _send_error(413, "Request body too large");
        return false;
    }
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _send_error(400, "Malformed JSON");
        return false;
    }
    return true;
}

static void _remove_point_from_sections(const char* pointName) {
    for (int s = 0; s < NUM_SECTIONS; s++) {
        DashboardSection& sec = dashboardSections[s];
        int write = 0;
        for (int p = 0; p < sec.pointCount; p++) {
            if (strcmp(sec.pointIds[p], pointName) != 0) {
                if (write != p) {
                    strncpy(sec.pointIds[write], sec.pointIds[p], MAX_NAME_LEN);
                }
                write++;
            }
        }
        sec.pointCount = write;
    }
}

// ==============================================================================
// GET /api/data — Live Telemetry Polling
// ==============================================================================

void handleData() {
    String json = "{";

    uint32_t freeRam  = ESP.getFreeHeap();
    uint32_t totalRam = ESP.getHeapSize();
    uint32_t usedRam  = totalRam - freeRam;
    int      ramPct   = (usedRam * 100) / totalRam;

    json += "\"sys\":{";
    json += "\"ram_used\":"  + String(usedRam)  + ",";
    json += "\"ram_total\":" + String(totalRam) + ",";
    json += "\"ram_pct\":"   + String(ramPct);
    json += "},";

    json += "\"binary\":[";
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        json += "{\"name\":\""     + String(binaryPoints[i].name) + "\",";
        json += "\"value\":"       + String(binaryPoints[i].value ? "true" : "false") + ",";
        json += "\"writable\":"    + String(binaryPoints[i].writable ? "true" : "false") + "}";
        if (i < NUM_BINARY_POINTS - 1) json += ",";
    }
    json += "],";

    json += "\"analog\":[";
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        json += "{\"name\":\""     + String(analogPoints[i].name)  + "\",";
        json += "\"value\":"       + String(analogPoints[i].value) + ",";
        json += "\"writable\":"    + String(analogPoints[i].writable ? "true" : "false") + "}";
        if (i < NUM_ANALOG_POINTS - 1) json += ",";
    }
    json += "]}";

    server.send(200, "application/json", json);
}

// ==============================================================================
// GET /api/system — Network Configuration
// ==============================================================================

void handleSystem() {
    Preferences pref;
    pref.begin("bms-app", true);

    String currentSTA     = pref.isKey("sta_ssid") ? pref.getString("sta_ssid") : String(WIFI_SSID);
    String currentSTAPass = pref.isKey("sta_pass") ? pref.getString("sta_pass") : String(WIFI_PASS);
    String currentAP      = pref.isKey("ap_ssid")  ? pref.getString("ap_ssid")  : String(AP_SSID);
    String currentAPPass  = pref.isKey("ap_pass")  ? pref.getString("ap_pass")  : String(AP_PASS);
    pref.end();

    String json = "{";
    json += "\"isAP\":"      + String(WiFi.getMode() == WIFI_AP ? "true" : "false") + ",";
    json += "\"staSSID\":\"" + currentSTA      + "\",";
    json += "\"staPASS\":\"" + currentSTAPass  + "\",";
    json += "\"apSSID\":\""  + currentAP       + "\",";
    json += "\"apPASS\":\""  + currentAPPass   + "\",";
    json += "\"clientIP\":\"" + server.client().remoteIP().toString() + "\",";
    json += "\"espIP\":\""   + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

// ==============================================================================
// GET /api/config — Full Configuration Export
// ==============================================================================

void handleGetConfig() {
    DynamicJsonDocument doc(8192);

    JsonArray binArr = doc.createNestedArray("binary");
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        JsonObject o = binArr.createNestedObject();
        o["name"]           = binaryPoints[i].name;
        o["modbusCoil"]     = binaryPoints[i].modbusCoil;
        o["bacnetInstance"] = binaryPoints[i].bacnetInstance;
        o["protocol"]       = binaryPoints[i].protocol;
        o["writable"]       = binaryPoints[i].writable;
    }

    JsonArray anaArr = doc.createNestedArray("analog");
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        JsonObject o = anaArr.createNestedObject();
        o["name"]           = analogPoints[i].name;
        o["modbusReg"]      = analogPoints[i].modbusReg;
        o["bacnetInstance"] = analogPoints[i].bacnetInstance;
        o["modbusScale"]    = analogPoints[i].modbusScale;
        o["unit"]           = analogPoints[i].unit;
        o["protocol"]       = analogPoints[i].protocol;
        o["writable"]       = analogPoints[i].writable;
    }

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

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ==============================================================================
// POST /api/config/layout — Persist Dashboard Layout
// ==============================================================================

void handlePostLayout() {
    DynamicJsonDocument doc(4096);
    if (!_parse_body(doc)) return;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) { _send_error(400, "Expected JSON array"); return; }

    NUM_SECTIONS = 0;
    for (JsonObject obj : arr) {
        if (NUM_SECTIONS >= MAX_SECTIONS) break;
        DashboardSection& sec = dashboardSections[NUM_SECTIONS++];
        strncpy(sec.id,    obj["id"]    | "section-?", MAX_NAME_LEN);
        strncpy(sec.label, obj["label"] | "Section",   MAX_NAME_LEN);
        sec.widthCols  = obj["widthCols"] | (uint8_t)4;
        sec.pointCount = 0;
        JsonArray pts  = obj["points"];
        if (pts) {
            for (JsonVariant v : pts) {
                if (sec.pointCount >= MAX_POINTS_PER_SECTION) break;
                strncpy(sec.pointIds[sec.pointCount++], v.as<const char*>(), MAX_NAME_LEN);
            }
        }
    }

    if (!state_save_to_json()) { _send_error(500, "Filesystem write failed"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
}

// ==============================================================================
// POST /api/point/add — Create a New Point
// ==============================================================================

void handleAddPoint() {
    DynamicJsonDocument doc(1024);
    if (!_parse_body(doc)) return;

    const char* type = doc["type"] | "";
    const char* name = doc["name"] | "";

    if (strlen(name) == 0 || strlen(name) >= MAX_NAME_LEN) {
        _send_error(400, "Invalid or missing point name"); return;
    }

    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (strcmp(binaryPoints[i].name, name) == 0) {
            _send_error(400, "Point name already exists"); return;
        }
    }
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (strcmp(analogPoints[i].name, name) == 0) {
            _send_error(400, "Point name already exists"); return;
        }
    }

    uint8_t  proto    = doc["protocol"]       | (uint8_t)(PROTO_MODBUS | PROTO_BACNET);
    bool     writable = doc["writable"]        | true;
    uint16_t bacInst  = doc["bacnetInstance"]  | (uint16_t)0;

    if (strcmp(type, "binary") == 0) {
        if (NUM_BINARY_POINTS >= MAX_POINTS) {
            _send_error(507, "Binary point array is full"); return;
        }
        uint16_t coil = doc["modbusCoil"] | (uint16_t)0;

        if ((proto & PROTO_MODBUS) && state_is_coil_used(coil)) {
            _send_error(409, "Modbus coil address already in use"); return;
        }
        if ((proto & PROTO_BACNET) && state_is_bv_used(bacInst)) {
            _send_error(409, "BACnet BV instance already in use"); return;
        }

        BinaryPoint& pt = binaryPoints[NUM_BINARY_POINTS];
        strncpy(pt.name, name, MAX_NAME_LEN);
        pt.modbusCoil     = coil;
        pt.bacnetInstance = bacInst;
        pt.value          = false;
        pt.protocol       = proto;
        pt.writable       = writable;
        NUM_BINARY_POINTS++;

    } else if (strcmp(type, "analog") == 0) {
        if (NUM_ANALOG_POINTS >= MAX_POINTS) {
            _send_error(507, "Analog point array is full"); return;
        }
        uint16_t reg   = doc["modbusReg"]   | (uint16_t)0;
        float    scale = doc["modbusScale"]  | 1.0f;
        const char* unit = doc["unit"]       | "";

        if ((proto & PROTO_MODBUS) && state_is_reg_used(reg)) {
            _send_error(409, "Modbus register address already in use"); return;
        }
        if ((proto & PROTO_BACNET) && state_is_av_used(bacInst)) {
            _send_error(409, "BACnet AV instance already in use"); return;
        }

        AnalogPoint& pt = analogPoints[NUM_ANALOG_POINTS];
        strncpy(pt.name, name, MAX_NAME_LEN);
        pt.modbusReg      = reg;
        pt.bacnetInstance = bacInst;
        pt.value          = 0.0f;
        pt.modbusScale    = (scale == 0.0f) ? 1.0f : scale;
        pt.protocol       = proto;
        pt.writable       = writable;
        strncpy(pt.unit, unit, 16);
        NUM_ANALOG_POINTS++;

    } else {
        _send_error(400, "type must be 'binary' or 'analog'"); return;
    }

    const char* sectionId = doc["sectionId"] | "";
    bool registered = false;

    if (strlen(sectionId) > 0) {
        for (int s = 0; s < NUM_SECTIONS; s++) {
            if (strcmp(dashboardSections[s].id, sectionId) == 0) {
                if (dashboardSections[s].pointCount < MAX_POINTS_PER_SECTION) {
                    strncpy(dashboardSections[s].pointIds[dashboardSections[s].pointCount++], name, MAX_NAME_LEN);
                }
                registered = true;
                break;
            }
        }
    }

    if (!registered) {
        if (NUM_SECTIONS == 0 && NUM_SECTIONS < MAX_SECTIONS) {
            strncpy(dashboardSections[0].id,    "section-default", MAX_NAME_LEN);
            strncpy(dashboardSections[0].label, "Default",         MAX_NAME_LEN);
            dashboardSections[0].widthCols  = 4;
            dashboardSections[0].pointCount = 0;
            NUM_SECTIONS = 1;
        }
        if (NUM_SECTIONS > 0 && dashboardSections[0].pointCount < MAX_POINTS_PER_SECTION) {
            strncpy(dashboardSections[0].pointIds[dashboardSections[0].pointCount++], name, MAX_NAME_LEN);
        }
    }

    if (!state_save_to_json()) { _send_error(500, "Filesystem write failed"); return; }
    
    server.send(200, "application/json", "{\"ok\":true}");
    pendingReboot = true; // Safely triggers ESP.restart() in the main loop
}

// ==============================================================================
// POST /api/point/update — Modify Point Metadata
// ==============================================================================

void handleUpdatePoint() {
    DynamicJsonDocument doc(1024);
    if (!_parse_body(doc)) return;

    const char* name    = doc["name"]    | "";
    const char* newName = doc["newName"] | "";

    bool found = false;

    for (int i = 0; i < NUM_BINARY_POINTS && !found; i++) {
        if (strcmp(binaryPoints[i].name, name) != 0) continue;
        found = true;

        if (doc.containsKey("protocol"))       binaryPoints[i].protocol       = doc["protocol"];
        if (doc.containsKey("writable"))       binaryPoints[i].writable       = doc["writable"];
        if (doc.containsKey("bacnetInstance")) binaryPoints[i].bacnetInstance = doc["bacnetInstance"];

        if (doc.containsKey("modbusCoil")) {
            uint16_t coil = doc["modbusCoil"];
            if ((binaryPoints[i].protocol & PROTO_MODBUS) && state_is_coil_used(coil, i)) {
                _send_error(409, "Modbus coil address already in use"); return;
            }
            binaryPoints[i].modbusCoil = coil;
        }

        if (strlen(newName) > 0 && strcmp(newName, name) != 0) {
            for (int s = 0; s < NUM_SECTIONS; s++) {
                for (int p = 0; p < dashboardSections[s].pointCount; p++) {
                    if (strcmp(dashboardSections[s].pointIds[p], name) == 0) {
                        strncpy(dashboardSections[s].pointIds[p], newName, MAX_NAME_LEN);
                    }
                }
            }
            strncpy(binaryPoints[i].name, newName, MAX_NAME_LEN);
        }
    }

    for (int i = 0; i < NUM_ANALOG_POINTS && !found; i++) {
        if (strcmp(analogPoints[i].name, name) != 0) continue;
        found = true;

        if (doc.containsKey("protocol"))       analogPoints[i].protocol       = doc["protocol"];
        if (doc.containsKey("writable"))       analogPoints[i].writable       = doc["writable"];
        if (doc.containsKey("bacnetInstance")) analogPoints[i].bacnetInstance = doc["bacnetInstance"];
        if (doc.containsKey("unit"))           strncpy(analogPoints[i].unit,  doc["unit"] | "", 16);
        if (doc.containsKey("modbusScale")) {
            float s = doc["modbusScale"];
            analogPoints[i].modbusScale = (s == 0.0f) ? 1.0f : s;
        }
        if (doc.containsKey("modbusReg")) {
            uint16_t reg = doc["modbusReg"];
            if ((analogPoints[i].protocol & PROTO_MODBUS) && state_is_reg_used(reg, i)) {
                _send_error(409, "Modbus register address already in use"); return;
            }
            analogPoints[i].modbusReg = reg;
        }

        if (strlen(newName) > 0 && strcmp(newName, name) != 0) {
            for (int s = 0; s < NUM_SECTIONS; s++) {
                for (int p = 0; p < dashboardSections[s].pointCount; p++) {
                    if (strcmp(dashboardSections[s].pointIds[p], name) == 0) {
                        strncpy(dashboardSections[s].pointIds[p], newName, MAX_NAME_LEN);
                    }
                }
            }
            strncpy(analogPoints[i].name, newName, MAX_NAME_LEN);
        }
    }

    if (!found) { _send_error(404, "Point not found"); return; }
    if (!state_save_to_json()) { _send_error(500, "Filesystem write failed"); return; }
    
    server.send(200, "application/json", "{\"ok\":true}");
    pendingReboot = true;
}

// ==============================================================================
// POST /api/point/delete — Remove a Point
// ==============================================================================

void handleDeletePoint() {
    DynamicJsonDocument doc(256);
    if (!_parse_body(doc)) return;

    const char* name = doc["name"] | "";
    if (strlen(name) == 0) { _send_error(400, "Missing point name"); return; }

    bool found = false;

    for (int i = 0; i < NUM_BINARY_POINTS && !found; i++) {
        if (strcmp(binaryPoints[i].name, name) != 0) continue;
        found = true;
        for (int j = i; j < NUM_BINARY_POINTS - 1; j++) {
            binaryPoints[j] = binaryPoints[j + 1];
        }
        NUM_BINARY_POINTS--;
    }

    for (int i = 0; i < NUM_ANALOG_POINTS && !found; i++) {
        if (strcmp(analogPoints[i].name, name) != 0) continue;
        found = true;
        for (int j = i; j < NUM_ANALOG_POINTS - 1; j++) {
            analogPoints[j] = analogPoints[j + 1];
        }
        NUM_ANALOG_POINTS--;
    }

    if (!found) { _send_error(404, "Point not found"); return; }

    _remove_point_from_sections(name);

    if (!state_save_to_json()) { _send_error(500, "Filesystem write failed"); return; }
    
    server.send(200, "application/json", "{\"ok\":true}");
    pendingReboot = true;
}

// ==============================================================================
// POST /api/point/write — Live Value Write
// ==============================================================================

/**
 * @brief Writes a new live value to an existing writable point.
 * @details CRITICAL FIX: Immediately propagates the web-originated write to 
 * Modbus and BACnet stacks to prevent race conditions where the fast-running 
 * read loops overwrite the RAM with stale bus data.
 */
void handleWritePoint() {
    DynamicJsonDocument doc(256);
    if (!_parse_body(doc)) return;

    const char* name = doc["name"] | "";
    if (strlen(name) == 0 || !doc.containsKey("value")) {
        _send_error(400, "Missing name or value"); return;
    }

    // --- Process Binary Points ---
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (strcmp(binaryPoints[i].name, name) != 0) continue;
        if (!binaryPoints[i].writable) { _send_error(403, "Point is read-only"); return; }
        
        bool newVal = doc["value"].as<bool>();
        binaryPoints[i].value = newVal;

        // Propagate to protocol stacks immediately
        #ifdef ENABLE_MODBUS
            if (binaryPoints[i].protocol & PROTO_MODBUS) {
                modbus_write_binary(i);
            }
        #endif

        #ifdef ENABLE_BACNET
            if (binaryPoints[i].protocol & PROTO_BACNET) {
                Binary_Value_Present_Value_Set(binaryPoints[i].bacnetInstance, newVal ? BINARY_ACTIVE : BINARY_INACTIVE);
            }
        #endif

        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    // --- Process Analog Points ---
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (strcmp(analogPoints[i].name, name) != 0) continue;
        if (!analogPoints[i].writable) { _send_error(403, "Point is read-only"); return; }
        
        float newVal = doc["value"].as<float>();
        analogPoints[i].value = newVal;

        // Propagate to protocol stacks immediately
        #ifdef ENABLE_MODBUS
            if (analogPoints[i].protocol & PROTO_MODBUS) {
                modbus_write_analog(i);
            }
        #endif

        #ifdef ENABLE_BACNET
            if (analogPoints[i].protocol & PROTO_BACNET) {
                // Third parameter is priority, standard is 16 for generic web writes
                Analog_Value_Present_Value_Set(analogPoints[i].bacnetInstance, newVal, 16);
            }
        #endif

        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }

    _send_error(404, "Point not found");
}

// ==============================================================================
// GET /api/point/check — Address Conflict Validation
// ==============================================================================

void handleCheckAddress() {
    String type = server.arg("type");
    bool modbusOk = true;
    bool bacnetOk = true;

    String excludeName = server.arg("excludeName");
    int excludeBin = -1, excludeAna = -1;
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (String(binaryPoints[i].name) == excludeName) { excludeBin = i; break; }
    }
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (String(analogPoints[i].name) == excludeName) { excludeAna = i; break; }
    }

    if (type == "binary") {
        if (server.hasArg("modbusCoil")) {
            uint16_t coil = server.arg("modbusCoil").toInt();
            modbusOk = !state_is_coil_used(coil, excludeBin);
        }
        if (server.hasArg("bacnetInstance")) {
            uint16_t inst = server.arg("bacnetInstance").toInt();
            bacnetOk = !state_is_bv_used(inst, excludeBin);
        }
    } else if (type == "analog") {
        if (server.hasArg("modbusReg")) {
            uint16_t reg = server.arg("modbusReg").toInt();
            modbusOk = !state_is_reg_used(reg, excludeAna);
        }
        if (server.hasArg("bacnetInstance")) {
            uint16_t inst = server.arg("bacnetInstance").toInt();
            bacnetOk = !state_is_av_used(inst, excludeAna);
        }
    }

    String json = "{\"modbusOk\":";
    json += modbusOk ? "true" : "false";
    json += ",\"bacnetOk\":";
    json += bacnetOk ? "true" : "false";
    json += ",\"available\":";
    json += (modbusOk && bacnetOk) ? "true" : "false";
    json += "}";
    server.send(200, "application/json", json);
}

// ==============================================================================
// GET /api/scan — Wi-Fi Network Scan
// ==============================================================================

void handleScan() {
    int currentMode = WiFi.getMode();
    if (currentMode == WIFI_AP) WiFi.mode(WIFI_AP_STA);

    int    n    = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        if (i < n - 1) json += ",";
    }
    json += "]";

    if (currentMode == WIFI_AP) WiFi.mode(WIFI_AP);
    server.send(200, "application/json", json);
}

// ==============================================================================
// GET /api/switch_network — Credential Persist & Reboot
// ==============================================================================

void handleSwitchNetwork() {
    String mode = server.arg("mode");
    Preferences pref;
    pref.begin("bms-app", false);

    if (mode == "local") {
        pref.putBool("force_ap", true);
        if (server.hasArg("ap_ssid") && server.arg("ap_ssid").length() > 0)
            pref.putString("ap_ssid", server.arg("ap_ssid"));
        if (server.hasArg("ap_pass") && server.arg("ap_pass").length() >= 8)
            pref.putString("ap_pass", server.arg("ap_pass"));
    } else {
        pref.putBool("force_ap", false);
        if (server.hasArg("ssid") && server.arg("ssid").length() > 0)
            pref.putString("sta_ssid", server.arg("ssid"));
        if (server.hasArg("pass"))
            pref.putString("sta_pass", server.arg("pass"));
    }
    pref.end();

    server.send(200, "text/plain", "Rebooting...");
    pendingReboot = true;
}

// ==============================================================================
// SERVER INITIALISATION & MAIN LOOP
// ==============================================================================

void web_server_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] FATAL: LittleFS mount failed. Upload the filesystem image.");
        return;
    }
    Serial.println("[WEB] LittleFS mounted.");

    state_load_from_json();

    server.serveStatic("/",          LittleFS, "/index.html");
    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/app.js",    LittleFS, "/app.js");

    server.on("/api/data",   handleData);
    server.on("/api/system", handleSystem);

    server.on("/api/config",         HTTP_GET,  handleGetConfig);
    server.on("/api/config/layout",  HTTP_POST, handlePostLayout);
    server.on("/api/point/add",      HTTP_POST, handleAddPoint);
    server.on("/api/point/update",   HTTP_POST, handleUpdatePoint);
    server.on("/api/point/delete",   HTTP_POST, handleDeletePoint);
    server.on("/api/point/write",    HTTP_POST, handleWritePoint);
    server.on("/api/point/check",    HTTP_GET,  handleCheckAddress);

    server.on("/api/scan",           handleScan);
    server.on("/api/switch_network", handleSwitchNetwork);

    server.begin();
    Serial.println("[WEB] HTTP server started on port 80.");
}

void web_server_task() {
    server.handleClient();
}