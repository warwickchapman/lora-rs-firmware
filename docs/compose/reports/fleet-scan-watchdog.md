---
feature: fleet-scan-watchdog
status: delivered
specs: []
plans:
  - .mimocode/plans/1782836882158-eager-mountain.md
branch: main
commits: (uncommitted)
---

# Fleet Scan Watchdog — Final Report

## What Was Built

Two fixes to prevent Fleet UI from getting permanently stuck after a scan or serial timeout:

1. **Firmware scan watchdog**: A 60-second hard cap on `fleet_scan_active_` in `tickFleetScan()`. If a scan runs longer than 60 seconds, it auto-cancels and logs `fleet_scan_timeout`. This prevents the scan from suppressing normal peer maintenance indefinitely.

2. **Background timeout recovery**: When a background `lora_inventory_status` command times out on the serial link, the Flasher suppresses the error banner and keeps last good rows visible. Scan state is only cleared if the scan has been active for longer than the firmware watchdog window plus grace (75s), preventing a single missed serial response from hiding a genuinely active scan.

## Architecture

### Firmware side

`src/state_machine.cpp` — `tickFleetScan()` now checks `now - fleet_scan_started_ms_` at the top of each tick. If the elapsed time exceeds 60 seconds, the scan is cancelled and `fleet_scan_active_` is cleared. This re-enables `tickPeerMaintenance()` which was suppressed while the scan was active.

### Flasher side

`tools/flasher/src/components/Flasher.vue` — `refreshGatewaySnapshot()` catch block now detects background fleet timeouts by matching the error string `"timed out waiting for"`. On match, it calls `shouldClearScanStateOnTimeout()` which only clears scan state if the scan has been active for >75s. The helper function lives in `useFleetInventoryPolling.ts` and is unit-tested.

`fleetScanActiveSinceMs` ref tracks when the UI first entered scan mode. It is set both when `beginLoraInventoryScan()` starts a scan and when `inventory.scan?.active` is observed. It is reset on scan completion, cancel, cache clear, and stale timeout recovery.

### Data flow

```
Firmware scan starts → fleet_scan_active_ = true
  → tickPeerMaintenance() suppressed
  → tickFleetScan() probes each address
  → scan completes → fleet_scan_active_ = false
  → tickPeerMaintenance() resumes

If scan runs > 60s:
  → watchdog cancels scan
  → fleet_scan_active_ = false
  → maintenance resumes

If lora_inventory_status times out on serial:
  → Flasher catches timeout error
  → Suppresses error banner, keeps last good rows
  → If scan active >75s (stale): clears scan state, resumes cache polling
  → If scan active <75s (fresh): keeps scan state, retries next cycle
```

### Design Decisions

- **60-second hard cap, not adaptive**: Simple threshold. A 10-peer scan at 1s intervals should complete in ~10s. 60s provides generous headroom without complexity.
- **75s stale threshold, not first-timeout**: Background fleet refreshes are not user-initiated. A single missed serial response should not hide a genuinely active scan. The 75s threshold (firmware 60s + 15s grace) ensures the scan is truly stuck before clearing state.
- **fleetScanActiveSinceMs set on scan start**: The timestamp is recorded when the UI starts a scan, not only when `inventory.scan.active` is observed. This prevents the "never observed active" hole where immediate timeouts leave the timestamp at zero.
- **OTA label expiry unchanged**: Not directly verified as part of this fix. Existing `rowStateUntilMs` timers (30s for "Updated", 60s for "No reboot seen") remain in place.

## Verification

| Test | Result |
|------|--------|
| `pio test -e native` (79 cases) | All passed |
| `npm run test:unit --prefix tools/flasher` (102 cases) | All passed |
| `npm run build --prefix tools/flasher` | Built successfully |

### Manual verification steps (per Colin's brief)

1. **10 configured peers, heartbeat 60s**: Confirm one peer maintenance update roughly every 6s
2. **Start scan, let it complete**: UI must leave scan mode and resume cache polling
3. **Force lora_inventory_status timeout**: UI must not stay permanently in "Stop scan"
4. **OTA follow-up active**: Fleet refresh must not show persistent misleading timeout banners
5. **Firmware scan watchdog**: Simulate/force active scan past 60s — confirm it cancels and maintenance resumes
6. **Field quick test**: Click "Stop scan", wait 10-15s, refresh Fleet — updates should resume

## Journey Log

- [diagnosis] Colin identified the coupling: firmware scan suppresses maintenance, UI timeout prevents learning scan completion, creating a permanent stale state
- [fix] Two-part approach: firmware watchdog prevents indefinite suppression, UI timeout handler breaks the stale-state cycle
- [lesson] Background serial admin timeouts should not surface as operator-visible error banners — they're routine congestion, not failures
