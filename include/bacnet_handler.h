/**
 * @file bacnet_handler.h
 * @brief BACnet/IP protocol handler and dynamic binding declarations.
 * @details Exposes the global state as BACnet objects and intercepts write requests.
 * @author [Your Name / Project Name]
 * @date 2024
 */

#pragma once
#include <Arduino.h> // Required for IPAddress class definition

// ==============================================================================
// BACNET HANDLER (Declarations)
// ==============================================================================

/**
 * @brief Initializes the BACnet stack and binds it to a specific IP address.
 * @param ip The IPAddress to bind to (typically STA primary IP or AP fallback IP).
 */
void bacnet_init(IPAddress ip);

/**
 * @brief Synchronizes internal state variables to BACnet objects.
 * @note Called continuously in the main loop to keep BACnet Present_Values updated.
 */
void bacnet_update_objects();

/**
 * @brief Processes incoming BACnet UDP packets and handles stack routines.
 * @note Must be called frequently in the main loop.
 */
void bacnet_task();