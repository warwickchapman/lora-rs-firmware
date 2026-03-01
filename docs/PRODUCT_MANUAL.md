# Sensible LoRa Relay Switcher (LRS)
## Product Manual

Firmware baseline: current state-machine firmware in this repository (`/Users/warwick/Code/LoRa/lora_rs`).

## 1. Product Overview
LRS extends a control signal over LoRa between identical ESP8266-based boards.

Mode/role model:
- `standalone` mode with role `none`
- `paired` mode with roles `transmitter` / `receiver`
- `mesh` mode with roles `coordinator` / `node`

Operationally, TX/coordinator behavior and RX/node behavior are selected from the commissioned role.

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
- Configurable commissioning mode/role on identical hardware (`standalone`, `paired`, `mesh`).
- SoftAP + web configuration console (password protected).
- LittleFS persistent settings.
- Optional MQTT bridge (STA mode).
- OTA support in STA mode.
- Structured logs available through serial plus `GET /api/logs.csv` and `GET /api/logs.txt`.
- DS18B20 support (local + remote telemetry over LoRa).

## 4. Networking and Access
Default access:
1. Join AP SSID: `lrs-<chipid>`
2. Open `http://192.168.4.1`
3. Login: user `admin`, password = factory-derived credential

mDNS behavior:
- AP-side service hostname: `lrs.local`
- STA-side hostname: configurable (`<lan_hostname>.local`)

## 5. Configuration (Web UI)
Tabs:
- Status
- LoRa
- Network
- MQTT
- Sensors
- Factory

Minimum required settings:
- Mode and role (mode-aware)
- Local address
- Remote address
- LoRa Fleet Key (encryption passphrase)

## 6. Topologies
- `paired` mode: classic TX/RX behavior with heartbeat + ACK semantics.
- `mesh` mode: coordinator/node role naming with the same underlying TX/RX branch behavior.
- `standalone` mode: local-only role (`none`) for non-paired local operation.
- Addressing still defines 1-to-1, 1-to-many, and many-to-1 layouts.

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
- Web auth: HTTP Basic Auth.
- Failed-login rate limiting enabled.
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
- If the gate is enabled, do not use MQTT `control` for RX units configured with the same address as TX `remote_address`, because heartbeat/input LoRa control can overwrite MQTT state.
- If you must MQTT-control the TX-paired address, disable `Input drives LoRa relay control`.

## 10. Sensor Support (Current)
Implemented now:
- Local DS18B20 reading
- DS18B20 temperature encoded in LoRa packet and shown as remote telemetry on peer
- Dry-contact state represented in LoRa sensor fields

Planned (not yet implemented):
- Tank level sensor integrations
- Flow sensor integrations
- Additional I2C/analog sensor models

## 11. Compatibility Note
Current LoRa payload format is 12 encrypted bytes.
This is not wire-compatible with older 8-byte or 4-byte payload firmware.
Update paired TX/RX devices together.

## 12. Field Diagnostics
Status page provides:
- Relay state
- LoRa RSSI and last LoRa packet age
- WiFi station state and RSSI
- AP status
- Sensor tiles (including dry-contact OPEN/CLOSED and local/remote temperature state)

Logs:
- `GET /api/logs.csv` download
- `GET /api/logs.txt` live text view

## 13. Provisioning and Sticker Data
Provisioning tooling:
- `/Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py`
- `/Users/warwick/Code/LoRa/lora_rs/tools/lrs_provisioning_cli.py`
- `/Users/warwick/Code/LoRa/lora_rs/tools/post_upload_sticker.py`

Factory metadata includes:
- serial
- chip ID
- MAC
- factory role/addresses
- AP/admin credentials
