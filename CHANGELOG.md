# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows SemVer.

## [Unreleased]

### Changed
- Flasher Windows packaging policy simplified to `MSI + portable ZIP` (NSIS setup EXE removed from release workflow and asset contract).
- Fleet LoRa provisioning UI polish: clearer scan table labels (`Cur Addr`, `New Addr`, `FW Ver`), improved table presentation, inline disabled reason for `Provision All`, and compact session progress text with elapsed time.
- LoRa discovery now runs as a two-phase cycle: short coordinator `DiscoverStart` broadcast burst followed by a silent randomized reply window on targets, reducing coordinator-talk collisions during announce collection.
- Provisioning discovery flow was simplified by removing automatic retry (`DiscoveryRetry` / `retry_once` / “Search more” path); scans are now operator-driven single pass (`Start Discovery`).
- Provisioning device rows are now sticky by chip ID during discovery/readiness updates and are only cleared when provisioning starts or the session is cancelled.
- Provisioning discovery defaults are now tuned for small bench batches: default estimated device count is `2` in UI/API start flow.
- Provisioning discovery timing is now less pessimistic for high estimates: shorter per-device reply scaling, capped max reply window, and early completion once expected count is reached and settle time has elapsed.
- Provisioning verify is now more resilient: coordinator probes the newly assigned address on fleet key before downgrade, and devices that applied but could not confirm are labeled `applied_unconfirmed` instead of hard-failed.
- Provisioning auto-assignment is now constrained to address range `1..32`.
- Provisioning target reliability improvement: each factory-key device now sends two discovery announce frames per `DiscoverStart` (second announce uses short jitter), while coordinator dedupe remains chip-ID-based.
- Provisioning address assignment is now sticky across separate discovery runs on the same coordinator uptime using a fixed-size chip/address registry (12 entries): already-provisioned addresses are reserved, known chips prefer their prior address, and full remote factory reset clears registry entries for that address.
- Provisioning start API no longer hard-fails when immediate radio TX budget is unavailable; discovery broadcasts are now queued from the coordinator tick, reducing intermittent `Discovery start failed: request_failed` starts.
- Provisioning discovery resilience tuned for first-pass reliability: coordinator `DiscoverStart` burst count increased to `2`, and discovery no longer exits early on estimated-count settle before the reply window closes.
- Provisioning target announce timing now spreads both announce frames across the full reply window using deterministic chip-ID slot offsets plus bounded jitter, reducing repeated cross-device announce collisions.
- Provisioning session summary text now avoids misleading estimate-denominator phrasing (`x/y`), and reports `Found N (estimated E)` with `Verified V / Found N` progress.
- Fleet Devices scan defaults were re-tuned for operational range management: default scan range is now `1..32` (operator-adjustable up to `254`).
- Fleet landing behavior now defaults to `Manage` when no known fleet devices exist, and defaults to `Devices` when known devices are present.
- Fleet scan receive filtering on RX was relaxed for `PollRequest`: any same-fleet-key transmitter can scan/discover devices (not only the configured paired source address).
- Fleet Devices table/detail now include a `Web UI` link when a device URL is known (`web_ui_url` present in fleet data).

## [0.5.9-alpha] - 2026-03-05

### Changed
- Flasher serial-port auto-selection now uses recency tracking and only auto-switches when no active operation is running (flash, monitor, or device info), reducing accidental port flips during live work.
- Flasher fallback handling was tightened when a selected port disappears so reconnect/reset events recover more predictably without interrupting active workflows.
- Windows UI dropdown readability was improved by enforcing high-contrast option styling for native select popups.
- Windows portable package now includes an `esptool.exe` alias alongside the target-specific sidecar binary name, fixing portable runs that expect the plain executable filename.

## [0.5.8-alpha] - 2026-03-05

### Changed
- LoRa paired ACK handling now enforces explicit command correlation: RX includes the acknowledged command counter in ACK payload (`b8..b11`), and TX only clears pending state on counter match.
- TX paired input-control retries are now bounded by `tx_command_retry_timeout_ms` (default `180000` ms) and use light retry jitter to reduce synchronization collisions under repeated loss.
- Added RX fail-safe policy settings with default-safe behavior preserved:
  - `rx_failsafe_mode` (`hold_last` default; optional `force_off` / `force_on`)
  - `rx_failsafe_timeout_ms` (`180000` ms default)
- Settings persistence/API now include:
  - `tx_command_retry_timeout_ms`
  - `rx_failsafe_mode`
  - `rx_failsafe_timeout_ms`
- Flasher release packaging now includes a Windows portable ZIP (`thanda-lora-flasher-<version>-windows-x64-portable.zip`) alongside MSI and setup EXE.

