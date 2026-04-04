# ESP32 Multi-Protocol BMS Gateway

[![Version](https://img.shields.io/badge/Version-v1.0.0-brightgreen.svg)](https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/releases/tag/v1.0.0)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-Arduino%20%7C%20PlatformIO-00979D.svg)](https://platformio.org/)
[![BACnet/IP](https://img.shields.io/badge/BACnet%2FIP-Port_47808-4A90D9.svg)](https://github.com/bacnet-stack/bacnet-stack)
[![Modbus TCP](https://img.shields.io/badge/Modbus_TCP-Port_502-D94A4A.svg)](https://modbus.org/)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)
[![Flash Tool](https://img.shields.io/badge/Flash-ESP_Web_Tools-orange.svg)](https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol)

> **A $10 industrial protocol bridge — Modbus TCP to BACnet/IP — running on a single ESP32-S3.**
> Flash it from your browser, connect it to your network, and your BACnet supervisor discovers it instantly.
> No middleware. No cloud. No dedicated server.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Quick Install — Zero Code Required](#quick-install--zero-code-required)
- [Manual Installation — For Developers](#manual-installation--for-developers)
- [Hardware Requirements](#hardware-requirements)
- [Usage & Configuration](#usage--configuration)
- [Architecture](#architecture)
- [Protocol Reference](#protocol-reference)
- [License & Author](#license--author)

---

## Overview

The **ESP32 Multi-Protocol BMS Gateway** is an open-source firmware that transforms an ESP32-S3 into a fully bidirectional industrial gateway. It simultaneously exposes data points over **BACnet/IP** and **Modbus TCP**, while serving an embedded **High Performance HMI** (ISA 101 compliant) directly from the device flash.

**The core use case:** you have equipment that speaks Modbus TCP (a VFD, a sensor, a PLC output) and your building supervisor speaks BACnet/IP (EBO, Desigo CC, SCADA). This device sits between them, translates in real time, and costs under $10 in hardware.

**Typical applications:**

- Connecting HVAC equipment or drives to a BACnet/IP supervisor
- Exposing Modbus TCP slave data to a BACnet network without a PC gateway
- Local commissioning and monitoring via an embedded web HMI
- Rapid BMS point map prototyping before production deployment

---

## Features

**Plug & Play — Zero Installation**
- Flash directly from your browser via [ESP Web Tools](https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol) — no IDE, no drivers, no command line
- Automatic Wi-Fi Access Point fallback (`BMS-Gateway-Config`) when no saved network is reachable
- Accessible on the LAN via mDNS at `http://bms-gateway.local` — no IP address to memorize

**Industrial Web HMI (ISA 101 Compliant)**
- Redesigned interface following the ISA 101 High Performance HMI standard: desaturated colors, SVG icons, color contrast reserved for operational states (RUN/STOP) and alarms only
- Single-Page Application (SPA) — near-instant page loads served directly from ESP32 flash
- Live point configuration (Modbus register / BACnet object mapping) with real-time address conflict detection
- Front-end value caching via `sessionStorage` — display does not blank during a device reboot

**C++ Engine & Protocol Stack**
- **Simultaneous BACnet/IP and Modbus TCP** — both servers run concurrently with no interference
- **Expanded BACnet stack limits:** 64 Analog Values, 64 Binary Values, 128 objects maximum — stable operation with tools like YABE and professional supervisors
- **LittleFS `config.json`** — all point configurations are saved to flash and survive power cuts
- **NVRAM** — Wi-Fi credentials and AP/STA mode override persist across reboots and OTA updates
- **Safe Reboot mechanism** — configuration changes from the web UI trigger a clean, watchdog-safe restart sequence

**Data-Driven Architecture**
- All BMS points are declared once in `state.cpp` and automatically reflected across both protocols and the web UI
- No duplicated register maps or object tables — a single source of truth by design

---

## Quick Install — Zero Code Required

No development environment is required. The entire flashing process happens in your browser in under two minutes.

### Step 1 — Open the Flash Page

Navigate to the project GitHub Pages using the link or QR code below:

**[https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol](https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol)**

<div align="center">
  <img src="assets/QRcode_Installer-web.png" alt="QR Code for Web Installer" width="150"/>
</div>

> **Browser requirement:** Google Chrome or Microsoft Edge (desktop). ESP Web Tools requires WebSerial API support, which Firefox does not currently provide.

### Step 2 — Connect and Flash

1. Plug your ESP32-S3 into your computer via USB.
2. Click **"Connect"** on the flash page and select the correct COM port from the browser dialog.
3. Click **"Install ESP32 BMS Gateway"** and wait for the process to complete (approximately 60–90 seconds).
4. The device reboots automatically when finished.

> If this is not a fresh board, perform a full **Erase Flash** before installing to avoid partition conflicts. This option is available directly on the flash page.

### Step 3 — First Boot

On first boot, or whenever no saved Wi-Fi network is found, the device creates its own Access Point:

| Parameter | Value |
| :--- | :--- |
| SSID | `BMS-Gateway-Config` |
| Password | `admin1234` |
| Fallback IP | `192.168.4.1` |

Connect to this network, then open `http://bms-gateway.local` or `http://192.168.4.1` in your browser.

> **Security notice:** Change the default AP credentials before deploying in a production environment.

### Step 4 — Join Your Infrastructure Network

In the web interface, open the **Network** panel:

1. Click **Scan** to list available Wi-Fi networks.
2. Select your network and enter the passphrase.
3. Click **Connect**. The device saves credentials to NVRAM and reboots.

After reconnecting, BACnet/IP (UDP `47808`) and Modbus TCP (TCP `502`) are immediately available on your LAN. The dashboard is accessible at `http://bms-gateway.local` from any browser on the same network.

---

## Manual Installation — For Developers

Use this method to modify the firmware, add custom data points, or contribute to the project.

**Requirements:** [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)

### Step 1 — Clone the Repository

```bash
git clone https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol.git
cd ESP32-BMS-Gateway-Multi-Protocol
```

### Step 2 — Open in PlatformIO

Open the cloned folder in VS Code. PlatformIO will detect `platformio.ini` and resolve all dependencies automatically.

### Step 3 — Erase Flash (First Use)

Mandatory on a new board or after any partition layout change:

```
PlatformIO sidebar → Platform → Erase Flash
```

### Step 4 — Build and Upload the Filesystem

The web dashboard (HTML, CSS, JS) lives in `data/` and is packaged into a LittleFS image:

```
PlatformIO sidebar → Platform → Build Filesystem Image
PlatformIO sidebar → Platform → Upload Filesystem Image
```

### Step 5 — Build and Upload the Firmware

```bash
pio run --target upload
```

Or use the **Upload** button (→) in the PlatformIO toolbar.

### Step 6 — Monitor

Open the Serial Monitor at **115200 baud** to observe boot logs, Wi-Fi status, assigned IP address, and protocol initialization output.

### Adding a Custom Data Point

Declare the new point in `state.cpp`. Both the BACnet and Modbus handlers enumerate the state registry at runtime — no other files need to be modified.

---

## Hardware Requirements

### Recommended Target

| Parameter | Specification |
| :--- | :--- |
| **SoC** | ESP32-S3 |
| **Flash** | 16 MB (N16R8 variant recommended) |
| **PSRAM** | 8 MB (optional — not required for operation) |
| **USB** | USB-C with data lines (not charge-only) |

The firmware is built and tested on the **ESP32-S3 N16R8**. This variant provides sufficient flash for the LittleFS partition (web dashboard), the firmware binary, and future OTA partitions with comfortable margin.

PSRAM is not required for the gateway to operate. It becomes beneficial if you significantly expand the point map (50+ points) or enable future features such as data logging.

### Minimum Viable Hardware

Any ESP32-S3 development board with at least **8 MB of flash** will run the firmware. The web dashboard filesystem image (`littlefs.bin`) is flashed at address `0x610000` and requires roughly 1.5 MB of available flash beyond the application partition.

> Standard ESP32 (non-S3) and ESP32-S2 variants are **not supported** due to differences in the USB peripheral and memory layout used by this firmware.

---

## Usage & Configuration

### Connecting via the AP Fallback

If the gateway cannot reach its saved Wi-Fi network (network down, credentials changed, first boot), it automatically activates its fallback Access Point within 30 seconds. No physical reset or serial port access is required.

Connect to `BMS-Gateway-Config` (password: `admin1234`) from any device, then open `http://192.168.4.1`.

### Configuring BMS Points

Once on the dashboard:

1. Navigate to the **Points** configuration panel.
2. For each data point, assign a **name**, a **Modbus register address** (Holding Register or Coil), and a **BACnet object** (Analog Value or Binary Value with its instance number).
3. The interface checks for address conflicts in real time and highlights any duplicates before you save.
4. Click **Save Configuration**. The device performs a Safe Reboot, applying the new point map cleanly without triggering the watchdog.

Configuration is written to `/config.json` on the LittleFS partition. It persists through power cuts, OTA updates, and reboots.

### Default Point Map (AHU Simulator)

The firmware ships with a pre-configured Air Handling Unit simulator for out-of-the-box integration testing:

| Point | BACnet Object | Modbus Address | Description | Range |
| :--- | :---: | :---: | :--- | :--- |
| Fan Status | BV:0 | Coil 10 | Fan run/stop command | 0 = Stop, 1 = Run |
| Fan Speed | AV:0 | Holding Reg. 100 | VSD setpoint | 0–100 % |
| Temp Setpoint | AV:1 | Holding Reg. 101 | Supply air setpoint | ×10 — e.g., `210` = 21.0 °C |

> **Modbus scaling note:** The scaling factor is dynamically configurable per point via the Web UI. For example, TempSetpoint uses a ×10 scale to preserve one decimal place in a 16-bit integer register (value 21.0 is transmitted as 210), while FanSpeed uses a ×1 scale.

### Web Interface Navigation

| Section | Description |
| :--- | :--- |
| **Dashboard (View Mode)** | Live view of all configured data points, gauges, and their current values. |
| **Dashboard (Edit Mode)** | Add, edit, remove, or drag-and-drop points and sections. Map Modbus/BACnet addresses. |
| **Network Config** | Wi-Fi scan, credential input, connection status, and explicit AP/STA mode override. |

---

## Architecture

```
ESP32-BMS-Gateway-Multi-Protocol/
│
├── src/
│   ├── main.cpp                ← Orchestration: setup(), loop(), task scheduling, Safe Reboot
│   ├── state.cpp               ← Centralized point registry (AnalogPoint, BinaryPoint)
│   ├── bacnet_handler.cpp      ← BACnet/IP stack, dynamic UDP routing, object callbacks
│   ├── modbus_handler.cpp      ← Modbus TCP register map ↔ shared state synchronization
│   └── web_handler.cpp         ← REST API, point config, network scan/connect, mDNS
│
├── data/                       ← Front-end assets packaged into LittleFS
│   ├── index.html              ← Single-Page Application shell
│   ├── style.css               ← ISA 101 compliant stylesheet
│   └── app.js                  ← Real-time polling, sessionStorage caching, conflict detection
│
├── lib/
│   └── bacnet/                 ← Vendored C BACnet stack, optimized for embedded targets
│
├── docs/                       ← GitHub Pages — ESP Web Tools flash interface
│
└── platformio.ini              ← Build config: 16 MB flash layout, partition table, build flags
```

### Data Flow

All three interfaces share a single state registry. A write from any source — BACnet supervisor, Modbus master, or web UI — is immediately visible on all others. There is no protocol-to-protocol translation layer.

```
BACnet/IP Supervisor ──► bacnet_handler.cpp ──┐
                                               │
Modbus TCP Master    ──► modbus_handler.cpp ──►│──► state.cpp (shared registry) ──► all interfaces
                                               │
Web Dashboard        ──► web_handler.cpp    ──┘
```

---

## Protocol Reference

### BACnet/IP

| Parameter | Value |
| :--- | :--- |
| Transport | UDP, port `47808` |
| Supported services | `ReadProperty`, `WriteProperty`, `Who-Is` / `I-Am` |
| Object types | Binary Value (BV), Analog Value (AV) |
| Capacity | 64 AV, 64 BV, 128 objects maximum |
| Discovery | Responds to broadcast `Who-Is` — zero manual address configuration |

### Modbus TCP

| Parameter | Value |
| :--- | :--- |
| Transport | TCP, port `502` |
| FC01 | Read Coils |
| FC05 | Write Single Coil |
| FC03 | Read Holding Registers |
| FC06 | Write Single Register |
| FC16 | Write Multiple Registers |
| Addressing | 0-based for both coils and holding registers |

### REST API

The web dashboard communicates with the firmware over a lightweight JSON REST API on port `80`. All endpoints can be called directly with `curl` or any HTTP client for scripting and integration testing.

---

## License & Author

This project is licensed under the **MIT License** — see [LICENSE](LICENSE) for full terms.

**Author:** DoodzProg — [github.com/DoodzProg](https://github.com/DoodzProg)

**Third-party components:**

[`bacnet-stack`](https://github.com/bacnet-stack/bacnet-stack) — Licensed under **GNU GPL v2 or later**. The vendored BACnet library in `lib/bacnet/` retains its original GPL license. Distributions of modified versions of this firmware must comply with the GPL terms for that component.

---

<div align="center">

*Built for the field. Tested with YABE. Powered by too much coffee.*

**[Flash Now — No Setup Required](https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol)**

</div>