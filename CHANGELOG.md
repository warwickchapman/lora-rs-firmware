# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows SemVer.

## [Unreleased]

### Added
- API-first provisioning helper CLI: `tools/lrs_provisioning_cli.py` for logging into a coordinator, applying local settings (role/addresses/WiFi), running LoRa provisioning discovery + provision-all, and optionally broadcasting fleet WiFi credentials without relying on the embedded web UI.
- Example provisioning profile file: `tools/lrs_provisioning_profile.example.json`.
- Optional temporary UDP log mirroring control endpoint (`POST /api/logging/udp`) and provisioning CLI commands (`udp-log-start` / `udp-log-stop`) for bench debugging without web UI log buffering.
- Automations reimplementation Phase 1/2 foundations: separate rules store (`/automation_rules.json`), low-heap `GET/POST /api/automation-rules`, and an Automations builder shell with JSON preview.
- Compile-time automations feature flag (`LRS_ENABLE_AUTOMATIONS`) with UI/API/backend gating and UI compile-out support for lean firmware variants.
- Automations Phase 3 runtime foundation (`AutomationRulesEngine`) that compiles rules into fixed-size RAM structs, evaluates matches in an app-owned tick, and enforces v1 guardrails outside the state-machine hot path.
- Automations Phase 3b self-relay actuation (`set_relay(self, ...)`) with first-match execution, no-repeat behavior when already in the desired state, and dedicated automation relay ownership logging/status reason on RX.
- Automations Phase 3c timing semantics (`for_ms`, predicate `for_ms`, `cooldown_ms`) with fixed-size per-rule runtime state and transition-based hold/cooldown logging to avoid tick spam.
- Automations Phase 4 instrumentation pass: targeted heap/duration logs for `GET/POST /api/automation-rules` and rules compile/reload success/failure metrics (heap/max-block before/after, duration, doc capacity).
- Commissioning setup API (`POST /api/setup/commissioning`) and first-login commissioning page with mode/role selection, Fleet key validation, and capability toggles.
- Status payload fields for mode-aware UI identity: `mode`, `role_name`, `chip_id`, and `factory_serial`.

### Changed
- LoRa replay protection now tracks a per-sender boot/session nonce in addition to the monotonic counter, so a peer reboot no longer gets permanently dropped as replay traffic.
- `/api/settings` now uses copy-validate-commit semantics (parse into a temporary settings copy, validate/clamp, then commit), preventing rejected requests from leaving partially mutated runtime config in RAM.
- Config boot loading no longer forces a config file rewrite on every boot solely to increment `audit_boot_count`, reducing avoidable flash wear and eliminating false `config_migrated` logs during normal startup.
- Normal settings/export APIs no longer expose DS18B20 `sensor_temp_pin` / `sensor_temp_interval_s` fields that are not honored at runtime on this hardware (sensor pin is fixed and polling cadence is derived from heartbeat).
- `/api/network/test` is now commissioning-only (setup flow) and uses a shorter bounded connect-test window to reduce control-loop stalls and prevent accidental production use.
- `/api/fleet` now sizes its JSON document dynamically from current peer count (bounded), reducing transient heap spikes when only a small number of remotes are tracked.
- Automations builder now surfaces v1 limits in the UI and maps backend validation errors to clearer installer-facing messages before/after save.
- WiFi STA reconnect now defers active reconnect/scan attempts when free heap or max free block is below safety thresholds, reducing low-memory reconnect pressure that can destabilize ESP8266 under heavy UI/API load.
- Setup flow now supports full commissioning on first login instead of Fleet-key-only setup.
- Main web console header now shows device identity (serial/chip) with one-click copy and mode-aware role title.
- Default `input_control_paired_lora_enabled` is now `false` to avoid unintentionally blocking automation control on fresh configs.
- Documentation now reflects commissioning-first setup, canonical mode/role terms, and current control-authority precedence.
- Web UI heap-pressure pass: removed Diagnostics/Logs pages from the embedded UI shell, removed stale diagnostics/log JS handlers, slowed header/status-lite polling cadence, and increased status-lite cache TTL to reduce repeated JSON rebuild churn.
- Provisioning status endpoint now uses compact payload responses to avoid large dynamic JSON allocations during active provisioning and low-memory periods.
- Request logging now avoids per-request `String` allocations for path/IP, reducing allocator churn in high-frequency API polling paths.
- Bounded polling endpoints now use stack-backed JSON docs where safe (`/api/status-lite`, `/api/session`) to reduce transient heap pressure.

