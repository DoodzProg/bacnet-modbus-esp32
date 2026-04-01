/**
 * @file bacnet_handler.cpp
 * @brief BACnet/IP protocol handler and dynamic binding implementation.
 * @details Integrates the pure C BACnet stack, handles UDP socket routing, 
 * maps internal state to BACnet objects, and intercepts write properties.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#include "bacnet_handler.h"
#include "config.h"
#include "state.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#ifdef ENABLE_MODBUS
  #include "modbus_handler.h"
#endif

// ==============================================================================
// SERVER INSTANCES & UDP WRAPPERS
// ==============================================================================

WiFiUDP bacnetUDP;          
bool    udpStarted = false; 
IPAddress current_bacnet_ip;

/**
 * @brief Helper to retrieve the local IP in standard 32-bit format.
 */
static uint32_t _get_local_ip() {
    return (uint32_t)current_bacnet_ip[0] | ((uint32_t)current_bacnet_ip[1] << 8) | 
           ((uint32_t)current_bacnet_ip[2] << 16) | ((uint32_t)current_bacnet_ip[3] << 24);
}

/**
 * @brief Helper to send UDP packets for the BACnet C stack.
 */
static int _udp_send(uint32_t dest_ip, uint16_t dest_port, uint8_t* data, uint16_t len) {
    if (!udpStarted) { bacnetUDP.begin(BACNET_PORT); udpStarted = true; }
    IPAddress ip(dest_ip & 0xFF, (dest_ip >> 8) & 0xFF, (dest_ip >> 16) & 0xFF, (dest_ip >> 24) & 0xFF);
    bacnetUDP.beginPacket(ip, dest_port);
    bacnetUDP.write(data, len);
    return bacnetUDP.endPacket() ? len : -1;
}

/**
 * @brief Helper to receive UDP packets for the BACnet C stack.
 */
static int _udp_recv(uint32_t* src_ip, uint16_t* src_port, uint8_t* buf, uint16_t max_len) {
    if (!udpStarted) { bacnetUDP.begin(BACNET_PORT); udpStarted = true; }
    int len = bacnetUDP.parsePacket();
    if (len <= 0) return 0;
    
    *src_port = bacnetUDP.remotePort();
    IPAddress ip = bacnetUDP.remoteIP();
    *src_ip = (uint32_t)ip[0] | ((uint32_t)ip[1] << 8) | ((uint32_t)ip[2] << 16) | ((uint32_t)ip[3] << 24);
    if (len > max_len) len = max_len;
    return bacnetUDP.read(buf, len);
}

// Map native ESP32 UDP functions to the BACnet stack expectations
extern "C" {
    uint32_t esp32_get_local_ip(void)                                    { return _get_local_ip(); }
    int esp32_udp_send(uint32_t d, uint16_t p, uint8_t* b, uint16_t l)   { return _udp_send(d, p, b, l); }
    int esp32_udp_recv(uint32_t* i, uint16_t* p, uint8_t* b, uint16_t l) { return _udp_recv(i, p, b, l); }
}

// ==============================================================================
// BACNET STACK INCLUDES (Pure C)
// ==============================================================================

extern "C" {
    #include "bacnet/bacdef.h"
    #include "bacnet/bacstr.h"
    #include "bacnet/config.h" 
    
    #define BACnetHostOctetString BACnet_Octet_String
    #define BACnetHostCharacterString BACnet_Character_String

    #include "bacnet/apdu.h"
    #include "bacnet/npdu.h"
    #include "bacnet/datalink/datalink.h"
    #include "bacnet/datalink/bip.h"
    #include "bacnet/basic/object/av.h"
    #include "bacnet/basic/object/bv.h"
    
    #include "bacnet/basic/services.h" 
    #include "bacnet/basic/service/h_whois.h"
    #include "bacnet/basic/service/h_iam.h"
    #include "bacnet/basic/service/h_rp.h"
    #include "bacnet/basic/service/h_wp.h"
    #include "bacnet/bacapp.h" // VITAL FOR DECODING
}

