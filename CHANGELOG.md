# Changelog

All notable changes to this pre-release project are documented here in current operator-facing terms.

## [Unreleased]

### Flasher Features
- Monotonically serializes MQTT admin commands per gateway session using a Promise chain to prevent out-of-order execution and replay rejections.
- Implemented automatic MQTT session establishment handshake (`admin_challenge`) and automatic one-time retry handling when sessions are invalid or expired.
- Stripped Unix time `ts` and `ttl_ms` parameters from MQTT command payloads.
- Renamed the Provision connection option from "Remote MQTT Broker" to "MQTT".
- Renamed "Forget device" context menu button to "Remove from Gateway", updated confirmation dialogs, and renamed "Discovered Candidates" header to "Same-Key Adoption Candidates".
- Added `addr` and `remote_addr` fields to the Tauri Rust `MqttGatewayPayload` struct to ensure MQTT discovery details are correctly matching and parsed in the UI.
- Extracts `chip_id` from the canonical peer topic segment format (e.g. `02_lrs-0048d1bb`) in the Tauri MQTT backend and emits it to the frontend.
- Matches telemetry updates to seeded inventory rows using both `address` and `chip_id` (normalizing identities), surfacing address collisions as UI conflict warnings and blocking mismatched data merges.
- Filtered incoming telemetry in MQTT mode to prevent stale or retained MQTT messages from automatically generating non-existent remote rows in the configured devices list.
- Configured the frontend inventory list to immediately remove rows upon successful unpair command executions.
- Updated the targeted factory reset modal to clarify options between full decommissioning vs keeping the device in the gateway's secure fleet (renamed "Keep Fleet Key" to "Reset but keep in fleet").
- Suppressed the fleet key configuration warning if the fleet key is already populated in the form during MQTT gateway load.
- Improved local broker UX: selecting "Local MQTT Broker" now automatically starts the broker (if not already running) and connects Flasher's MQTT client.
- Redesigned "Configure Session Connection" as a troubleshooting and details-only panel with a "Retry Start/Connect" option on failure.
- Renamed and enhanced "Copy Gateway MQTT Settings" to copy gateway-compatible settings with clear usage instructions.
- Unified MQTT, Local Broker, and USB Serial transports into a session-wide **Gateway Session Connection** banner.
- Integrated an embedded `rumqttd = "0.20"` MQTT broker inside the Tauri backend.
- Added support for starting the local broker once per Flasher session on a user-specified port (defaulting to `1883`), including validation to prevent port conflicts.
- Refactored Monitor, Settings, and Fleet tabs to consume the shared Gateway Session Connection state.
- Renamed "Scan count" parameter to "Max remotes" in EasyPair/provisioning UI and command inputs.
- Adjusted empty list placeholder text and main Provision tab descriptions to adapt dynamically to serial vs MQTT transport mode.
- Increased EasyPair UI state-machine wait safety timeout from 130s to 150s.
- Exposed copyable gateway MQTT settings strings and LAN IP listing for easier manual client setup.


