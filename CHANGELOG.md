# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows SemVer.

## [Unreleased]

### Added
- Firmware USB serial admin now has Phase 1A local-maintenance parity commands: `status`, authenticated `get_config`, authenticated `set_config`, and authenticated `factory_reset`, giving Flasher a non-HTTP path for inspecting health, editing core config, rebooting, and resetting a USB-connected device.
- Firmware now accepts authenticated USB serial admin `udp_log_control`, `remote_udp_log_control`, and `ota_pull` commands so local maintenance can enable temporary UDP log mirroring, ask a gateway to enable UDP logs on a WiFi-connected LoRa remote, and make a WiFi-connected device pull firmware from a Flasher-hosted URL without using the legacy REST OTA endpoint.
- MQTT admin now accepts local `udp_log_control` and `ota_pull` commands plus gateway-mediated `peer/<addr>/udp_log_control` commands for remote UDP log enable/disable on remotes that already have WiFi, moving network maintenance control onto the MQTT/LoRa admin path instead of the on-device Web UI.
- Flasher Flash mode now includes a Local admin panel for serial-admin status, basic device/WiFi/MQTT/sensor config editing, reboot, and guarded factory reset actions without opening the on-device Web UI.
- Firmware serial admin now exposes gateway-driven LoRa inventory commands (`start_lora_inventory`, `lora_inventory_status`, and `cancel_lora_inventory`) using bounded encrypted poll probes instead of HTTP discovery.
- Firmware LoRa inventory now uses a compact maintenance-status response so Fleet rows can show remote chip ID, firmware version, WiFi connected/IP, and MQTT state without HTTP/REST.
- Firmware LoRa inventory now includes remote uptime in the compact maintenance-status response so Flasher can distinguish expected OTA reboots from unexpected restarts.
- Flasher Fleet mode now has a dense LoRa inventory table driven by a selected USB gateway, with compact rows for address, role/mode, WiFi state, link RSSI/freshness, and OTA eligibility.
- Flasher Fleet mode now starts a temporary local firmware file server and shows operator-ready OTA pull details, including reachable URLs, SHA256, size, and the MQTT `ota_pull` payload to send to online devices.
- Flasher Fleet mode replaces the old Network tab with a Winbox-style fleet header and expanded device table; firmware serving and UDP logging are now status/per-device actions instead of separate primary panes.
- Flasher tabs are now ordered Flash, Provision, and Fleet; the app restores the last active tab on launch.

### Fixed
- Firmware now disables Soft AP as soon as STA WiFi connects and only brings Soft AP back after STA disconnect/failure when disconnected fallback is enabled.
- Flasher now keeps one per-port serial device state shared by Flash and Provision, so device details, serial-admin support/status/config, gateway WiFi state, and scanned WiFi networks stay consistent when switching tabs on the same selected USB port.
- Flasher/Firmware Fleet scan now refuses uncommissioned or factory-key gateways with a clear commissioning message instead of showing an active scan stuck at `0 probes sent`.
- Flasher Fleet scan now loads the selected gateway identity itself and forces a fresh gateway status after provisioning, so operators no longer need to visit Flash first or fight stale factory-default state after commissioning.
- Flasher Fleet scan now limits the default LRS gateway inventory sweep to the supported 12 remote addresses instead of wasting airtime probing the full `1..254` LoRa address range.
- Firmware EasyPair address allocation now assigns remotes from the supported `1..12` range without preserving stale/current remote addresses or runtime peer cache entries, so a fresh one-remote pairing starts at LoRa address `1`.
- Firmware EasyPair address allocation no longer treats a just-verified provisioned remote as a conflicting runtime peer, preventing a row assigned to address `1` from briefly flipping to `2` before the gateway target list is saved.
- Flasher Provision WiFi now adopts the gateway's serial-admin WiFi status when available, so a gateway that is already connected shows as connected without forcing another WiFi scan or `Connect Gateway` action.
- Flasher Provision WiFi now requires the USB gateway to confirm it is connected to the selected WiFi network before exposing `Send to Remotes`, avoiding a dead-end remote-send action when the gateway has not joined WiFi yet.
- Flasher Provision WiFi now times out failed gateway WiFi joins sooner, and changing the SSID/password cancels the current connection wait so the operator can immediately retry corrected credentials.
- Flasher Fleet rows now show uptime and highlight reboot events: expected reboots after a row Flash action are marked as progress, while unexpected uptime drops are treated as an alarm state.
- Flasher Fleet row Flash now requires confirmed WiFi/IP, automatically follows the selected remote with focused LoRa inventory probes, and expires stale `Flash sent` state into `No reboot seen` when no reboot is observed.
- Flasher serial monitoring now stays active across tab changes and only yields when another operation needs the same USB port, so a remote serial monitor can keep running while Fleet uses a separate gateway port.
- Flasher Flash local admin now avoids the stale Phase 1A firmware warning once status/config can load, and the Flash pane scrolls so expanded config controls remain reachable.
- Flasher Fleet mode no longer depends on device-side HTTP discovery, Web UI login, REST OTA upload, Web UI opening, or REST UDP-log enablement. The old LAN scan/update table was removed rather than kept as a compatibility shim.
- Flasher release CI dispatch is now tag-locked to prevent accidental `*-dev` release creation from `main` after post-release version auto-bump; `tools/release_flasher_assets.py dispatch-ci` now uses the target release tag as `--ref`, and `package_flasher.yml` blocks `create_release=true` when not running on a tag ref.