## [0.4.3-alpha] - 2026-03-01

### Added
- **UI Version Identity**: Displayed version number (`v0.4.3-alpha`) in the application subtitle.
- **UI Section Renaming**: Renamed "Interface Controls" to "Device Configuration" and bottom section to "Device Details" for improved UX.

## [0.4.2-alpha] - 2026-03-01
- **Documentation Alignment Pass**: Refreshed provisioning/automation/role references across manuals, including canonical `standalone|paired|mesh` roles and `input_control_paired_lora_enabled` naming.
- **Automation Execution**: Rules now execute only when `execution_mode` is `standalone`.

## [0.4.1-alpha] - 2026-03-01

### Added
- **Thanda Branding**: Integrated official "Thanda" logo and lightning bolt iconography across UI, icons, and bundles.
- **Multi-Architecture macOS Support**: Automated CI builds for both Intel (x86_64) and Apple Silicon (arm64) using dedicated DMGs.
- **Linux Portability**: Bundled `libmpv` directly into the Linux binary for out-of-the-box compatibility on Ubuntu 25.10+ and others.
- **Linux Permissions Guard**: Added a startup check for `dialout` group membership with a guided "Copy Command" fix UI.
- **Desktop Flasher UI Polish**: Removed redundant headers, implemented glassmorphism aesthetics, and enabled maximized startup.

### Fixed
- **PyInstaller Recursion Loop**: Resolved a critical `RecursionError` on Linux by delaying `esptool` imports.
- **Environment Hardening**: Updated `run.sh` to automatically detect and repair broken virtual environments.
- **Logo Transparency**: Fixed "fake transparency" artifacts by converting background colors to true alpha channels.

### Security
- **Source Code Protection**: Modified CI/CD to prevent proprietary password generation logic from syncing to public repositories.

## [0.4.0-alpha] - 2026-02-28
- Integrated initial Thanda branding and Desktop Flasher v1.0 features.

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
- Documentation alignment pass: refreshed provisioning/automation/role references across operator/developer manuals, including canonical `standalone|paired|mesh` roles, current provisioning APIs/CLI, and `input_control_paired_lora_enabled` naming.
- Web UI heap-pressure pass: removed Diagnostics/Logs pages from the embedded UI shell, removed stale diagnostics/log JS handlers, slowed header/status-lite polling cadence, and increased status-lite cache TTL to reduce repeated JSON rebuild churn.
- Provisioning status endpoint now uses compact payload responses to avoid large dynamic JSON allocations during active provisioning and low-memory periods.
- Request logging now avoids per-request `String` allocations for path/IP, reducing allocator churn in high-frequency API polling paths.
- Bounded polling endpoints now use stack-backed JSON docs where safe (`/api/status-lite`, `/api/session`) to reduce transient heap pressure.
- Heap fragmentation stabilization tranche (M1-M6):
  - M1: added unified heap-probe telemetry across hot endpoints (`/api/status-lite`, `/api/status-static`, `/api/fleet`, `/api/provisioning/status`, `/api/settings` GET/POST) with threshold/large-delta/periodic log gating.
  - M2: added single-flight guards for status cache builds and stale-cache fallback on build failure; tightened fleet cache sizing bounds.
  - M3: provisioning status now defaults to compact payloads during active sessions and low-heap periods, with explicit `session.compact_reason` metadata; provisioning JSON responses were further streamed to reduce temporary allocation churn.
  - M4: applied JSON deserialization filters across remaining admin/config/provisioning POST handlers to prevent allocation from unknown fields.
  - M5: hardened config load/save with early non-object JSON rejection, pre-serialize size bounds, and atomic temp-write+rename save path.
  - M6: documented endpoint measurement scope, soak validation strategy, and acceptance criteria in `docs/internal/memory-budget-and-measurement.md`.
- Replay table capacity is now configurable via compile-time policy (`LRS_REPLAY_TRACKED_SOURCES`) and defaults to `16` (down from `32`) to reduce static RAM while preserving headroom over the `8`-peer runtime cap.
- Added startup memory-policy logging to report effective caps (`max_peers`, `replay_sources`, `max_prov_devices`) for field validation.

### Notes
- Memory checkpoints during this tranche (`python3 -m platformio run -e lrs_za`):
  - M1: RAM `54584 / 81920`, Flash `718967 / 1044464`
  - M2: RAM `54600 / 81920`, Flash `719143 / 1044464`
  - M3: RAM `54648 / 81920`, Flash `718447 / 1044464`
  - M4: RAM `54648 / 81920`, Flash `718431 / 1044464`
  - M5: RAM `54968 / 81920`, Flash `719071 / 1044464`

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