### Firmware Features
- Aligned raw ESP chip ID mapping to a canonical 24-bit identity across serial and MQTT bridges, publishing and subscribing to topics under the `00xxxxxx` namespace.
- Pruned all telemetry metrics from the `lora_inventory_status` response payload when requested over MQTT to ensure command responses contain only the authoritative seed list (`address` + `chip_id`) and fit comfortably within the 1024-byte MQTT packet client buffer ceiling. In addition, capped same-key discovery candidates to at most 4 entries, pruned candidate timestamps (`last_seen_ms`, `age_ms`), and exposed `candidate_total` and `candidate_truncated` to avoid silent UI blind spots.
- Optimized the `provisioning_status` response payload over MQTT using a highly compact array-based schema for devices (`[["chip_id", address, rssi, "state"], ...]`) and removing verbose debug logs to ensure 12-device status reports stay safely below the 1024-byte packet limit.
- Added validation for MQTT command response publishes, logging failures as warnings featuring the command name and payload size.
- Configured Flasher to normalize compact array-based MQTT and verbose object-based Serial payloads, and rate-limited MQTT polling error output to at most once per 12 seconds.
- Cleaned up old stale raw-ID MQTT discovery topics by publishing retained null payloads on connection.
- Replaced Unix time/NTP synchronization validation for MQTT admin commands with a lightweight challenge-response session mechanism.
- Added `admin_challenge` command to issue 5-minute sessions (tracked via local `millis()`), resetting sequence count.
- Enforced strict sequence monotonicity (`seq > last_seq`) per gateway session to provide replay protection without external clocks.
- Retired the circular duplicate request ID cache for MQTT command deduplication.
- Enhanced `clearPeerRetainedTopics()` to clear both canonical address+chip and legacy address-only topic namespaces on the MQTT broker on unpair.
- Added missing properties to the retained topic cleanup list (`fw_version`, `ip`, `power_save_listen_only`, `power_save_active`, and `chip_id`).
- Loop clear all instances (0 to 5) for all supported sensor domains (`input`, `temperature`, and `tank_level`) under `/sensor/<kind>/<instance>/value` and `/sensor/<kind>/<instance>/state`.
- Transitioned provisioning coordinator discovery to maximum capacity ("Max remotes") listen semantics instead of treating target count as an exact requirement.
- Implemented a bounded deterministic discovery timing model defined as `10s + 2s * max_remotes` (capped at 45s maximum), preventing hangs or timeouts when fewer devices are powered.
- Renamed the API parameter `expected_remotes` (or `expected_count`) to `max_remotes` across the admin executor dispatcher, executor status payloads, and state machine internals.
- Added `SensorState::Waiting` (`"waiting"`) state to represent sensors that are detected but have not completed their first reading yet.
- Removed `wifi_phy_mode` configuration setting and user-configurable PHY mode handling, defaulting entirely to the ESP8266 SDK's automatic mixed-mode (802.11n/b/g/n) negotiation for improved compatibility with modern enterprise and consumer APs.
- Optimized heap usage and reduced heap pressure to prevent fragmentation on ESP8266:
  - Budgeted MQTT packet buffer size to a maximum of **1024 bytes** (down from 2944 bytes).
  - Replaced long-lived dynamic `String` fields with `FixedSettingString` in `NodeStateMachine` and `AdminExecutor`.
  - Replaced class-level dynamic topic buffers in `MqttBridge` with stack-allocated formatting.
  - Replaced `settings_backup.h` / `SettingsBackup` capture-restore logic with direct struct copy assignments.
  - Optimized Dallas temperature sensor address representation to format on-stack instead of storing on the heap.
  - Implemented Compact Operational Config contract for `get_config` and `set_config` over MQTT (excluding keys/secrets).
- Restored UDP log control (`UdpLogControl` message type, `udp_log_control` / `remote_udp_log_control` command surfaces) with tight heap limits: local gateway logging is MQTT-only and rejects serial commands with `mqtt_required`, while remote log control enforces firmware-side peer WiFi/IP eligibility.
- Removed MQTT Secret Export support (`allow_mqtt_secret_export`) entirely for enhanced security.
- Implemented Phase 2 Step 1 for Replacement Gateway Discovery & Explicit Peer Adoption:
  - Added volatile discovery candidate tracking capped at 12 entries (`Settings::kAddressListCap`) with explicit lifecycle states (`SeenAddressOnly`, `Identified`, `Readdressing`, `Adopted`, `Failed`, `ResetRequested`).
  - Introduced `MessageType::Readdress = 'D'` protocol message (payload: 4-byte `chip_id` LE, 1-byte address/reset, 1-byte op) with wrong-source/destination exclusions.
  - Implemented chip-scoped readdressing and reset commands, allowing gateway to safely adopt remotes and resolve conflicts/out-of-range addresses without affecting existing peer configurations.
  - Implemented transaction retry and timeout logic (4 total transmissions over 500ms intervals) for gateway-side adoption.
  - Exposed candidate lists and adoption status in the `lora_inventory_status` response.
  - Added native unit tests verifying candidate lifecycle, reason evaluation, conflict/out-of-range readdressing, and full-fleet safety reset guarding.
