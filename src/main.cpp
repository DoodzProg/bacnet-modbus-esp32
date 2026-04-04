/**
 * @file        main.cpp
 * @brief       Main entry point for the Dynamic BMS Gateway.
 * @details     Handles the main loop, Wi-Fi initialization (persistent AP/STA), 
 * mDNS routing, and launches the tasks for the different protocols.
 * Important: State loading via JSON must occur before protocol initialization.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>      // Local DNS resolution (bms-gateway.local)
#include <Preferences.h>  // Non-volatile memory (NVRAM) for network configuration
#include "secrets.h"
#include "config.h"       // Keep this for hardware defines like LED_PIN
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

// AP Fallback credentials (Global variables mapping to extern definitions)
const char* AP_SSID = "BMS-Gateway-Config";
const char* AP_PASS = "admin1234";

// Reboot flag triggered safely outside of async web server callbacks
extern volatile bool pendingReboot;

// ==============================================================================
// MAIN ARDUINO SETUP
// ==============================================================================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // 1. Initialize NVRAM and read parameters silently
    Preferences pref;
    pref.begin("bms-app", false); 
    bool forceAP = pref.getBool("force_ap", false);
    
    // Using isKey prevents verbose [E][Preferences] errors in the terminal on first boot
    String current_sta_ssid = pref.isKey("sta_ssid") ? pref.getString("sta_ssid") : String(WIFI_SSID);
    String current_sta_pass = pref.isKey("sta_pass") ? pref.getString("sta_pass") : String(WIFI_PASS);
    String current_ap_ssid  = pref.isKey("ap_ssid")  ? pref.getString("ap_ssid")  : String(AP_SSID);
    String current_ap_pass  = pref.isKey("ap_pass")  ? pref.getString("ap_pass")  : String(AP_PASS);
    pref.end(); // Close memory cleanly immediately

    Serial.println("\n=================================");
    
    // 2. Exclusive network selection logic (AP or STA)
    if (forceAP) {
        // --- AP MODE FORCED BY USER (Persistent) ---
        Serial.println("[WIFI] Access Point Mode FORCED (User Request)");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(current_ap_ssid.c_str(), current_ap_pass.c_str());
        
        Serial.print("[AP] Network created: "); Serial.println(current_ap_ssid);
        Serial.print("[AP] IP Address     : "); Serial.println(WiFi.softAPIP());
    } 
    else {
        // --- STANDARD STA MODE (With AP Fallback trigger) ---
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
            Serial.print("[STA] IP Address: "); Serial.println(WiFi.localIP());
        } else {
            // FALLBACK: Router is unreachable, activate local AP
            Serial.println("\n[!] STA connection failed. Switching to AP mode (Fallback).");
            WiFi.disconnect(true);
            WiFi.mode(WIFI_AP);
            WiFi.softAP(current_ap_ssid.c_str(), current_ap_pass.c_str());
            
            Serial.print("[AP] Fallback network: "); Serial.println(current_ap_ssid);
            Serial.print("[AP] IP Address      : "); Serial.println(WiFi.softAPIP());
        }
    }

    // 3. Enable mDNS (Allows local domain resolution)
    if (MDNS.begin("bms-gateway")) {
        Serial.println("[SYSTEM] mDNS active. Web UI available at: http://bms-gateway.local");
    }

    Serial.println("=================================\n");

    // 4. CORE BOOT SEQUENCE - STRICT ORDER REQUIRED
    // The Web Server MUST initialize first because it mounts LittleFS
    // and loads the dynamic JSON configuration (state_load_from_json).
#ifdef ENABLE_WEB
    Serial.println("[SYSTEM] Mounting LittleFS and loading dynamic JSON config...");
    web_server_init();
    Serial.println("[SYSTEM] Web Server & Dynamic State ready.");
#endif

    // 5. PROTOCOL BOOT SEQUENCE
    // Now that variables are loaded in RAM, we can expose them to the buses.
#ifdef ENABLE_MODBUS
    modbus_init();
    Serial.println("[SYSTEM] Modbus TCP ready.");
#endif

#ifdef ENABLE_BACNET
    IPAddress bacnet_bind_ip;
    
    // Dynamically assign BACnet binding IP based on the active Wi-Fi mode
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
    // 1. Process Network Tasks
#ifdef ENABLE_MODBUS
    modbus_task();
#endif

#ifdef ENABLE_WEB
    web_server_task();
#endif

#ifdef ENABLE_BACNET
    bacnet_task();
#endif

    // 2. Synchronize States (Modbus -> Global State -> BACnet)
#ifdef ENABLE_MODBUS
    modbus_sync_from_registers();
#endif

#ifdef ENABLE_BACNET
    bacnet_update_objects();
#endif

    // 3. Hardware Feedback (Status LED based on first binary point if it exists)
    if (NUM_BINARY_POINTS > 0) {
        // Assuming LOW triggers the LED on this specific board
        digitalWrite(LED_PIN, binaryPoints[0].value ? LOW : HIGH);
    }

    // 4. Safe Reboot Handling (Triggered by Web UI modifications)
    if (pendingReboot) {
        Serial.println("\n[SYSTEM] Configuration changed by Web UI. Executing Safe Reboot...");
        delay(500); // Give the TCP socket a moment to flush the 200 OK response
        ESP.restart();
    }
}