## [0.8.0-beta] - 2026-05-07

### Changed
- Web UI runtime policy is now maintenance-window based: HTTP handling, status SSE, and captive DNS start on boot, but the web console disables itself after 60 seconds without explicit user activity so steady-state firmware work is not paying Web UI overhead.
- Fleet provisioning now uses one consistent ESP8266 gateway cap of 12 remotes, matching the known-peer and paired-target limits instead of mixing 8-device discovery with 12-device fleet state.
- Firmware now exposes a small `LRS:`-prefixed USB serial admin protocol for Flasher-driven EasyPair foundations: identity, gateway configuration, LoRa discovery/provisioning status/actions, target-list finalization, Web UI maintenance re-enable, and reboot.
- Flasher now starts on a Pair Devices workflow, with the previous Serial and Network tools retained as USB Cable and Network maintenance modes.
- Pair Devices now generates Web UI-compatible readable fleet keys, supports show/copy controls, offers a quick fill for the derived gateway factory password, and moves manual provisioning steps behind an advanced section so the primary operator action is unambiguous.
- Pair Devices now includes WiFi setup: scan networks from the selected USB gateway, save gateway STA credentials, and send those credentials to paired remotes over LoRa using the existing fleet WiFi provisioning path.
- Firmware now supports an authenticated Identify LED command over USB serial admin and HTTP; Flasher exposes it on Pair Devices and USB Cable with a matching three-flash/pause/three-flash UI animation.
- EasyPair now keeps scan capacity separate from runtime relay targets: the gateway no longer stores speculative `1..N` paired addresses during prepare, and Flasher finalizes the gateway target list from verified provisioned devices only.
- Flasher serial actions now coordinate USB-port ownership across flashing, device-info reads, serial monitoring, and EasyPair commands; leaving USB Cable stops the serial monitor, the USB port selection and last-read device details are shared between USB Cable and Pair Devices, and the erase-before-flash and monitor-after-flash checkboxes remember their last state.
- Release automation now captures PlatformIO RAM/Flash usage for `lrs_za` and `lrs_us`, prints per-release deltas versus the previous release in run output, and appends historical metrics records under `docs/release_build_metrics.csv` + `docs/release_build_metrics.md`.

