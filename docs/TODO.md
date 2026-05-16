# TODO

## Done
- Build-time log level override support (`LRS_LOG_LEVEL_DEFAULT`) is implemented and usable from `platformio.ini` build flags.
- Log level numeric mapping for `LRS_LOG_LEVEL_DEFAULT` (`0=ERROR`, `1=WARN`, `2=INFO`, `3=DEBUG`) is implemented.
- MQTT dependency guard is implemented: `mqtt_control_enabled` requires `mqtt_client_enabled` across UI and API validation paths.
- Security review reference report captured at `docs/internal/security-review-2026-04-26.md`.
- Flasher-first EasyPair commissioning foundation is implemented: USB serial admin protocol, selected USB gateway, LoRa remote discovery/provisioning, verified target finalization, WiFi credential provisioning, and local Identify LED action.
- Web UI maintenance lifecycle is implemented: Web UI starts on boot, explicit user activity extends the 60-second window, background polling/SSE/captive probes do not extend it, and HTTP/SSE/captive DNS runtime work stops after inactivity. USB serial admin can re-enable it with `enable_web`.
- Serial Admin Phase 1A is implemented: firmware exposes `status`, authenticated `get_config`, authenticated `set_config`, and authenticated `factory_reset`; Flasher Flash mode exposes local status/config/reboot/factory-reset controls over the `LRS:` serial admin protocol.
- Flasher Fleet mode no longer uses device-side HTTP discovery/login/REST OTA/REST UDP-log paths; it now provides a temporary local firmware file server plus per-device UDP-log actions, with OTA and UDP log-control commands moving to serial/MQTT and gateway-mediated LoRa admin where the target remote already has WiFi.
- Flasher Fleet mode now reads the TX/gateway-owned peer cache over serial admin; a selected, commissioned USB gateway can explicitly scan remotes over encrypted LoRa maintenance-status packets and render a dense device table without HTTP/REST discovery.

## Web UI Removal Migration
- Extend Fleet inventory actions on top of the bounded maintenance-status packet, including identify, selected-device OTA, and WiFi/log actions that use the reported IP/connectivity state.
- Add selected-device `ota_pull` from Fleet inventory rows once WiFi/IP reachability is reported by the bounded maintenance-status response; add bulk OTA only after one-device flow is reliable.
- Add MQTT admin request/reply topics for online device status/config actions with broker ACL guidance and non-retained secret handling.
- Default versioned maintenance debug telemetry to disabled once an explicit device debug mode exists; for pre-release diagnostics it is currently enabled by default so Flasher/Fleet can collect heap, fragmentation, relay feedback, and uptime from remotes.
- Expand gateway-mediated LoRa admin allowlist for remote status, identify, sensor config, WiFi provision/enable/disable, reboot, guarded factory reset, and OTA-pull trigger where the payload can fit safely.
- Add staged gateway workflows for remote address, role, mode, fleet key, and shared radio parameter changes so a bad direct write cannot strand field devices.
- After serial, MQTT, and LoRa admin parity are verified on hardware, remove Web UI/REST/captive DNS/`ESP8266WebServer` from normal firmware and record RAM/flash deltas.

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

## Radio Configuration Change Protocol
- Implement a safe gateway-controlled staged protocol for changing shared LoRa radio parameters across paired devices.
- Shared radio parameters that must remain coordinated across participating radios:
  - frequency
  - spreading factor
  - bandwidth
  - coding rate
  - explicit/implicit header mode
  - CRC setting
  - preamble assumptions
- Keep TX power gateway-managed as part of the same workflow even though it does not affect packet decode compatibility.
- During normal paired operation, prevent manual local changes to shared radio parameters on remote units.
- Allow local/manual override only in factory mode, unpaired mode, recovery mode, or explicit installer/service mode.
- Implement staged flow:
  - `PREPARE` on current radio config
  - `COMMIT` on current radio config
  - `VERIFY` on new radio config
  - `CONFIRM` on new radio config
- `PREPARE_RADIO_CONFIG` must carry:
  - `tx_id`
  - `new_frequency`
  - `new_spreading_factor`
  - `new_bandwidth`
  - `new_coding_rate`
  - `new_tx_power`
  - `new_preamble`
  - `new_crc_mode`
  - `new_header_mode`
- Remote behavior on `PREPARE`:
  - validate requested config
  - store pending config in RAM or temporary storage only
  - do not apply yet
  - ACK readiness on the current working config
- Gateway rule: if any required remote does not ACK `PREPARE`, do not proceed to global `COMMIT`.
- `COMMIT_RADIO_CONFIG` must carry:
  - `tx_id`
  - `switch_at_time`
  - `confirm_timeout_s`
- Remote behavior on `COMMIT`:
  - ACK commit received on the current working config
  - switch to pending config at `switch_at_time`
  - do not save permanently yet
  - start confirm timeout after switching
- Gateway behavior on `VERIFY`:
  - switch to new radio config at `switch_at_time`
  - poll each remote on the new config
  - verify reply from each required remote on the new config
- Recovery behavior for missing remotes after switch:
  - try missing remote on the new config
  - if still no reply, switch back to old config and try the missing remote there
  - re-run `PREPARE` / `COMMIT` individually for that remote
- `CONFIRM_RADIO_CONFIG` behavior:
  - gateway sends `CONFIRM` only after required remotes are verified on the new config
  - remotes save new config permanently to flash
  - remotes clear pending config
  - remotes treat the new config as known-good
- Auto-revert safety rule:
  - if a remote switches to pending config but does not receive `CONFIRM` before timeout, revert to the previous known-good config
- Suggested confirm timeout ranges:
  - `SF9/SF10`: `20-30 s`
  - `SF11`: `45-60 s`
  - `SF12`: `60-90 s`
