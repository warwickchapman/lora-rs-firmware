# Admin API Contract Notes and Freeze Policy

## Purpose
Define what is frozen during refactor phases and what may still change by explicit exception.

## Contract Freeze Policy (Refactor Phases)
### Frozen by default
- Route paths
- Core endpoint semantics (what action an endpoint performs)
- Authentication/session behavior
- LoRa/provisioning protocol semantics
- MQTT topic schema

### Not blindly frozen
Response schemas are not globally frozen field-by-field across all endpoints.

Use endpoint-class policy below.

## Endpoint Compatibility Policy
### UI-internal endpoints (browser UI owned by firmware)
- Allowed: non-breaking field additions, redaction/masking improvements, internal cleanup
- Allowed with review: field removals/renames if UI is updated in same change and milestone scope permits
- Not allowed during split-only phases: semantic changes, route changes, auth changes

### Operator-facing / automation-facing admin endpoints
- Prefer schema stability during split phases
- Allowed exceptions must be explicitly listed and reviewed
- If changing, document compatibility impact and migration notes

## Explicit Exception List (Pre-freeze / outside split-only work)
These are allowed behavior changes when separately reviewed, even if contract freeze is otherwise active:
- security redaction fixes (remove/mask secrets in normal reads)
- `/api/network/test` behavior changes toward safer async/non-blocking design (if scheduled and reviewed)
- targeted sensor/admin contract cleanup (only if explicitly scoped and documented)

## WebConsole Behavior Notes to Preserve During Split
- `/api/status` remains retired (`410`)
- status flow is SSE-first
- low-memory fallback page remains diagnostic/manual-refresh oriented
- `/?force_full=1` remains supported
- settings/status GET responses preserve secret redaction behavior
- `/api/logs*` retired behavior remains
- UDP log mirroring endpoint semantics remain (UDP-only, TTL-based, default-off)
