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
- `Reboot` (`'B'`)
- `SensorConfig` (`'K'`)
- `FleetKeyControl` (`'Z'`)

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
- When paired LoRa input control is enabled, TX sends `Change` as a paced
  multi-target group command: first broadcast to destination `255`, then
  non-actuating `PollRequest` confirmations for missing ACKs.
- When paired LoRa input control is enabled, periodic heartbeat sync uses the
  same multi-target group command path rather than the legacy single
  `remote_address`.
- When paired LoRa input control is disabled, TX sends periodic `Heartbeat` to
  the configured `remote_address`.
- RX applies `Change`/`Heartbeat`/`Mqtt` relay state.
- RX sends `Ack` for `Change` and `Heartbeat`.
- `Ack` carries the acknowledged command counter in payload bytes `b8..b11` (`unix_time_s` slot reused for ACK correlation).
- RX sends `MqttStatus` for `Mqtt` with applied relay/input/temp state.
- TX may send `PollRequest` to RX.
- RX replies to `PollRequest` with `PollResponse` carrying relay/input/temp and telemetry fields.
- TX may send `MaintenanceRequest` to RX.
- RX replies to `MaintenanceRequest` with versioned `MaintenanceStatus` pages (using payload version `2`). 
  - Page `0` (Identity) carries identity and connectivity.
  - Page `3` (Version) carries the firmware build number and uptime.
  - Page `2` (Sensors) carries page-indexed generic sensor readings from the local `SensorRegistry` (up to 2 readings per 4-byte slot per page; packs `kind`, `instance`, `state`, `scale`, and clamped `int16` values).
  - Page `1` (Debug) carries diagnostic statistics (heap free, max block, fragmentation, relay feedback, and input feedback).
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
- The remote node maintains strict stateful LoRa telemetry silence throughout both control-frame assembly and the entire HTTP download loop (cleared only on reboot or explicit failure).
- The HTTP firmware stream is hashed while being written to the inactive OTA slot. The update is finalized only if the final SHA256 matches the authenticated digest. On any hash validation failure or download error, the remote node explicitly clears its active OTA state to lift silence blocks.
- `FactoryReset` (`'X'`) carries a compact command payload to request remote factory reset.
- `FactoryReset` supports an option to preserve the current shared fleet key during reset.
- `Reboot` (`'B'`) carries a compact magic-value command payload for a targeted remote reboot.
- `SensorConfig` (`'K'`) carries a compact magic-value command payload that updates remote DS18B20 and tank-sensor enablement.
- `FleetKeyControl` (`'Z'`) performs targeted same-key Fleet Key rollover using segmented `start`, `data`, and `commit` packets. The old fleet key authenticates the rollover command; the target switches to the new key only after a complete transfer and commit validation.

Targeting rules:
- `Reboot`, `SensorConfig`, `FleetKeyControl`, `UdpLogControl`, `OtaPullControl`, and `FactoryReset` must be addressed to the target device's LoRa address.
- Broadcast is reserved for controlled provisioning-style flows. Do not use broadcast for destructive or lockout-prone maintenance commands.
- Operators should verify the target identity in Flasher Fleet before sending reboot, sensor config, Fleet Key, OTA, or factory-reset commands.

## MQTT-to-LoRa Semantics
- MQTT `relay` topic sets only the local node relay state immediately.
- MQTT `control` topic is handled only when node role is TX.
- `control` payload: JSON with `addr` and `relay` (`0`/`1`).
- `addr` as JSON number is decimal (example: `40`).
- `addr` as JSON string is parsed as hex (example: `"0x28"` or `"28"`).
- TX publishes local `addr` topic value as `0xNN`.
- TX publishes peer node trees under canonical MQTT path `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/...` (where `NN` is the two-digit decimal address, and `peer_chipid` is the hexadecimal chip ID of the remote peer).
- Peer status leaves include operational topics such as `relay`, `input`, `ack_state`, `sensor/<kind>/<instance>/value`, and `sensor/<kind>/<instance>/state`.
- TX accepts peer control leaves:
  - `poll_interval_s`
  - `poll_now`
  - `wifi` (`1`/`0`, `on`/`off`, `enable`/`disable`, or JSON `{ "enabled": true|false }`)
  - `forget` (payload `1` removes node from TX runtime and clears retained peer subtree topics)
- TX rejects destination `0x00` and `0xFF`.
- On accepted `control`, TX sends LoRa message type `Mqtt` to `addr`.
- RX replies with `MqttStatus` (counter echoed), and TX retries on timeout using bounded backoff until `mqtt_remote_retry_timeout_ms`.
- TX also supports periodic polling by sending `PollRequest` and expecting `PollResponse` with the same counter.
- TX publishes confirmed peer Wi-Fi state under `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/wifi` as retained `1`, `0`, or empty when unknown.
- Paired TX input-control waits for slotted ACKs after the broadcast
  command, then polls missing remotes using `PollRequest` (visibility only, no late actuation) one at a time until the hard retry
  deadline (`tx_command_retry_timeout_ms`). `flags.bit2` (0x04) on `PollRequest` means `b8..b11` (unixTimeS) carries a paired group command correlation id; matching `PollResponse` echoes it and must not set `time_authoritative`.

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

Within the current 12-byte protocol generation, `WifiProvision`/`WifiControl`/`UdpLogControl`/`OtaPullControl`/`FactoryReset`/`Reboot`/`SensorConfig`/`FleetKeyControl` do not change frame size; they only define additional message types and alternate payload semantics.

Any future change that changes packet size, encrypted payload layout, replay behavior, addressing rules, or Fleet Key derivation is a breaking protocol change and should use a major version boundary or explicit protocol-version signaling.

## Remote MQTT Administration Protocol (Phase 1)
To allow remote gateway control over LAN or cloud networks:
- Topics:
  - Admin command topic: `<root>/lrs-<gateway_chip_id>/admin_command`
  - Admin response topic: `<root>/lrs-<gateway_chip_id>/admin_response`
- JSON Schema for commands:
  ```json
  {
    "id": "req-123456",
    "cmd": "status",
    "admin_password": "...",
    "ts": 1717171717,
    "ttl_ms": 5000
  }
  ```
- Command validation rules on gateway:
  1. If NTP sync is active, checks that `ts` is within `ttl_ms` of current Unix time to prevent stale execution. If NTP is not yet active, this time check is bypassed to ensure boot rescue availability.
  2. Protects against duplicate requests using a circular request cache of size 10.
  3. Validates the `admin_password` against the gateway's configured `admin_password`.
- JSON Response layout:
  ```json
  {
    "ok": true,
    "cmd": "status",
    "id": "req-123456",
    "...": "command-specific response payload"
  }
  ```
- Credential protection:
  - The `"get_config"` command refuses to export secrets unless the `allow_mqtt_secret_export` config setting is explicitly set to `true` on the gateway.

