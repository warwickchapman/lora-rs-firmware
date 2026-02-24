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
