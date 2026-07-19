# Change Playbook (Safe Ways to Modify Hot Code)

## Rules of Engagement
- Prefer behavior-preserving refactors first.
- Do not combine structural moves with semantic changes.
- Keep commits small and reversible by feature slice.
- Validate after every risky milestone using runtime memory metrics.

## Hot-Path Code (LoRa / State Machine / App tick)
Before changing:
- identify runtime and wire-protocol invariants in `../PROTOCOL.md`
- map call order and side effects
- define exact acceptance checks (protocol + timing + memory)

During changes:
- avoid new allocations in tick/RX/TX paths
- avoid hidden `String` churn
- preserve `runtime_` cache usage
- preserve replay/provisioning semantics exactly
- observe fixed-payload field-ownership rule: `sensor_mask` is authoritative; `b5` is multiplexed; no encoder may infer or silently add a meaning

After changes:
- run runtime memory checks (`max_free_block` primary)
- compare logs for slow-phase warnings and expected protocol events

## Admin/Runtime Split Work
- Preserve low-heap thresholds and fallback paths exactly.
- Preserve serial-admin authentication and redacted-secret behavior exactly.
- Keep local maintenance owned by Flasher over USB serial admin.
- Do not “clean up” wrappers until post-split stabilization milestone.

## Exception Handling
If current code differs from documented assumptions:
- treat mismatch as new plan input
- update docs/ADRs before proceeding
- do not force implementation to match stale documentation blindly
