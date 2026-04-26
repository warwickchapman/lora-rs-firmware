# TODO

## Sensors Roadmap (ESP8266 Track)
- Add sensor type selection for dry-contact input semantics (`float switch`, `start/stop`, generic dry contact).
- Add UI and payload mapping for tank level model(s).
- Define flow sensor model and units.
- Implement reserved `sensor_analog0` usage and scaling conventions.
- Add sensor alarm/status thresholds in UI.

## Hardware and Architecture
- Document CN1 usage and strap-pin caveats in production manuals.
- Keep ESP8266 support primary for current product.
- Evaluate ESP32 baseboard migration path separately (no current firmware migration committed).

## Protocol and Migration
- Introduce explicit protocol version field in packet.
- Add backward/compatibility migration policy for future payload changes.

## Security Review Actions (2026-04-26)
- Immediate containment: rotate credentials for devices whose OTA/admin auth values were committed, remove real OTA secrets from `platformio.ini`, and replace tracked per-device OTA environments with placeholder templates plus an ignored local operator profile.
- Firmware credential redesign: replace deterministic chip-ID-derived AP/admin credentials with per-device random factory credentials, record them in controlled factory outputs, and force admin password rotation during first commissioning.
- Flasher/release trust: add a signed release manifest containing firmware asset names, regions, versions, and SHA256 hashes; make the flasher verify the manifest signature and asset hash before flashing downloaded firmware.
- Runtime randomness: add a central ESP8266 secure-random helper seeded during boot and use it for web session tokens, LoRa packet nonces, boot nonces, provisioning session nonces, and transfer IDs.
- Network security documentation: document isolated installer/OT network requirements for HTTP admin, ArduinoOTA, MQTT control, fleet provisioning, and support access; treat MQTT control as requiring broker ACLs plus VLAN/VPN isolation unless TLS is later proven practical.
- SoftAP posture: decide whether `ap_always_on` should default off after successful STA commissioning and document the recovery path if the AP is disabled.
- Secret export/support handling: make normal config export redacted by default, add an explicit include-secrets path if needed, and document how to handle stickers, CSVs, screenshots, flasher logs, and support bundles.
- Flasher hardening: add a restrictive Tauri CSP, keep shell permissions limited to the esptool sidecar, and avoid remote UI assets.
- LoRa operational risk: document jamming/interference limits, required link-margin checks, and intentional RX fail-safe selection for each installation.
- Reference report: `docs/internal/security-review-2026-04-26.md`.

## Provisioning
- Add pair-mode provisioning flow (first unit TX, second unit RX, linked output records).

## UX / API Cleanup
- Unify `deployment_key` and `fleet_passphrase` terminology under user-facing `Shared Fleet Key` (short form: `Fleet Key` where space is tight); place helper text directly under the key input explaining it is the shared passphrase used to derive LoRa encryption/authentication keys; choose one canonical API field name and treat old names as temporary input aliases only.
- Public firmware repo README update (`lora-rs-firmware`): add a brief desktop flasher summary covering what the app is, supported operating systems/architectures, and include at least one UI screenshot in the README.

## Flasher

