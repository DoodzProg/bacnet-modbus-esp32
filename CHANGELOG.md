# Changelog

All notable changes to this project are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

---

## [1.1.0] — 2026-04-09

### Added
- **mDNS hostname customization** — device accessible at `http://bms-<name>.local`; name set via web UI, persisted in NVRAM.
- **Persistent custom AP SSID** — user-supplied AP network name stored in NVRAM and respected on every reboot (previously always overridden by device name).
- **HTTP Basic Authentication** — SHA-256 password hash stored in NVRAM (`bms-auth` namespace); default-password banner displayed on dashboard until credentials are changed.
- **Network Configuration modal — 3-tab redesign:**
  - *Identity & Security* tab: device name + password change (default panel).
  - *Join Existing Network* tab: STA credentials + Wi-Fi scan.
  - *Create Isolated Network* tab: AP SSID + password.
  - Each network tab exposes its own "Apply & Reboot" button in the modal footer.
- **Clickable post-reboot links** — mDNS URL (STA mode) and mDNS URL (AP mode) rendered as blue hyperlinks in all reboot confirmation messages.
- **Browser probe silencers** — explicit 204 handlers for `/favicon.ico`, `/apple-touch-icon.png`, `/apple-touch-icon-precomposed.png`, `/manifest.json`, `/robots.txt`; eliminates spurious `_handleRequest(): request handler not found` log lines caused by the Arduino WebServer library.
- **`POST /api/device/name`** — sets device name (mDNS hostname + default AP SSID prefix); triggers safe reboot.
- **`POST /api/auth/change`** — updates web UI password; verifies current password before accepting new one.

### Changed
- **`/api/switch_network` promoted to POST with JSON body** — Wi-Fi credentials are no longer exposed as URL query parameters (no longer visible in server logs, browser history, or proxy traces).
- **`handleSystem()` refactored** — single NVS read block for all `bms-app` keys; eliminated redundant double-open and duplicate JSON build that remained from an earlier refactor.
- **`bms-auth` NVS namespace opened read-write throughout** — `_check_auth()` and `handleSystem()` no longer open `bms-auth` read-only; namespace is auto-created on first boot, eliminating the `nvs_open failed: NOT_FOUND` log spam.
- **Dashboard point cards redesigned:**
  - Protocol badges (`MB Reg:xxx`, `BN AV:xxx`) moved to **line 1** (top of card, left-aligned).
  - Point name moved to **line 2** (full card width, `text-overflow: ellipsis`, case preserved — no forced uppercase).
- **Network button** label simplified to **"Network"** (was context-sensitive: "Isolate Device (Local AP)" / "Connect to Enterprise Wi-Fi").
- **Modal close button** relabeled **"Close"** (was "Cancel").
- **`_handle_captive_redirect()`** returns 204 No Content in STA mode (was 404 Not Found).

### Fixed
- **AP SSID bug** — device always derived the AP SSID from the device name, ignoring the SSID typed by the user in the modal. `main.cpp` now reads `ap_ssid` from NVRAM and applies it with correct priority over the derived fallback.
- **NVS log spam** — `nvs_open failed: NOT_FOUND` printed to serial on every API poll when `bms-auth` namespace did not yet exist (first boot or before first password change).

### Security
- Wi-Fi and AP credentials now sent exclusively via POST request body (JSON) — never as URL query parameters.

---

## [1.0.1] — 2026-04-08

### Added
- `CHANGELOG.md` — this file; tracks all version history going forward.
- `CONTRIBUTING.md` — build prerequisites, code style rules, and pull-request checklist.
- `docs/BUILD_RELEASE.md` — step-by-step procedure for building and publishing release binaries.
- `docs/PARTITION_LAYOUT.md` — full partition table with offsets, sizes, and role descriptions.
- `-Wall -Wextra` compiler flags in `platformio.ini` with `‑Wno‑unused‑parameter` to suppress expected Arduino framework noise.
- Doxygen `@brief` on all internal HTTP handler functions in `web_handler.cpp`.
- Hardware notes block in `config.h` documenting LED behaviour and BOOT button (GPIO 0).

### Changed
- Version bumped to **v1.0.1** across all source file headers, `platformio.ini`, the web dashboard badge, and `README.md`.
- API Surface comment in `web_handler.cpp` updated to `v1.0.1`.

### Removed
- LED status logic: `LED_PIN` define, `pinMode`/`digitalWrite` calls, and the hardware-feedback block in `loop()`.
  Rationale: the YD-ESP32-S3 N16R8 board has no general-purpose controllable LED; system state is now observable exclusively via Serial logs and the Web UI.
- `#include "config.h"` from `main.cpp` (no longer needed after LED removal).

---

## [1.0.0] — 2026-04-04

### Added
- Browser-based flashing via [ESP Web Tools](https://DoodzProg.github.io/ESP32-BMS-Gateway-Multi-Protocol) — zero IDE, zero command line.
- GitHub Pages flash interface (`docs/index.html`, `docs/manifest.json`, merged firmware binary).
- Complete Doxygen headers on all public functions and data structures across `src/` and `include/`.
- Professional `README.md` with architecture diagram, protocol reference, and QR code installer link.

### Changed
- Production-ready release milestone; all core features stable and validated with YABE and professional BACnet supervisors.

---

## [0.3.0] — 2026-03-xx

### Added
- Persistent Wi-Fi credentials stored in NVRAM (survive OTA and power cycles).
- Explicit AP / STA mode toggle from the web UI.
- Automatic AP fallback (`BMS-Gateway-Config`) when STA connection fails within 10 seconds.
- mDNS registration — device accessible at `http://bms-gateway.local` with no IP lookup.
- `sessionStorage` front-end value cache — dashboard does not blank during device reboot.
- Click-to-copy for device and client IP addresses in the top bar.

### Changed
- Network panel redesigned with Wi-Fi scan, credential input, and mode-override controls.

---

## [0.2.0] — 2026-03-xx

### Added
- Full bidirectional gateway: writes from any source (BACnet, Modbus, Web UI) propagate immediately to all others.
- ISA 101 High Performance HMI compliant dashboard (desaturated palette, SVG icons, color reserved for operational states and alarms).
- Drag-and-drop section and point reordering with persistent layout saved to LittleFS.
- Live address-conflict detection in the point-configuration UI.
- Modbus scaling factor per analog point (e.g., ×10 for one decimal place over integer registers).
- Dark / light theme toggle with OS preference detection.

### Changed
- REST API expanded to 11 endpoints covering full point and layout CRUD.

---

## [0.1.0] — 2026-02-xx

### Added
- Initial release for ESP32-S3.
- Simultaneous **BACnet/IP** (UDP 47808) and **Modbus TCP** (TCP 502) server.
- Embedded web server on port 80 serving a Single-Page Application from LittleFS.
- Pre-configured AHU (Air Handling Unit) simulator point map: FanStatus (BV:0 / Coil 10), FanSpeed (AV:0 / Reg 100), TempSetpoint (AV:1 / Reg 101).
- Single-source-of-truth architecture: all points declared once in `state.cpp`, reflected automatically across all three protocol handlers.
- JSON configuration persistence to `/config.json` on LittleFS.
- Safe Reboot mechanism via `pendingReboot` flag to prevent watchdog crashes during web-triggered restarts.

[Unreleased]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v1.0.1...v1.1.0
[1.0.1]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v0.3.0...v1.0.0
[0.3.0]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/DoodzProg/ESP32-BMS-Gateway-Multi-Protocol/releases/tag/v0.1.0
