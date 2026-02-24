# ADR-0003: WebConsole Contract Freeze Policy During Split

## Status
Accepted (Milestone 1 baseline)

## Decision
Freeze route paths and core semantics during split phases, but do not blindly freeze every response field shape.

## Policy
- Frozen: route paths, core semantics, auth/session behavior.
- Endpoint schema handling:
  - UI-internal endpoints may accept controlled non-breaking field changes and redaction improvements.
  - Operator/integration-facing endpoints prefer schema stability during split phases.
- Explicit exception list governs allowed behavior changes outside split-only work.

## Rationale
Prevents preserving defects as immutable contract while still protecting behavior during refactors.
