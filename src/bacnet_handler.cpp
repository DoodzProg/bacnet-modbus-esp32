/**
 * @file        bacnet_handler.cpp
 * @brief       BACnet/IP protocol handler and dynamic binding implementation.
 * @details     Integrates the pure C BACnet stack, handles UDP socket routing, 
 * conditionally maps internal state to BACnet objects based on PROTO_BACNET, 
 * and intercepts write properties to maintain global synchronization.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
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

static uint32_t _get_local_ip() {
    return (uint32_t)current_bacnet_ip[0] | ((uint32_t)current_bacnet_ip[1] << 8) | 
           ((uint32_t)current_bacnet_ip[2] << 16) | ((uint32_t)current_bacnet_ip[3] << 24);
}

static int _udp_send(uint32_t dest_ip, uint16_t dest_port, uint8_t* data, uint16_t len) {
    if (!udpStarted) { bacnetUDP.begin(BACNET_PORT); udpStarted = true; }
    IPAddress ip(dest_ip & 0xFF, (dest_ip >> 8) & 0xFF, (dest_ip >> 16) & 0xFF, (dest_ip >> 24) & 0xFF);
    bacnetUDP.beginPacket(ip, dest_port);
    bacnetUDP.write(data, len);
    return bacnetUDP.endPacket() ? len : -1;
}

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

extern "C" {
    uint32_t esp32_get_local_ip(void)                                      { return _get_local_ip(); }
    int esp32_udp_send(uint32_t d, uint16_t p, uint8_t* b, uint16_t l)   { return _udp_send(d, p, b, l); }
    int esp32_udp_recv(uint32_t* i, uint16_t* p, uint8_t* b, uint16_t l) { return _udp_recv(i, p, b, l); }
}

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
    #include "bacnet/bacapp.h" 
}

#ifndef MAX_MPDU
#define MAX_MPDU MAX_APDU 
#endif
uint8_t Handler_Transmit_Buffer[MAX_MPDU];

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
            
            int active_av = 0;
            int active_bv = 0;
            for (int i = 0; i < NUM_ANALOG_POINTS; i++) { if (analogPoints[i].protocol & PROTO_BACNET) active_av++; }
            for (int i = 0; i < NUM_BINARY_POINTS; i++) { if (binaryPoints[i].protocol & PROTO_BACNET) active_bv++; }
            int total_objects = 1 + active_av + active_bv; 

            if (rpdata->array_index == 0) {
                apdu_len = encode_application_unsigned(apdu, total_objects);
            } 
            else if (rpdata->array_index == BACNET_ARRAY_ALL) {
                apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_DEVICE, BACNET_DEVICE_ID);
                for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
                    if (analogPoints[i].protocol & PROTO_BACNET) apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_ANALOG_VALUE, analogPoints[i].bacnetInstance);
                }
                for (int i = 0; i < NUM_BINARY_POINTS; i++) {
                    if (binaryPoints[i].protocol & PROTO_BACNET) apdu_len += encode_application_object_id(&apdu[apdu_len], OBJECT_BINARY_VALUE, binaryPoints[i].bacnetInstance);
                }
            } 
            else {
                if (rpdata->array_index == 1) {
                    apdu_len = encode_application_object_id(apdu, OBJECT_DEVICE, BACNET_DEVICE_ID);
                } 
                else if (rpdata->array_index <= 1 + active_av) {
                    int target = rpdata->array_index - 1; 
                    int current = 0;
                    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
                        if (analogPoints[i].protocol & PROTO_BACNET) {
                            current++;
                            if (current == target) {
                                apdu_len = encode_application_object_id(apdu, OBJECT_ANALOG_VALUE, analogPoints[i].bacnetInstance);
                                break;
                            }
                        }
                    }
                } 
                else if (rpdata->array_index <= total_objects) {
                    int target = rpdata->array_index - 1 - active_av;
                    int current = 0;
                    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
                        if (binaryPoints[i].protocol & PROTO_BACNET) {
                            current++;
                            if (current == target) {
                                apdu_len = encode_application_object_id(apdu, OBJECT_BINARY_VALUE, binaryPoints[i].bacnetInstance);
                                break;
                            }
                        }
                    }
                } 
                else {
                    return BACNET_STATUS_ERROR; 
                }
            }
            rpdata->application_data_len = apdu_len; return apdu_len;
        }
        
        if (rpdata->object_property == PROP_OBJECT_NAME) {
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

    bool Device_Write_Property(BACNET_WRITE_PROPERTY_DATA* wp_data) {
        Serial.println("\n====================================");
        Serial.println("[BACNET] WRITE REQUEST INTERCEPTED!");

        if (wp_data->object_property == PROP_PRESENT_VALUE) {
            static BACNET_APPLICATION_DATA_VALUE value;
            int decode_len = bacapp_decode_application_data(wp_data->application_data, wp_data->application_data_len, &value);
            
            if (decode_len < 0) {
                wp_data->error_class = ERROR_CLASS_PROPERTY;
                wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
                return false;
            }

            if (wp_data->object_type == OBJECT_ANALOG_VALUE) {
                if (value.tag == BACNET_APPLICATION_TAG_REAL) {
                    float new_val = value.type.Real;
                    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
                        if ((analogPoints[i].protocol & PROTO_BACNET) && analogPoints[i].bacnetInstance == wp_data->object_instance) {
                            analogPoints[i].value = new_val;
                            #ifdef ENABLE_MODBUS
                                modbus_write_analog(i);
                            #endif
                            Analog_Value_Present_Value_Set(wp_data->object_instance, new_val, wp_data->priority);
                            return true;
                        }
                    }
                } 
            } 
            else if (wp_data->object_type == OBJECT_BINARY_VALUE) {
                if (value.tag == BACNET_APPLICATION_TAG_ENUMERATED) {
                    bool new_val = (value.type.Enumerated == BINARY_ACTIVE);
                    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
                        if ((binaryPoints[i].protocol & PROTO_BACNET) && binaryPoints[i].bacnetInstance == wp_data->object_instance) {
                            binaryPoints[i].value = new_val; 
                            #ifdef ENABLE_MODBUS
                                modbus_write_binary(i); 
                            #endif
                            Binary_Value_Present_Value_Set(wp_data->object_instance, new_val ? BINARY_ACTIVE : BINARY_INACTIVE);
                            return true;
                        }
                    }
                }
            }

            wp_data->error_class = ERROR_CLASS_PROPERTY;
            wp_data->error_code = ERROR_CODE_INVALID_DATA_TYPE;
            return false;
        }

        if (wp_data->object_type == OBJECT_ANALOG_VALUE) return Analog_Value_Write_Property(wp_data);
        if (wp_data->object_type == OBJECT_BINARY_VALUE) return Binary_Value_Write_Property(wp_data);

        return false;
    }
  
    uint32_t Network_Port_Index_To_Instance(unsigned i) { (void)i; return 0; }
    bool Analog_Value_Name_Set(uint32_t object_instance, const char *new_name);
    bool Binary_Value_Name_Set(uint32_t object_instance, const char *new_name);
}

void bacnet_init(IPAddress ip) {
    current_bacnet_ip = ip;
    
    Device_Set_Object_Instance_Number(BACNET_DEVICE_ID);
    BACNET_CHARACTER_STRING bacnet_name;
    characterstring_init_ansi(&bacnet_name, BACNET_DEVICE_NAME);
    Device_Set_Object_Name(&bacnet_name);

    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (analogPoints[i].protocol & PROTO_BACNET) {
            Analog_Value_Create(analogPoints[i].bacnetInstance);
            Analog_Value_Name_Set(analogPoints[i].bacnetInstance, analogPoints[i].name);
        }
    }
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (binaryPoints[i].protocol & PROTO_BACNET) {
            Binary_Value_Create(binaryPoints[i].bacnetInstance);
            Binary_Value_Name_Set(binaryPoints[i].bacnetInstance, binaryPoints[i].name);
        }
    }

    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    apdu_set_confirmed_handler(SERVICE_CONFIRMED_WRITE_PROPERTY, handler_write_property);

    bip_set_port(BACNET_PORT);
    
    BACNET_IP_ADDRESS local_addr = {0}; 
    local_addr.address[0] = ip[0];
    local_addr.address[1] = ip[1];
    local_addr.address[2] = ip[2];
    local_addr.address[3] = ip[3];
    local_addr.port = BACNET_PORT;
    
    bip_set_addr(&local_addr);
    bip_set_subnet_prefix(24); 
    
    if (!bip_init(NULL)) {
        Serial.println("[BACNET] ERROR: Failed to initialize BIP stack!");
        return;
    }

    Send_I_Am(&Handler_Transmit_Buffer[0]);
}

void bacnet_update_objects() {
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (binaryPoints[i].protocol & PROTO_BACNET) {
            BACNET_BINARY_PV bv = binaryPoints[i].value ? BINARY_ACTIVE : BINARY_INACTIVE;
            Binary_Value_Present_Value_Set(binaryPoints[i].bacnetInstance, bv);
        }
    }
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (analogPoints[i].protocol & PROTO_BACNET) {
            Analog_Value_Present_Value_Set(analogPoints[i].bacnetInstance, analogPoints[i].value, 16);
        }
    }
}

void bacnet_task() {
    static uint8_t pdu[MAX_APDU]; 
    static BACNET_ADDRESS src;
    
    uint16_t pdu_len = bip_receive(&src, pdu, MAX_APDU, 0);
    if (pdu_len > 0) {
        #ifdef ENABLE_MODBUS
        extern ModbusIP mb;  
        mb.task();           
        #endif
        npdu_handler(&src, pdu, pdu_len);
    }
}