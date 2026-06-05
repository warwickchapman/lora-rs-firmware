# Changelog

All notable changes to this pre-release project are documented here in current operator-facing terms.

## [Unreleased]

### Firmware Features
- Raw ESP uptime (`uptime_ms`) is now exclusively populated from raw millisecond telemetry (`kMaintenancePageVersion`) and is no longer assigned from coarse debug minutes telemetry.
- Gateway cached peer uptime is cleared immediately on standard non-uptime telemetry status packets (Heartbeats, PollResponses, ACKs, MqttStatus) to prevent stale uptime carry-forward.
- Maintenance telemetry page bursts are protected from clearing fresh uptime by enforcing a 5-second grace period before clearing.

### Firmware Fixes
- Filter out invalid source addresses (`0`, `255`, and the local address) early in receive handling to prevent remote peer cache pollution.
- Restructured configuration validation: Remote/Receiver nodes can now set and save `mqtt_control_enabled = true` without triggering config validation resets on boot. This allows receiver nodes to successfully authorize LoRa-bridged MQTT commands.

### MQTT Improvements
- Gateway now publishes an empty string `""` to clear retained `peers/<addr>/uptime_ms` topics when peer uptime is cleared or absent.

### Flasher UI Improvements
- Added platform-aware keyboard shortcuts (CMD + 1..5 on macOS, CTRL + 1..5 on Windows/Linux) to switch between Flash, Provision, Fleet, Monitor, and Settings views, complete with editable-target input guards and navigation hover tooltip hints.
- Exposed MQTT control inputs ("Accept gateway MQTT commands" and "Controllers" list) for Remote/Receiver units in the settings tab, clarifying that remote nodes must authorize the Gateway's local LoRa address.
- Added a warning banner and inline warning status on the Gateway MQTT settings tab if paired input-control is enabled, explaining that physical input overrides block MQTT relay commands to prevent conflicting state loops.
- Simplified operator views in Fleet and Monitor: displays a single unified `Relay` and `Input` state, removing raw feedback columns and confusing `ack X · fb Y` details.
- Gateway Relay card simplified to show unified Relay and Input columns in a clean 2-column layout.
- Prevented the Flasher from caching and re-hydrating old `uptime_ms` values from history into the active row display.

### Flasher UI Fixes
- Scope gateway session key mismatch invalidation to the active Fleet gateway, clearing both visible rows and hidden history on gateway session changes, port changes, and local factory resets.
- Fixed a provisioning issue where resuming or re-running commissioning on the same gateway failed with an error about the commissioned gateway fleet key not being fetched, by updating the fleet key source to `gateway` immediately after a successful `configure_gateway` command.
- Local USB factory reset now clears the fleet key by default so reset remotes can be rediscovered by EasyPair.
- Standardized default LoRa Spreading Factor to SF7 to match clean factory/erased firmware nodes, enabling direct out-of-the-box discovery without manual settings adjustments.

### Script & Tooling Fixes
- Fixed host-side ESP8266 chip ID parsing (in Flasher backend and Python tools: `factory_provision.py`, `flash_release.py`, `post_upload_sticker.py`, `derive_passwords.py`) to mask the parsed value to the lower 24 bits (matching on-device `ESP.getChipId()`). This ensures correct derived credentials, SSID names, and serial numbers for units with the highest byte set in their hardware chip ID.
- Standardized the factory provisioning script (`factory_provision.py`) to output 8-character lowercase passwords, aligning credential generation across stickers, CSV entries, Flasher UI, and on-device defaults.


## [0.9.2-beta] - 2026-06-01

