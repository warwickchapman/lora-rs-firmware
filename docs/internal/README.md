# Internal Firmware Architecture Docs

This folder is maintainer-facing documentation for the ESP8266 LoRa/MQTT firmware refactor and long-term operation.

Scope:
- takeover notes
- runtime invariants
- contract/freeze policy for refactor work
- memory measurement guidance
- file ownership and safe-change playbooks
- ADRs for architecture decisions

These are not customer docs.

## Milestone 1 status
This baseline was created before monolith splitting to support behavior-preserving refactors.

## Review gates
Use the acceptance criteria in the refactor milestone plan plus:
- runtime memory checks (`ESP.getMaxFreeBlockSize()` primary)
- route/contract compatibility policy in `admin-api-contract-notes.md`
- runtime invariants in `runtime-invariants.md`

## Heap Pressure Milestones (2026-02-25)
Baseline and milestone measurements for `lrs_za`:

- Baseline: RAM `57440 / 81920`, Flash `713863 / 1044464`
- M1 (UI payload slimming): RAM `57408 / 81920`, Flash `711447 / 1044464`
- M2 (poll/cache tuning): RAM `57408 / 81920`, Flash `711447 / 1044464`
- M3 (provisioning status compact for active sessions): RAM `57408 / 81920`, Flash `711383 / 1044464`
- M4 (always compact provisioning status): RAM `57392 / 81920`, Flash `709743 / 1044464`
- M5 (request-log allocation cleanup): RAM `57388 / 81920`, Flash `709595 / 1044464`
- M6 (stack JSON for status-lite/session): RAM `57388 / 81920`, Flash `709627 / 1044464`
- M7 (validation + docs/changelog + stale UI handler cleanup): RAM `57404 / 81920`, Flash `706931 / 1044464`

Net from baseline to M7:
- RAM: `-36` bytes
- Flash: `-6932` bytes