#ifndef MAX_MPDU
#define MAX_MPDU MAX_APDU 
#endif
uint8_t Handler_Transmit_Buffer[MAX_MPDU];

// ==============================================================================
// DEVICE OBJECT OVERRIDES
// ==============================================================================

extern "C" {
    uint32_t Device_Object_Instance_Number(void) { return BACNET_DEVICE_ID; }
    bool Device_Set_Object_Instance_Number(uint32_t n) { (void)n; return true; }
    bool Device_Set_Object_Name(const BACNET_CHARACTER_STRING* s) { (void)s; return true; }
    bool Device_Valid_Object_Instance_Number(uint32_t n) { return n == BACNET_DEVICE_ID; }
    unsigned Device_Count(void) { return 1; }
    uint32_t Device_Index_To_Instance(unsigned i) { (void)i; return BACNET_DEVICE_ID; }
    int Device_Segmentation_Supported(void) { return 0; }
    uint16_t Device_Vendor_Identifier(void) { return 0; }

    bool Device_Objects_Property_List_Member(int object_type, uint32_t object_instance, int object_property) {
        if (object_type == OBJECT_ANALOG_VALUE || object_type == OBJECT_BINARY_VALUE) return true;
        if (object_type == OBJECT_DEVICE) {
            switch (object_property) {
                case PROP_OBJECT_LIST:
                case PROP_OBJECT_NAME:
                case PROP_PROPERTY_LIST:
                case PROP_OBJECT_TYPE:
                    return true;
                default:
                    return false;
            }
        }
        return false;
    }

    int Device_Read_Property(BACNET_READ_PROPERTY_DATA* rpdata) {
        if (rpdata->object_type == OBJECT_ANALOG_VALUE) return Analog_Value_Read_Property(rpdata);
        if (rpdata->object_type == OBJECT_BINARY_VALUE) return Binary_Value_Read_Property(rpdata);
        if (rpdata->object_property == PROP_OBJECT_LIST) {
            int apdu_len = 0; uint8_t* apdu = rpdata->application_data;
            int total_objects = 1 + NUM_ANALOG_POINTS + NUM_BINARY_POINTS;
            if (rpdata->array_index == 0) {
                apdu_len = encode_application_unsigned(apdu, total_objects);
            } else if (rpdata->array_index == BACNET_ARRAY_ALL) {
                apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_DEVICE, BACNET_DEVICE_ID);
                for (int i = 0; i < NUM_ANALOG_POINTS; i++) apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_ANALOG_VALUE, analogPoints[i].bac_instance);
                for (int i = 0; i < NUM_BINARY_POINTS; i++) apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_BINARY_VALUE, binaryPoints[i].bac_instance);
            } else {
                if (rpdata->array_index == 1) apdu_len = encode_application_object_id(apdu, OBJECT_DEVICE, BACNET_DEVICE_ID);
                else if (rpdata->array_index <= 1 + NUM_ANALOG_POINTS) apdu_len = encode_application_object_id(apdu, OBJECT_ANALOG_VALUE, analogPoints[rpdata->array_index - 2].bac_instance);
                else if (rpdata->array_index <= total_objects) apdu_len = encode_application_object_id(apdu, OBJECT_BINARY_VALUE, binaryPoints[rpdata->array_index - 2 - NUM_ANALOG_POINTS].bac_instance);
                else return BACNET_STATUS_ERROR; 
            }
            rpdata->application_data_len = apdu_len; return apdu_len;
        }
        if (rpdata->object_property == PROP_OBJECT_NAME) {
            // FIX: 'static' prevents stack overflow for large BACnet string structures
            static BACNET_CHARACTER_STRING name; 
            characterstring_init_ansi(&name, BACNET_DEVICE_NAME);
            rpdata->application_data_len = encode_application_character_string(rpdata->application_data, &name);
            return rpdata->application_data_len;
        }
        if (rpdata->object_property == PROP_PROPERTY_LIST) {
            int apdu_len = 0; uint8_t* apdu = rpdata->application_data;
            apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_OBJECT_NAME);
            apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_OBJECT_TYPE);
            apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_PRESENT_VALUE);
            apdu_len += encode_application_enumerated(&apdu[apdu_len], PROP_STATUS_FLAGS);
            rpdata->application_data_len = apdu_len; return apdu_len;
        }
        if (rpdata->object_property == PROP_OBJECT_TYPE) {
            rpdata->application_data_len = encode_application_enumerated(rpdata->application_data, OBJECT_DEVICE);
            return rpdata->application_data_len;
        }
        return BACNET_STATUS_ERROR;
    }

    // ==============================================================================
    // PROPERTY WRITE HANDLER (INTERCEPTOR)
    // ==============================================================================
    
    bool Device_Write_Property(BACNET_WRITE_PROPERTY_DATA* wp_data) {
        Serial.println("\n====================================");
        Serial.println("[BACNET] WRITE REQUEST INTERCEPTED!");
        Serial.printf(" - Object Type : %d\n", wp_data->object_type);
        Serial.printf(" - Instance    : %d\n", wp_data->object_instance);
        Serial.printf(" - Property ID : %d\n", wp_data->object_property);
        Serial.printf(" - Priority    : %d\n", wp_data->priority);
        Serial.printf(" - Payload Len : %d bytes\n", wp_data->application_data_len);

        if (wp_data->object_property == PROP_PRESENT_VALUE) {
            Serial.println("[BACNET] Attempting to write Present_Value...");
            
            // FIX: 'static' moves this large struct to global RAM to prevent crash
            static BACNET_APPLICATION_DATA_VALUE value;
            
            int decode_len = bacapp_decode_application_data(wp_data->application_data, wp_data->application_data_len, &value);
            
            if (decode_len < 0) {
                Serial.println("[ERROR] Cannot decode payload (Invalid BACnet Tag).");
                wp_data->error_class = ERROR_CLASS_PROPERTY;
                wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
                return false;
            }

            Serial.printf("[BACNET] Decoded Tag Type: %d\n", value.tag);

            if (wp_data->object_type == OBJECT_ANALOG_VALUE) {
                if (value.tag == BACNET_APPLICATION_TAG_REAL) {
                    float new_val = value.type.Real;
                    Serial.printf("[BACNET] Extracted Float Value: %.2f\n", new_val);
                    
                    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
                        if (analogPoints[i].bac_instance == wp_data->object_instance) {
                            Serial.printf("[BACNET] Found local matching AV (Index %d). Updating Modbus...\n", i);
                            analogPoints[i].value = new_val;
                            #ifdef ENABLE_MODBUS
                                modbus_write_analog(i);
                                Serial.println("[MODBUS] Register successfully updated!");
                            #endif
                            Analog_Value_Present_Value_Set(wp_data->object_instance, new_val, wp_data->priority);
                            Serial.println("[SUCCESS] Write fully processed. Sending ACK to YABE.");
                            Serial.println("====================================\n");
                            return true;
                        }
                    }
                    Serial.println("[ERROR] Instance not found in Analog Array.");
                } else if (value.tag == BACNET_APPLICATION_TAG_NULL) {
                    Serial.println("[BACNET] Received NULL (Relinquish command). Ignoring for now.");
                    return true; 
                } else {
                    Serial.println("[ERROR] Payload is not a REAL (Float) number!");
                }
            } 
            else if (wp_data->object_type == OBJECT_BINARY_VALUE) {
                if (value.tag == BACNET_APPLICATION_TAG_ENUMERATED) {
                    bool new_val = (value.type.Enumerated == BINARY_ACTIVE);
                    Serial.printf("[BACNET] Extracted Binary Value: %d\n", new_val);
                    
                    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
                        if (binaryPoints[i].bac_instance == wp_data->object_instance) {
                            
                            // Apply correction here: Update the global state
                            binaryPoints[i].value = new_val; 
                            
                            #ifdef ENABLE_MODBUS
                                modbus_write_binary(i);
                                Serial.println("[MODBUS] Coil successfully updated!");
                            #endif
                            Binary_Value_Present_Value_Set(wp_data->object_instance, new_val ? BINARY_ACTIVE : BINARY_INACTIVE);
                            Serial.println("[SUCCESS] Write processed. Sending ACK to YABE.");
                            Serial.println("====================================\n");
                            return true;
                        }
                    }
                }
            }

            wp_data->error_class = ERROR_CLASS_PROPERTY;
            wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
            return false;
        }

        Serial.println("[BACNET] Property is not Present_Value. Delegating to native stack...");
        if (wp_data->object_type == OBJECT_ANALOG_VALUE) return Analog_Value_Write_Property(wp_data);
        if (wp_data->object_type == OBJECT_BINARY_VALUE) return Binary_Value_Write_Property(wp_data);

        return false;
    }
  
    uint32_t Network_Port_Index_To_Instance(unsigned i) { (void)i; return 0; }
    bool Analog_Value_Name_Set(uint32_t object_instance, const char *new_name);
    bool Binary_Value_Name_Set(uint32_t object_instance, const char *new_name);
}

