# Changelog

## [Unreleased]

### Firmware Changes
- Replaced adoption's uncorrelated readdress confirmation with a small transaction matching the proven factory-reset handoff: one 32-bit transaction ID, one bounded request retry, and two staggered `ReaddressStatus` copies. A remote reports `save_failed` from its old address or `committed` from its saved new address, while duplicate/already-applied requests resend status without another flash write.
- Forget now carries the removed peer's already-known chip identity into the gateway's existing bounded, volatile candidate table after persistence succeeds. A same-key remote is therefore immediately adoptable without depending on a second maintenance identity exchange; no trust or remote configuration is retained.
- The gateway now reports adoption as complete only after both the remote address save and gateway peer-record save succeed. A failed gateway save rolls its in-memory peer edit back and offers a local-only retry; unconfirmed radio adoption remains safely retryable, and a full fleet refuses Adopt instead of silently converting it into a factory reset.
- Advanced the release Flasher compatibility contract to revision 3 for the clean pre-1.0 adoption protocol break.
- Corrected **Reset but keep in fleet** to preserve a remote's assigned role, address, and controller address along with its Fleet Key. Previously it preserved only the key, causing the reset device to boot with gateway defaults while its stale peer entry remained on the real gateway.
- Factory reset now clears the ESP8266 SDK's persistent station credentials as well as the application configuration when WiFi preservation is not selected, preventing a reset remote from reconnecting with a stale SDK WiFi profile.
- Advanced the release Flasher compatibility contract to revision 2 because the confirmed reset protocol is a clean pre-1.0 break; release firmware requires Flasher 0.10.5 or newer for normal commands.
- Replaced fire-and-forget remote factory reset with a correlated transaction on a new wire type, ensuring legacy and current reset packets cannot be misinterpreted across mixed firmware. The gateway sends a 32-bit reset ID, performs one bounded retry when no response arrives, and accepts only an authenticated confirmation matching the target, ID, and reset options.
- A remote now saves its reset configuration before sending two staggered `FactoryResetStatus` confirmations under its current Fleet Key, then reboots. Save failure is reported without rebooting. A full reset removes the gateway peer record only after confirmation and successful gateway persistence; unconfirmed devices remain recoverable.

### Flasher Features
- Moved the shared Fleet/Monitor Gateway Session into a compact top-right Connections control. The header now distinguishes the current task target from the shared MQTT broker, restores the editor on demand, and automatically collapses it after a newly selected gateway becomes active while keeping explicit Flash, Provision, and Settings targets beside their operations.
- Consolidated Fleet and Monitor transport selection into one aligned Gateway Session panel with explicit USB, remote MQTT, and local MQTT choices. Selecting remote MQTT immediately connects with the saved broker parameters, selecting local MQTT immediately starts and connects the broker, and gateway changes still avoid automatic LoRa scans or diagnostic loads. Settings keeps an independent explicit USB/MQTT device target and reuses the shared broker connection.
- Restored an explicit shared-broker connection control for MQTT Provision workflows. Pair discovery and WiFi Load & Scan now clearly require a connected broker, and MQTT gateway WiFi status, scanning, saving, and confirmation consistently use the selected Provision gateway rather than the serial-only target.
- Fleet now shows precise adoption stages and failures: sending, waiting for the remote, saving the gateway, unconfirmed, remote save failed, or gateway save failed. Full fleets show a non-actionable `fleet full` reason, and gateway persistence can be retried without sending another LoRa command.
- Fleet now shows explicit factory-reset stages instead of treating age or disappearance as proof. Selected remotes are reset sequentially, the sequence stops at the first failed or unconfirmed device, and a full-reset row disappears only after the gateway has safely removed its confirmed peer record.
- Manual Forget and gateway factory reset now warn when they can strand devices. The remote reset modal explains that missing confirmation retains the gateway record for retry.

## [0.10.4] - 2026-08-22

