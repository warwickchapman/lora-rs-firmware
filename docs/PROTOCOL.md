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
- `OtaPullControl` (`'O'`)
- `FactoryReset` (`'X'`)
- `Provisioning` (`'V'`)
- `MaintenanceRequest` (`'Q'`)
- `MaintenanceStatus` (`'T'`)
- `Reboot` (`'B'`)
- `SensorConfig` (`'K'`)
- `FleetKeyControl` (`'Z'`)
- `UdpLogControl` (`'U'`)

## Encrypted Payload Layout (12 bytes)
- `b0`: `relay_state`
- `b1`: `input_state`
- `b2`: `flags`
  - bit0: `time_authoritative` (`1` when sender time is NTP-authoritative)
- `b3`: `temp_code`
  - `0xFF` = not present
  - otherwise signed int8 Celsius (`int8_t`)
- `b4`: `sensor_mask`
  - bit0 (0x01): digital input/status present in `b5`
  - bit1 (0x02): temperature present in `b3`
  - bit2 (0x04): downlink RSSI present in `b6` (`sensor_analog0_lsb`)
  - bit3 (0x08): WiFi enabled state present in `b5`
  - *Note: `0x01` and `0x08` are mutually exclusive. A packet declaring both is invalid.*
  - other bits reserved
- `b5`: `sensor_digital0` (multiplexed: carries digital input if bit0 set, or WiFi state if bit3 set)
- `b6`: `sensor_analog0_lsb` (multiplexed: carries downlink RSSI if bit2 set)
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
  multi-target group command: two replay-identical broadcasts to destination `255`, then up
  to two bounded direct `Change` retries for each missing ACK. Direct retries
  are idempotent and use the same command ID; their ACKs are immediate rather
  than broadcast-slotted.
- Gateway relay safety is symmetric: the gateway relay changes only after every
  configured remote confirms the same group command, for both `Off` and `On`.
  The gateway relay is therefore an
  all-remotes-confirmed indicator, not an independent command output.
- When paired LoRa input control is enabled, periodic heartbeat sync uses the
  same multi-target group command path. Gateway targets always come from its
  known-peer registry; an empty registry sends no fleet traffic.
- RX applies relay state from `Change`, `Mqtt`, and `Heartbeat` only when the
  `kFlagPairedInputSlave` flag is set (input-control heartbeats).
- RX sends `Ack` for `Change` and `Heartbeat`. For a broadcast paired `Change`,
  RX waits 250 ms before its address-ranked slot so the duplicate broadcast has
  completed; it reserves LoRa airtime until that ACK is sent, coalescing one
  maintenance or poll response and deferring all other low-priority pages/pushes.
  Its `input_state` is always the
  remote's locally sampled dry-contact state; it never echoes the gateway input
  carried by a control command.
- `Ack` carries the acknowledged command counter in payload bytes `b8..b11` (`unix_time_s` slot reused for ACK correlation).
- RX sends `MqttStatus` for `Mqtt` with applied relay/input/temp state.
- TX may send `PollRequest` to RX.
- RX replies to `PollRequest` with `PollResponse` carrying relay/input/temp and telemetry fields.
- TX may send `MaintenanceRequest` to RX.
- TX sends `MaintenanceRequest` carrying the request version (set to `kMaintenancePayloadVersion`, i.e., `4`) in payload byte `b0` and diagnostics flag in `b1`.
- RX replies to `MaintenanceRequest` with versioned `MaintenanceStatus` pages (using payload version `4`). This is not compatible with maintenance-page versions `2` or `3`; paired nodes must be upgraded together.
  - Page `0` (Identity) carries identity, connectivity, IP address, signed WiFi RSSI in `b6` (0 means unavailable), marked relay state in `b7` (`0xA0` = Off, `0xA1` = On), and the sampled dry-contact input state in flag bit `b2.7` (`0` = Open, `1` = Closed). A valid identity page makes both relay and input state known.
  - Page `3` (Version) carries the firmware build number and uptime.
  - Page `2` (Sensors) carries page-indexed generic sensor readings from the local `SensorRegistry` (up to 2 readings per 4-byte slot per page; packs `kind`, `instance`, `state` (0=Disabled, 1=Missing, 2=Fault, 3=Ok, 4=Overrange, 5=Waiting), `scale`, and clamped `int16` values).
  - Page `1` (Debug) carries diagnostic statistics (heap free, max block, fragmentation, and uptime minutes).
