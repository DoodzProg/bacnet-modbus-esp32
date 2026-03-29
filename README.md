# ESP32 Modbus & BACnet Simulator 🌐

![Version](https://img.shields.io/badge/Version-0.1.0-orange.svg)
![Platform](https://img.shields.io/badge/Platform-ESP32--S3-blue.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino-00979D.svg)
![BACnet](https://img.shields.io/badge/Protocol-BACnet%2FIP-green.svg)
![Modbus](https://img.shields.io/badge/Protocol-Modbus_TCP-red.svg)

This project turns an ESP32-S3 into a multi-protocol communication node for Building Management Systems (BMS). It simultaneously exposes data via **BACnet/IP**, **Modbus TCP**, and a **Web Dashboard**. It acts as a simple, wireless gateway over an existing Wi-Fi network.

The ultimate goal of this project is to provide a generic, modular foundation for any sensor or actuator connected to an ESP32.

## 🚀 Core Features

* **BACnet/IP Server (Port 47808):** Integration of the standard C stack (BACnet-stack) on ESP32 using a custom UDP datalink layer (`bip-esp32.c`). Supports `Who-Is`, `I-Am`, and `ReadProperty` requests. *(Note: In the future, the goal is to integrate BACnet `WriteProperty` support for bidirectional control).*
* **Modbus TCP Server (Port 502):** Exposes Coils and Holding Registers for real-time industrial control.
* **Embedded Web Server (Port 80):** Asynchronous HTML/JS user interface to view and control data from a web browser.
* **Real-time Synchronization:** Modifying a value via Modbus instantly updates the Web UI and the corresponding BACnet object.

## 📦 Current Version (v0.1.0): "AHU" Use Case

For this initial release (`v0.1.0`), the code implements an **Air Handling Unit (AHU / CTA)** simulator. 
Why this choice? Because it is one of the most standard and representative equipment types in building automation. This allows for easy testing of 3 very common variable types:

| Data Name | BACnet Object | Modbus Register | Web Interface | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Fan Status** | Binary Value: 0 | Coil: 10 | Status (ON/OFF) | ON/OFF state |
| **Fan Speed** | Analog Value: 0 | Hreg: 100 | Speed (%) | 0 to 100% *(Note: In some software like QModMaster, there is a +1 offset, making it Hreg 101)* |
| **Temp Setpoint** | Analog Value: 1 | Hreg: 101 | Temperature (°C) | x10 Multiplier (e.g., 210 = 21.0°C) *(Note: With the +1 offset in QModMaster, the setpoint is on Hreg 102)* |

## 🧰 Hardware Requirements & Compatibility

This project was developed and thoroughly tested on the following board:
* **Model:** YD-ESP32-S3 N16R8 (8MB PSRAM, 16MB Flash)
* **Purchase Link:** [AliExpress - ESP32-S3 N16R8](https://fr.aliexpress.com/item/1005006266375800.html) *(Select "N16R8 with Cable")*

> **Note on other ESP32-S3 boards:**
> The code works perfectly on generic ESP32-S3 **N16R8** boards. However, the built-in LED pin may vary depending on the manufacturer (configured here on `16` for the blue LED). If you use another board, simply modify `#define LED_PIN 16` in `main.cpp`. 
> *Note: If you see the "PSRAM ID read error" in the serial monitor at boot on some generic boards, simply ignore it. It is a harmless bootloader warning that does not affect the simulator.*

## 📂 Project Architecture

* `src/main.cpp`: Main logic, web server, Modbus mapping, and BACnet objects configuration.
* `lib/bacnet/`: The BACnet C software stack. *(Note: These files were manually added/imported from the original base BACnet-stack repository)*.
* `lib/bacnet/datalink/bip-esp32.c`: The **custom datalink layer** bridging the C stack and the ESP32's `WiFiUDP` library.
* `lib/bacnet/library.json`: PlatformIO configuration file optimized to exclude unused BACnet objects and drastically save RAM.

## ⚙️ Installation & Deployment

1. **Clone the repository:**
   ```bash
   git clone https://github.com/DoodzProg/bacnet-modbus-esp32.git
   ```

2. **Configure Wi-Fi credentials:**
   For security reasons, Wi-Fi passwords are not included. Create a file named `secrets.h` in the `include/` folder based on this template:
   ```cpp
   // include/secrets.h
   #ifndef SECRETS_H
   #define SECRETS_H

   #define WIFI_SSID "Your_WiFi_Network_Name"
   #define WIFI_PASS "Your_WiFi_Password"

   #endif
   ```

3. **Build and Upload:**
   Open the project in **VS Code** with the **PlatformIO** extension. Click the `Upload` button (➡️).
   *Troubleshooting:* On some generic boards, you may need to hold the `BOOT` button, press the `RST` button once, and release `BOOT` to force flash mode.

## 🛠️ Recommended Testing Tools

* **[YABE (Yet Another BACnet Explorer)](https://sourceforge.net/projects/yetanotherbacnetexplorer/):** Add your local PC IP, send a `Who-Is` request, and the `ESP32-CTA-Sim` node will appear.
* **[QModMaster](https://sourceforge.net/projects/qmodmaster/):** Connect to the ESP32 IP address on port `502` to read and write registers.
* **Web Browser:** Simply navigate to `http://<ESP32_IP_ADDRESS>` to view the real-time dashboard.