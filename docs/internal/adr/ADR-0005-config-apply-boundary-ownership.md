# ADR-0005: `App` Retains Config-Apply Boundary Ownership

## Status
Accepted (Milestone 1 baseline)

## Decision
`App` remains the owner of cross-subsystem config apply and restart side effects during the monolith split program.

## Rationale
- Centralizes lifecycle sequencing (`radio_`, `sm_`, `mqtt_`, `sensors_`, networking, OTA) in one place.
- Reduces risk of hidden restarts or inconsistent apply behavior.
- Matches current code structure and stability-oriented design.

## Implication
Monolith splitting should not move apply/restart ownership into `WebConsole` or other modules.
