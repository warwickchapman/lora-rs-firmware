# Feature Parity Review (main + mqtt branches)

## Implemented Parity
- TX/RX relay workflow over LoRa.
- ACK + heartbeat + timeout behavior.
- TX local relay echo after ACK with 500 ms delay.
- MQTT bridge (optional via settings).
- MQTT relay/control behavior from mqtt branch.
- 1-to-many and many-to-1 addressing patterns.
- OTA in STA mode.
- DS18B20 support (ported and integrated).

## Improved vs Legacy
- Stateful modular runtime (`app`, `state_machine`, `radio_protocol`, `web_console`).
- Persistent LittleFS settings and authenticated web console.
- AP SSID stabilized to `lrs-<chipid>` (role changes no longer rename AP).
- Better status UX (LoRa/WiFi/AP sections, relay/input/sensor visuals).
- Live text logs API plus CSV export.
- MQTT `last_updated` publish field.
- MQTT safeguards for control priority (short socket timeout, LoRa tick before MQTT).
- Sensor-capable packet structure with reserved fields for future expansion.

## Known Delta from Old Firmware
- LoRa encrypted payload expanded from 4 bytes to 8 bytes.
- Mixed old/new packet firmware is not interoperable.

## Remaining Gaps / Planned
- Rich analog sensor support (tank/flow/4-20 mA conditioning) remains pending.
- ESP32 hardware migration remains future architecture option only.