### Fixed
- Commissioning Wi-Fi password entry now suppresses password-manager generated-password prompts.
- Commissioning now refuses to save a Wi-Fi password without an SSID and reports whether Wi-Fi credentials were actually stored.
- Commissioning now sends its save response before applying Wi-Fi/network restarts so the captive browser does not misread a successful Wi-Fi save as failed.
- Commissioning Wi-Fi selection now keeps a separate selected-SSID state so mobile captive browsers cannot visually show an SSID while posting an empty one.
- Commissioning no longer redirects straight into the full Status UI after saving Wi-Fi credentials, avoiding heap pressure while STA association is in progress.
- Settings > Wi-Fi no longer allows saving a half-loaded form after a low-heap settings fetch failure, preventing accidental `wifi_admin_enabled=false` saves.
- Settings > Wi-Fi now pauses live status SSE and avoids automatic Wi-Fi scanning on page open to reduce ESP8266 heap pressure while loading the settings form.
- Wi-Fi scan and commissioning save/skip endpoints remain usable during first-use setup even when the captive browser has no active web-console session.
- Empty LAN hostname fields are populated with the computed `lrs-<chipid>` hostname so OTA/network identity matches the header.
- Wi-Fi admin shutdown now marks the Wi-Fi stack disabled after turning the interface off, avoiding repeated `wifi_admin_disabled` log spam.
- Flasher now hides Identify LED actions until the selected USB port has confirmed LRS serial-admin firmware support, preventing blank or unsupported boards from showing an unusable identify control.
- Flasher device-info reads now reuse the MAC address from the initial esptool chip probe when available, avoiding a second esptool call on the normal path.

## [0.7.0-beta] - 2026-04-27

### Changed
- Wi-Fi behavior and controls were expanded from `0.6.5-alpha` with practical range/stability tuning:
  - STA reconnect now scans for the configured SSID and reconnects with channel+BSSID instead of blind `WiFi.begin`.
  - Default PHY is `11b`, Wi-Fi sleep is off, and default TX power is region-capped (`20.5 dBm` ZA/EU, `19.37 dBm` US).
  - Settings > Wi-Fi now exposes advanced controls for TX power, PHY mode, sleep, static IP, channel lock, local Wi-Fi enable, and disconnected Soft AP fallback policy.
  - Default host identity is consistently `lrs-<chipid>` for Wi-Fi/OTA display and usage.
- TX now supports remote Wi-Fi enable/disable over LoRa, with Fleet/MQTT visibility of each remote device's confirmed Wi-Fi state.
- Fleet > Devices moved from passive discovery-only behavior to explicit master-list management (12-device cap):
  - Added `Add Device` by LoRa address (`1..254`).
  - Replaced runtime-only `Forget` UX with persistent `Delete` behavior for known peers.
  - `/api/fleet` now reports known-peer count/cap metadata so UI can show occupancy and known-vs-visible context.
  - Fleet Devices view was hardened against initial-load race conditions so table rendering remains stable.
- Web login flow is now safer than `0.6.5-alpha`:
  - Login form explicitly posts to `/api/login`.
  - Username/password query params are scrubbed from `/login` URL state to avoid lingering credential traces.
- Flasher monitoring UX was improved with clearer serial activity behavior and release-side esptool hygiene:
  - Activity monitor behavior and visibility were improved in the flasher UI.
  - Release packaging now fetches `esptool v5.2.0`.
  - Repo now stops tracking `tools/flasher/src-tauri/esptool-*` sidecar binaries directly (fetched at packaging/release time).

## [0.6.5-alpha] - 2026-04-26

### Added
- Flasher now has a Network mode for current-LAN LRS discovery, one-device-at-a-time HTTP OTA updates, direct Web UI opening, per-device admin password override, and UDP log monitoring on fixed port `5514` after OTA or on demand.

### Changed
- Flasher network discovery uses local adapter IPv4 netmasks to derive scan ranges and direct-IP HTTP probing only; it does not use mDNS or `lrs-*.local` hostnames.
- Flasher LAN scans now report progress, list devices as they are found, and accept older LRS login pages even when device identity metadata is unavailable, showing those devices as password-required instead of hiding them.
- Flasher Network mode now keeps per-host Web UI, password copy, OTA, and UDP actions inside each discovered-device row, with derived-password and build-detail hints shown inline and expanded UDP logs retaining copy/open/stop controls.
- Flasher now remembers successfully tested non-factory Network admin passwords per device and uses them ahead of derived factory passwords on future scans; Serial device details now label the derived value as `Factory password`.

## [0.6.4-alpha] - 2026-04-24

### Changed
- Paired TX input-control confirmation is now more consistent under packet loss: retry scheduling starts in sub-second intervals (instead of multi-second first retries), reducing cases where the local TX relay mirror waits several seconds for remote ACK confirmation.

## [0.6.3-alpha] - 2026-04-24