- RX may also send unsolicited `PollResponse` (push-on-change mode) to report local input changes without an explicit poll.
- RX nodes with enabled sensors may send unsolicited `MaintenanceStatus` sensor pages to the gateway at a conservative 60-second operational cadence. This is sensor-state reporting, not a gateway-owned diagnostic sweep.
- TX/gateway firmware must not run perpetual maintenance sweeps for Fleet/Monitor freshness. Fleet scans, diagnostics, and inventory enrichment are explicit low-priority observability work and must yield to relay/input control.
- TX applies relay state from a command ACK immediately. A missed command ACK invalidates that relay state until a later ACK, normal status, or identity page confirms it; Fleet must show this as unknown rather than `Off`.
- TX accepts ACK only when the embedded acknowledged counter matches the currently pending command.

### Control-state integrity

Relay and dry-contact input are operational state, not diagnostics. A gateway records
relay state only from a valid command ACK, an operational status response, or the
marked identity-page relay field. It records input state only from a frame that
explicitly supplies input state, such as `PollResponse`, `MqttStatus`, or the Input
sensor reading. Omitted, timed-out, or old-format values are unknown; they do not mean
`Off`, `On`, `Open`, or `Closed`.

The command ACK is the fast relay-state repair path. Its staggered replies must include
an initial gateway receive-turnaround guard before the first remote slot, so address
`1` cannot reply before the gateway is receiving. Normal maintenance then repairs
state at low priority without introducing extra observability frames. Gateway command
input is control context, not peer telemetry, and must never overwrite a remote's local
dry-contact state in firmware or Fleet cache.

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
- Transmitting a remote factory-reset frame is not delivery confirmation. The gateway retains the remote's configured address/chip record after either reset variant; remove that record only through the explicit gateway forget action once the reset has been verified.
- `Reboot` (`'B'`) carries a compact magic-value command payload for a targeted remote reboot.
- `SensorConfig` (`'K'`) carries a compact magic-value command payload that updates remote DS18B20 and tank-sensor enablement.
- `FleetKeyControl` (`'Z'`) performs targeted same-key Fleet Key rollover using segmented `start`, `data`, and `commit` packets. The old fleet key authenticates the rollover command; the target switches to the new key only after a complete transfer and commit validation.

- `Reboot`, `SensorConfig`, `FleetKeyControl`, `OtaPullControl`, `UdpLogControl`, and `FactoryReset` must be addressed to the target device's LoRa address.
- Broadcast is reserved for controlled provisioning-style flows. Do not use broadcast for destructive or lockout-prone maintenance commands.
- Operators should verify the target identity in Flasher Fleet before sending reboot, sensor config, Fleet Key, OTA, factory-reset, or UDP log control commands.

## MQTT-to-LoRa Semantics
- MQTT `relay` topic is **read-only status** — retained, published by the gateway. Do not publish to it.
- MQTT `set/relay` topic accepts `1` or `0` payloads to set the local relay state immediately. Publish **non-retained**.
- MQTT `control` topic has been removed.
- TX publishes local `addr` topic value as `0xNN`.
- TX publishes peer node trees under canonical MQTT path `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/...` (where `NN` is the two-digit decimal address, and `peer_chipid` is the hexadecimal chip ID of the remote peer).
- Peer status leaves include operational topics such as `relay`, `input`, `ack_state`, `sensor/<kind>/<instance>/value`, and `sensor/<kind>/<instance>/state`.
- TX accepts peer command leaves under `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/`:
  - `set/relay` — payload `1` or `0`, forwarded as LoRa `Mqtt` command
  - `poll_interval_s`
  - `poll_now`
  - `wifi` (`1`/`0`, `on`/`off`, `enable`/`disable`, or JSON `{ "enabled": true|false }`)
  - `forget` (payload `1` removes node from TX runtime and clears retained peer subtree topics)