## Logging / Observability
- Build-time log level override (`LRS_LOG_LEVEL_DEFAULT`): set in `platformio.ini` via `build_flags` (e.g. `-DLRS_LOG_LEVEL_DEFAULT=3`) to change the default runtime verbosity for a build.
- Log level numeric values for `LRS_LOG_LEVEL_DEFAULT`: `0=ERROR`, `1=WARN`, `2=INFO` (normal default), `3=DEBUG`.
- Use `DEBUG` temporarily for diagnostics (startup watchdog investigation, provisioning flow tracing, API polling behavior); revert to `INFO` after testing to reduce log volume/serial overhead.
- Add lightweight crash breadcrumbs for ESP8266 Web UI instability: keep a documented exception-decoding workflow for bench/support use, persist last-reset context (`reset reason`, `heap_free`, `max_free_block`, and active web feature/path if known), and avoid building a heavyweight always-on crash-report pipeline unless later evidence shows it is needed.
- Phase 1 (minimal patch, ESP8266-safe): keep structured logging focused on diagnosability with low overhead: levels (`ERROR/WARN/INFO/DEBUG`), categories (`SYS/WIFI/NTP/LORA/SENSOR/WEB/API/FS`), redaction helpers, web/API request summaries (status + duration), and heap diagnostics on high-risk endpoints (`/api/status`, `/api/fleet`, `/api/provisioning/status`); keep default level at `INFO`; keep polling endpoints (`/api/status`, `/api/session`) at `DEBUG`.
- Phase 2 (later mass refactor): convert remaining ad-hoc prints across modules to the shared logging API; standardize event names/fields; add state-change/rate-limited logging patterns; review LoRa/web/API logs for spam/noise; expand structured coverage for provisioning/fleet workflows; document log taxonomy and operational/debug logging policy.
- Phase 2 guardrails: no secret leakage (fleet keys, passwords, tokens), avoid heap-heavy log string construction in hot paths, and preserve current runtime timing priorities (LoRa control path before MQTT).

## MQTT / Heap Discipline
- Measure fragmentation impact of recent MQTT topic-churn reduction using paired before/after probes (`heap_free`, `max_free_block`, `heap_frag_percent`) around `applyConfig()`, MQTT enable/disable, reconnect, and steady-state publish loops; treat `max_free_block` as the primary success metric.
- In the Web UI, only enable the `MQTT control enabled` checkbox when `MQTT client enabled` is ticked; keep the dependency obvious in the form state instead of relying on save-time validation alone.
- Deferred optimization (only if needed): tighten MQTT topic buffer sizes, reduce persistent topic buffers, and move rarely used topic buffers to stack/cold helpers to claw back static RAM **only after** confirming the publish-path refactor improves `max_free_block` stability.

## Fleet (Fleet-Wide Tools / Actions)
- Broadcast WiFi provisioning (current feature; keep as anchor item).
- Add fleet WiFi provisioning acknowledgements/status tracking (LoRa per-device ACK + optional WiFi join result) so UI can show `sent/acked/connected/failed` instead of broadcast-send-only feedback.
- Add fleet-wide remote factory reset for `selected` or `all` devices, with `keep fleet key` option.
- Add staged fleet key rotation workflow.
- Add broadcast poll / discovery refresh.
- Add fleet-wide schedule defaults push.
- Add batch firmware rollout trigger (future; depends on OTA over LoRa / coordination support).
- Add fleet health summary (stale/offline counts, last-seen distribution).
- Add fleet export (device list, RSSI snapshot, statuses).

## System / Diagnostics (Device-Level Tools)
- Add `System > Diagnostics` sub-menu to group troubleshooting tools and avoid clutter in top-level System settings.
- Add remote serial console / TCP serial monitor (VS Code-style monitor over TCP/IP) under `System > Diagnostics`.

## Memory / Peer Cache Optimization
- Reduce `PeerRuntime` RAM footprint (if heap pressure persists):
  - Pack booleans/enums into a bitfield byte: `in_use`, `temp_valid`, `downlink_rssi_valid`, `pending`, `poll_pending`, `ack_state`.
  - Combine retry fields: `retry_step` + `poll_retry_step` into a single byte (4 bits each).
  - Store `pending_relay` as 1 bit.
  - Consider narrower counters/timestamps if safe (risk: wrap on long intervals); `last_cmd_counter` could be `uint16_t` if protocol permits.
  - Drop `downlink_rssi_valid` and use sentinel `downlink_rssi = -128` to save a byte.
  - If polling is optional, move poll state into a separate struct allocated only when enabled.

## WiFi Provisioning UX
- When clicking `Use` on a scanned WiFi network, auto-fill SSID and focus the password field.
- Scroll password field into view after `Use` (especially on mobile).
- If network is open (no password), skip password focus and move to connect/save action.
- If password is already prefilled/stored, prefer focusing connect/save instead of forcing password edit.

