---
feature: MQTT Relay Command Topics
status: delivered
specs:
  - docs/plans/2026-06-27-mqtt-relay-command-topics.md
plans:
  - docs/plans/2026-06-27-mqtt-relay-command-topics.md
branch: main
commits: 88b134d6..1f64e84c
---

# MQTT Relay Command Topics — Final Report

## What Was Built

Replaced the broken MQTT relay control model with clean `set/relay` command topics. The previous design had three problems: (1) the `relay` topic was dual-purpose (read/write), creating confusion; (2) the JSON `control` topic for peer relay was undocumented and non-functional; (3) the RX-side authorization rejected MQTT commands from paired gateways due to provisioning bugs storing wrong addresses.

The new protocol separates status from commands:
- **Status** (retained, read-only): `<root>/lrs-<gateway>/relay`, `<root>/lrs-<gateway>/peers/<NN_lrs-peer>/relay`
- **Commands** (non-retained, writable): `<root>/lrs-<gateway>/set/relay`, `<root>/lrs-<gateway>/peers/<NN_lrs-peer>/set/relay`

Commands accept plain `1` or `0` payloads. The legacy JSON `control` topic is removed.

## Architecture

### Gateway (TX) Side

`mqtt_bridge.cpp` subscribes to `set/relay` and `peers/+/set/relay` when `mqtt_control_enabled` is true. The `relay` status topic and `control` topic subscriptions are removed.

Handler flow for local `set/relay`:
1. Validates `mqtt_control_enabled`
2. Validates payload is exactly `0` or `1` (logs `mqtt_set_relay_bad_payload` otherwise)
3. Calls `sm_->mqttSetLocalRelay()`

Handler flow for peer `peers/<addr>/set/relay`:
1. Validates `role_tx` and `mqtt_control_enabled`
2. Parses address segment via `parsePeerAddressSegmentCstr()`
3. Validates payload is `0` or `1` (logs `mqtt_peer_set_relay_bad_payload` otherwise)
4. Calls `sm_->mqttSendPeerRelay()` which sends `MessageType::Mqtt` over LoRa
5. Logs `mqtt_peer_set_relay` or `mqtt_peer_set_relay_fail` based on return value

`mqttSendPeerRelay()` has two blocking conditions:
- `!role_tx` → logs `mqtt_remote_relay_no_tx`
- `input_control_paired_lora_enabled` → logs `mqtt_remote_relay_blocked`

### Remote (RX) Side

`state_machine.cpp` receives `MessageType::Mqtt` packets. The previous code had two blocks:

1. **`mqtt_control_enabled` check** (removed): Remotes don't need their own MQTT control flag — the gateway already validated and forwarded the command.

2. **`isAuthorizedMqttController()`** (simplified): In paired mode, accepts any same-key source. The fleet key authentication via the radio layer is sufficient authorization. Address-list matching was removed for paired mode because provisioning stores wrong gateway addresses (`remote_address=1` instead of `0xFE`).

The `paired_input_slave_mode_` check at line 3427 still blocks MQTT commands when the remote is in slave mode (set by `kFlagPairedInputSlave` in heartbeat flags).

### Diagnostic Events

| Event | Side | Meaning |
|-------|------|---------|
| `mqtt_set_relay_bad_payload` | Gateway | Local set/relay received non-0/1 payload |
| `mqtt_peer_set_relay_bad_payload` | Gateway | Peer set/relay received non-0/1 payload |
| `mqtt_peer_set_relay` | Gateway | Peer relay command accepted and sent |
| `mqtt_peer_set_relay_fail` | Gateway | Peer relay command send failed |
| `mqtt_remote_relay_no_tx` | Gateway | Device is not TX role |
| `mqtt_remote_relay_blocked` | Gateway | `input_control_paired_lora_enabled` blocking |
| `mqtt_remote_relay_tx` | Gateway | LoRa packet sent |
| `rx_mqtt_unauthorized_source` | Remote | Authorization rejected (paired mode now bypasses) |
| `rx_slave_block_mqtt` | Remote | `paired_input_slave_mode_` blocking |

## Usage

**Local relay control:**
```
Topic:   <root>/lrs-<gateway>/set/relay
Payload: 1 or 0 (non-retained)
```

**Peer relay control:**
```
Topic:   <root>/lrs-<gateway>/peers/<NN_lrs-peer_chipid>/set/relay
Payload: 1 or 0 (non-retained)
```

**Prerequisites:**
- Gateway: `mqtt_control_enabled = true`, `input_control_paired_lora_enabled = false`
- Remotes: Must be provisioned and paired with the gateway
- MQTT Explorer: Use "raw" mode, not "json" mode

## Verification

1. Factory reset all units, provision via USB
2. Confirm `input_control_paired_lora_enabled = false` on gateway (it resets to `true` after provisioning — must be disabled manually)
3. Publish `1` to `lora/lrs-<gateway>/set/relay` → local relay closes
4. Publish `1` to `lora/lrs-<gateway>/peers/<NN>/set/relay` → remote relay closes
5. Publish `0` → relays open
6. Publishing to `relay` status topics has no effect
7. Publishing to `control` has no effect (not subscribed)

## Known Issues

1. **`input_control_paired_lora_enabled` resets after provisioning**: The provisioning code at `app.cpp:768` sets this to `true` for TX devices. Operators must manually disable it after each provisioning. This is a separate bug.

2. **Provisioning stores wrong `remote_address`**: Remotes get `remote_address=1` instead of the gateway's actual LoRa address (`0xFE`). This was worked around by accepting any same-key source in paired mode, but the provisioning code should be fixed.

3. **Dual address formats**: The codebase has both hex (`0xFE`) and decimal (`254`) address representations exposed to users via `addr_hex` and `addr_dec` MQTT topics. This creates confusion and should be cleaned up.

## Journey Log

- [dead end] Original `peers/+/relay` topic approach — gateway doesn't subscribe to it, writes silently dropped
- [dead end] JSON `control` topic — worked on paper but had multiple silent failure modes (role_tx check, address parsing, authorization)
- [pivot] Adopted `set/relay` topic shape per Colin's review — clean read/write separation
- [lesson] RX-side `mqtt_control_enabled` check blocked forwarded commands from paired gateways — remotes don't need their own MQTT control flag
- [lesson] Provisioning stores wrong gateway address on remotes (`remote_address=1` vs actual `0xFE`) — fleet key auth is sufficient authorization in paired mode
