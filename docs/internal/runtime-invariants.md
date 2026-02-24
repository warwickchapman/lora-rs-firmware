# Runtime Invariants (Do-Not-Break Rules)

## Scope
These invariants must be preserved during monolith split phases unless an ADR or explicit review approval says otherwise.

## Global Runtime Invariants
- `App::tick()` phase ordering remains unchanged unless explicitly planned and reviewed.
- Hot paths must avoid hidden heap-heavy work and blocking behavior.
- Config changes apply only through explicit apply/config boundaries (`App::applyUpdatedConfig`).
- Hot paths use cached primitive runtime config values; cold/admin paths may use `String`/JSON/config access.

## LoRa / Protocol Invariants
- LoRa protocol behavior and packet semantics must not change during split phases.
- Replay protection semantics must not change (trusted-source behavior, stale eviction, full-table policy).
- Provisioning protocol semantics must not change (factory-key discovery vs production-key verify/apply).
- LoRa WiFi provisioning start/data/commit/hash semantics must not change.
- Factory reset over LoRa semantics must not change.

## `NodeStateMachine` Invariants (Future split protection)
- `tick()` ordering remains identical.
- Current `tick()` order (as of Milestone 5 prep) must be preserved:
  - `resetRadioTxBudgetForTick()`
  - `tickReceive()`
  - role branch: `tickTransmitter()` or `tickReceiver()`
  - `tickProvisioningTarget(millis())`
  - `tickLed()`
  - `finishRadioTxBudgetForTick()`
- One-radio-TX-per-tick budget semantics remain identical.
- No reintroduction of deep `Settings` copies.
- No new hot-path `String` config reads in TX/RX/replay/provisioning dispatch paths.
- Use `runtime_` cached primitives in hot paths unless unavoidable and reviewed.
- Preserve lazy provisioning storage lifecycle exactly:
  - allocate only on provisioning start/discovery path
  - free on cancel/reset/apply-config paths as currently implemented
  - no resize/reallocation patterns introduced
- Milestone 5 split scope guard:
  - replay/time-LED/peer/WiFi-factory helper moves are allowed
  - no TX/RX branch ordering or provisioning frame dispatch changes in helper-move commits

## `WebConsole` Stability/Heap Invariants (Recent wins to preserve)
- `/api/status` legacy compat endpoint remains retired and returns `410`.
- Status UI flow is SSE-first; do not reintroduce periodic HTTP status polling fallback in normal flow.
- Low-memory fallback page is diagnostic/manual-refresh oriented; do not add auto-poll loops there.
- `/?force_full=1` escape hatch remains available.
- Normal GET settings/status-style responses keep secret redaction behavior.
- `/api/network/test` remains setup-only and capped to the current synchronous window (~4s max).
- `/api/logs` history remains retired (`410` behavior); do not reintroduce in-memory log buffer in split phases.
- UDP log mirroring remains UDP-only, TTL-based, default-off.

## Web Memory-Guard Invariants (Preserve Exactly During Splits)
- Low-heap reject thresholds remain unchanged unless explicitly reviewed.
- Stale-cache fallback behavior remains unchanged.
- SSE low-heap connect/deny/degraded behavior remains unchanged.
- Low-memory fallback page trigger conditions remain unchanged.