### Firmware Changes
- Added a release-only Flasher compatibility contract. Release firmware reports its minimum Flasher compatibility revision through serial status and compact remote maintenance version data, and rejects incompatible normal commands while leaving direct flashing available.
- Converted MQTT remote relay control to a short, correlated 5.5-second transaction using a 32-bit `mqtt_command_id` (`b8..b11`) and `kFlagMqttTransaction` flag (0x04). Removed `mqtt_remote_retry_timeout_ms` and replaced the 5-minute background retry loop with a fixed 4-attempt control budget (`0, +500 ms, +1.5 s, +3.0 s`). Gateway emits non-retained transaction outcomes (`Confirmed`, `Timeout`, `Mismatch`, `Untracked`, `Superseded`) to `<root>/event/cmd_result`.
- Gateway MQTT peer telemetry now publishes explicit `wifi_connected` state. A blank IP remains unknown rather than being treated as proof that a remote is offline.
- Redesigned remote OTA Pull as an acknowledged, bounded LoRa handoff. Every manifest carries a transfer ID; the remote validates the complete manifest, waits for gateway receive turnaround, sends `manifest_accepted`, and only then starts its HTTP download. If acknowledgement is lost, the gateway retransmits the manifest once before reporting the handoff as unconfirmed.
- Added compact fixed-size per-address OTA status records and the authenticated `remote_ota_status` admin command. Gateway status is correlated by destination and transfer ID, rejects stale or premature acknowledgements, and retains accepted sessions long enough to capture a later remote `download_failed` report for HTTP or checksum failure.
- Remote OTA status frames now enter through the configured-peer receive path before destination and transfer-ID validation. This fixes successful handoffs being discarded before Flasher could observe them. The new status-frame contract is deliberately protocol breaking; legacy remotes require one direct Wi-Fi update before they can use Fleet OTA Pull.

### Flasher Features
- Flasher stamps serial and MQTT admin requests with its compatibility revision, allowing release firmware to produce a clear update-required refusal per incompatible device.
- MQTT Fleet treats WiFi/IP identity data as pending until a remote reports it, rather than showing Offline solely because its retained IP leaf is empty.
- Remote OTA now serializes only the LoRa manifest handoff. Once acknowledged, the next remote starts while accepted remotes download and confirm independently. Compact per-address gateway records preserve late failure correlation, and Flasher paces confirmation refreshes one at a time. Success is validated by expected version and remote uptime decrease.
- Fixed Fleet Age so fresh serial peer-detail responses rebase the displayed age while newer MQTT telemetry remains authoritative.
- Replaced variable-width Remote OTA protocol messages in Fleet rows with fixed-width, colour-coded `Stage 1/4` through `Stage 4/4` badges. Technical details remain available on hover without making the table resize as an update progresses.
- Flasher now discovers a newly built local `~N` firmware when its window regains focus and revalidates it before flash or OTA commands. Local dev artifacts follow the repo `VERSION` directly, so `~10` and later builds cannot be hidden by lexical filename ordering.
- Late remote OTA confirmation remains correlated for ten minutes without continued polling. A remote that returns after the active watchdog timeout is shown as `Updated` only when the requested version and a reboot are both observed, instead of being mislabelled `Restarted`.

### Developer Tooling
- Added `tools/update_fleet.sh` for the one-time direct-Wi-Fi bootstrap of the recorded 12-remote test fleet and gateway. It uses the current ZA `firmware.bin`, derives each ArduinoOTA password locally, defaults to a dry run, accepts remote numbers or `g`, and stops at the first failed or indeterminate upload instead of retrying blindly.

## [0.10.3] - 2026-07-20

### Firmware Changes
- MQTT remote polling now follows a compact relay/input response with the existing bounded sensor pages when a remote has enabled temperature or tank sensors. Headless MQTT therefore refreshes operational sensor readings without a second sensor packet format or diagnostics traffic.
- Enforced gateway-only MQTT clients: remotes no longer connect to brokers or retain an MQTT-control configuration. All remote MQTT control and telemetry is mediated by the paired gateway over LoRa.
- EasyPair now collects the full discovery set before allocating addresses. New devices are assigned and provisioned in ascending chip-ID order, while known chips retain their saved address.
- EasyPair allocation ignores transient peer-cache rows; only configured targets and the current session reserve addresses. Late-poll verification and configured reservations are now visible in the provisioning log.
- Saving the gateway's MQTT remote-refresh preference no longer disconnects its MQTT client.

### Flasher Features
- Reorganised Settings into General, Control, and MQTT responsibilities. Gateway operators now choose one relay-control source, MQTT or gateway input; MQTT remote refresh settings are available on the MQTT tab.
- Discovered EasyPair devices show `Pending` until discovery completes and the gateway has allocated addresses.
- Settings saves over MQTT now send only changed fields and split large changes into bounded, ordered commands so they fit the ESP8266 MQTT receive limit.

## [0.10.2] - 2026-07-19

