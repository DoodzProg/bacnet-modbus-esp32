/**
 * @file        main.cpp
 * @brief       Main entry point for the Dynamic BMS Gateway.
 * @details     Handles the main loop, Wi-Fi initialization (persistent AP/STA),
 * mDNS routing, captive portal (AP mode), and launches the protocol tasks.
 * Important: State loading via JSON must occur before protocol initialization.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-09
 * @version     1.1.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>      // Local DNS resolution (bms-gateway.local)
#include <DNSServer.h>    // Captive portal — redirects all DNS queries in AP mode
#include <Preferences.h>  // Non-volatile memory (NVRAM) for network configuration
#include "config.h"
#include "secrets.h"
#include "state.h"

// Conditional includes based on platformio.ini flags
#ifdef ENABLE_MODBUS
  #include "modbus_handler.h"
#endif

#ifdef ENABLE_BACNET
  #include "bacnet_handler.h"
#endif

#ifdef ENABLE_WEB
  #include "web_handler.h"
#endif

// ==============================================================================
// GLOBAL VARIABLES
// ==============================================================================

// AP Fallback credentials (fallback defaults, overridden by NVS at runtime)
const char* AP_SSID = "BMS-Gateway-Config";
const char* AP_PASS = "admin1234";

// Reboot flag triggered safely outside of async web server callbacks
extern volatile bool pendingReboot;

// True when the device is operating in AP mode (forced or fallback).
// Exported so web_handler.cpp can skip HTTP auth in AP mode.
bool g_isAPMode = false;

// DNS server for captive portal (AP mode only)
static DNSServer dnsServer;
static const byte DNS_PORT = 53;

// ==============================================================================
// HELPERS
// ==============================================================================

/**
 * @brief Sanitises a user-supplied device name into a valid mDNS hostname segment.
 * @details Converts to lowercase and replaces any character that is not
 *          alphanumeric or a hyphen with a hyphen. Result is capped at 32 chars.
 * @param  raw  Raw device name from NVS.
 * @return Sanitised lowercase hostname segment (e.g. "hvac-ahu-01").
 */
static String _sanitise_hostname(const String& raw) {
    String out = raw;
    out.toLowerCase();
    for (int i = 0; i < (int)out.length(); i++) {
        char c = out[i];
        if (!isAlphaNumeric(c) && c != '-') out[i] = '-';
    }
    if (out.length() > 32) out = out.substring(0, 32);
    return out;
}

