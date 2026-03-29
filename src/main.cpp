/*
 * AHU (Air Handling Unit) Multi-Protocol Simulator
 * Target Hardware: ESP32-S3 (WROOM/N16R8)
 * * This firmware simultaneously exposes three interfaces:
 * - BACnet/IP (UDP Port 47808)
 * - Modbus TCP (TCP Port 502)
 * - HTTP Web Server (TCP Port 80)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ModbusIP_ESP8266.h>
#include <WiFiUdp.h>
#include "secrets.h"

// ============================================================
// CONFIGURATION
// ============================================================
// Built-in LED pin. Set to 16 for the YD-ESP32-S3 N16R8 board used in this project !
// If you use a different ESP32-S3 board, update this to match your specific hardware ;)
#define LED_PIN 16
#define BACNET_PORT 47808

// ============================================================
// GLOBAL STATE VARIABLES
// ============================================================
// These variables act as the central memory shared across all 3 protocols.
bool     fanStatus = false; // ON/OFF state of the AHU fan
uint16_t fanSpeed  = 0;     // Fan speed percentage (0-100%)
uint16_t tempSet   = 210;   // Temperature setpoint (Multiplier x10, e.g., 210 = 21.0°C)

// ============================================================
// SERVER INSTANCES
// ============================================================
WebServer server(80);       // HTTP server for the dashboard
ModbusIP  mb;               // Modbus TCP server handler
WiFiUDP   bacnetUDP;        // UDP socket specifically for BACnet communications
bool      udpStarted = false; // Flag to ensure UDP socket is initialized only once

// ============================================================
// UDP BRIDGE (C++ Functions exposed to the BACnet C Stack)
// ============================================================
// The standard BACnet-stack (written in C) cannot natively access the ESP32's 
// network hardware. We provide these C++ wrapper functions to act as a bridge.

/**
 * Retrieves the local IP address of the ESP32 in a 32-bit integer format.
 * This is required by the BACnet stack to identify itself on the network.
 */
static uint32_t _get_local_ip() {
    IPAddress ip = WiFi.localIP(); 
    // Pack the 4 bytes of the IP address into a single uint32_t (Little Endian format)
    return (uint32_t)ip[0]        |
           ((uint32_t)ip[1] << 8) |
           ((uint32_t)ip[2] << 16)|
           ((uint32_t)ip[3] << 24);
}

/**
 * Sends a raw UDP packet containing a BACnet datagram.
 */
static int _udp_send(uint32_t dest_ip, uint16_t dest_port, uint8_t* data, uint16_t len) {
    // Ensure the UDP socket is open before trying to send
    if (!udpStarted) { bacnetUDP.begin(BACNET_PORT); udpStarted = true; }
    
    // Unpack the 32-bit destination IP back into an Arduino IPAddress object
    IPAddress ip(dest_ip & 0xFF,
                (dest_ip >> 8)  & 0xFF,
                (dest_ip >> 16) & 0xFF,
                (dest_ip >> 24) & 0xFF);
                
    bacnetUDP.beginPacket(ip, dest_port);
    bacnetUDP.write(data, len);
    
    // Return the length of data sent if successful, otherwise -1
    return bacnetUDP.endPacket() ? len : -1;
}

/**
 * Polls the UDP socket to check if a BACnet datagram has arrived.
 */
static int _udp_recv(uint32_t* src_ip, uint16_t* src_port, uint8_t* buf, uint16_t max_len) {
    if (!udpStarted) { bacnetUDP.begin(BACNET_PORT); udpStarted = true; }
    
    int len = bacnetUDP.parsePacket();
    // If no packet is available, exit early
    if (len <= 0) return 0;
    
    // Retrieve sender's details
    *src_port = bacnetUDP.remotePort();
    IPAddress ip = bacnetUDP.remoteIP();
    
    // Pack sender's IP into uint32_t
    *src_ip = (uint32_t)ip[0]        |
              ((uint32_t)ip[1] << 8) |
              ((uint32_t)ip[2] << 16)|
              ((uint32_t)ip[3] << 24);
              
    // Prevent buffer overflow
    if (len > max_len) len = max_len;
    
    // Read the payload into the provided buffer
    return bacnetUDP.read(buf, len);
}