### Firmware Fixes
- Normal maintenance identity responses now carry the remote dry-contact input state, so Fleet hydration repairs `Open`/`Closed` state without waiting for MQTT, a command ACK, or a generic sensor page.
- Maintenance payload version is now `4` so older identity pages without an explicit input-state bit are rejected as unknown rather than misreported as `Open`.

### Firmware Changes
- Fixed provisioning and other custom-payload LoRa frames being incorrectly rejected as malformed operational sensor/input payloads.
- Fixed contradictory MQTT/Fleet peer input state reports by consolidating operational state updates to a single authoritative helper and removing conflicting assignments from diagnostic maintenance frames.
- A gateway factory reset now clears its canonical retained MQTT peer trees from the broker before erasing its configuration. If the broker is disconnected, the reset proceeds normally but logs `retained_cleanup_skipped`.

## [0.10.1] - 2026-07-18

### Firmware Changes
- Group-control traces now preserve and identify superseded commands, and every broadcast log records both its logical command ID and actual transport counter. A new gateway input state explicitly supersedes an unfinished command while preserving the gateway relay's last aggregate-confirmed state.
- Paired group control now sends two replay-identical broadcast copies before its ACK window. Remote ACKs use an explicit 250 ms post-receipt lead, and a remote reserves LoRa airtime for that ACK by deferring one coalesced maintenance or poll response plus all pending low-priority pages/pushes. Direct recovery `Change` retries remain immediate-ACK, one-peer control traffic.
- Maintenance payload version is now `3`: identity-page bytes `b6`/`b7` carry WiFi RSSI and marked relay state. This is a deliberate pre-1.0 protocol break; upgrade paired nodes together.
- Paired group-control windows now suppress all non-control LoRa work, including Fleet scans, candidate probes, MQTT polling, OTA convenience traffic, and explicit maintenance/diagnostic requests, so they cannot interrupt relay-command acknowledgements.
- Low-priority Fleet, candidate, and explicit maintenance/diagnostic requests now enter a fixed coalescing queue instead of failing or competing with relay-control timing. The queue is bounded to the fleet capacity and never carries control traffic.
- The gateway relay now changes only after every configured remote confirms the requested state. It is the all-remotes-confirmed fleet indicator for both `On` and `Off`.
- Cleaned paired addressing before release: remotes now store a single `controller_address`; gateways use only `known_peer_addresses` for fleet targeting. Removed the ambiguous `remote_address` and duplicate paired-target configuration surfaces.
- Reworked Fleet inventory admin responses for ESP8266 memory safety: `lora_inventory_status` now returns a compact configured-peer seed list plus scan/candidate metadata, while full cached details for a single remote are fetched with the new `lora_inventory_peer` command.
- Added `refresh_lora_peer` so Flasher can request normal maintenance pages for one remote at a time before reading that peer's cached details, keeping Fleet/Monitor current without rebuilding a whole-fleet JSON payload.
- Kept Fleet diagnostics out of the normal inventory hot path; heap/fragmentation/debug uptime remain explicit per-peer diagnostics instead of being serialized for the whole fleet on every refresh.
- Removed obsolete `rx_push_on_change_enabled` and `rx_push_min_interval_ms` settings; input changes now use the fixed operational push path instead of misleading configurable knobs.
- Relay state in Fleet now requires an actual state-carrying packet or ACK; group-command timeout bookkeeping no longer creates peer-cache entries that look like real `Off` relay reports.
- Codified relay/input control as the top priority path: removed the gateway's always-on round-robin maintenance sweep, kept Fleet/Monitor inventory as explicit low-priority observability, and moved normal input/sensor reporting to remote-originated operational pushes.
- Restored paired LoRa relay-state convergence as the highest-priority control path: startup sync is delayed briefly for rebooting remotes, and paired heartbeat now reasserts relay state unless MQTT control is explicitly the relay authority.
- Removed the temporary gateway event ring/admin command in favor of the existing firmware serial log stream, keeping diagnostic history out of ESP8266 RAM.
- Provisioning now treats **Max remotes** as a hard session-wide limit: additional new-device announces are rejected after the chosen capacity is reached, including after discovery completes.
- Fleet scans now take radio-scheduler priority over background peer polling, and paired input-control heartbeats no longer start full relay group commands unless the input state actually changes.
- MQTT retained peer cleanup now clears only canonical `peers/<NN_lrs-chip>/...` topics and actual known sensor leaves, avoiding legacy `peers/<NN>/...` cleanup trees and fake sensor slots in MQTT browsers.
- Fixed heartbeat relay control: plain heartbeats no longer leak input state as relay commands when paired input control is disabled.
- MQTT config completion sentinel now publishes `false` on partial failure instead of staying stuck.
- Removed temporary MQTT retained-config success-path diagnostics while preserving failure-only logs and retry behavior for invalid retained-config documents.
- Reject locked LoRa frequency changes via `set_config` with explicit error instead of silent overwrite.
- Adoption candidate evaluation no longer treats unknown chip ID as a proven mismatch, preventing false conflicts for address-only candidates.
- Upgraded configuration schema version from 3 to 4.
- Deprecated the persisted `"role"` string field in favor of `"role_tx"` (boolean).
- Modernized user/operator-facing role terminology in JSON output, status, and identity endpoints from legacy `"transmitter"`/`"receiver"` to `"gateway"`/`"remote"`.
- Added transparent config migration on load to convert legacy `"role"` values and old transmitter/receiver labels to `"role_tx"`.
- Remote WiFi STA RSSI now travels in the normal maintenance identity page, exposing `wifi_rssi_dbm` for every connected peer without a diagnostics poll; heap and fragmentation remain diagnostics-only.
- Normal maintenance identity pages now carry a marked relay state, so Fleet corrects a missed command ACK without extra LoRa traffic; timed-out command state is exposed as unknown instead of a misleading `Off`.
- Deferred group-command ACKs now reserve an initial receive guard before the first remote replies, preventing address `1` from answering before the gateway can receive it.
- Matching group-command ACKs now make the commanded relay state authoritative in the gateway peer cache, so every acknowledging remote is updated immediately and an inconsistent echoed ACK bit cannot display a false `Off`.
- Replaced missing-ACK group recovery polls with bounded direct idempotent relay-command retries. The gateway relay now changes only after every configured remote confirms the same command, for both `Off` and `On`.
- Fixed remote dry-contact reporting after paired relay commands: receiving a gateway input-control frame no longer overwrites a remote's local input state, and delayed ACKs sample the remote input pin when sent.
- Fleet's compact serial inventory seed now carries known relay and input state for every peer, so a broadcast control change refreshes all rows from the gateway cache without waiting for one-at-a-time detail hydration.
- Gateway publishes `wifi_rssi_dbm` retained MQTT topic for peers when connected and available, publishing empty payloads when unknown, offline, or cleared.

