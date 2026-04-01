/**
 * @file web_handler.cpp
 * @brief REST API and Embedded Web Server utilizing LittleFS for front-end assets.
 * @details Serves front-end assets from flash memory and handles API requests 
 * for telemetry data, Wi-Fi scanning, network configuration, and system stats.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#include "web_handler.h"
#include "state.h"
#include "secrets.h" 
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>

extern const char* AP_SSID;
extern const char* AP_PASS;

WebServer server(80);

// ==============================================================================
// REST API ENDPOINTS
// ==============================================================================

/**
 * @brief Handles /api/data requests.
 * @details Fetches current RAM usage and iterates over state arrays.
 * @return JSON representation of system stats, Analog, and Binary points.
 */
void handleData() {
    // Note: To develop locally without flashing, uncomment CORS header below:
    // server.sendHeader("Access-Control-Allow-Origin", "*");
    
    String json = "{";

    // --- System RAM Monitoring ---
    uint32_t freeRam = ESP.getFreeHeap();
    uint32_t totalRam = ESP.getHeapSize();
    uint32_t usedRam = totalRam - freeRam;
    int ramPct = (usedRam * 100) / totalRam;
    
    json += "\"sys\":{";
    json += "\"ram_used\":" + String(usedRam) + ",";
    json += "\"ram_total\":" + String(totalRam) + ",";
    json += "\"ram_pct\":" + String(ramPct);
    json += "},";

    // --- Telemetry Points ---
    json += "\"binary\":[";
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        json += "{\"name\":\"" + String(binaryPoints[i].name) + "\",";
        json += "\"value\":" + String(binaryPoints[i].value ? "true" : "false") + "}";
        if (i < NUM_BINARY_POINTS - 1) json += ",";
    }
    json += "],";
    
    json += "\"analog\":[";
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        json += "{\"name\":\"" + String(analogPoints[i].name) + "\",";
        json += "\"value\":" + String(analogPoints[i].value) + "}";
        if (i < NUM_ANALOG_POINTS - 1) json += ",";
    }
    json += "]}";
    
    server.send(200, "application/json", json);
}

/**
 * @brief Handles /api/system requests.
 * @return JSON holding active network configurations and IPs.
 */
void handleSystem() {
    Preferences pref;
    pref.begin("bms-app", true); 
    
    String currentSTA = pref.isKey("sta_ssid") ? pref.getString("sta_ssid") : String(WIFI_SSID);
    String currentSTAPass = pref.isKey("sta_pass") ? pref.getString("sta_pass") : String(WIFI_PASS);
    String currentAP = pref.isKey("ap_ssid") ? pref.getString("ap_ssid") : String(AP_SSID);
    String currentAPPass = pref.isKey("ap_pass") ? pref.getString("ap_pass") : String(AP_PASS);
    pref.end();

    String json = "{";
    json += "\"isAP\":" + String(WiFi.getMode() == WIFI_AP ? "true" : "false") + ",";
    json += "\"staSSID\":\"" + currentSTA + "\",";
    json += "\"staPASS\":\"" + currentSTAPass + "\",";
    json += "\"apSSID\":\"" + currentAP + "\",";
    json += "\"apPASS\":\"" + currentAPPass + "\",";
    json += "\"clientIP\":\"" + server.client().remoteIP().toString() + "\",";
    json += "\"espIP\":\"" + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

/**
 * @brief Handles /api/scan requests.
 * @return JSON array of surrounding Wi-Fi networks (SSID and RSSI).
 */
void handleScan() {
    int currentMode = WiFi.getMode();
    // Momentarily switch to AP_STA to allow scanning while in AP mode
    if (currentMode == WIFI_AP) WiFi.mode(WIFI_AP_STA); 
    
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
        if (i < n - 1) json += ",";
    }
    json += "]";
    
    // Revert back to original mode
    if (currentMode == WIFI_AP) WiFi.mode(WIFI_AP); 
    server.send(200, "application/json", json);
}

/**
 * @brief Handles /api/switch_network requests.
 * @details Parses parameters, saves credentials to NVRAM, and triggers a reboot.
 */
void handleSwitchNetwork() {
    String mode = server.arg("mode");
    Preferences pref;
    pref.begin("bms-app", false);
    
    if (mode == "local") {
        pref.putBool("force_ap", true);
        if(server.hasArg("ap_ssid") && server.arg("ap_ssid").length() > 0) pref.putString("ap_ssid", server.arg("ap_ssid"));
        if(server.hasArg("ap_pass") && server.arg("ap_pass").length() >= 8) pref.putString("ap_pass", server.arg("ap_pass"));
    } else {
        pref.putBool("force_ap", false);
        if(server.hasArg("ssid") && server.arg("ssid").length() > 0) pref.putString("sta_ssid", server.arg("ssid"));
        if(server.hasArg("pass")) pref.putString("sta_pass", server.arg("pass"));
    }
    pref.end();

    server.send(200, "text/plain", "Rebooting...");
    
    // Ensure HTTP response is fully sent before restarting hardware
    server.client().flush();
    delay(1000); 
    ESP.restart();
}

// ==============================================================================
// SERVER INITIALIZATION & LOOP
// ==============================================================================

/**
 * @brief Mounts LittleFS, registers HTTP routes, and starts the web server.
 */
void web_server_init() {
    // 1. Mount the filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[WEB] ERROR: LittleFS mount failed. Did you upload the Filesystem Image?");
        return;
    }
    
    Serial.println("[WEB] LittleFS Mounted Successfully.");

    // 2. Static File Routing
    server.serveStatic("/", LittleFS, "/index.html");
    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/app.js", LittleFS, "/app.js");

    // 3. API Endpoints
    server.on("/api/data", handleData);
    server.on("/api/system", handleSystem);
    server.on("/api/scan", handleScan);
    server.on("/api/switch_network", handleSwitchNetwork);
    
    // Start Server
    server.begin();
}

/**
 * @brief Polls incoming client connections.
 * @note Should be called continuously in the main loop.
 */
void web_server_task() {
    server.handleClient();
}