// ============================================================
// EXTERN "C" LINKAGE FOR THE DATALINK LAYER
// ============================================================
// These functions bind our custom C++ bridge to the 'bip-esp32.c' datalink file.
extern "C" {
    uint32_t esp32_get_local_ip(void)                                { return _get_local_ip(); }
    int esp32_udp_send(uint32_t d, uint16_t p, uint8_t* b, uint16_t l) { return _udp_send(d, p, b, l); }
    int esp32_udp_recv(uint32_t* i, uint16_t* p, uint8_t* b, uint16_t l) { return _udp_recv(i, p, b, l); }
}

// ============================================================
// BACNET STACK INCLUDES (Pure C)
// ============================================================
extern "C" {
    #include "bacnet/bacdef.h"
    #include "bacnet/bacstr.h"
    #include "bacnet/config.h" 
    
    // --- C/C++ COMPILER FIX ---
    // Resolve naming conflicts between standard C implementations and C++ compilers
    #define BACnetHostOctetString BACnet_Octet_String
    #define BACnetHostCharacterString BACnet_Character_String

    #include "bacnet/apdu.h"
    #include "bacnet/npdu.h"
    #include "bacnet/datalink/datalink.h"
    #include "bacnet/datalink/bip.h"
    #include "bacnet/basic/object/av.h"
    #include "bacnet/basic/object/bv.h"
    
    // Required services for device discovery and data reading
    #include "bacnet/basic/services.h" 
    #include "bacnet/basic/service/h_whois.h"
    #include "bacnet/basic/service/h_iam.h"
    #include "bacnet/basic/service/h_rp.h"
}

// Ensure the transmission buffer size is defined to prevent compilation errors
#ifndef MAX_MPDU
#define MAX_MPDU MAX_APDU 
#endif
uint8_t Handler_Transmit_Buffer[MAX_MPDU];