### Flasher Features
- Fleet and Monitor now hydrate serial gateway inventory one cached remote at a time after the compact seed response, avoiding multi-kilobyte whole-fleet JSON responses from ESP8266 gateways while progressively filling firmware, IP, relay, input, sensor, uptime, and RSSI fields.
- Serial Fleet/Monitor refresh now asks the gateway to refresh one remote's normal maintenance state before reading that row, so firmware, WiFi/IP, uptime, and sensor fields can converge progressively while Flasher is open.
- Gateway Events now retains raw serial evidence lines as well as structured firmware `event=` logs, highlights crash/reset signatures, and includes exact raw text in Copy output for debugging.
- Fleet no longer preserves stale relay values when the gateway inventory row omits `relay_state`, so unknown relay state is shown as unknown instead of a cached `Off`/`On`.
- Fleet and Monitor now capture firmware event log lines emitted during serial-admin activity, keeping a host-side Gateway Events buffer with Copy/Clear controls while the serial monitor is unavailable.
- Treat empty retained MQTT sensor telemetry as a delete/clear signal so cleared sensor topics no longer appear as enabled sensors in Fleet or remote settings.
- Added a clear Fleet gateway unexpected reboot alert when gateway uptime rolls backward outside an expected gateway flash/reboot flow.
- Repurposed the existing WiFi column in the Fleet table to display compact signal bars and dBm (e.g., `▂▄▆ -67`) for connected remote devices when the RSSI is known, coloring it by signal strength.
- Renamed the existing Fleet RSSI column to "LoRa RSSI" with a detailed tooltip.
- Integrated safety checks at the frontend boundary to ignore and filter out `wifi_rssi_dbm` values of 0 (sentinels for unknown/disconnected states).

## [0.10.0-beta] - 2026-06-29