## [0.2.2-alpha] - 2026-02-21

### Added
- TX remote-node telemetry model with per-node runtime state (relay/input/temp, RSSI, last-seen, poll state, ACK state).
- New LoRa message types for remote command confirmation and polling: `MqttStatus` (`S`), `PollRequest` (`P`), and `PollResponse` (`R`).
- MQTT remote subtree publishing on TX under canonical paths: `<root>/lrs-<tx_chipid>/remote/0xNN/...`.
- MQTT remote control leaves on TX: `poll_interval_s`, `poll_now`, and `forget` (with retained-topic cleanup).
- New web console `Remotes` page for TX with live remote list, per-node details, and remote actions.
- New LoRa advanced controls in UI/config: MQTT remote retry timeout, TX scheduled polling, TX default poll interval, RX push-on-change, and RX push minimum interval.
- Complete implementation spec document: `docs/LRS_COMPLETE_IMPLEMENTATION_SPEC.json`.
- Time sharing: STA-connected nodes now fetch UTC from NTP and include `unix_time_s` in LoRa payloads for peers to sync local time.

### Changed
- Firmware version updated to `0.2.2-alpha`.
- TX now retries remote MQTT LoRa commands with bounded backoff until a configurable timeout instead of single-shot fire-and-forget.
- RX now returns explicit status for MQTT-triggered relay actions, allowing TX to report `pending`/`ok`/`timeout` per remote node.
- TX can run periodic remote polling; RX can optionally push unsolicited status on local input change with interval guardrails.
- Topic/address formatting is now normalized around `0xNN` representation for local and remote address topics.
- Config loading was hardened to preserve existing config files on JSON parse failure and recover admin password when possible.
- OTA admin-password changes now trigger a controlled reboot path so new OTA credentials are reliably applied.
- Documentation was expanded for protocol semantics, MQTT remote trees, polling, push-on-change, and operational defaults.
- Encrypted LoRa payload length increased from 8 to 12 bytes to carry shared UTC timestamp data (`unix_time_s`).

## [0.2.1-alpha] - 2026-02-15

### Added
- Standalone release flasher helper: `tools/flash_release.py` (reads chip ID, flashes binary, prints AP/admin password).
- Front-and-center no-VSCode/no-PlatformIO flashing guidance in `README.md` and `docs/USER_GUIDE.md`.

### Changed
- Firmware version updated to `0.2.1-alpha`.
- LoRa runtime is now disabled when using the default deployment key (`lora-default-passphrase`) to reduce accidental insecure operation.
- TX state machine now handles failed LoRa sends more safely (prevents stale pending/ACK wait states on failed transmit).
- Web console mobile UX was improved (better viewport behavior, larger form controls, password show/hide controls).
- Diagnostics and status presentation were improved for clearer field troubleshooting.
- Release tooling/sticker output alignment was improved for field flashing and credential recovery workflows.

## [0.2.0-alpha] - 2026-02-15

### Added
- Modular runtime architecture (`app`, `state_machine`, `radio_protocol`, `web_console`, `mqtt_bridge`, `sensor_manager`, `config_store`, `log_buffer`).
- DS18B20 support with configurable pin/interval.
- Extended encrypted payload telemetry fields for sensor data.
- Build metadata surfaced in API/UI/MQTT (`fw_version`, git SHA/branch, dirty state, build id/date).

### Changed
- Firmware version set to `0.2.0-alpha` in build configuration.
- AP behavior moved to role-independent SSID format `lrs-<chipid>`.
- MQTT behavior guarded for control-loop safety and adds `last_updated`.
- Status UI reorganized with clearer LoRa/WiFi/AP sections.

### Removed
- Unused legacy runtime remnant: `src/functions.h`.

### Notes
- See `/Users/warwick/Code/LoRa/lora_rs/docs/DELTA_2026-02-14.md` for a detailed migration summary from the previous docs baseline.

## [0.1.1] - 2024-06-13

### Changed
- Added encryption using AESLib. (tag `v0.1.1`)

## [0.1.0] - 2024-06-11

### Changed
- Major refactor with heartbeat, ACK, and timeout support. (tag `v0.1`)
