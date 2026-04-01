/**
 * @file bip-esp32.c
 * @brief Custom BACnet/IP (BIP) Datalink Layer for ESP32.
 * @details Acts as a routing bridge between the standard C BACnet-stack 
 * and the ESP32's hardware-specific WiFiUDP C++ library.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "bacnet/bacdef.h"
#include "bacnet/bacdcode.h"
#include "bacnet/bacenum.h"
#include "bacnet/datalink/bip.h"

// ==============================================================================
// C++ BRIDGE EXTERN POINTERS
// ==============================================================================

// These functions are implemented in the Arduino C++ environment (bacnet_handler.cpp)
// and are called from this pure C file via extern pointers.
extern int esp32_udp_send(uint32_t dest_ip, uint16_t dest_port,
                           uint8_t* data, uint16_t len);
extern int esp32_udp_recv(uint32_t* src_ip, uint16_t* src_port,
                           uint8_t* buf, uint16_t max_len);
extern uint32_t esp32_get_local_ip(void);

// ==============================================================================
// GLOBAL VARIABLES
// ==============================================================================

static uint16_t BIP_Port = 0xBAC0; // Default BACnet port (47808)
static uint32_t BIP_Address = 0;   // Local IP address in uint32 format
static bool BIP_Initialized = false;

// ==============================================================================
// DATALINK INITIALIZATION
// ==============================================================================

bool bip_init(char* ifname) {
    (void)ifname; // Network interface name is not used on ESP32
    BIP_Address = esp32_get_local_ip();
    BIP_Initialized = (BIP_Address != 0);
    return BIP_Initialized;
}

void bip_cleanup(void) {
    BIP_Initialized = false;
}

bool bip_valid(void) {
    return BIP_Initialized;
}

void bip_set_port(uint16_t port) {
    BIP_Port = port;
}

uint16_t bip_get_port(void) {
    return BIP_Port;
}

bool bip_set_addr(const BACNET_IP_ADDRESS *addr) {
    if (addr) {
        // Copy the 4 bytes of the IP address into our local state variable
        memcpy(&BIP_Address, addr, 4);
        return true;
    }
    return false;
}

bool bip_get_addr(BACNET_IP_ADDRESS *addr) {
    if (addr) {
        memcpy(addr, &BIP_Address, 4);
        return true;
    }
    return false;
}

// ==============================================================================
// TRANSMISSION (SEND)
// ==============================================================================

int bip_send_pdu(BACNET_ADDRESS* dest,
                 BACNET_NPDU_DATA* npdu_data,
                 uint8_t* pdu,
                 unsigned pdu_len) {
    uint8_t buf[MAX_APDU + 10];
    uint16_t offset = 0;
    uint32_t dest_ip = 0;
    uint16_t dest_port = BIP_Port;

    // 1. Construct the BVLC (BACnet Virtual Link Control) Header
    buf[offset++] = 0x81; // BVLC Type: BACnet/IP
    buf[offset++] = 0x0A; // BVLC Function: Original-Unicast-NPDU
    buf[offset++] = (uint8_t)((pdu_len + 4) >> 8); // Length MSB
    buf[offset++] = (uint8_t)(pdu_len + 4);        // Length LSB

    // 2. Append the actual Protocol Data Unit (PDU)
    memcpy(&buf[offset], pdu, pdu_len);
    offset += pdu_len;

    // 3. Resolve Destination IP Address
    if (dest->net == BACNET_BROADCAST_NETWORK || dest->mac_len == 0) {
        // Format local broadcast address for ESP32 (Little-Endian)
        // Replaces the last byte of the subnet with 255 (0xFF)
        dest_ip = (BIP_Address & 0x00FFFFFF) | 0xFF000000;
    } else {
        // For BACnet/IP, the destination IP is stored in the MAC array
        memcpy(&dest_ip, &dest->mac[0], 4);
        dest_port = BIP_Port; // Force outgoing port to 47808
    }

    // 4. Send via C++ UDP Bridge
    int result = esp32_udp_send(dest_ip, dest_port, buf, offset);

    return result;
}

// ==============================================================================
// RECEPTION (RECEIVE)
// ==============================================================================

uint16_t bip_receive(BACNET_ADDRESS* src,
                     uint8_t* pdu,
                     uint16_t max_pdu,
                     unsigned timeout) {
    uint8_t buf[MAX_APDU + 10];
    uint32_t src_ip = 0;
    uint16_t src_port = 0;
    int len;
    uint16_t pdu_len = 0;

    // 1. Fetch data from the C++ UDP Bridge
    len = esp32_udp_recv(&src_ip, &src_port, buf, sizeof(buf));
    if (len < 4) return 0; // Packet too short to contain a valid BVLC header

    // 2. Validate BVLC Type
    if (buf[0] != 0x81) return 0; // Not a BACnet/IP packet

    // 3. Extract PDU Length from header
    pdu_len = (buf[2] << 8) | buf[3];
    if (pdu_len < 4) return 0;
    pdu_len -= 4; // Subtract the 4-byte BVLC header length

    // Prevent buffer overflows
    if (pdu_len > max_pdu) pdu_len = max_pdu;

    // 4. Copy the raw PDU payload
    memcpy(pdu, &buf[4], pdu_len);

    // 5. *** CRITICAL BROADCAST FIX FOR ESP32 ***
    // If the destination network is 0xFFFF (Global Broadcast), the default npdu_handler 
    // might drop the packet. We rewrite the control byte to strip the destination 
    // specifier, tricking the handler into treating the packet as local.
    if (pdu_len >= 2 && (pdu[1] & 0x20)) {
        // Verify if it is indeed a Global Broadcast (0xFFFF)
        if (pdu_len >= 4 && pdu[2] == 0xFF && pdu[3] == 0xFF) {
            pdu[1] &= ~0x20; // Clear the "DNET, DLEN, DADR present" flag
            
            // Shift the remaining payload left to overwrite the routing bytes
            // (0xFF 0xFF 0x00 0xFF = net + mac_len=0 + hop_count)
            memmove(&pdu[2], &pdu[6], pdu_len - 6);
            pdu_len -= 4;
        }
    }

    // 6. Populate the source address structure for the application layer
    src->mac_len = 6;
    memcpy(src->mac, &src_ip, 4);         // Sender's IP
    src->mac[4] = (src_port >> 8) & 0xFF; // Sender's Port MSB
    src->mac[5] = src_port & 0xFF;        // Sender's Port LSB
    src->net = 0;                         // Local network
    src->len = 0;
    
    return pdu_len;
}

// ==============================================================================
// ADDRESS MANAGEMENT
// ==============================================================================

void bip_get_broadcast_address(BACNET_ADDRESS* dest) {
    uint32_t broadcast = BIP_Address | 0xFF;
    dest->mac_len = 6;
    memcpy(dest->mac, &broadcast, 4);
    dest->mac[4] = (BIP_Port >> 8) & 0xFF;
    dest->mac[5] = BIP_Port & 0xFF;
    dest->net = BACNET_BROADCAST_NETWORK;
    dest->len = 0;
}

void bip_get_my_address(BACNET_ADDRESS* my_address) {
    my_address->mac_len = 6;
    memcpy(my_address->mac, &BIP_Address, 4);
    my_address->mac[4] = (BIP_Port >> 8) & 0xFF;
    my_address->mac[5] = BIP_Port & 0xFF;
    my_address->net = 0;
    my_address->len = 0;
}

// Static variable for explicit broadcast assignment
static BACNET_IP_ADDRESS BIP_Broadcast_Addr;

bool bip_set_broadcast_addr(const BACNET_IP_ADDRESS *addr) {
    if (addr) {
        memcpy(&BIP_Broadcast_Addr, addr, sizeof(BACNET_IP_ADDRESS));
        return true;
    }
    return false;
}

bool bip_set_subnet_prefix(uint8_t prefix) {
    // Stub to satisfy the BACnet stack linker dependencies. 
    // Subnet calculation is handled by the ESP32 WiFi library natively.
    (void)prefix;
    return true;
}

// ==============================================================================
// SYSTEM TIME STUB
// ==============================================================================

unsigned long mstimer_now(void) {
    // Links to the Arduino millis() function
    extern unsigned long millis(void);
    return millis();
}