## Firmware Runtime Behavior
- DS18B20 (GPIO0): split persistent `enabled` config from runtime `present` detection state.
- If DS18B20 is enabled but not detected, report `missing` / `not present`, skip readings/publish, and retry detection periodically.
- If DS18B20 is detected later, activate automatically without requiring a reboot or config rewrite.
- Do not auto-disable DS18B20 config on failed detection (avoid boot-time false negatives becoming sticky state).
- Consider configurable RX fail-safe in paired-input mode: latch last state indefinitely if TX stream disappears.
- Add runtime/API-visible Web UI enabled-state reporting (`webui_enabled=true/false`).
- Add runtime/API-visible REST API enabled-state reporting (`restapi_enabled=true/false`) if Web UI and API are independently controllable.
- Add HTTP and MQTT commands to enable/disable the Web UI to save memory, and report current Web UI state.
- Evaluate two-level memory-control model:
  - `webui_enabled=true/false`
  - `restapi_enabled=true/false`

## Observability / Logging
- Implement structured logging with levels: `ERROR`, `WARN`, `INFO` (default), `DEBUG`, `TRACE`.
- Standardize categories/tags (e.g. `SYS`, `WIFI`, `NTP`, `LORA`, `SENSOR`, `WEB`, `API`, `FS`).
- Standardize one-line log format with level/category, `t=<millis>`, optional `unix=<epoch>`, `event=<name>`, and key-value fields.
- Log web page/path requests and API calls with method, path, status, duration (`dur_ms`), and client IP (if available).
- Add mandatory secret redaction in logs (WiFi passwords, fleet keys, tokens, other secrets).
- Keep default logging at `INFO`; make `DEBUG` / `TRACE` opt-in diagnostics modes.
- Avoid log spam in tight loops (prefer state-change logging and/or repeated-warning rate limiting).

## Future Platform
- Cloud dashboard (future workstream): capture requirements and architecture options, but do not begin implementation yet.

## Memory / Stability
- Add post-commissioning low-memory Web UI mode to reduce heap pressure and improve stability.
- In low-memory mode, Web UI should either be disabled until reboot or run as a minimal hook/stub endpoint.
- When the hook is accessed, start full Web UI on demand.
- After a configurable inactivity timeout, revert from full Web UI back to low-memory mode.

## Testing / Stability
- Set up a stability test with two units switching every minute and a Raspberry Pi capturing console logs for the full exercise.
- Update Devmon uptime monitoring to treat monotonic uptime as the primary health signal during stability runs; ignore Unix time drift/resets for pass/fail.
- Test automation on a standalone unit first, then implement and verify automation behavior across multiple units.
- Validate `mesh` mode (gateway + multiple nodes) responsiveness and MQTT control reliability before wider deployment.
- Plan and execute a full field deployment test once `mesh` mode is confirmed stable on the bench.

## Devmon / Flasher Tooling
- Extend Devmon to run over serial on a Raspberry Pi instead of UDP so it can capture boot logs and reboot causes directly.
- Add reboot/crash pattern detection to the serial Devmon flow, including reset/reboot code extraction from serial logs.
- Port the crash/reboot detection logic from the flasher tool into the Raspberry Pi Devmon extension.

## Mesh Mode
- Verify `mesh` mode commissioning and runtime behavior for one gateway with multiple nodes.
- Confirm that MQTT-enabled mesh nodes remain responsive and that gateway-side control/status propagation is reliable under load.

## Paired Mode
- Implement multi-unit paired-mode workflow with unique addresses per unit.
- Ensure paired-mode TX builds an MQTT-readable tree of remote units and relay states for one-to-many simultaneous switching with status visibility.

## Deferred From Addressing/Fleet Release
- Standalone LoRa telemetry-only behavior (local-only control with optional LoRa status broadcasting).
- Multi-controller management UX beyond primary-controller-first behavior.
- Fleet WiFi policy automation (site templates / zone-based credential assignment).
- Extended connectivity verification signals beyond current LoRa/app-level status.
- Terminology redesign beyond current `Transmitter`/`Receiver` UI naming.
