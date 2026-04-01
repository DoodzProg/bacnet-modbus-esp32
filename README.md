# ESP32 Multi-Protocol BMS Gateway 🏗️

[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-Arduino-00979D.svg)](https://www.arduino.cc/)
[![BACnet/IP](https://img.shields.io/badge/BACnet%2FIP-Port_47808-green.svg)](https://github.com/bacnet-stack/bacnet-stack)
[![Modbus TCP](https://img.shields.io/badge/Modbus_TCP-Port_502-red.svg)](https://modbus.org/)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](LICENSE)

> **Transform an ESP32-S3 into an active, bidirectional industrial gateway.**
> Simultaneously expose your data over **BACnet/IP**, **Modbus TCP**, and a real-time **Web Dashboard** — all from a single embedded device.
> Built around a modular, data-driven architecture with persistent network management and zero-configuration discovery.

---

## 📋 Table of Contents

- [Overview](#-overview)
- [First Steps & Usage](#️-first-steps--usage)
- [Features](#-features)
- [Architecture](#-project-architecture)
- [Default Use Case — AHU Simulator](#-default-use-case-ahu-simulator)
- [Installation & Deployment](#️-installation--deployment)
- [Network Configuration Details](#-network-configuration-details)
- [Protocol Reference](#-protocol-reference)
- [License](#-license)

---

## 🔍 Overview

The **ESP32 Multi-Protocol BMS Gateway** bridges industrial automation protocols and modern web interfaces on a single, low-cost microcontroller. It is designed for engineers and integrators who need to expose BMS (Building Management System) data points over standard protocols — without a dedicated server, middleware, or cloud dependency.

**Typical applications include:**

- Connecting HVAC equipment to a BACnet/IP supervisor (SCADA, EBO, Desigo CC)
- Exposing sensor data to a Modbus TCP master (PLC, datalogger)
- Providing an embedded web HMI for local monitoring and commissioning
- Prototyping BMS point maps before deploying to production controllers

---

## 🕹️ First Steps & Usage

The gateway is designed to be **plug-and-play out of the box**. No configuration file or compilation is required for initial use.

### Step 1 — Power On & Connect

On first boot (or when no saved network is found), the device automatically creates its own Wi-Fi access point:

| Parameter | Value |
| :--- | :--- |
| **SSID** | `bms-gateway-config` |
| **Password** | `admin1234` |

Connect to this network from any computer, tablet, or smartphone.

> ⚠️ **Security Notice:** Change the default AP credentials before deploying in a production environment. The AP is intended for commissioning only.

### Step 2 — Open the Web Dashboard

Once connected to the AP, open your browser and navigate to:

```
http://bms-gateway.local
```

> **Fallback IP address:** [`http://192.168.4.1`](http://192.168.4.1) — use this if mDNS is not supported by your OS or browser.

The dashboard loads immediately and displays all live data points with their current values. No additional software or drivers are required.

### Step 3 — Join Your Local Network

Click the **"Network Config"** button in the top-right corner of the dashboard. This opens the network management panel, which allows you to:

1. **Scan** for available Wi-Fi networks in range (Enterprise WPA2, Home WPA/WPA2, or 4G/LTE hotspot).
2. **Select** your target network and enter the passphrase.
3. **Confirm** the connection. The device saves the credentials to its NVRAM and reboots automatically.

### Step 4 — Persistent Operation

After reconnecting to your local infrastructure:

- The gateway joins your network on every subsequent boot — no reconfiguration needed.
- BACnet/IP (UDP port `47808`) and Modbus TCP (port `502`) are immediately available to all devices on the same network.
- The web dashboard remains accessible at `http://bms-gateway.local` from any browser on the LAN.

> 💡 If the saved network becomes unreachable (wrong credentials, SSID change, etc.), the gateway **automatically falls back to AP mode** so you can reconfigure it without physical access to the serial port.

---

## 🚀 Features

| Feature | Detail |
| :--- | :--- |
| 🟢 **Bidirectional BACnet/IP** | Full `ReadProperty` and `WriteProperty` support. Remote supervisors can both read sensor data and write setpoints. |
| 🔴 **Modbus TCP Server** | Exposes Coils (binary outputs) and Holding Registers (analog values) on TCP port `502`. Compatible with any standard Modbus master. |
| 🌐 **Embedded Web Dashboard** | Flat-design HTML/CSS/JS interface served directly from a dedicated LittleFS partition on the ESP32 flash. No external server required. |
| ⚡ **Real-Time Protocol Sync** | Any write to a data point — via BACnet, Modbus, or the web UI — is instantly reflected across all three interfaces. Single source of truth. |
| 📡 **AP Fallback** | If the target Wi-Fi network is unavailable, the device reverts to its own AP automatically, preventing permanent lock-out. |
| 🔍 **Zero-Config mDNS Discovery** | Accessible via `bms-gateway.local` without knowing the IP address. Works on macOS, iOS, Linux, and most Windows environments. |
| 💾 **NVRAM Persistence** | Network credentials and configuration are stored in non-volatile memory. Settings survive power cycles and firmware updates. |
| 🏗️ **Data-Driven Architecture** | All BMS points are declared once in a central state file. Adding a new point automatically exposes it across all protocols. |

---

## 📐 Project Architecture

The project enforces a strict **Data-Driven Architecture**: every data point is declared once in `state.cpp` and is automatically reflected across all protocol handlers. There is no duplication of register maps or object tables.

```
ESP32-BMS-Gateway-Multi-Protocol/
│
├── src/
│   ├── main.cpp                ← Orchestration layer: setup(), loop(), task scheduling
│   ├── state.cpp               ← Centralized data point registry (BinaryPoint, AnalogPoint)
│   ├── bacnet_handler.cpp      ← BACnet/IP stack integration, dynamic UDP routing, object callbacks
│   ├── modbus_handler.cpp      ← Modbus TCP register mapping ↔ shared state synchronization
│   └── web_handler.cpp         ← REST API endpoints, network scan/connect, mDNS registration
│
├── data/                       ← Front-end assets (HTML, CSS, JS) — packaged into LittleFS
│   ├── index.html              ← Main dashboard page
│   ├── style.css               ← Flat-design stylesheet
│   └── app.js                  ← Real-time data polling and UI logic
│
├── lib/
│   └── bacnet/                 ← Vendored, heavily optimized C BACnet stack (embedded target)
│
└── platformio.ini              ← Build configuration: 8 MB flash layout, partition table, flags
```

### Data Flow

```
Write via Web UI
      │
      ▼
  state.cpp  ←──────────────────────────────────┐
  (shared)                                       │
      │                                          │
      ├──► bacnet_handler.cpp  ──► BACnet/IP Supervisor
      │
      └──► modbus_handler.cpp  ──► Modbus TCP Master
```

All three protocol handlers read from and write to the same shared state. There is no protocol-to-protocol translation layer — consistency is guaranteed by design.

---

## 📦 Default Use Case — AHU Simulator

The firmware ships with a pre-configured **Air Handling Unit (AHU)** simulator. This demonstrates a typical BMS point map and validates integration with a BACnet supervisor or Modbus master out of the box.

| Point Name | BACnet Object | Modbus Address | Web Interface | Description | Range / Unit |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **Fan Status** | Binary Value `BV:0` | Coil `10` | ON / OFF toggle | Fan run/stop command | `0` = Stop, `1` = Run |
| **Fan Speed** | Analog Value `AV:0` | Holding Register `100` | Slider (%) | Variable speed drive setpoint | `0` to `100` % |
| **Temp Setpoint** | Analog Value `AV:1` | Holding Register `101` | Numeric input (°C) | Supply air temperature setpoint | Stored as integer ×10 — e.g., `210` = 21.0 °C |

> 📝 **Modbus scaling note:** Analog values use a fixed ×10 multiplier to preserve one decimal place within integer registers. Your Modbus master must apply the corresponding ÷10 scaling on read and ×10 on write.

> 📝 **Extending the point map:** To add a new data point, declare it in `state.cpp`. The BACnet and Modbus handlers enumerate the state registry at runtime — no further changes are required.

---

## ⚙️ Installation & Deployment

### Prerequisites

- ESP32-S3 development board (8 MB flash recommended)
- USB cable with data lines (not charge-only)

---

### Method A — Quick Flash (Pre-compiled Binaries)

No development environment required. Download the latest binaries from the [**Releases page**](../../releases) and flash them using the [**Espressif Web Flash Tool**](https://espressif.github.io/esptool-js/) directly from your browser.

Flash the two files at the following addresses:

| File | Flash Address | Description |
| :--- | :---: | :--- |
| `firmware.bin` | `0x10000` | Main application firmware |
| `littlefs.bin` | `0x410000` | Web dashboard filesystem image |

**Procedure:**

1. Open the [ESP Tool](https://espressif.github.io/esptool-js/) in your browser.
2. Connect your ESP32-S3 via USB and select the correct COM port.
3. Add both files with their respective addresses.
4. Click **"Program"** and wait for both uploads to complete.
5. Press the **Reset** button on the board — the gateway boots immediately.

> ⚠️ If the device was previously flashed with other firmware, perform a full **Erase Flash** before flashing to avoid partition conflicts.

---

### Method B — Build from Source (PlatformIO)

Use this method if you want to modify the firmware, add custom data points, or contribute to the project.

**Requirements:** [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)

**Steps:**

1. **Clone the repository:**
   ```bash
   git clone https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol.git
   cd ESP32-BMS-Gateway-Multi-Protocol
   ```

2. **Open in PlatformIO:**
   Open the cloned folder in VS Code. PlatformIO will detect `platformio.ini` and install all dependencies automatically.

3. **Erase the flash** (mandatory on first use or after changing partition layout):
   ```
   PlatformIO sidebar → Platform → Erase Flash
   ```

4. **Build and upload the filesystem** (web dashboard assets):
   ```
   PlatformIO sidebar → Platform → Build Filesystem Image
   PlatformIO sidebar → Platform → Upload Filesystem Image
   ```

5. **Build and upload the firmware:**
   Click the standard **Upload** button (→) in the PlatformIO toolbar, or run:
   ```bash
   pio run --target upload
   ```

6. Open the **Serial Monitor** (115200 baud) to observe boot logs, IP address assignment, and protocol initialization status.

---

## 🌐 Network Configuration Details

| Parameter | Default Value | Notes |
| :--- | :--- | :--- |
| AP SSID | `bms-gateway-config` | Active when no saved network is found |
| AP Password | `admin1234` | Change before production deployment |
| AP IP Address | `192.168.4.1` | Fixed; used as fallback if mDNS fails |
| mDNS Hostname | `bms-gateway.local` | Resolves on the LAN after STA connection |
| BACnet/IP Port | UDP `47808` | Standard BACnet port — no firewall configuration needed on most LANs |
| Modbus TCP Port | TCP `502` | Standard Modbus port |
| Credential Storage | NVRAM (NVS) | Survives power cycles and OTA updates |

---

## 📡 Protocol Reference

### BACnet/IP

- **Transport:** UDP, port `47808`
- **Supported services:** `ReadProperty`, `WriteProperty`, `Who-Is` / `I-Am` (device discovery)
- **Object types exposed:** Binary Value (BV), Analog Value (AV)
- **Discovery:** The gateway responds to broadcast `Who-Is` requests and announces itself with `I-Am` — compatible with any BACnet/IP supervisor without manual address configuration.

### Modbus TCP

- **Transport:** TCP, port `502`
- **Supported function codes:**
  - `FC01` — Read Coils
  - `FC05` — Write Single Coil
  - `FC03` — Read Holding Registers
  - `FC06` — Write Single Register
  - `FC16` — Write Multiple Registers
- **Coil addressing:** 0-based (Coil `10` = address `10`)
- **Register addressing:** 0-based (Holding Register `100` = address `100`)

### REST API (Web Interface)

The web dashboard communicates with the firmware over a lightweight REST API served on port `80`. Endpoints follow a simple JSON structure and can be called directly with `curl` or any HTTP client for integration and testing.

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for full terms.

**Third-party components:**

- [`bacnet-stack`](https://github.com/bacnet-stack/bacnet-stack) — Licensed under **GNU GPL v2 or later**. The vendored BACnet library included in `lib/bacnet/` retains its original GPL license. If you distribute a modified version of this firmware, ensure your distribution complies with the GPL terms for that component.

---

<div align="center">

*Crafted with ☕ and an ungodly amount of BACnet Who-Is broadcasts.*

</div>