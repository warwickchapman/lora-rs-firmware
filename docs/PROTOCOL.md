# Protocol Reference

Defined in:
- `/Users/warwick/Code/LoRa/lora_rs/src/radio_protocol.h`
- `/Users/warwick/Code/LoRa/lora_rs/src/radio_protocol.cpp`

## Packet Layout
Packed fields:
- `dst` (1)
- `src` (1)
- `type` (1)
- `counter` (4)
- `boot_nonce` (4)
- `nonce` (8)
- `encrypted payload` (12)
- `mac` (8, truncated SHA-256 output)

## Message Types
- `Ack` (`'A'`)
- `Change` (`'C'`)
- `Heartbeat` (`'H'`)
- `Mqtt` (`'M'`)
- `MqttStatus` (`'S'`)
- `PollRequest` (`'P'`)
- `PollResponse` (`'R'`)
- `WifiProvision` (`'W'`)
- `WifiControl` (`'Y'`)
- `UdpLogControl` (`'U'`)
- `OtaPullControl` (`'O'`)
- `FactoryReset` (`'X'`)
- `Provisioning` (`'V'`)
- `MaintenanceRequest` (`'Q'`)
- `MaintenanceStatus` (`'T'`)

## Encrypted Payload Layout (12 bytes)
- `b0`: `relay_state`
- `b1`: `input_state`
- `b2`: `flags`
  - bit0: `time_authoritative` (`1` when sender time is NTP-authoritative)
- `b3`: `temp_code`
  - `0xFF` = not present
  - otherwise signed int8 Celsius (`int8_t`)
- `b4`: `sensor_mask`
  - bit0: digital input/status present
  - bit1: temperature present
  - other bits reserved
- `b5`: `sensor_digital0` (currently dry-contact state mirror)
- `b6`: `sensor_analog0_lsb` (reserved)
- `b7`: `sensor_analog0_msb` (reserved)
- `b8..b11`: `unix_time_s` (little-endian UTC epoch seconds; `0` means unavailable)

Notes:
- `sensor_analog0` is reserved for future analog sensor transport.
- Current firmware sets digital input and optional temperature fields.
- Internet-connected nodes fetch NTP time and include `unix_time_s` in outbound frames.
- Peers accept `unix_time_s` for local sync only when `flags.bit0` (`time_authoritative`) is set.
- Some message types (`WifiProvision`, `FactoryReset`) reuse the same encrypted 12-byte payload slot with custom byte layouts.
- Firmware implements this via a raw-payload send path (`sendRaw`) that preserves the same frame size, crypto, MAC, and replay protection.

## Crypto
- Encryption: AES-CTR (128-bit key)
- MAC: keyed SHA-256 over packet body, first 8 bytes transmitted
- Key derivation:
  - ENC key: `SHA256(fleet_passphrase + ":enc")`, first 16 bytes
  - MAC key: `SHA256(fleet_passphrase + ":mac")`, full 32 bytes

IV:
- `nonce[8] + counter[4] + 0x00000000[4]`

## Validation and Drops
Receiver accepts packet only if:
1. size matches packet struct
2. MAC is valid
3. destination/source addressing passes local rules
4. counter is strictly increasing per source

Otherwise packet is dropped and logged.

## TX/RX Control Semantics
- TX sends `Change` on debounced input transition.
- TX sends periodic `Heartbeat`.
- RX applies `Change`/`Heartbeat`/`Mqtt` relay state.
- RX sends `Ack` for `Change` and `Heartbeat`.
- `Ack` carries the acknowledged command counter in payload bytes `b8..b11` (`unix_time_s` slot reused for ACK correlation).
- RX sends `MqttStatus` for `Mqtt` with applied relay/input/temp state.
- TX may send `PollRequest` to RX.
- RX replies to `PollRequest` with `PollResponse` carrying relay/input/temp and telemetry fields.
- TX may send `MaintenanceRequest` to RX.
- RX replies to `MaintenanceRequest` with versioned `MaintenanceStatus` pages. Page `0`
  carries identity/connectivity (`version`, `page`, flags, chip ID, firmware
  version, IP). Page `2` follows in a later radio tick with sensor state:
  dry-contact input, rounded DS18B20 temperature, tank status, tank depth in mm,
  tank current in centi-mA, tank voltage in mV, and tank enabled/valid flags.
  When debug telemetry is enabled, page `1` follows in a later
  radio tick with heap free, max heap block, heap fragmentation, relay feedback,
  input feedback, and uptime.