// ============================================================
// WEB INTERFACE (Embedded HTML/JS)
// ============================================================
// Stored in PROGMEM to save RAM. Contains the UI and the AJAX polling logic.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Supervision CTA</title>
  <style>
    body { font-family: 'Segoe UI', sans-serif; background:#1e272e; color:#d2dae2; text-align:center; margin-top:50px; }
    .dashboard { display:flex; justify-content:center; gap:20px; flex-wrap:wrap; max-width:900px; margin:auto; }
    .card { background:#485460; padding:20px; border-radius:10px; width:200px; }
    h2 { font-size:14px; color:#0fbcf9; text-transform:uppercase; }
    .value { font-size:36px; font-weight:bold; margin:15px 0; }
    .off { color:#ff3f34; } .on { color:#0be881; }
    .badge { font-size:11px; background:#2d3436; border-radius:4px; padding:2px 6px; color:#b2bec3; }
  </style>
</head>
<body>
  <h1>Simulateur CTA</h1>
  <p><span class="badge">Modbus TCP :502</span> &nbsp; <span class="badge">BACnet/IP :47808</span></p>
  <div class="dashboard">
    <div class="card">
      <h2>Ventilateur</h2>
      <div id="fanStatus" class="value off">ARRET</div>
    </div>
    <div class="card">
      <h2>Vitesse Vent.</h2>
      <div id="fanSpeed" class="value">0 %</div>
    </div>
    <div class="card">
      <h2>Consigne Temp.</h2>
      <div id="tempSet" class="value">0.0 &deg;C</div>
    </div>
  </div>
  <script>
    // AJAX loop: Fetches data every 1 second and updates the DOM
    setInterval(() => {
      fetch('/api/data').then(r => r.json()).then(d => {
        let el = document.getElementById("fanStatus");
        el.innerHTML = d.fan_status ? "MARCHE" : "ARRET";
        el.className = "value " + (d.fan_status ? "on" : "off");
        document.getElementById("fanSpeed").innerHTML = d.fan_speed + " %";
        document.getElementById("tempSet").innerHTML = (d.temp_set/10).toFixed(1) + " &deg;C";
      });
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

/**
 * HTTP API Endpoint callback.
 * Serializes the current global states into a JSON string and sends it to the client.
 */
void handleData() {
    String json = "{";
    json += "\"fan_status\":" + String(fanStatus ? "true" : "false") + ",";
    json += "\"fan_speed\":"  + String(fanSpeed) + ",";
    json += "\"temp_set\":"   + String(tempSet);
    json += "}";
    server.send(200, "application/json", json);
}

// ============================================================
// BACNET OPTIMIZATION STUBS (Bypassing device.c)
// ============================================================
// To save significant RAM and Flash memory, we exclude the heavy default 
// 'device.c' object from compilation. Instead, we provide these mandatory 
// stub functions to satisfy the linker and manually handle BACnet routing.
extern "C" {
    uint32_t Device_Object_Instance_Number(void) { return 1234; }
    bool Device_Set_Object_Instance_Number(uint32_t n) { (void)n; return true; }
    bool Device_Set_Object_Name(const BACNET_CHARACTER_STRING* s) { (void)s; return true; }
    bool Device_Valid_Object_Instance_Number(uint32_t n) { return n == 1234; }
    unsigned Device_Count(void) { return 1; }
    uint32_t Device_Index_To_Instance(unsigned i) { (void)i; return 1234; }

    int Device_Segmentation_Supported(void) { return 0; } // SEGMENTATION_NONE
    uint16_t Device_Vendor_Identifier(void) { return 0; } // Vendor ID 0 = Unknown/Custom

  /**
   * Main router for BACnet ReadProperty requests.
   * Intercepts incoming read requests and directs them to the correct local object.
   */
  int Device_Read_Property(BACNET_READ_PROPERTY_DATA* rpdata) {
      // Debug logging for incoming read requests
      Serial.print("Device_Read_Property: type=");
      Serial.print(rpdata->object_type);
      Serial.print(" instance=");
      Serial.print(rpdata->object_instance);
      Serial.print(" property=");
      Serial.println(rpdata->object_property);

      // Delegate Analog Value and Binary Value requests to their respective handlers
      if (rpdata->object_type == OBJECT_ANALOG_VALUE)
          return Analog_Value_Read_Property(rpdata);
      if (rpdata->object_type == OBJECT_BINARY_VALUE)
          return Binary_Value_Read_Property(rpdata);

      // Handle mandatory Device Object properties (crucial for network discovery)
      if (rpdata->object_property == PROP_OBJECT_LIST) {
          int apdu_len = 0;
          uint8_t* apdu = rpdata->application_data;
          
          if (rpdata->array_index == 0) {
              // Array index 0 requests the total count of objects
              apdu_len = encode_application_unsigned(apdu, 4);
          } else if (rpdata->array_index == BACNET_ARRAY_ALL) {
              // Return the full list of objects supported by this device
              apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_DEVICE, 1234);
              apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_ANALOG_VALUE, 0);
              apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_ANALOG_VALUE, 1);
              apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_BINARY_VALUE, 0);
          } else {
              // Return a specific object based on the requested array index
              switch (rpdata->array_index) {
                  case 1: apdu_len = encode_application_object_id(apdu, OBJECT_DEVICE, 1234); break;
                  case 2: apdu_len = encode_application_object_id(apdu, OBJECT_ANALOG_VALUE, 0); break;
                  case 3: apdu_len = encode_application_object_id(apdu, OBJECT_ANALOG_VALUE, 1); break;
                  case 4: apdu_len = encode_application_object_id(apdu, OBJECT_BINARY_VALUE, 0); break;
                  default: return BACNET_STATUS_ERROR; // Out of bounds
              }
          }
          rpdata->application_data_len = apdu_len;
          return apdu_len;
      }
      
      // Respond with the device name
      if (rpdata->object_property == PROP_OBJECT_NAME) {
          int apdu_len = 0;
          uint8_t* apdu = rpdata->application_data;
          BACNET_CHARACTER_STRING name;
          characterstring_init_ansi(&name, "ESP32-CTA-Sim");
          apdu_len = encode_application_character_string(apdu, &name);
          rpdata->application_data_len = apdu_len;
          return apdu_len;
      }
      
      // List of supported properties for the Device object
      if (rpdata->object_property == PROP_PROPERTY_LIST) {
          int apdu_len = 0;
          uint8_t* apdu = rpdata->application_data;
          apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_OBJECT_NAME);
          apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_OBJECT_TYPE);
          apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_PRESENT_VALUE);
          apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_STATUS_FLAGS);
          rpdata->application_data_len = apdu_len;
          return apdu_len;
      }
      
      // Respond with the object type (Device)
      if (rpdata->object_property == PROP_OBJECT_TYPE) {
          int apdu_len = 0;
          uint8_t* apdu = rpdata->application_data;
          apdu_len = encode_application_enumerated(apdu, OBJECT_DEVICE);
          rpdata->application_data_len = apdu_len;
          return apdu_len;
      }
      
      // If the requested property is unknown or unsupported, return an error
      return BACNET_STATUS_ERROR;
  }
  
    // Remaining stubs to satisfy linker dependencies
    uint32_t Network_Port_Index_To_Instance(unsigned i) { (void)i; return 0; }
    bool Analog_Value_Name_Set(uint32_t object_instance, const char *new_name);
    bool Binary_Value_Name_Set(uint32_t object_instance, const char *new_name);
}