- Gated incoming telemetry peer creation in paired gateway mode using a new target resolver helper `isConfiguredOperationalPeer`, preventing unconfigured same-key telemetry from populating the operational peer cache.
- Completely removed `relay_feedback` and `input_feedback` telemetry fields and MQTT topics across the firmware, MQTT bridge, and Flasher application, relying purely on commanded `relay_state` and input sensor `input_state` for simplicity.
- Removed `maintenance_debug_telemetry_enabled` configuration flag, replacing automatic debug telemetry sweeps with an explicit, one-shot "Poll Diagnostics" admin command and UI action to retrieve Heap and Frag metrics.
- Simplified RSSI representation, completely removing `downlink_rssi` from admin status and `downlink_rssi_dbm` MQTT topic, and showing only a single gateway-observed uplink RSSI metric in the Monitor tab.
- Updated peer table uptime column to continuously extrapolate uptime based on age in the gateway's status cache.
- Formatted local gateway input cards in the UI to display "Open" or "Closed" instead of raw states.

- Rebuilt `lora_inventory_status` response to list only configured target addresses in `1..12` instead of dumping all `peer_count_` cache slots.
- Compacted `lora_inventory_status` JSON output by omitting default/falsy boolean flags and empty strings/IPs.
- Added rate-limited (30s) ESP heap metrics and JSON size telemetry logging before and after response generation.
- Refactored the sensor subsystem to use a generic, instance-aware `SensorRegistry` (maximum of 6 sensors) supporting multiple environmental/measurement sensors (DS18B20 temperature, tank level) and control inputs (dry contact), simplifying payload encoding and decoupling acquisition.
- Upgraded the MaintenanceStatus payload version to 2, implementing paginated LoRa transmission for compact and flexible remote sensor telemetry.
- Decoupled admin command execution from `SerialAdmin` to a transport-neutral `AdminExecutor`, allowing identical command capability over physical serial and MQTT connections.
- Implemented secure remote request validation over MQTT featuring a boot-safe duplicate request cache (circular request ID cache of size 10) and ntp-aware timestamp TTL checks.
- Enforced credential safety over MQTT by completely excluding secrets from MQTT configuration outputs.
- Wired `AdminExecutor` to `MqttBridge` to execute incoming commands on `<root>/lrs-<chip>/admin_command` and publish responses on `<root>/lrs-<chip>/admin_response`.
- Raw ESP uptime (`uptime_ms`) is now exclusively populated from raw millisecond telemetry (`kMaintenancePageVersion`) and is no longer assigned from coarse debug minutes telemetry.
- Gateway cached peer uptime is cleared immediately on standard non-uptime telemetry status packets (Heartbeats, PollResponses, ACKs, MqttStatus) to prevent stale uptime carry-forward.
- Maintenance telemetry page bursts are protected from clearing fresh uptime by enforcing a 5-second grace period before clearing.
- Added a BSS-allocated circular buffer (up to 16 entries) for gateway-side provisioning events. Expose these logs in the `provisioning_status` serial-admin endpoint with millisecond timestamps.

### Firmware Fixes
- Fixed candidate discovery pipeline head-of-line blocking by implementing decentralized, bounded (4 attempts) candidate query tracking. Telemetry packets (no chip ID) only reset active attempts if the candidate is already in `Failed` state (avoiding infinite query loops for unresponsive candidates) while automatically restoring self-healing. Adoption timings are tuned to 1000ms intervals with 5 retries to tolerate close-proximity test bench RF noise and remote flash write delays.
- Fixed stale remote IP addresses in the Fleet listing by zeroing out the IP payload structure on the remote when WiFi STA is disconnected, and clearing the cached remote IP in the gateway peer cache if the remote reports disconnected or sends a zero IP.
- Fixed transient "Fault" state on startup for DS18B20 temperature sensors by scheduling the first conversion immediately and mapping initial state to `waiting` until the first reading completes.
- Decoupled the transmitter and telemetry schedulers to prevent active paired group-command sync states from starving background task ticks. Added a transmission suppression guard to prevent background radio queries from colliding with the critical group ACK/poll response windows.
- In paired mode, treat an empty known peer list as an empty fleet, preventing target resolution from falling back to default remote address `1` or other default targets. Standalone mode preserves legacy point-to-point fallback behavior.
- Allow MessageType::Provisioning packets to bypass self-source validation (msg.src == runtime_.local_address), preventing gateway-side and remote-side packet drops when both devices share factory-default address 254.
- Filter out invalid source addresses (`0`, `255`, and the local address) early in non-provisioning receive handling to prevent remote peer cache pollution.
- Restructured configuration validation: Remote/Receiver nodes can now set and save `mqtt_control_enabled = true` without triggering config validation resets on boot. This allows receiver nodes to successfully authorize LoRa-bridged MQTT commands.
- Fix repeat-session provisioning failure on commissioned gateways. Discovery now enters a quiet coordinator mode, cancelling active scans, pending maintenance pages, group commands, and transient peer command retries/polls while preserving persistent peer identity caches. Normal operations are symmetrically restored via a centralized exit helper.

