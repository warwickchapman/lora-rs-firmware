# Project Instructions

- Guide poor design, UX, or architecture decisions directly, with the reason and a simpler alternative.
- Favor simple, reliable firmware and operator workflows over cleverness.
- Keep UX clean, intuitive, dense where useful, and neatly ordered.
- Maintain documentation and human-readable changelog entries when behavior changes.
- Do not discard uncommitted work. Inspect `git status --short` before editing.

## Control Priority Rule

Relay/input control is the product. Fleet, Monitor, inventory, diagnostics, firmware
metadata, WiFi details, heap stats, and UI freshness are observability.

- Observability must never create control-path load unless the operator explicitly asks for it.
- Relay/input control outranks Fleet scans, Monitor refreshes, diagnostics, OTA convenience, MQTT telemetry enrichment, and UI freshness.
- Firmware must not spend LoRa airtime on routine observability when there is no explicit consumer.
- Operational state is limited to relay state, input state, enabled sensor readings, and command ACK/failure state.
- Diagnostics/inventory includes firmware version, heap/free/frag, WiFi IP/RSSI, detailed uptime, and Fleet/Monitor freshness enrichment.
- Diagnostics/inventory must be low priority, interruptible, stale-tolerant, and clearly displayed as stale/deferred by Flasher rather than pressuring firmware.

### Control-State Integrity

- `relay_state` and `input_state` are control truth, not convenience telemetry. Their correctness outranks metadata, diagnostics, and UI freshness.
- A gateway or host may treat a control value as current only after receiving a frame that explicitly carries it: a valid command ACK, operational status/PollResponse/MqttStatus, or its designated normal maintenance field.
- Missing, timed-out, old-format, or unacknowledged control values are **unknown**. Hosts must render `-`; they must never coerce an absent value to `Off`, `On`, `Open`, or `Closed`.
- Any normal maintenance response used to repair peer state must explicitly carry the compact control state it claims to refresh. Identity, version, WiFi, and diagnostic fields must never reset, replace, or imply relay/input state.
- Command ACK staggering must reserve gateway receive-turnaround time before the first remote replies. Low-numbered nodes must not receive special control semantics or a shorter unsafe reply window.
- A missing group-command ACK is an unconfirmed actuator command, not a diagnostic condition. Recover with bounded idempotent direct control retries, not a non-actuating status poll. The gateway relay is the all-remotes-confirmed indicator: change it only after every configured remote confirms the same requested state, for both `On` and `Off`.

## ESP8266 Data Movement Rule

The ESP8266 gateway is memory-constrained. Host software is not. Design data
movement so the gateway does the least possible buffering, formatting, and
serialization work.

- Never build a large whole-fleet JSON/status payload when a compact summary plus one-row/detail fetches will do.
- Prefer incremental, bounded responses: one peer, one diagnostic page, one candidate, or one compact seed list at a time.
- Treat ArduinoJson object growth and temporary `String` serialization as expensive firmware work, even over USB serial.
- Do not justify bulky firmware responses by saying the host UI needs a complete table; the host must assemble tables progressively.
- Diagnostics are pulled explicitly by the operator or Flasher, not streamed or bundled into normal operational refreshes.
- If a command response can grow with fleet size, sensors, logs, retained state, or diagnostics, it needs a hard size budget and a simpler streaming or paging design.
- Prefer stale-but-clear UI state over firmware load that competes with relay/input control or LoRa scheduler timing.
- If a future plan adds whole-fleet telemetry, broad debug fields, or always-on observability traffic, reject it unless it proves why the incremental design cannot work.

## Plan Review Discipline

When reviewing an implementation plan against a defined boundary or constraint:

- Do not approve from a checklist. Test each item against the boundary rule.
- For every injected dependency, method, or option: state whether it is in-scope model/state or out-of-scope orchestration. Flag violations explicitly.
- Before approving, argue the rejection case. List what should be removed and why.
- Treat "not a blocker" as a decision that requires justification, not a default.

## Implementation Review Discipline

When reviewing an implementation against an approved plan:

- Verify: (1) the API matches what was approved, (2) no operator-facing behaviour changed unless explicitly planned, (3) every boundary rule from the plan is respected in the code, (4) tests cover the behaviours listed in the plan, not just happy paths.
- For every adapter/computed mapping, verify that every referenced variable/function exists in the current source and has the correct name and type.
- For every field, computed, template binding, and constant in the child, verify it matches the original behaviour in the source. Compare adapter mappings against the original expressions line by line.
- Flag anything added beyond the plan scope and anything from the plan that was skipped or simplified.

## Dev Build Versioning

- The repo-root `VERSION` file is the operator-visible firmware and Flasher version source of truth.
- Every development build that will be flashed, shared, or used for field testing must get a unique dev build revision before building.
- Use `MAJOR.MINOR.PATCH~DEVBUILD`, for example `0.9.2~1`, `0.9.2~2`, and `0.9.2~3`.
- The `~DEVBUILD` suffix implies a development build; do not add `-dev`.
- If `VERSION` is `0.9.2-dev`, the next development build is `0.9.2~1`.
- Increment only the dev build revision while staying on the same base patch.
- Do not reuse a dev build version for a different firmware or Flasher artifact.
- Use `python3 tools/bump_dev_build.py` before a new development build.
- Firmware builds record the source tree used for each `~DEVBUILD` value and reject reusing the same dev build number after code changes.
