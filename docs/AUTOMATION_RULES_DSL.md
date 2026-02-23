# Automation Rules and Edge Rules Engine (Address-Centric Draft)

## Goal
Define a rules model that treats every node as a `Peer` and uses address + signal predicates, while preserving current paired TX/RX behavior unless explicitly enabled.

## Compatibility Guardrail
Rules execution must be gated by runtime mode:
- `standalone`: rules allowed (default safe lane)
- `paired`: current paired behavior only (no rules)
- `paired+rules`: paired behavior + rules, behind explicit enable flag

Proposed flags:
- `rules_enabled` (bool)
- `rules_mode` (`standalone` | `paired+rules`)

If `rules_enabled=false`, behavior is identical to current firmware.

## Peer Runtime Schema
`Peer` becomes the single runtime entity (paired peer included).

```json
{
  "addr": "0x52",
  "source_type": "paired",
  "capabilities": {
    "mask": 27,
    "has_input": true,
    "has_relay": true,
    "has_temp": false,
    "has_uplink_rssi": true,
    "has_downlink_rssi": true
  },
  "signals": {
    "reachable": true,
    "input": 0,
    "relay": 1,
    "temp_c": 34.2,
    "uplink_rssi_dbm": -92,
    "downlink_rssi_dbm": -88
  },
  "freshness": {
    "last_seen_ms": 4421180,
    "expected_interval_ms": 60000,
    "stale_after_ms": 180000,
    "is_stale": false
  },
  "paired_compat": {
    "ack_state": "ok",
    "last_cmd_counter": 812,
    "poll_interval_ms": 60000,
    "last_poll_tx_ms": 4419000
  }
}
```

### `source_type`
- `paired`: peer in legacy paired control path
- `mqtt`: peer data learned primarily from MQTT route
- `overheard`: peer heard opportunistically
- `polled`: peer learned from poll-only flow
- `local`: this device represented as a peer (`0xFD` in examples)

### Capabilities bit definitions
- `0x01`: `has_input`
- `0x02`: `has_relay`
- `0x04`: `has_temp`
- `0x08`: `has_uplink_rssi`
- `0x10`: `has_downlink_rssi`

## Rule JSON Shape
```json
{
  "id": "rule_temp_fan_0x52",
  "name": "Fan close above 35C",
  "enabled": true,
  "mode_scope": ["standalone"],
  "when": {
    "all": [
      {"peer": "0x52", "field": "temp_c", "op": ">", "value": 35}
    ]
  },
  "for_ms": 0,
  "cooldown_ms": 15000,
  "then": [
    {"action": "set_relay", "peer": "0x52", "value": 1}
  ]
}
```

### Predicate fields
- `peer`: address key (`0xNN`)
- `field`: one of `reachable`, `input`, `relay`, `temp_c`, `uplink_rssi_dbm`, `downlink_rssi_dbm`, `is_stale`
- `op`: `==`, `!=`, `>`, `>=`, `<`, `<=`
- `value`: bool/number

### Condition logic
- `when.all`: every predicate must be true
- `when.any`: at least one predicate true
- both may exist; evaluation is `(all true) && (any true)` when both present

### Timing controls
- `for_ms`: condition must stay true continuously before actions fire
- `cooldown_ms`: minimum time between firings for this rule

### Actions
- `set_relay(peer_addr, on|off)` encoded as:
  - `{"action":"set_relay","peer":"0xFD","value":1}` for `on` / close
  - `{"action":"set_relay","peer":"0xFD","value":0}` for `off` / open

Transport resolution stays internal (local GPIO vs paired link vs MQTT path).

## Example Payloads (Your 4 Cases)
Assumed semantics (matching current examples):
- relay `OPEN=0`, `CLOSED=1`
- float/input `OPEN=0`, `CLOSED=1`
- `close relay` => `value: 1`
- `open relay` => `value: 0`

### 1) Standalone Temperature Trigger
IF peer `0x52` temp > `35C`, THEN close relay on `0x52`.

```json
{
  "id": "r1_temp_over_35_close_relay_0x52",
  "enabled": true,
  "mode_scope": ["standalone"],
  "when": {
    "all": [
      {"peer": "0x52", "field": "temp_c", "op": ">", "value": 35}
    ]
  },
  "cooldown_ms": 10000,
  "then": [
    {"action": "set_relay", "peer": "0x52", "value": 1}
  ]
}
```

### 2) Timeout / Fail-safe
IF peer `0x52` unreachable for `10 min`, THEN open relay on `0xFD`.

```json
{
  "id": "r2_0x52_unreachable_10m_open_0xfd",
  "enabled": true,
  "mode_scope": ["standalone", "paired+rules"],
  "when": {
    "all": [
      {"peer": "0x52", "field": "reachable", "op": "==", "value": false}
    ]
  },
  "for_ms": 600000,
  "cooldown_ms": 60000,
  "then": [
    {"action": "set_relay", "peer": "0xFD", "value": 0}
  ]
}
```

### 3) Simple Remote Trigger
IF peer `0x51` input is ON/CLOSED, THEN close relay on `0xFD`.

```json
{
  "id": "r3_input_0x51_close_0xfd",
  "enabled": true,
  "mode_scope": ["standalone", "paired+rules"],
  "when": {
    "all": [
      {"peer": "0x51", "field": "input", "op": "==", "value": 1}
    ]
  },
  "for_ms": 500,
  "cooldown_ms": 3000,
  "then": [
    {"action": "set_relay", "peer": "0xFD", "value": 1}
  ]
}
```

### 4) Multi-Condition Safety
IF `0xFD` relay OPEN AND `0xFD` input OPEN AND `0x51` input CLOSED, THEN close relay on `0xFD`.

```json
{
  "id": "r4_multicond_recover_close_0xfd",
  "enabled": true,
  "mode_scope": ["standalone", "paired+rules"],
  "when": {
    "all": [
      {"peer": "0xFD", "field": "relay", "op": "==", "value": 0},
      {"peer": "0xFD", "field": "input", "op": "==", "value": 0},
      {"peer": "0x51", "field": "input", "op": "==", "value": 1}
    ]
  },
  "for_ms": 1000,
  "cooldown_ms": 5000,
  "then": [
    {"action": "set_relay", "peer": "0xFD", "value": 1}
  ]
}
```

## Notes on TX/RX Language Migration
- Yes: your examples can be read purely as addresses and peer signals.
- TX/RX should be treated as operational mode context (for compatibility lanes), not entity type.
- In paired deployments, keep current ACK/timeout state machine unchanged unless `rules_enabled=true` and `rules_mode=paired+rules`.