- TX rejects destination `0x00` and `0xFF`.
- On accepted peer `set/relay`, TX sends LoRa message type `Mqtt` to `addr`.
- RX replies with `MqttStatus` (counter echoed), and TX retries on timeout using bounded backoff until `mqtt_remote_retry_timeout_ms`.
- TX also supports periodic polling by sending `PollRequest` and expecting `PollResponse` with the same counter.
- TX publishes confirmed peer Wi-Fi state under `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/wifi` as retained `1`, `0`, or empty when unknown.
- Paired TX input-control waits for slotted ACKs after the broadcast
  command, then polls missing remotes using `PollRequest` (visibility only, no late actuation) one at a time until the hard retry
  deadline (`tx_command_retry_timeout_ms`). `flags.bit2` (0x04) on `PollRequest` means `b8..b11` (unixTimeS) carries a paired group command correlation id; matching `PollResponse` echoes it and must not set `time_authoritative`.

> **Non-retained commands:** Do not publish retained messages to `set/relay` or `peers/.../set/relay`. MQTT brokers may replay retained command payloads on reconnect. Commands must be published as non-retained.

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

Within the current 12-byte protocol generation, `WifiProvision`/`WifiControl`/`OtaPullControl`/`FactoryReset`/`Reboot`/`SensorConfig`/`FleetKeyControl` do not change frame size; they only define additional message types and alternate payload semantics.

Any future change that changes packet size, encrypted payload layout, replay behavior, addressing rules, or Fleet Key derivation is a breaking protocol change and should use a major version boundary or explicit protocol-version signaling.

## Remote MQTT Administration Protocol (Phase 1 & 2)
To allow remote gateway control over LAN or cloud networks:
- **Local Broker (Phase 2)**: Flasher can host a local `rumqttd` broker. It runs in unauthenticated mode on port `1883` (or user-defined port if busy) persistently until Flasher exits. Gateways can be configured manually to point at this broker's LAN IP.
- Durable Transport Specifications:
  - **Packet Size Ceiling**: A fixed maximum packet size of **1024 bytes** is established. All inbound commands and outbound responses must remain under this ceiling to fit PubSubClient's allocated heap buffer on the ESP8266.
  - **Compact Operational Config**: MQTT `get_config` and `set_config` operations must adhere to a compact/partial config schema containing only operational parameters (e.g. Wi-Fi SSID, MQTT host/port, sensor enable flags). Secrets, large arrays, and roll-keys are restricted from MQTT read/write. Full configuration read/writes remain serial-only.
  - **Retention Rules**: Command messages (`admin_command`) and response messages (`admin_response`) must be published as non-retained (`retained=false`) to avoid executing stale operations on reconnect. Operational status and telemetry topics remain retained.
- Topics:
  - Admin command topic: `<root>/lrs-<gateway_chip_id>/admin_command`
  - Admin response topic: `<root>/lrs-<gateway_chip_id>/admin_response`
  - OTA status topic: `<root>/lrs-<gateway_chip_id>/ota_status` (retained, publishes `"downloading"`, `"failed:<code>"`, `"rebooting"`)
- MQTT Admin Handshake (`admin_challenge`):
  Before executing commands, the client sends an unauthenticated `admin_challenge` request to establish a session:
  ```json
  {
    "id": "req-123456",
    "cmd": "admin_challenge"
  }
  ```
  Response payload includes:
  ```json
  {
    "ok": true,
    "cmd": "admin_challenge",
    "id": "req-123456",
    "session_id": 4829104,
    "expires_in_ms": 300000
  }
  ```

- JSON Schema for standard commands (post-handshake):
  ```json
  {
    "id": "req-123457",
    "cmd": "status",
    "session_id": 4829104,
    "seq": 1,
    "admin_password": "..."
  }
  ```
