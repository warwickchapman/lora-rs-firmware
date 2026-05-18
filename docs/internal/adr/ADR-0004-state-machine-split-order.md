# ADR-0004: `NodeStateMachine` Split Order and Constraints

## Status
Accepted (Milestone 1 baseline)

## Decision
Split `NodeStateMachine` only after documentation/invariant prep, using a risk-ordered same-class multi-`.cpp` sequence.

## Split Order (High Level)
1. replay logic
2. time/LED helpers
3. peers + MQTT peer commands/polling
4. WiFi provisioning/factory reset helpers
5. provisioning gateway/target helpers
6. provisioning frame handler
7. TX/RX branches
8. `tickReceive()` near last
9. core orchestration (`tick`, begin/apply) last

## Constraints
- Preserve tick ordering exactly.
- Preserve one-TX-per-tick budget semantics.
- Preserve replay/provisioning semantics exactly.
