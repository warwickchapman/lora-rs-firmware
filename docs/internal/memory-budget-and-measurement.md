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