- Boot safety rule:
  - if pending radio config exists but was not confirmed, discard pending config and boot using previous known-good config
- Persistence rule:
  - never permanently save a new radio config until `CONFIRM` is received on the new config
- UI/configuration rule:
  - disable direct manual editing of shared radio parameters on paired remote units by default
  - show shared radio parameters as gateway-managed/read-only on remotes during normal operation
  - gateway UI may allow changes, but must apply them through this staged protocol only
- Advanced override mode:
  - protect with password or installer/service access
  - allow manual editing of frequency, spreading factor, bandwidth, coding rate, and header/CRC if exposed
  - warn clearly that changing these settings may break communication with the gateway
  - on override, apply immediately, mark device `out-of-sync with gateway`, and attempt reconnect using the new settings
  - provide recovery options: revert to last known-good config, or revert to factory defaults (for example `SF7`)
  - gateway should flag unreachable remotes after override as `Radio config mismatch / device unreachable`
- Design intent:
  - normal users get safe, coordinated changes via gateway only
  - advanced users retain a controlled manual recovery path
  - prevent permanent RF mismatch bricking while preserving field recovery options

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

## Provisioning
- Validate full 12-device EasyPair sessions on real hardware, including serial `provisioning_status` response sizing and operator-visible progress.
- Add production output records for EasyPair runs (gateway chip/serial, verified remote chip/address list, fleet key handling policy, firmware version, timestamp/operator).

## UX / API Cleanup
- Unify `deployment_key` and `fleet_passphrase` terminology under user-facing `Shared Fleet Key` (short form: `Fleet Key` where space is tight); place helper text directly under the key input explaining it is the shared passphrase used to derive LoRa encryption/authentication keys; choose one canonical API field name and treat old names as temporary input aliases only.
- Public firmware repo README update (`lora-rs-firmware`): add a brief desktop flasher summary covering what the app is, supported operating systems/architectures, and include at least one UI screenshot in the README.

## Flasher

## Logging / Observability
- Use `DEBUG` temporarily for diagnostics (startup watchdog investigation, provisioning flow tracing, API polling behavior); revert to `INFO` after testing to reduce log volume/serial overhead.
- Add lightweight crash breadcrumbs for ESP8266 Web UI instability: keep a documented exception-decoding workflow for bench/support use, persist last-reset context (`reset reason`, `heap_free`, `max_free_block`, and active web feature/path if known), and avoid building a heavyweight always-on crash-report pipeline unless later evidence shows it is needed.
- Phase 1 (minimal patch, ESP8266-safe): keep structured logging focused on diagnosability with low overhead: levels (`ERROR/WARN/INFO/DEBUG`), categories (`SYS/WIFI/NTP/LORA/SENSOR/WEB/API/FS`), redaction helpers, web/API request summaries (status + duration), and heap diagnostics on high-risk endpoints (`/api/status`, `/api/fleet`, `/api/provisioning/status`); keep default level at `INFO`; keep polling endpoints (`/api/status`, `/api/session`) at `DEBUG`.
- Phase 2 (later mass refactor): convert remaining ad-hoc prints across modules to the shared logging API; standardize event names/fields; add state-change/rate-limited logging patterns; review LoRa/web/API logs for spam/noise; expand structured coverage for provisioning/fleet workflows; document log taxonomy and operational/debug logging policy.
- Phase 2 guardrails: no secret leakage (fleet keys, passwords, tokens), avoid heap-heavy log string construction in hot paths, and preserve current runtime timing priorities (LoRa control path before MQTT).

## MQTT / Heap Discipline
- Measure fragmentation impact of recent MQTT topic-churn reduction using paired before/after probes (`heap_free`, `max_free_block`, `heap_frag_percent`) around `applyConfig()`, MQTT enable/disable, reconnect, and steady-state publish loops; treat `max_free_block` as the primary success metric.
- Deferred optimization (only if needed): tighten MQTT topic buffer sizes, reduce persistent topic buffers, and move rarely used topic buffers to stack/cold helpers to claw back static RAM **only after** confirming the publish-path refactor improves `max_free_block` stability.

## Fleet (Fleet-Wide Tools / Actions)
- Broadcast WiFi provisioning (current feature; keep as anchor item).
- Add fleet WiFi provisioning acknowledgements/status tracking (LoRa per-device ACK + optional WiFi join result) so UI can show `sent/acked/connected/failed` instead of broadcast-send-only feedback.
- Add fleet-wide remote factory reset for `selected` or `all` devices, with `keep fleet key` option.
- Add remote unit `Identify` action so a gateway can request a specific remote to flash its LED for physical identification.
- Expose `Identify` from `Fleet > Devices` and via MQTT command path.
- Keep the current Identify pattern reserved and consistent across firmware and Flasher UI: three fast flashes, pause, three fast flashes.
- Verify remote Identify uses the same higher-priority temporary LED mode so it remains visually distinct from normal RSSI / link-state indication.
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
- Add runtime-visible Web UI maintenance state reporting (`webui_enabled=true/false`, idle timeout remaining, last activity age, re-enable source) where it is useful for Flasher/support tooling.
- Consider whether a non-USB re-enable path is still worth adding. Current recovery path is reboot or USB serial admin `enable_web`; avoid adding HTTP/MQTT toggles unless there is a clear field-support need.
- Keep REST/API lifecycle coupled to Web UI for ESP8266 simplicity unless a future support workflow strongly justifies a separate always-on API surface.

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
- Measure steady-state heap and `max_free_block` before/after Web UI idle shutdown to confirm HTTP/SSE/captive DNS shutdown removes normal-operation pressure as intended.
- Keep the Web UI disabled-after-inactivity model simple on ESP8266. Do not add a separate low-memory hook/stub mode unless real field data shows reboot/USB re-enable is insufficient.

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
