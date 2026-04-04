/**
 * @file        modbus_handler.cpp
 * @brief       Modbus TCP Server (Slave) implementation.
 * @details     Handles the initialization, polling, and synchronization of Modbus 
 * coils and holding registers with the global dynamic telemetry state.
 * Registers are allocated based on the bitmask PROTO_MODBUS.
 * @author      Doodz (DoodzProg)
 * @date        2026-04-04
 * @version     1.0.0
 * @repository  https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol
 */

#include "modbus_handler.h"
#include "config.h"
#include "state.h"
#include <ModbusIP_ESP8266.h>

// ==============================================================================
// SERVER INSTANCE
// ==============================================================================

ModbusIP mb; /**< Modbus TCP server handler instance */

// ==============================================================================
// INITIALIZATION
// ==============================================================================

void modbus_init() {
    mb.server();
    
    // Dynamically initialize binary points (Coils) based on the global state
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (binaryPoints[i].protocol & PROTO_MODBUS) {
            mb.addCoil(binaryPoints[i].modbusCoil);
            mb.Coil(binaryPoints[i].modbusCoil, binaryPoints[i].value);
        }
    }

    // Dynamically initialize analog points (Holding Registers)
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (analogPoints[i].protocol & PROTO_MODBUS) {
            mb.addHreg(analogPoints[i].modbusReg);
            uint16_t raw_val = (uint16_t)(analogPoints[i].value * analogPoints[i].modbusScale);
            mb.Hreg(analogPoints[i].modbusReg, raw_val);
        }
    }
}

// ==============================================================================
// TASKS & SYNCHRONIZATION
// ==============================================================================

void modbus_task() {
    mb.task(); 
}

void modbus_sync_from_registers() {
    // Read current states from Modbus Coils and update the global state
    for (int i = 0; i < NUM_BINARY_POINTS; i++) {
        if (binaryPoints[i].protocol & PROTO_MODBUS) {
            binaryPoints[i].value = mb.Coil(binaryPoints[i].modbusCoil);
        }
    }

    // Read current values from Modbus Holding Registers, apply scaling, and update
    for (int i = 0; i < NUM_ANALOG_POINTS; i++) {
        if (analogPoints[i].protocol & PROTO_MODBUS) {
            uint16_t raw_val = mb.Hreg(analogPoints[i].modbusReg);
            analogPoints[i].value = (float)raw_val / analogPoints[i].modbusScale;
        }
    }
}

// ==============================================================================
// WRITERS (Pushing updates to Modbus)
// ==============================================================================

void modbus_write_binary(int index) {
    if (index >= 0 && index < NUM_BINARY_POINTS && (binaryPoints[index].protocol & PROTO_MODBUS)) {
        mb.Coil(binaryPoints[index].modbusCoil, binaryPoints[index].value);
    }
}

void modbus_write_analog(int index) {
    if (index >= 0 && index < NUM_ANALOG_POINTS && (analogPoints[index].protocol & PROTO_MODBUS)) {
        uint16_t raw_val = (uint16_t)(analogPoints[index].value * analogPoints[index].modbusScale);
        mb.Hreg(analogPoints[index].modbusReg, raw_val);
    }
}