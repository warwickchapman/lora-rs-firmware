# Change Playbook (Safe Ways to Modify Hot Code)

## Rules of Engagement
- Prefer behavior-preserving refactors first.
- Do not combine structural moves with semantic changes.
- Keep commits small and reversible by feature slice.
- Validate after every risky milestone using runtime memory metrics.

## Hot-Path Code (LoRa / State Machine / App tick)
Before changing:
- identify runtime invariants in `runtime-invariants.md`
- map call order and side effects
- define exact acceptance checks (protocol + timing + memory)

During changes:
- avoid new allocations in tick/RX/TX paths
- avoid hidden `String` churn
- preserve `runtime_` cache usage
- preserve replay/provisioning semantics exactly

After changes:
- run runtime memory checks (`max_free_block` primary)
- compare logs for slow-phase warnings and expected protocol events

## WebConsole Split Work
- Preserve low-heap thresholds and fallback paths exactly.
- Preserve auth/session behavior exactly.
- Preserve route registration behavior exactly in route split milestone.
- Do not “clean up” wrappers until post-split stabilization milestone.

## Exception Handling
If current code differs from documented assumptions:
- treat mismatch as new plan input
- update docs/ADRs before proceeding
- do not force implementation to match stale documentation blindly
