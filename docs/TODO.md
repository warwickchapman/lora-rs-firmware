# TODO

## Done
- Build-time log level override support (`LRS_LOG_LEVEL_DEFAULT`) is implemented and usable from `platformio.ini` build flags.
- Log level numeric mapping for `LRS_LOG_LEVEL_DEFAULT` (`0=ERROR`, `1=WARN`, `2=INFO`, `3=DEBUG`) is implemented.
- MQTT dependency guard is implemented: `mqtt_control_enabled` requires `mqtt_client_enabled` across firmware and Flasher validation paths.
- Flasher-first EasyPair commissioning foundation is implemented: USB serial admin protocol, selected USB gateway, LoRa remote discovery/provisioning, verified target finalization, WiFi credential provisioning, and local Identify LED action.
- Serial Admin Phase 1A is implemented: firmware exposes `status`, authenticated `get_config`, authenticated `set_config`, and authenticated `factory_reset`; Flasher Flash mode exposes local status/config/reboot/factory-reset controls over the `LRS:` serial admin protocol.
- Flasher Fleet mode provides a temporary local firmware file server plus per-device UDP-log actions, with OTA and UDP log-control commands available through serial/MQTT and gateway-mediated LoRa admin where the target remote already has WiFi.
- OTA pull now requires a 64-character SHA256. Gateway-mediated LoRa OTA sends the digest over encrypted LoRa control before the remote downloads `/firmware.bin`, and firmware verifies the HTTP stream before finalizing the update.
- Flasher Fleet mode reads the TX/gateway-owned peer cache over serial admin; a selected, commissioned USB gateway can explicitly scan remotes over encrypted LoRa maintenance-status packets and render a dense device table.
- Local maintenance uses Flasher over USB serial admin.

## Admin and Fleet Roadmap
- Extend Fleet inventory actions on top of the bounded maintenance-status packet for remaining remote WiFi/config/reboot actions that use the reported IP/connectivity state.
- Add bulk OTA only after the one-device Flasher Fleet flow has enough hardware soak time.
- Add MQTT admin request/reply topics for online device status/config actions with broker ACL guidance and non-retained secret handling.
- Default versioned maintenance debug telemetry to disabled once an explicit device debug mode exists; for pre-release diagnostics it is currently enabled by default so Flasher/Fleet can collect heap, fragmentation, relay feedback, and uptime from remotes.
- Expand gateway-mediated LoRa admin allowlist for remote status, identify, sensor config, WiFi provision/enable/disable, reboot, guarded factory reset, and OTA-pull trigger where the payload can fit safely.
- Add staged gateway workflows for remote address, role, mode, fleet key, and shared radio parameter changes so a bad direct write cannot strand field devices.

## Sensors Roadmap (ESP8266 Track)
- Add sensor type selection for dry-contact input semantics (`float switch`, `start/stop`, generic dry contact).
- Add tank calibration UI and per-site alarm/status thresholds.
- Define flow sensor model and units.
- Decide whether future analog sensors should use `sensor_analog0` or additional maintenance telemetry pages.

## Hardware and Architecture
- Document CN1 usage and strap-pin caveats in production manuals.
- Keep ESP8266 support primary for current product.
- Evaluate ESP32 baseboard migration path separately (no current firmware migration committed).

## Protocol
- Introduce explicit protocol version field in packet.
- Define the update policy for future payload changes.

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

## Provisioning
- Validate full 12-device EasyPair sessions on real hardware, including serial `provisioning_status` response sizing and operator-visible progress.
- Add production output records for EasyPair runs (gateway chip/serial, verified remote chip/address list, fleet key handling policy, firmware version, timestamp/operator).

## UX / Config Cleanup
- Unify `deployment_key` and `fleet_passphrase` terminology under user-facing `Shared Fleet Key` (short form: `Fleet Key` where space is tight); place helper text directly under the key input explaining it is the shared passphrase used to derive LoRa encryption/authentication keys; choose one canonical config field name.
- Public firmware repo README update (`lora-rs-firmware`): add a brief desktop flasher summary covering what the app is, supported operating systems/architectures, and include at least one UI screenshot in the README.

## Flasher

