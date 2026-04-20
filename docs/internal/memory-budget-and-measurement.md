# Memory Budget and Measurement Guide (ESP8266)

## Purpose
Refactor work here is stability-sensitive. Linker RAM numbers are not sufficient. Runtime fragmentation and max free block must be tracked.

## Primary Runtime Metrics
Collect and compare:
- `ESP.getMaxFreeBlockSize()` (primary regression signal)
- `ESP.getFreeHeap()`
- `ESP.getHeapFragmentation()`

Helper wrappers also exist in logger namespace:
- `lrslog::heapMaxFreeBlock()`
- `lrslog::heapFree()`
- `lrslog::heapFragPercent()`

## Required Measurement Points (Risky milestones)
At minimum, capture metrics at:
- boot idle (post-startup stabilization)
- after `/login`
- after `/` page load
- after status page open (`/api/status-static` + SSE)
- after settings save/apply
- provisioning start
- provisioning end/cancel

## Build-Time Checks (Supplementary)
Per build target (`lrs_za`, `lrs_us`):
- program size (flash)
- static RAM summary (`.data + .bss`)

Note: A refactor can preserve linker RAM while regressing runtime fragmentation.

## Logging/Collection Notes
- Prefer existing device logs that already emit heap/free-block in slow-phase and web API logs.
- Compare before/after under similar workflow timing.
- When reporting, treat `max_free_block` drops as high priority even if free heap is unchanged.

## Milestone 1 expectation
UI asset extraction is a maintainability/build organization improvement. Do not expect runtime heap improvement from asset extraction alone.

## Heap Stabilization Baseline (2026-02)
Baseline for this memory-stabilization tranche:
- `python3 -m platformio run -e lrs_za`
- RAM: `57440 / 81920`
- Flash: `713863 / 1044464`

### Instrumented Endpoints
Heap-probe telemetry is attached to:
- `/api/status-lite`
- `/api/status-static`
- `/api/fleet`
- `/api/provisioning/status`
- `/api/settings` (GET/POST)

Telemetry fields (before/after):
- `heap_free`
- `max_free_block`
- `heap_frag`
- `dur_ms`

Logging is throttled and emitted on:
- threshold breach,
- large delta,
- periodic sample interval.

### Soak Procedure
Run each milestone through:
1. Build check: `python3 -m platformio run -e lrs_za`
2. Runtime soak (10-20 min):
   - WebUI open
   - status polling and SSE active
   - one provisioning flow (discover + provision)
3. Functional checks:
   - settings save/import and reboot persistence
   - commissioning save/apply
   - MQTT test path

### Acceptance Criteria
- Fragmentation plateaus during soak (no persistent `max_free_block` ratchet down in idle polling window).
- No endpoint regressions/timeouts on hot APIs.
- No behavior regressions in settings, provisioning, commissioning, or MQTT flows.

## Replay Table Policy (2026-02 update)
- Replay protection source capacity is now compile-time configurable via `LRS_REPLAY_TRACKED_SOURCES` (default `16`).
- Peer runtime capacity remains `LRS_MAX_PEERS` (default `8`), and replay source capacity must be `>= LRS_MAX_PEERS`.
- Rationale:
  - keep deterministic static RAM bounds,
  - preserve transient sender headroom during provisioning/commissioning,
  - avoid oversized always-on tables on ESP8266.

### Replay Validation Signals
During soak and provisioning runs, monitor:
- `rx_replay_table_full_drop`
- `rx_replay_table_evict`
- `rx_replay_table_stale_evict`

Expected result for healthy operation:
- no persistent replay-table full drops in normal coordinator workflows.

Rollback trigger:
- frequent `rx_replay_table_full_drop` events during normal discovery/provisioning flow.
- If hit, raise `LRS_REPLAY_TRACKED_SOURCES` (recommended next step: `24`, then `32` if still needed).