### Firmware Features
- Added working remote sensor support for dry-contact input state, DS18B20 temperature, and KIT0139-style 4-20 mA tank level telemetry. Sensor values now flow from remotes into the gateway peer cache, Flasher Monitor/Fleet, and MQTT peer topics.
- Added PowerSave as a clear two-state remote mode: `Full Power` or `PowerSave`. PowerSave disables WiFi, Serial Admin, OTA, MQTT, LEDs, and background services until the remote is woken by LoRa command.
- Added `power_save_listen_only` for ultra-lean remote nodes, disabling WiFi, background sensor polling, MQTT, status LEDs, and Serial Admin immediately on boot or remote command.
- Added gateway-mediated remote commands over LoRa, including remote identify, reboot, guarded factory reset, sensor configuration, WiFi provisioning, OTA pull, and targeted Fleet Key rollover.
- Added secure same-key encrypted LoRa Fleet Key rollover (`MessageType::FleetKeyControl`) so operators can update remote Fleet Keys without physical access.
- Added persistent address-to-chip ID pairing so gateways remember which physical device owns each LoRa address across gateway restarts, Flasher reconnects, and factory-reset recovery.
- Added versioned development build reporting such as `0.9.2~122`, including compact LoRa maintenance encoding so Fleet, Monitor, provisioning, and discovery can show full beta/dev build identity.

### Firmware Fixes
- Reworked remote OTA around a stateful flow for reliability. Firmware suppresses LoRa chatter during the full OTA pull, paces gateway-mediated control frames over multiple ticks, and rejects duplicate OTA triggers while a pull is active.
- OTA pull now verifies the exact HTTP stream it flashes, and gateway-mediated OTA sends the expected SHA256 over encrypted LoRa before the remote downloads firmware.
- Improved input-based relay control so fleet relay actuation happens together. The gateway sends one actuating broadcast/group `Change`, waits for slotted ACKs, and then uses non-actuating correlated `PollRequest` confirmations for missing ACKs instead of late per-node targeted `Change` retries.
- Fixed paired input-control heartbeat to use the same multi-target group path as input changes, preventing only the legacy primary `remote_address` from being kept in sync.
- Fixed recovered receivers accepting gateway input-control packets from LoRa address `254` when fleet key matches, even if stale controller-pairing metadata remains after provisioning or OTA recovery.
- Fixed gateway peer-cache truth handling: relay state updates from valid ACKs or real remote status, while dry-contact input state is marked known only from actual remote telemetry or sensor pages.
- Fixed ACK handling so ACKs no longer mark remote dry-contact input as verified or copy the gateway's desired input state into Fleet sensor rows.
- Fixed provisioning address exhaustion and duplicate stale rows by treating chip ID as physical identity, reclaiming old addresses for factory-reset devices, and collapsing stale rows when a chip reports from a new LoRa address.
- Fixed gateway target-list preservation. New provisioning batches merge target addresses by default instead of destructively replacing the existing fleet list.
- Fixed receiver provisioning/config repair so paired-input gateway control is disabled on remotes without resetting the receiver back to address `1`.
- Fixed remote maintenance scan collisions by destination-checking `MaintenanceRequest` replies and returning to compact one-response-per-probe inventory behavior.
- Fixed remote uptime reporting by moving it onto the normal maintenance version page instead of relying on optional debug telemetry.
- Fixed sensor telemetry reporting by restoring cascading maintenance pages for sensor/debug data, including DS18B20 temperature and 4-20 mA tank readings.
- Fixed maintenance snapshot timeouts by omitting unknown/default row fields and accepting larger bounded serial-admin responses for 12-row fleet caches.
- Fixed ESP8266 heap pressure by replacing long-lived heap-backed settings strings with fixed inline buffers and removing several transient `String` helpers from WiFi, hostname, DS18B20, MQTT, settings, and serial-admin paths.

### Firmware Improvements
- Improved MQTT peer topics with LWT availability, `peers/` naming, decimal addressing, compound chip/address paths, `uptime_ms`, `last_seen_age_s`, timeout blanking, and publish-cache sizing tied to `LRS_MAX_PEERS`.
- Improved local and remote maintenance direction: Flasher over USB serial admin is now the primary local admin surface, while remote maintenance uses MQTT admin for online devices and gateway-mediated LoRa admin for bounded remote actions.

