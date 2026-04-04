/**
 * @file        modbus_handler.h
 * @brief       Modbus TCP Server (Slave) stack declarations.
 * @details     Synchronizes Modbus registers (Coils/Holding) with the global state structure.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#pragma once
#include <ModbusIP_ESP8266.h>

// ==============================================================================
// MODBUS HANDLER (Declarations)
// ==============================================================================

/** @brief Global Modbus IP instance. */
extern ModbusIP mb; 

/**
 * @brief Initializes the Modbus TCP server and configures the registers.
 */
void modbus_init();

/**
 * @brief Processes incoming Modbus TCP requests.
 * @note Must be called frequently in the main loop.
 */
void modbus_task();

/**
 * @brief Synchronizes internal state variables from Modbus register changes.
 */
void modbus_sync_from_registers();

/**
 * @brief Forces a write operation to a specific binary point.
 * @param index The array index of the binary point in the global state.
 */
void modbus_write_binary(int index);

/**
 * @brief Forces a write operation to a specific analog point.
 * @param index The array index of the analog point in the global state.
 */
void modbus_write_analog(int index);