// ==============================================================================
// MAIN ARDUINO SETUP
// ==============================================================================
void setup() {
    Serial.begin(115200);

    // 1. Initialize NVRAM and read parameters silently
    Preferences pref;
    pref.begin("bms-app", false);
    bool forceAP = pref.getBool("force_ap", false);

    // Using isKey prevents verbose [E][Preferences] errors in the terminal on first boot
    String current_sta_ssid = pref.isKey("sta_ssid") ? pref.getString("sta_ssid") : String(WIFI_SSID);
    String current_sta_pass = pref.isKey("sta_pass") ? pref.getString("sta_pass") : String(WIFI_PASS);
    String deviceName       = pref.isKey("device_name") ? pref.getString("device_name") : String(DEFAULT_DEVICE_NAME);
    String nvs_ap_ssid      = pref.isKey("ap_ssid")     ? pref.getString("ap_ssid")     : String("");
    pref.end();

    // Build mDNS hostname and AP SSID from device name (or fall back to defaults)
    String mdnsName;
    String apSsid;
    if (deviceName.length() > 0) {
        String safe = _sanitise_hostname(deviceName);
        mdnsName = "bms-" + safe;
        String upper = deviceName; upper.toUpperCase();
        apSsid = "BMS-GW-" + upper;
        if (apSsid.length() > 32) apSsid = apSsid.substring(0, 32);
    } else {
        mdnsName = "bms-gateway";
        apSsid   = String(AP_SSID); // "BMS-Gateway-Config"
    }
    // User-supplied AP SSID (set via web UI) takes priority over the derived value
    if (nvs_ap_ssid.length() > 0) {
        apSsid = nvs_ap_ssid;
        if (apSsid.length() > 32) apSsid = apSsid.substring(0, 32);
    }

    // Read AP password from NVS (allows changing it via web UI)
    Preferences prefAP;
    prefAP.begin("bms-app", true);
    String current_ap_pass = prefAP.isKey("ap_pass") ? prefAP.getString("ap_pass") : String(AP_PASS);
    prefAP.end();

    Serial.println("\n=================================");

    // 2. Exclusive network selection logic (AP or STA)
    if (forceAP) {
        // --- AP MODE FORCED BY USER (Persistent) ---
        Serial.println("[WIFI] Access Point Mode FORCED (User Request)");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSsid.c_str(), current_ap_pass.c_str());
        g_isAPMode = true;

        Serial.print("[AP] SSID       : "); Serial.println(apSsid);
        Serial.print("[AP] IP Address : "); Serial.println(WiFi.softAPIP());
    } else {
        // --- STANDARD STA MODE (With AP Fallback) ---
        Serial.println("[WIFI] Initializing Wi-Fi (Priority STA Mode)...");
        WiFi.mode(WIFI_STA);

        Serial.print("[STA] Connecting to: "); Serial.println(current_sta_ssid);
        WiFi.begin(current_sta_ssid.c_str(), current_sta_pass.c_str());

        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < 20) {
            delay(500);
            Serial.print(".");
            retries++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n--- WIFI CONNECTED (STA) ---");
            Serial.print("[STA] IP Address : "); Serial.println(WiFi.localIP());
            Serial.print("[STA] mDNS       : http://"); Serial.print(mdnsName); Serial.println(".local");
        } else {
            // FALLBACK: Router unreachable, activate local AP
            Serial.println("\n[!] STA connection failed. Switching to AP mode (Fallback).");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(apSsid.c_str(), current_ap_pass.c_str());
            g_isAPMode = true;

            Serial.print("[AP] Fallback SSID : "); Serial.println(apSsid);
            Serial.print("[AP] IP Address    : "); Serial.println(WiFi.softAPIP());
        }
    }

    // 3. Start captive portal DNS server in AP mode
    //    Responds to ALL DNS queries with the device IP, triggering the OS
    //    captive portal browser automatically when a client connects.
    if (g_isAPMode) {
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        Serial.println("[AP] Captive portal DNS started.");
    }

    // 4. Enable mDNS
    if (MDNS.begin(mdnsName.c_str())) {
        Serial.print("[SYSTEM] mDNS active: http://"); Serial.print(mdnsName); Serial.println(".local");
    }

    Serial.println("=================================\n");

    // 5. CORE BOOT SEQUENCE - STRICT ORDER REQUIRED
    // The Web Server MUST initialize first because it mounts LittleFS
    // and loads the dynamic JSON configuration (state_load_from_json).
#ifdef ENABLE_WEB
    Serial.println("[SYSTEM] Mounting LittleFS and loading dynamic JSON config...");
    web_server_init();
    Serial.println("[SYSTEM] Web Server & Dynamic State ready.");
#endif

    // 6. PROTOCOL BOOT SEQUENCE
#ifdef ENABLE_MODBUS
    modbus_init();
    Serial.println("[SYSTEM] Modbus TCP ready.");
#endif

#ifdef ENABLE_BACNET
    IPAddress bacnet_bind_ip;
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
        bacnet_bind_ip = WiFi.localIP();
        Serial.println("[SYSTEM] Assigning BACnet to STA (Primary Network)");
    } else {
        bacnet_bind_ip = WiFi.softAPIP();
        Serial.println("[SYSTEM] Assigning BACnet to AP (Fallback/Forced Network)");
    }
    bacnet_init(bacnet_bind_ip);
    Serial.println("[SYSTEM] BACnet/IP ready.");
#endif
}

// ==============================================================================
// MAIN ARDUINO LOOP
// ==============================================================================
void loop() {
    // 1. Process captive portal DNS (AP mode only)
    if (g_isAPMode) dnsServer.processNextRequest();

    // 2. Process Network Tasks
#ifdef ENABLE_MODBUS
    modbus_task();
#endif

#ifdef ENABLE_WEB
    web_server_task();
#endif

#ifdef ENABLE_BACNET
    bacnet_task();
#endif

    // 3. Synchronize States (Modbus -> Global State -> BACnet)
#ifdef ENABLE_MODBUS
    modbus_sync_from_registers();
#endif

#ifdef ENABLE_BACNET
    bacnet_update_objects();
#endif

    // 4. Safe Reboot Handling (Triggered by Web UI modifications)
    if (pendingReboot) {
        Serial.println("\n[SYSTEM] Configuration changed by Web UI. Executing Safe Reboot...");
        delay(500);
        ESP.restart();
    }
}
