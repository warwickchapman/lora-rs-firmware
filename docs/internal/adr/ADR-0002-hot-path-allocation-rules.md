# ADR-0002: Hot-Path Allocation Rules

## Status
Accepted (Milestone 1 baseline)

## Decision
Hot runtime paths must avoid hidden heap churn and new dynamic allocation patterns.

## Rules
- No new hot-loop `String` churn in RX/TX/tick paths.
- No new `DynamicJsonDocument` in hot/timing-sensitive paths.
- No `Settings` deep copies reintroduced in runtime modules.
- Prefer cached primitive runtime config (`runtime_`) in hot paths.
- Preserve lazy provisioning storage lifecycle in `NodeStateMachine`.

## Rationale
ESP8266 stability is strongly affected by fragmentation and max free block, not only total free heap.