// ============================================================
// BACNET INITIALIZATION & UPDATER
// ============================================================

/**
 * Configures the BACnet stack, creates objects, and broadcasts the "I-Am" message.
 */
void bacnet_init() {
    Device_Set_Object_Instance_Number(1234);

    BACNET_CHARACTER_STRING bacnet_name;
    characterstring_init_ansi(&bacnet_name, "ESP32-CTA-Sim");
    Device_Set_Object_Name(&bacnet_name);

    // Initialize Analog Value objects
    Analog_Value_Create(0);
    Analog_Value_Name_Set(0, "FanSpeed");

    Analog_Value_Create(1);
    Analog_Value_Name_Set(1, "TempSetpoint");

    // Initialize Binary Value object
    Binary_Value_Create(0);
    Binary_Value_Name_Set(0, "FanStatus");

    // Map network services to their respective handlers
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);

    // Initialize the BACnet/IP DataLink layer
    bip_set_port(BACNET_PORT);
    BACNET_IP_ADDRESS local_addr = {0};
    IPAddress ip = WiFi.localIP();
    local_addr.address[0] = ip[0];
    local_addr.address[1] = ip[1];
    local_addr.address[2] = ip[2];
    local_addr.address[3] = ip[3];
    local_addr.port = BACNET_PORT;
    
    bip_set_addr(&local_addr);
    bip_set_subnet_prefix(24); // Assume standard /24 subnet
    bip_init(NULL);

    // Broadcast presence to the network
    Send_I_Am(&Handler_Transmit_Buffer[0]);
    Serial.println("I-Am envoye !");
}

/**
 * Synchronizes the physical/Modbus global variables into the BACnet stack structures.
 */