### Changed
- Addressing defaults now align with a gateway-at-the-top convention:
  - Fresh TX/gateway defaults start at local address `254` with first remote target `1`.
  - Provisioned RX/controller expectations remain low-to-high (`1..32` in current fleet provisioning auto-assignment).
- Addressing now follows a single canonical source (`remote_address`) across runtime and web APIs; role-specific address arrays are synchronized from that canonical value so Settings and Status stay consistent after saves.
- LoRa frequency is now firmware-region locked end-to-end:
  - `REGION_US` builds force `915 MHz` and prevent user selection.
  - `REGION_ZA` builds force `433 MHz` and prevent user selection.
- Flasher region handling is more resilient in live workflows:
  - Region auto-detection and device-details ordering are improved for clearer operator context.
  - Local firmware file selection now remains available when cloud release metadata is unavailable (offline-safe flashing path).

## [0.6.2-alpha] - 2026-04-21

### Changed
- Flasher/esptool command selection is now runtime-compatible across bundled esptool variants: the desktop flasher probes `esptool -h` and automatically uses either hyphen (`erase-flash`/`write-flash`) or underscore (`erase_flash`/`write_flash`) operation names as supported by the sidecar binary.
- Release automation now has a stricter asset-contract verifier in `tools/release_manager.py` that can validate both repositories contain the full expected firmware + flasher binary set and report explicit missing filenames instead of silently completing.
- Completed `v0.6.1-alpha` release asset set in both repos by adding the missing macOS DMGs (`macos-arm64` and `macos-x86_64`) so both release pages now carry the same full binary lineup.

## [0.6.1-alpha] - 2026-04-21

### Changed
- MQTT reconnect behavior now uses bounded Fibonacci backoff (up to 300 seconds), with reconnect-failure log telemetry carrying the active retry delay.
- Runtime cleanup from the mDNS removal pass now also removes MDNS logging category/classification and drops `lan_mdns` from MQTT discovery payloads.
- Config defaults were aligned for current hardware/field behavior:
  - `mqtt_host` remains `venus.local` (explicitly restored)
  - DS18B20 sensor support defaults to opt-in (`sensor_temp_enabled=false`).
- Flasher macOS startup now offers a one-click move-to-`/Applications` flow for non-debug builds, then relaunches the installed app.
- OTA host defaults in `platformio.ini` were updated away from `lrs.local` to direct `192.168.4.1` for the US OTA environment, and stale mDNS build-flag remnants were removed.
- Release tooling now auto-advances `VERSION` to next patch `-dev` after successful publish to both repos, commits the bump, and pushes to `main` by default.
- Release/process docs were updated to reflect the auto post-release version bump and current ad-hoc local build versioning expectations.

## [0.6.0] - 2026-04-18