### Firmware Changes
- Provisioning and gateway setup now preserve a gateway's disabled input-control setting instead of silently re-enabling physical gateway input control; remotes still force the invalid setting off.
- Replaced MQTT `relay` topic (was dual read/write) with explicit `set/relay` command topic for local relay control. The `relay` topic is now read-only status telemetry.
- Added `peers/<NN_lrs-peer_chipid>/set/relay` command topic for remote peer relay control via LoRa, replacing the JSON `control` topic.
- Removed legacy MQTT `control` topic (JSON `{addr, relay}` format). Use `set/relay` topics instead.
- Added diagnostic logging for MQTT relay command failures (bad payload, no TX role, address parse errors).
- Command topics (`set/relay`, `peers/.../set/relay`) must be published non-retained to prevent broker replay on reconnect.

### Flasher Features
- Monotonically serializes MQTT admin commands per gateway session using a Promise chain to prevent out-of-order execution and replay rejections.
- Implemented automatic MQTT session establishment handshake (`admin_challenge`) and automatic one-time retry handling when sessions are invalid or expired.
- Stripped Unix time `ts` and `ttl_ms` parameters from MQTT command payloads.
- Renamed the Provision connection option from "Remote MQTT Broker" to "MQTT".
- Changed Provision over MQTT to load gateway configuration from retained `config/#` topics instead of the monolithic `get_config` admin response, avoiding 15-second timeouts from oversized MQTT command responses on ESP8266.
- Renamed "Forget device" context menu button to "Remove from Gateway", updated confirmation dialogs, and renamed "Discovered Candidates" header to "Same-Key Adoption Candidates".
- Added `addr` and `remote_addr` fields to the Tauri Rust `MqttGatewayPayload` struct to ensure MQTT discovery details are correctly matching and parsed in the UI.
- Extracts `chip_id` from the canonical peer topic segment format (e.g. `02_lrs-0048d1bb`) in the Tauri MQTT backend and emits it to the frontend.
- Matches telemetry updates to seeded inventory rows using both `address` and `chip_id` (normalizing identities), surfacing address collisions as UI conflict warnings and blocking mismatched data merges.
- Filtered incoming telemetry in MQTT mode to prevent stale or retained MQTT messages from automatically generating non-existent remote rows in the configured devices list.
- Configured the frontend inventory list to immediately remove rows upon successful unpair command executions.
- Extended compact MQTT provisioning status rows with current address and firmware components so the Provision UI shows real values instead of unknown placeholders while staying below the ESP8266 MQTT packet ceiling.
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
- Implemented Phase 4 default address constants centralization: centralized min/max address validation bounds (`kMinAddress`/`kMaxAddress`), default gateway (`kGatewayAddress`), and default first remote (`kFirstRemoteAddress`) constants into `runtime_utils.h`, replacing duplicate inner definitions and raw `1`/`254` literals across config loading, admin parsing, app, and tests.
- Implemented Phase 3 pending command orchestration cleanup: extracted all pending command lifecycle management and parameter storage from `NodeStateMachine` into a standalone `PendingCommandManager` pure-data component. Split reset paths cleanly into `resetAll()` and `resetOnConfigApply()` to preserve existing initialization behavior, implemented last-write-wins overwrite semantics, and eliminated heap allocation `String` construction at call sites (WiFi provision, Fleet Key, and Fleet Provision Apply) in favor of stack buffer passing, reducing state machine direct variables count and dependency footprint.
- Implemented Phase 2 peer runtime slot management extraction: extracted peer runtime slot management and lazy poll-state ownership from `NodeStateMachine` into a standalone `PeerManager` helper module introducing no new dynamic allocations (preserving existing lazy poll allocation). Cleaned up state machine dependency graph and eliminated header circular dependencies by moving peer data definitions (`PeerAckState`, `PeerStatusSnapshot`, `PeerRuntime`, `PollRuntime`) into a neutral `peer_types.h` boundary header. Maintained high-level orchestration, chip ID resolution, and provisioning caches inside the state machine while delegating peer cache operations, left-shifting compaction, and status snapshot compilation cleanly to `PeerManager` without altering existing runtime behavior, eviction, or dynamic memory layouts.
- Implemented Phase 1 heap-stability and low-risk maintainability cleanup: centralized deployment key logic in `runtime_utils`, replaced dynamic `String` allocations in hot and control paths (OTA URL formatting and log redactions) with stack-allocated/bounded buffers, converted long-lived `std::function` callbacks in `logger` and `AdminExecutor` to raw function pointers with casting context, factored `MqttBridge::publishStatus` into local and peer helpers, and added compile-time `static_assert` size guards for state-machine structures.
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
