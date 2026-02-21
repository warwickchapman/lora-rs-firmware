# Developer Guide

## 1. Scope
Current production firmware for ESP8266 LRS devices (TX/RX on identical hardware), including:
- LoRa relay control and ACK logic
- Web configuration console
- MQTT bridge
- Sensor baseline (DS18B20 + dry-contact state)

Repo:
- `/Users/warwick/Code/LoRa/lora_rs`

## 2. Build Targets
Defined in `/Users/warwick/Code/LoRa/lora_rs/platformio.ini`:
- `lrs_za` (433 MHz, `REGION_ZA`)
- `lrs_us` (915 MHz, `REGION_US`)

Commands:
- `python3 -m platformio run -e lrs_za`
- `python3 -m platformio run -e lrs_us`

## 3. Runtime Modules
- `src/app.*`: orchestrator (networking, mdns, OTA, tick order)
- `src/config_store.*`: LittleFS settings + deterministic defaults/provisioning
- `src/radio_protocol.*`: LoRa packet encode/decode, crypto/MAC
- `src/state_machine.*`: TX/RX logic, timeout/ack, relay/input state
- `src/mqtt_bridge.*`: MQTT publish/subscribe bridge
- `src/sensor_manager.*`: DS18B20 detection/reads
- `src/web_console.*`: embedded UI + REST endpoints
- `src/log_buffer.*`: FIFO logs, CSV/text export

## 4. Critical Defaults
- AP SSID: `lrs-<chipid>`
- AP mDNS: `lrs.local`
- Heartbeat default: 60 s
- ACK timeout default: 5 s
- DS18B20 default pin: GPIO0
- MQTT host default: `venus.local`

## 5. Tick Order and Performance
In `App::tick`:
1. networking update
2. mDNS update prep
3. inject local temperature into state machine
4. state machine tick (LoRa control path)
5. MQTT tick
6. sensor manager tick
7. web tick

This ordering keeps LoRa control priority above MQTT.

## 6. Networking Behavior
- Mode: AP+STA
- AP starts immediately
- STA attempts if credentials exist
- On stable STA and `ap_always_on=false`, AP can be disabled
- On STA failure, AP fallback remains available
- WiFi power-save disabled; TX power set high for stable local-link behavior

## 7. Web API
- `GET /`
- `GET /api/status`
- `GET /api/settings`
- `POST /api/settings`
- `GET /api/factory`
- `GET /api/wifi/scan`
- `GET /api/logs.csv`
- `GET /api/logs.txt`
- `POST /api/reboot`

## 8. Packet and Compatibility
Current payload is 8 encrypted bytes with sensor fields.
Older 4-byte payload firmware is not wire-compatible.

If payload semantics change again, add protocol-version signaling first.

## 9. MQTT Summary
MQTT runs only when:
- enabled in config
- host set
- STA connected

Base path:
- `<root>/lrs-<chipid>/...`

Published status topics (retained, every 10 s):
- `input`
- `dry_contact`
- `relay`
- `temp_c`
- `remote_temp_c`
- `type`
- `addr`
- `last_updated`
  - `addr` is published as `0xNN` (for example `0x9D`).

Subscribed control topics:
- `relay`: sets local relay directly on that node.
- `control`: TX-only JSON control for remote LoRa relay send.
- `control.addr` parsing: JSON number = decimal address, JSON string = hex address.
- TX publishes per-remote child state under `<root>/lrs-<tx_chipid>/remote/0xNN/...` (canonical path).
- TX can be commanded to poll remotes via:
  - `<root>/lrs-<tx_chipid>/remote/0xNN/poll_interval_s`
  - `<root>/lrs-<tx_chipid>/remote/0xNN/poll_now`
  - `<root>/lrs-<tx_chipid>/remote/0xNN/forget` (payload `1` removes runtime node and clears retained remote subtree topics)

TX input-to-LoRa control gate:
- Setting: `tx_input_lora_control_enabled` (LoRa tab).
- `true` (default): TX input transitions send `Change`; heartbeat relay field follows TX input state.
- `false`: TX still reports local input status, but does not send paired input-driven `Change`/`Heartbeat`; MQTT remote control remains active.
- MQTT deployments should avoid targeting RX nodes at the TX-paired `remote_address` unless this gate is `false`.

MQTT remote retry control:
- Setting: `mqtt_remote_retry_timeout_ms` (default 300000 ms / 300 s).
- TX retries `Mqtt` command sends with bounded Fibonacci-like backoff until timeout.
- RX replies with `MqttStatus` (`'S'`) including applied state and telemetry.
- TX polling uses `PollRequest` (`'P'`) / `PollResponse` (`'R'`) with the same retry-timeout window.
- TX scheduled polling controls:
  - `tx_mqtt_remote_polling_enabled` (default `false`)
  - `tx_mqtt_remote_default_poll_interval_ms` (default `60000`, enforced range `60000..3600000`)
  - `poll_interval_s` MQTT command is clamped to `0` (disable) or `60..3600` seconds.
- RX push-on-change controls:
  - `rx_push_on_change_enabled` (default `false`)
  - `rx_push_min_interval_ms` (default `60000`, enforced range `60000..3600000`)
  - when enabled, RX sends unsolicited `PollResponse` on debounced local input change, rate-limited by `rx_push_min_interval_ms`.

Discovery:
- Topic: `<root>/discovery/lrs-<chipid>`
- Retained JSON payload with identity/network/build fields.
- Published on connect and then every 60 s.

Reconnect/perf guards:
- Reconnect attempt interval: 30 s.
- MQTT socket timeout: 1 s.
- LoRa state machine tick executes before MQTT tick.

`last_updated` value is uptime milliseconds at publish time.

## 10. Hardware Input Notes (ESP8266)
CN1:
- +3V3, GND, GPIO0, GPIO2, GND, AIN

Constraints:
- GPIO0/GPIO2 are boot-sensitive / shared-purpose pins.
- AIN path is limited and board-conditioned.
- Advanced analog sensor support remains staged work.

## 11. Current Sensor Support
- Local DS18B20 read
- Remote LoRa temperature decode/display
- Dry-contact status included in sensor digital field

Future fields already reserved in payload for additional sensor telemetry.

## 12. Firmware Versioning Policy
- Use SemVer: `MAJOR.MINOR.PATCH[-PRERELEASE]`.
- `PATCH`: bug fixes, UI fixes, no protocol/config breaking change.
- `MINOR`: new backward-compatible features (new pages, MQTT fields, sensors).
- `MAJOR`: any breaking protocol/config behavior change.
- `PRERELEASE`: use suffixes like `-alpha`, `-beta`, `-rc.1` for non-final builds.

Build identity at runtime includes:
- `fw_version` (from `platformio.ini` `custom_fw_version`)
- `fw_git_sha`, `fw_git_branch`, `fw_dirty`
- `build_date`, `build_time`

Visibility:
- Web UI status table
- `/api/status`
- `/api/factory`
- MQTT discovery topic (`<root>/discovery/lrs-<chipid>`)