### Changed
- Active Fleet > Manage > LoRa provisioning now uses a single dedicated provisioning-status update path while discovery/provisioning is live, keeps footer memory/FW stats updating from that payload, and avoids the generic status SSE churn that could leave long runs looking stale near completion.
- Provisioning throughput is higher in this pre-release firmware: the coordinator now sends bounded bursts of setup/apply frames for the current node instead of effectively one provisioning frame per tick, and verify timing is shorter so 8-device batches complete materially faster without changing the provisioning packet format.
- Build tooling now regenerates the embedded web console more reliably during PlatformIO builds, and `tools/build_web_console.py` can also be run directly as a manual fallback before compiling in VS Code.
- LoRa provisioning UX is being simplified around a clearer mobile-first scan flow: scans now default to the full 8-device batch size, completion states read explicitly as scan/provision outcomes, and zero-result scans now direct operators to check power and factory mode instead of implying the coordinator is still waiting.
- Sensors UI now makes DS18B20 intent clearer by treating it as an opt-in feature for fitted hardware rather than something boards are assumed to use by default.
- Provisioning and fleet operations are now substantially more reliable in small-batch bench and field workflows, with better discovery timing, sticky addressing, clearer operator feedback, and fewer false failures.
- Fleet state now survives reboot more usefully: known peers are persisted, fleet landing defaults are smarter, and device data now surfaces direct `Web UI` links when a remote URL is known.
- Fleet WiFi provisioning now supports targeted sends as well as broadcast, and the UI/API path now preserves stored credentials correctly when no explicit override is posted.
- Fleet scan and discovery behavior is now less collision-prone and more predictable: same-fleet-key scans are less restrictive, discovery uses a two-phase coordinator/reply window, and targets spread announce frames more safely across the reply window.
- Provisioning sessions are now operator-driven single-pass runs with sticky chip-ID rows, more realistic estimate defaults, clearer table labels/progress text, and fewer confusing low-memory states.
- Provisioning discovery now quiesces cleanly once it reaches `Ready`: scan-only runtime behavior stops after completion, the coordinator releases `prov_active`/normal-TX pause for that terminal scan state, provisioning live updates no longer keep treating `Ready` as an active scan, and Fleet > Manage can return to a passive terminal state without losing discovered rows.
- The Fleet > Manage > LoRa scan view now keeps its countdown moving locally between server updates, forces a final refresh when the scan window expires, and avoids the heavier fleet SSE stream once that page is idle so terminal scan results feel less stuck under low heap.
- The dedicated "Console Unavailable (Low Memory)" interstitial has been removed so `/` always attempts to serve the real console; low-heap conditions are still logged, but operators are no longer blocked behind a separate recovery page.
- Fleet > Manage > LoRa has been simplified again: the scan UI no longer shows a countdown/elapsed timer, scan progress uses plain in-progress/complete states, and Scan Again keeps previously found rows while adding any newly discovered devices.
- Provisioning scan-start failures now report clearer causes in the web UI (timeout, connection failure, low memory, or device rejection) and emit a dedicated API rejection log entry with heap context when the device refuses a scan start.
- Post-login web console startup is lighter again: the embedded UI builder now strips indentation and standalone JS comments before embedding, status bootstrap no longer blocks on the first lite fetch before live updates can start, and the ESP8266 streams the large inline console page in larger PROGMEM chunks to reduce slow first paint after login.
- Stopping a discovery run after devices have replied now preserves the discovered batch and promotes the session to `Ready` instead of clearing the list, so operators can stop early and move straight to `Provision`.
- Provisioning discovery/provisioning phase transitions no longer stall behind the per-tick radio TX budget gate, so scan deadlines can still expire and move to `Ready` even if no further radio send is allowed in that tick.
- Paired TX link health no longer stays stuck in `timeout` after valid heartbeat/state-sync ACKs from the RX: unsolicited paired ACKs now clear stale timeout state as proof of live comms, while command-confirmation ACK matching remains strict for actual control semantics.
- mDNS has been removed completely from the firmware, embedded web UI, and documentation; the console now relies on direct IP access, status/discovery payloads no longer publish `.local` fields, and the flasher shortcut opens the STA IP when one is seen in monitor logs or falls back to the Soft AP IP.
- Fleet > Manage > LoRa no longer traps discovered/provisioning rows inside a height-capped internal scroller; the device table now expands naturally so the page scrolls instead of hiding up to 8 devices in a nested pane.
- Provisioning address assignment is now constrained to `1..32`, reserved across discovery runs on the same coordinator uptime, and more tolerant of devices that apply successfully but cannot be re-confirmed immediately (`applied_unconfirmed`).
- Provisioning start no longer fails immediately when radio TX budget is briefly unavailable; discovery starts are now queued from coordinator tick for better first-pass reliability.
- Runtime/web cleanup removed dead root artifacts (`web_console_ui_assets_orig.cpp`, `.new`), deduplicated shared role/WiFi helpers into `runtime_utils`, renamed `defaultLanHostnameForRole(bool)` to `defaultLanHostname()`, and reduced transient `String` churn in status/config identity formatting.
- Windows flasher packaging is now simplified to `MSI + portable ZIP`; the NSIS setup EXE is no longer part of the release workflow or asset contract.
- Linux flasher packaging metadata was tightened so the app identifies cleanly as `Thanda LoRa Flasher` / `thanda-lora-flasher` with AppStream metadata for software-center presentation.
- Linux release assets now ship `.AppImage.tar.gz` only; raw `.AppImage` upload has been removed from the release workflow.
- The web login page now shows a chip-ID slug badge for faster device identification before sign-in.
- `devmon_web` now distinguishes `no IP` vs `serial preferred`, pauses active IP checks while serial is attached, and uses clearer icon-based IP/serial status indicators in both device tabs and cards.

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