void bacnet_update_objects() {
    // BV 0 = Fan Status
    BACNET_BINARY_PV bv = fanStatus ? BINARY_ACTIVE : BINARY_INACTIVE;
    Binary_Value_Present_Value_Set(0, bv);

    // AV 0 = Fan Speed (0.0 - 100.0)
    // Priority 16 (lowest priority) is used for default values
    Analog_Value_Present_Value_Set(0, (float)fanSpeed, 16);

    // AV 1 = Temperature Setpoint (Requires division by 10 for Modbus scaling logic)
    Analog_Value_Present_Value_Set(1, (float)tempSet / 10.0f, 16);
}

/**
 * Non-blocking task that checks for incoming BACnet UDP packets and processes them.
 */
void bacnet_task() {
    uint8_t pdu[MAX_APDU];
    BACNET_ADDRESS src = {0};
    
    // Check if any packet was received by the Datalink layer
    uint16_t pdu_len = bip_receive(&src, pdu, MAX_APDU, 0);
    
    if (pdu_len > 0) {
        // --- Packet Debugging Logs ---
        Serial.print(">>> PDU recu, taille : ");
        Serial.println(pdu_len);
        Serial.print("    src IP : ");
        Serial.print(src.mac[0]); Serial.print(".");
        Serial.print(src.mac[1]); Serial.print(".");
        Serial.print(src.mac[2]); Serial.print(".");
        Serial.print(src.mac[3]);
        Serial.print("  port : ");
        Serial.println((src.mac[4] << 8) | src.mac[5]);
        Serial.print("    pdu[0]=0x"); Serial.print(pdu[0], HEX);
        Serial.print(" pdu[1]=0x"); Serial.println(pdu[1], HEX);
        
        Serial.print("    net="); Serial.print(src.net);
        Serial.print(" mac_len="); Serial.println(src.mac_len);
        Serial.print("    PDU complet : ");
        
        // Print the first few bytes of the payload for inspection
        for (int i = 0; i < pdu_len && i < 12; i++) {
            Serial.print("0x"); Serial.print(pdu[i], HEX); Serial.print(" ");
        }
        Serial.println();

        // Forward the packet to the Network/Application layer handlers
        npdu_handler(&src, pdu, pdu_len);
        Serial.println("    npdu_handler execute.");
    }
}

// ============================================================
// MAIN ARDUINO SETUP & LOOP
// ============================================================
void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // 1. Initialize Wi-Fi in Station Mode (Client)
    WiFi.mode(WIFI_STA);
    
    // 2. Connect to the existing Wi-Fi network (credentials loaded from secrets.h)
    WiFi.begin(WIFI_SSID, WIFI_PASS); 
    
    Serial.print("\nConnexion au WiFi...");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    
    Serial.println("\n--- WIFI CONNECTE ---");
    Serial.print("Nouvelle IP de l'ESP32 : "); 
    Serial.println(WiFi.localIP());

    // 3. Initialize Modbus TCP Server
    mb.server();
    mb.addCoil(10);   // Register for Fan Status
    mb.addHreg(100);  // Register for Fan Speed
    mb.addHreg(101);  // Register for Temperature Setpoint

    // 4. Initialize HTTP Web Server
    server.on("/", []() { server.send(200, "text/html", INDEX_HTML); });
    server.on("/api/data", handleData);
    server.begin();

    // 5. Initialize BACnet Stack
    bacnet_init();

    // Startup confirmation logs
    Serial.println("Modbus TCP pret sur port 502");
    Serial.println("BACnet/IP pret sur port 47808");
    Serial.println("Serveur Web pret sur port 80");
}

void loop() {
    // 1. Process incoming network requests
    mb.task();
    server.handleClient();
    bacnet_task();

    // 2. Synchronization: Read states from the Modbus Registers (Source of Truth)
    fanStatus = mb.Coil(10);
    fanSpeed  = mb.Hreg(100);
    tempSet   = mb.Hreg(101);

    // 3. Synchronization: Push the updated states to the BACnet objects
    bacnet_update_objects();

    // 4. Hardware Feedback: Drive the onboard LED based on Fan Status
    digitalWrite(LED_PIN, fanStatus ? LOW : HIGH);
}