### MQTT Improvements
- Tightened peer telemetry publishing semantics: publish last_seen_ms, last_seen_age_s, uplink_rssi_dbm, heap metrics, uptime, firmware version, and IP as blank values instead of default/zero placeholders when a peer has no live status yet.
- Updated MQTT schema to publish normalized `sensor/<kind>/<instance>/value` and `sensor/<kind>/<instance>/state` topics.
- Gateway now publishes an empty string `""` to clear retained `peers/<addr>/uptime_ms` topics when peer uptime is cleared or absent.
- Removed legacy MQTT topic-clearing and boot-time Sweeper logic from the gateway firmware (`mqtt_bridge.cpp`/`mqtt_bridge.h`) to prevent empty legacy directories (`peer/0x01`, `peers/01`, `peers/0x01`, `peers/1`) from appearing in MQTT Explorer. Canonical `peers/<NN_lrs-chipid>/...` cleanup is preserved for explicit forget/remove actions.


### Flasher UI Improvements
- Removed the WiFi "PHY mode" dropdown selector from the network settings view.
- Replaced static dashboard sensor cards with dynamic sensor rendering mapped from the generic `SensorRegistry`.
- Added MQTT Gateway OTA upgrade support: when Fleet connection mode is MQTT, the "Upgrade gateway" button initiates a local firmware file server, dispatches the `ota_pull` admin command to the gateway over MQTT, and monitors MQTT discovery reports for successful version updates.
- Fixed the "Load gateway" button disabling logic in Fleet mode to correctly evaluate selected MQTT gateways.
- Integrated remote MQTT gateway connection and control support to Fleet (network) and Settings tabs, supporting connection state indicators and dynamic MQTT gateway selection.
- Created reactive listeners for MQTT telemetry, status, discovery, and transactional response events.
- Added platform-aware keyboard shortcuts (CMD + 1..5 on macOS, CTRL + 1..5 on Windows/Linux) to switch between Flash, Provision, Fleet, Monitor, and Settings views, complete with editable-target input guards and navigation hover tooltip hints.

- Exposed MQTT control inputs ("Accept gateway MQTT commands" and "Controllers" list) for Remote/Receiver units in the settings tab, clarifying that remote nodes must authorize the Gateway's local LoRa address.
- Added a warning banner and inline warning status on the Gateway MQTT settings tab if paired input-control is enabled, explaining that physical input overrides block MQTT relay commands to prevent conflicting state loops.
- Simplified operator views in Fleet and Monitor: displays a single unified `Relay` and `Input` state, removing raw feedback columns and confusing `ack X · fb Y` details.
- Gateway Relay card simplified to show unified Relay and Input columns in a clean 2-column layout.
- Prevented the Flasher from caching and re-hydrating old `uptime_ms` values from history into the active row display.
- Parse, deduplicate, and display gateway-side provisioning event logs (`[FW] [timestamp] ...`) in the Provisioning tab activity log during EasyPair.

### Flasher UI Fixes
- Scope gateway session key mismatch invalidation to the active Fleet gateway, clearing both visible rows and hidden history on gateway session changes, port changes, and local factory resets. The session key includes `role_tx` and `local_address` to cleanly handle role/address reconfiguration, and local factory reset cache clearing is gated on the reset port matching the active gateway.
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