- RX may also send unsolicited `PollResponse` (push-on-change mode) to report local input changes without an explicit poll.
- TX applies ACK-confirmed relay state with 500 ms delay.
- TX accepts ACK only when the embedded acknowledged counter matches the currently pending command.

## Provisioning and Reset LoRa Extensions
- `WifiProvision` (`'W'`) carries segmented WiFi credentials (SSID + password) using custom payload bytes.
- Transfer format is `start`, `data`, `commit` messages over multiple packets.
- WiFi provisioning is broadcast to `0xFF` and accepted only by devices in the same fleet (same fleet key / valid MAC).
- Receiver validates transfer completeness and hash before applying credentials.
- `WifiControl` (`'Y'`) carries remote Wi-Fi enable/disable control and status.
- TX sends `WifiControl` op `set` (`payload b0=1`) with desired enabled state in `payload b1` (`1` enabled, `0` disabled).
- Broadcast disable uses destination `0xFF`; targeted enable/disable uses the remote LoRa address.
- RX persists the requested Wi-Fi enabled state, replies with `WifiControl` op `status` (`payload b0=2`), and echoes the command counter in payload bytes `b8..b11`.
- TX stores Wi-Fi state confirmations in runtime peer state only; polling/status responses can refresh the state after reboot.
- `OtaPullControl` (`'O'`) triggers a remote WiFi-connected node to pull `/firmware.bin` from a temporary HTTP server.
- OTA pull control is segmented as `start`, four `hash` chunks, and `commit`. The encrypted LoRa payload carries the server host/port and the 32-byte firmware SHA256; the target refuses to flash without a complete digest.
- The HTTP firmware stream is hashed while being written to the inactive OTA slot. The update is finalized only if the final SHA256 matches the authenticated digest.
- `FactoryReset` (`'X'`) carries a compact command payload to request remote factory reset.
- `FactoryReset` supports an option to preserve the current shared fleet key during reset.

## MQTT-to-LoRa Semantics
- MQTT `relay` topic sets only the local node relay state immediately.
- MQTT `control` topic is handled only when node role is TX.
- `control` payload: JSON with `addr` and `relay` (`0`/`1`).
- `addr` as JSON number is decimal (example: `40`).
- `addr` as JSON string is parsed as hex (example: `"0x28"` or `"28"`).
- TX publishes local `addr` topic value as `0xNN`.
- TX publishes peer node trees under canonical MQTT path `<root>/lrs-<tx_chipid>/peer/0xNN/...`.
- Peer status leaves include `input`, `dry_contact`, `temp_c`, `tank_status`,
  `tank_depth_mm`, `tank_current_ma`, and `tank_voltage_mv` when that telemetry
  is known from maintenance status.
- TX accepts peer control leaves:
  - `poll_interval_s`
  - `poll_now`
  - `wifi` (`1`/`0`, `on`/`off`, `enable`/`disable`, or JSON `{ "enabled": true|false }`)
  - `forget` (payload `1` removes node from TX runtime and clears retained peer subtree topics)
- TX rejects destination `0x00` and `0xFF`.
- On accepted `control`, TX sends LoRa message type `Mqtt` to `addr`.
- RX replies with `MqttStatus` (counter echoed), and TX retries on timeout using bounded backoff until `mqtt_remote_retry_timeout_ms`.
- TX also supports periodic polling by sending `PollRequest` and expecting `PollResponse` with the same counter.
- TX publishes confirmed peer Wi-Fi state under `<root>/lrs-<tx_chipid>/peer/0xNN/wifi` as retained `1`, `0`, or empty when unknown.
- Paired TX input-control retries use a low-latency bounded backoff (first retry in sub-second range), with small jitter and a hard retry deadline (`tx_command_retry_timeout_ms`).

## Timing Defaults
- `heartbeat_ms`: 60000 (60 s)
- `ack_timeout_ms`: 5000 (5 s)
- `mqtt_remote_retry_timeout_ms`: 300000 (300 s)
- `tx_command_retry_timeout_ms`: 180000 (180 s)
- `rx_failsafe_mode`: `hold_last` (default), options: `force_off`, `force_on`
- `rx_failsafe_timeout_ms`: 180000 (180 s)

## Compatibility
The current 12-byte payload format is not wire-compatible with older 8-byte payload firmware.
Upgrade paired nodes together.

Gateway-mediated remote OTA requires digest-capable firmware on both the USB gateway and target remote; older one-packet OTA trigger firmware will not interoperate with the SHA256-segmented trigger.

Within the current 12-byte protocol generation, `WifiProvision`/`WifiControl`/`OtaPullControl`/`FactoryReset` do not change frame size; they only define additional message types and alternate payload semantics.