### Flasher Features
- Added bulk flasher foundations and UX: independent USB port handling, per-port serial job queues, recycled-port status cleanup, weighted erase/write/verify progress, decimal esptool percentage parsing, and automatic removal of unplugged devices from the bulk status list.
- Added explicit `Forget Device` action for removing outdated or replaced remotes from the Fleet list.
- Added a `Relay` column to Fleet so each remote shows `On`, `Off`, or `waiting` from the gateway peer cache.
- Introduced a consolidated Fleet Actions menu and tabbed Settings modal for WiFi provisioning, sensor configuration, Fleet Key changes, logs, reboot, flash/OTA, and factory reset actions.
- Added a sticky Flasher Settings footer with always-visible `Save config` and `Reboot` actions.
- Added a Settings diagnostic action to copy the currently fetched device configuration as JSON for field recovery and paired-target inspection.
- Added Fleet Key visibility/editing in Flasher with hide/show control, remote change warnings, and explicit safety confirmation.

### Flasher Fixes
- Fixed OTA UI recovery cases where rows could remain stuck on `Downloading OTA...`, lose the queued label too early, revert to `Waiting for reboot`, or fail to advance to the next queued device after retry/update state changed.
- Fixed Flasher serial-port races by separating selected USB ports for Flash, Provision, Fleet, Monitor, and Settings, and by queueing serial jobs per port.
- Fixed Flasher Settings partial-load cases by normalizing serial-admin responses before binding fields and writing accepted settings/status back into the active settings cache.
- Fixed packaged Flasher release fetching when the repo-local `.pio/build/lrs_za/firmware.bin` does not exist, preserving release downloads and the manual file picker.
- Fixed Flasher firmware-version display so it shows exactly what the device reports rather than inferring suffixes from the Flasher app version.
- Fixed gateway/Fleet startup races by retrying gateway reads, avoiding duplicate gateway loads, and refreshing device identity after flash/reset/provisioning changes.

### Flasher Improvements
- Improved Fleet and Monitor to use the gateway peer cache as the primary view, preserve cached rows across tab changes, refresh automatically, and rate-limit explicit LoRa force scans.
- Improved Fleet rows for unseen remotes by showing clean `-` placeholders instead of misleading `0s`, `live`, `waiting`, or default sensor values.
- Improved Fleet table readability: renamed `LoRa` to `Addr`, shortened WiFi `Connected` to `OK`, dynamically hides the IP column when no active remote reports an IP, and colorizes dry-contact `Closed`/`Open` status words.
- Added helpful Uptime tooltips for `Unexpected reboot`, `Rebooted (OTA)`, and `No reboot seen (OTA)` states.
- Added smart PowerSave telemetry in Flasher: WiFi transitions through a pending offline state when PowerSave turns on, and IP address clears to `-` when the device reports offline.
- Improved operator notifications so Flasher says whether power configuration or sensor configuration is being sent to a remote.
- Improved provisioning flow: gateway fleet key is treated as authoritative, commissioned gateway re-keying is rejected, advanced Prepare/Scan loads the selected gateway automatically, and scan wording separates factory/unprovisioned count from total fleet size.
- Improved EasyPair and dense factory scans with preserved target lists, reserved known addresses, and wider reply timing to reduce missed factory remotes.

### Build And Release Improvements
- Added native PlatformIO tests for pure firmware helpers (`FixedSettingString`, mode/role parsing) and Python tests for build-version parsing. These are now part of the release validation gate.
- Added release automation cleanup for old GitHub Actions runs, keeping the latest 10 `package_flasher.yml` runs by default.
- Improved release/build hygiene: firmware and Flasher metadata are aligned to `0.9.2-beta`, flasher manifests sync from `VERSION`, and build documentation emphasizes `lrs_za`, `lrs_us`, and native tests.

### Removed
- Removed the device-hosted operator UI/admin services from normal firmware.
- Removed stale device-admin helper scripts, old contract/spec documents, orphaned Web UI extraction code, and local DevMon helper scripts.
- Removed dormant onboard automation-rule runtime code until automation returns with a supported Flasher and serial-admin management surface.
- Removed pseudo-`mesh` aliases and unsupported settings audit fields because they implied routing and observability behavior that the firmware does not provide.
