# Takeover Risk Register (Baseline)

## R1: `WebConsole` monolith regressions during split
- Risk: low-heap guards/SSE/auth/route behaviors drift during file moves.
- Impact: runtime instability, request storms, broken admin flows.
- Mitigation:
  - preserve thresholds/behavior exactly
  - split UI assets first
  - route split before feature splits
  - runtime memory checks after risky milestones

## R2: `NodeStateMachine` semantic regressions during split
- Risk: replay/provisioning/TX-RX ordering drift while moving code.
- Impact: protocol correctness failures, control failures, hard-to-debug field regressions.
- Mitigation:
  - docs + invariants first
  - same-class split only
  - leave TX/RX core moves late
  - validate protocol behaviors and memory metrics per step

## R3: Assumption drift between plan and code
- Risk: handoff doc references stale implementation state.
- Impact: incorrect refactor constraints or wrong milestone sequencing.
- Mitigation:
  - mandatory re-baseline current code vs assumptions at milestone start
  - treat mismatches as plan input, not deviation

## R4: Hidden heap regressions despite unchanged linker RAM
- Risk: file moves and helper changes alter `String`/JSON behavior and fragmentation.
- Impact: reduced `max_free_block`, instability under admin load.
- Mitigation:
  - runtime memory metrics in acceptance criteria
  - `max_free_block` primary regression signal
