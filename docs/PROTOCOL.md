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
- `nonce` (8)
- `encrypted payload` (8)
- `mac` (8, truncated SHA-256 output)

## Message Types
- `Ack` (`'A'`)
- `Change` (`'C'`)
- `Heartbeat` (`'H'`)
- `Mqtt` (`'M'`)

## Encrypted Payload Layout (8 bytes)
- `b0`: `relay_state`
- `b1`: `input_state`
- `b2`: `flags`
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

Notes:
- `sensor_analog0` is reserved for future analog sensor transport.
- Current firmware sets digital input and optional temperature fields.

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
- RX sends `Ack` for `Change` and `Heartbeat` (not for `Mqtt`).
- TX applies ACK-confirmed relay state with 500 ms delay.

## MQTT-to-LoRa Semantics
- MQTT `relay` topic sets only the local node relay state immediately.
- MQTT `control` topic is handled only when node role is TX.
- `control` payload: JSON with `addr` and `relay` (`0`/`1`).
- `addr` as JSON number is decimal (example: `40`).
- `addr` as JSON string is parsed as hex (example: `"0x28"` or `"28"`).
- TX rejects destination `0x00` and `0xFF`.
- On accepted `control`, TX sends LoRa message type `Mqtt` to `addr`.
- `Mqtt` LoRa messages are not ACKed by RX.

## Timing Defaults
- `heartbeat_ms`: 60000 (60 s)
- `ack_timeout_ms`: 5000 (5 s)

## Compatibility
The current 8-byte payload format is not wire-compatible with older 4-byte payload firmware.
Upgrade paired nodes together.
