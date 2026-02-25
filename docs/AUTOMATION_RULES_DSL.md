# Automation Rules (v1 MVP)

This document describes the current reimplementation scope and guardrails for the embedded Automations feature.

## v1 Scope

- Address-centric rule model (`self` or peer address tokens)
- Visual builder + JSON preview
- Separate rules file: `/automation_rules.json`
- API: `GET/POST /api/automation-rules`
- Action type: `set_relay` only
- Builder condition mode: `when.all` only (AND)
- Compile-time feature gate: `LRS_ENABLE_AUTOMATIONS`

## Current Implementation Status

- Phase 1: storage + validation API (`complete`)
- Phase 2: builder UI shell (`complete`)
- Phase 3a: runtime engine skeleton (`complete`)
  - Compiles rules into fixed-size RAM structs
  - Evaluates first-match rule in app-owned tick
  - Logs matches/guard blocks only
- Phase 3b: self-relay actuation (`complete`)
  - Executes `set_relay(self, 0|1)` only
  - First-match rule wins
  - Skips repeat writes when relay is already in the desired state
- Phase 3c: timing semantics (`complete`)
  - Supports rule `for_ms`
  - Supports predicate `for_ms`
  - Supports rule `cooldown_ms`
  - Uses fixed-size runtime timing state (no JSON parsing in tick)
- Phase 4: observability instrumentation (`in progress`)
  - Heap/duration logging for automations API GET/POST
  - Heap/duration/doc-cap logging for rules compile/reload success/failure
  - Additional hardware soak/repetition testing still required

## Runtime Guardrails (v1 path)

- `execution_mode` must be `standalone` for runtime to proceed
- TX units are blocked only when `tx_input_lora_control_enabled=true`
- No JSON parsing occurs in the runtime tick path
- Rule compilation/reload happens outside the hot state machine path

## Builder / Schema Limits (v1)

- Max rules: `8`
- Max predicates per rule: `4`
- Max actions per rule: `4`
- JSON payload cap: `6144` bytes

## Example Shape

```json
{
  "schema_version": 1,
  "enabled": false,
  "execution_mode": "standalone",
  "peer_display": "addresses",
  "action_target": "self",
  "rules": [
    {
      "id": "rule_1",
      "name": "Example",
      "enabled": true,
      "for_ms": 0,
      "cooldown_ms": 60000,
      "when": {
        "all": [
          { "peer": "self", "field": "temp_c", "op": ">", "value": 35 }
        ]
      },
      "then": [
        { "type": "set_relay", "peer": "self", "value": 1 }
      ]
    }
  ]
}
```

## Notes for Next Phase

- Phase 4 adds observability and heap instrumentation around rules compile/load and API usage
  - Manual validation checklist still required (repeated page loads/saves, low-heap behavior, reboot soak)