- Command validation sequence on gateway:
  1. **Strict Envelope Gate**: Verify presence of required fields (`id`, `cmd`). Reject malformed envelopes with `"invalid_envelope"`.
  2. **Handshake Pass-Through**: If `cmd` is `"admin_challenge"`, bypass session validation and dispatch directly.
  3. **Session Check**: Verify that `session_id` is provided (else `"admin_session_required"`), matches the active session (else `"admin_session_invalid"`), and that the session has not expired based on local `millis()` elapsed time (else `"admin_session_expired"`).
  4. **Sequence Replay Gate**: Verify that `seq` is provided and is strictly greater than `last_seq`. If not, reject with `"admin_sequence_replay"`. Note: once a sequence validation succeeds, the gateway immediately updates `last_seq` to the new sequence number, meaning that sequence number is permanently consumed even if subsequent command dispatch fails password verification or business logic.
  5. **Command Dispatch**: Proceed to execute the command. Password-gated commands will authenticate `admin_password` against the gateway configuration during execution.
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
  - Secrets are completely excluded from get_config responses sent over MQTT. Secret exports are only available over USB serial mode.
  - Flasher provisioning over MQTT must read retained `config/#` topics for gateway configuration state instead of issuing `get_config`, keeping the admin command channel for small action/status commands only.
- ESP8266 command-response budget:
  - The gateway must not build a complete host-facing table in firmware. The host is responsible for progressively assembling Fleet/Monitor views from compact summary responses, retained telemetry, and explicit one-peer/detail reads.
  - Gateway command responses must remain small enough to avoid heap fragmentation, large temporary `String` buffers, and long serial/MQTT stalls.
  - `lora_inventory_status` returns only the Fleet seed list (`address`, `role`, `mode`, and `chip_id` when cached) plus scan/candidate metadata. It must not serialize full telemetry for every peer.
  - USB serial Fleet/Monitor clients may request a normal one-remote maintenance refresh with `refresh_lora_peer`, then fetch that peer's detailed cached state with `lora_inventory_peer`. Normal peer reads return operational state such as firmware, IP/WiFi, relay, input, sensors, uptime, and LoRa/WiFi RSSI for that one peer only. Heap/free-block/fragmentation/debug uptime are diagnostics and require an explicit diagnostics request.
  - MQTT Fleet clients use the same seed-list model and decorate rows from retained peer telemetry topics. The firmware MQTT client still enforces a strict `1024`-byte packet size ceiling; same-key discovery candidates are capped at at most 4 entries over MQTT, heavy timestamps (`last_seen_ms`, `age_ms`) are omitted, and `candidate_total`/`candidate_truncated` expose the truncation state. Similarly, `provisioning_status` responses are optimized using a compact array-based schema over MQTT (`devices` as `[["chip_id", current_address, assigned_address, rssi, "state", fw_major, fw_minor, fw_patch, fw_build], ...]`), and verbose debug logs are pruned.

## UDP Mirroring Controls

- **udp_log_control** (Gateway UDP Mirroring)
  - **Scope**: Local gateway only.
  - **Transport**: MQTT-only. Rejects USB serial admin execution with `mqtt_required`.
  - **Schema**:
    ```json
    {
      "cmd": "udp_log_control",
      "enabled": true,
      "host": "192.168.1.100",
      "port": 5514,
      "ttl_s": 300
    }
    ```
- **remote_udp_log_control** (Remote UDP Mirroring)
  - **Scope**: Targets a specific remote node address via the LoRa bridge.
  - **Transport**: May be sent over USB serial or MQTT gateway command bridge.
  - **Eligibility**: Requires the remote node to have active/confirmed Wi-Fi connectivity and a valid IP address stored in the gateway's peer cache. Rejects with `peer_wifi_unavailable` if ineligible.
  - **Schema**:
    ```json
    {
      "cmd": "remote_udp_log_control",
      "address": 42,
      "enabled": true,
      "host": "192.168.1.100",
      "port": 5514,
      "ttl_s": 300
    }
    ```
- **UdpLogControl Frame Layout (`'U'`)**:
  - `relay_state`: Op code (`1` = set)
  - `input_state`: Enabled status (`0` = disable, `1` = enable)
  - `flags` / `temp_code`: 16-bit destination port (little-endian)
  - `sensor_mask` / `sensor_digital0` / `sensor_analog0`: 32-bit TTL duration in seconds (little-endian)
  - `unix_time_s`: 32-bit destination IPv4 host address (each byte represents an octet)
