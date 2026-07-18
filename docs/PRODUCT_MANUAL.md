# Sensible LoRa Relay Switcher (LRS)
## Product Manual

Firmware baseline: current state-machine firmware in this repository (`/Users/warwick/Code/LoRa/lora_rs`).

## 1. Product Overview
LRS extends a control signal over LoRa between identical ESP8266-based boards.

Mode/role model:
- `standalone` mode with role `none`
- `paired` mode with roles `transmitter` / `receiver`

Operationally, TX and RX behavior are selected from the commissioned role.

Primary use: reliable remote control signaling (generator start/stop interfaces, contactor coil control, remote dry-contact extension).

## 2. Hardware Scope
- MCU: ESP8266 (ESP-12F / ESP8266MOD class)
- LoRa: Ra-01 (433 MHz ZA), Ra-01H (915 MHz US)
- Supply: 5-80 VDC section on board
- Relay output: dry-contact switching output
- Inputs:
- 2-wire dry-contact input terminal (main control input)
- CN1 6-way sensor header:
  - Pin 1: +3V3
  - Pin 2: GND
  - Pin 3: GPIO0/PROG (shared boot strap pin)
  - Pin 4: GPIO2/LED (shared LED pin)
  - Pin 5: GND
  - Pin 6: AIN (ESP8266 ADC path, board-scaled)

Important ESP8266 constraints:
- GPIO0/GPIO2 are routed for flexibility but are boot-sensitive pins.
- AIN path is limited and not equivalent to multi-channel industrial analog input.
- Complex mixed analog sensor support is future ESP32-track work.

## 3. Core Features
- LoRa relay control with ACK/timeout behavior.
- Configurable commissioning mode/role on identical hardware (`standalone`, `paired`).
- USB local maintenance through the desktop Flasher app.
- LittleFS persistent settings.
- Optional MQTT bridge (STA mode).
- OTA support in STA mode.
- Structured logs available through serial, optional temporary UDP mirroring, and Flasher Monitor/Fleet views.
- DS18B20 support (local + remote telemetry over LoRa).

## 4. Networking and Access
Default local access is USB serial through Flasher. Remote online control and status use MQTT on installer-managed networks.

## 5. Configuration (Flasher)
Flasher tabs:
- Flash
- Provision
- Fleet
- Monitor
- Settings

Minimum required settings:
- Mode and role (mode-aware)
- Local address
- Controller address (remote only)
- LoRa Fleet Key (encryption passphrase)

## 6. Topologies
- `paired` mode: classic TX/RX behavior with heartbeat + ACK semantics.
- `standalone` mode: local-only role (`none`) for non-paired local operation.
- Addressing still defines 1-to-1, 1-to-many, and many-to-1 layouts.

Fleet workflow:
- `Fleet > Manage > LoRa`: discovery + provisioning for factory devices.
- `Fleet > Devices`: peer scan defaults to `1..32` (manual max `254`) and shows cached known peers immediately.
- `Fleet > Manage > WiFi`: send WiFi credentials to all known peers or target a single peer with optional override credentials.

Addressing and mode/role together define effective behavior and control ownership.

## 7. Timing and Safety
Current defaults:
- Heartbeat: 60 s
- ACK timeout: 5 s

TX behavior:
- Sends `Change` on input state change.
- Sends periodic `Heartbeat`.
- If ACK timeout expires while waiting, TX enters timeout and drops TX relay OFF.
- Later valid ACK/heartbeat ACK returns link to `Idle` and re-syncs relay state.

## 8. Security
- USB serial admin is the local maintenance path.
- MQTT control requires broker authentication, ACLs, and an isolated installer/operations network.
- LoRa payload encryption + packet authentication enabled.
- Credentials are derived from chip identity + product secret.

## 9. MQTT (Optional)
MQTT runs only when:
- MQTT enabled in settings
- MQTT host configured
- STA WiFi connected

Performance protections:
- LoRa state machine runs before MQTT in main loop.
- MQTT reconnect attempts are rate-limited.
- MQTT socket timeout is short (1 s) to reduce impact if broker is unavailable.

TX LoRa input-control gate:
- LoRa tab includes `Input drives LoRa relay control` when role is TX.
- Enabled: TX input and heartbeat drive paired RX relay state.
- Disabled: TX input is still available for telemetry/status, but TX does not send input-driven LoRa relay changes.

MQTT deployment note:
- If the gate is enabled, do not use MQTT `control` for RX units paired to the gateway input-control path, because LoRa input control can overwrite MQTT state.
- If you must MQTT-control the TX-paired address, disable `Input drives LoRa relay control`.

## 10. Sensor Support (Current)
Implemented now:
- Generic, instance-aware `SensorRegistry` supporting up to 6 configured sensor slots.
- Local environmental/measurement sensors (DS18B20 temperature, 4-20 mA tank level) and control inputs (dry contact).
- Sensor status and readings carried over LoRa via paginated version 2 `MaintenanceStatus` telemetry.
- Dynamic rendering of all reporting sensors in the Flasher Monitor/Fleet views and publication to the gateway MQTT registry topics (`sensor/<kind>/<instance>/value`).

Planned (not yet implemented):
- Tank calibration UI and alarm thresholds
- Flow sensor integrations
- Additional I2C/analog sensor models

## 11. Compatibility Note
Current LoRa payload format is 12 encrypted bytes.
This is not wire-compatible with older 8-byte or 4-byte payload firmware.
Update paired TX/RX devices together.

## 12. Field Diagnostics
Flasher status and Monitor/Fleet views provide:
- Relay state
- LoRa RSSI and last LoRa packet age
- WiFi station state and RSSI
- AP status
- Sensor tiles (including dry-contact OPEN/CLOSED and local/remote temperature state)

Logs:
- USB serial activity log
- optional temporary UDP log mirroring from Flasher/Fleet

## 13. Provisioning and Sticker Data
Provisioning tooling:
- `/Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py`
- `/Users/warwick/Code/LoRa/lora_rs/tools/post_upload_sticker.py`

Factory metadata includes:
- serial
- chip ID
- MAC
- factory role/addresses
- AP/admin credentials