## Logging / Observability
- Use `DEBUG` temporarily for diagnostics (startup watchdog investigation, provisioning flow tracing, API polling behavior); revert to `INFO` after testing to reduce log volume/serial overhead.
- Add lightweight crash breadcrumbs for ESP8266 instability: keep a documented exception-decoding workflow for bench/support use, persist last-reset context (`reset reason`, `heap_free`, `max_free_block`, and active subsystem if known), and avoid building a heavyweight always-on crash-report pipeline unless later evidence shows it is needed.
- Phase 1 (minimal patch, ESP8266-safe): keep structured logging focused on diagnosability with low overhead: levels (`ERROR/WARN/INFO/DEBUG`), categories (`SYS/WIFI/NTP/LORA/SENSOR/FS`), redaction helpers, and heap diagnostics around high-risk serial-admin, MQTT, LoRa, and provisioning paths; keep default level at `INFO`.
- Phase 2 (later mass refactor): convert remaining ad-hoc prints across modules to the shared logging API; standardize event names/fields; add state-change/rate-limited logging patterns; expand structured coverage for provisioning/fleet workflows; document log taxonomy and operational/debug logging policy.
- Phase 2 guardrails: no secret leakage (fleet keys, passwords, tokens), avoid heap-heavy log string construction in hot paths, and preserve current runtime timing priorities (LoRa control path before MQTT).

## MQTT / Heap Discipline
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

## Observability / Logging
- Implement structured logging with levels: `ERROR`, `WARN`, `INFO` (default), `DEBUG`, `TRACE`.
- Standardize categories/tags (e.g. `SYS`, `WIFI`, `NTP`, `LORA`, `SENSOR`, `FS`).
- Standardize one-line log format with level/category, `t=<millis>`, optional `unix=<epoch>`, `event=<name>`, and key-value fields.
- Log serial-admin and remote-admin requests with command, status, duration (`dur_ms`), and source where available.
- Keep default logging at `INFO`; make `DEBUG` / `TRACE` opt-in diagnostics modes.
- Avoid log spam in tight loops (prefer state-change logging and/or repeated-warning rate limiting).

## Future Platform
- Cloud dashboard (future workstream): capture requirements and architecture options, but do not begin implementation yet.

## Memory / Stability
- Watch Settings `get_config` heap headroom on ESP8266 during local maintenance.

## Testing / Stability
- Update bench serial monitoring to treat monotonic uptime as the primary health signal during stability runs; ignore Unix time drift/resets for pass/fail.
- Validate gateway plus multiple remotes responsiveness and MQTT control reliability before wider deployment.
- Plan and execute a full field deployment test once gateway/remotes are confirmed stable on the bench.

## Bench / Flasher Tooling
- Add a small Raspberry Pi serial logger if long soak tests need unattended boot-log and reboot-cause capture outside Flasher.
- Add reboot/crash pattern detection to that serial logger, including reset/reboot code extraction from serial logs.

## Gateway / Remote Mode
- Verify paired-mode commissioning and runtime behavior for one gateway with multiple remotes.
- Confirm that MQTT-enabled remotes remain responsive and that gateway-side control/status propagation is reliable under load.

## Paired Mode
- Implement multi-unit paired-mode workflow with unique addresses per unit.
- Ensure paired-mode TX builds an MQTT-readable tree of remote units and relay states for one-to-many simultaneous switching with status visibility.

## Architecture Extractions (Deferred -- Pull Only If Pain Drives)
- GroupCommandManager: extract live TX group command state (pending relay/input, command counters, ack matching, retry/deadline) from `NodeStateMachine`. Higher risk than Phase 3; only if group command debugging or modification becomes painful.
- ChunkedLoRaTransfer: extract shared chunk/start/data/commit transfer pattern used by WiFi provision, fleet key, and OTA pull control. Protocol paths; needs careful tests. Only if provisioning bugs or transfer maintenance pain forces it.
- ProvisioningCoordinator / ProvisioningTarget: extract provisioning subsystem from `NodeStateMachine`. Biggest remaining knot, highest regression risk. Only if provisioning bugs or maintainability pain force it.
- Peer limit review: evaluate `LRS_MAX_PEERS = 12` vs 8 for 1.0 release. Metric-driven decision based on actual deployment sizes. Current RAM savings estimate: ~880 bytes at 8 peers.

## Deferred From Addressing/Fleet Release
- Standalone LoRa telemetry-only behavior (local-only control with optional LoRa status broadcasting).
- Multi-controller management UX beyond primary-controller-first behavior.
- Flasher-managed automation rules, including a serial-admin configuration surface and bench validation before reintroducing any onboard runtime.
- Fleet WiFi policy automation (site templates / zone-based credential assignment) as part of the future Flasher-managed automation workstream.
- Extended connectivity verification signals beyond current LoRa/app-level status.
- Terminology redesign beyond current `Transmitter`/`Receiver` UI naming.