// ==============================================================================
// INITIALIZATION & TASK LOOP
// ==============================================================================

void bacnet_init(IPAddress ip) {
    current_bacnet_ip = ip;
    
    // 1. Basic Device configuration
    Device_Set_Object_Instance_Number(BACNET_DEVICE_ID);
    BACNET_CHARACTER_STRING bacnet_name;
    characterstring_init_ansi(&bacnet_name, BACNET_DEVICE_NAME);
    Device_Set_Object_Name(&bacnet_name);

    // 2. Dynamic Object Creation
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        Analog_Value_Create(analogPoints[i].bac_instance);
        Analog_Value_Name_Set(analogPoints[i].bac_instance, analogPoints[i].name);
    }
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        Binary_Value_Create(binaryPoints[i].bac_instance);
        Binary_Value_Name_Set(binaryPoints[i].bac_instance, binaryPoints[i].name);
    }

    // 3. Service Handlers Registration
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);

    // 4. Network Binding (Dynamic IP)
    bip_set_port(BACNET_PORT);
    
    BACNET_IP_ADDRESS local_addr = {0}; 
    local_addr.address[0] = ip[0];
    local_addr.address[1] = ip[1];
    local_addr.address[2] = ip[2];
    local_addr.address[3] = ip[3];
    local_addr.port = BACNET_PORT;
    
    bip_set_addr(&local_addr);
    bip_set_subnet_prefix(24); // Standard Subnet Mask (255.255.255.0)
    
    if (!bip_init(NULL)) {
        Serial.println("[BACNET] ERROR: Failed to initialize BIP stack!");
        return;
    }

    // 5. Announce presence on the network
    Send_I_Am(&Handler_Transmit_Buffer[0]);
    Serial.print("[BACNET] Stack bound to IP: ");
    Serial.println(ip);
}

void bacnet_update_objects() {
    // Keep internal values synchronized with BACnet objects
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        BACNET_BINARY_PV bv = binaryPoints[i].value ? BINARY_ACTIVE : BINARY_INACTIVE;
        Binary_Value_Present_Value_Set(binaryPoints[i].bac_instance, bv);
    }
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        Analog_Value_Present_Value_Set(analogPoints[i].bac_instance, analogPoints[i].value, 16);
    }
}

void bacnet_task() {
    static uint8_t pdu[MAX_APDU]; 
    static BACNET_ADDRESS src;
    
    uint16_t pdu_len = bip_receive(&src, pdu, MAX_APDU, 0);
    if (pdu_len > 0) {
        #ifdef ENABLE_MODBUS
        extern ModbusIP mb;  // Expose Modbus instance to allow calling mb.task() directly
        mb.task();           // Keeps Modbus connection alive during intensive BACnet processing
        #endif
        npdu_handler(&src, pdu, pdu_len);
    }
}