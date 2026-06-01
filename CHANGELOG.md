# Changelog

All notable changes to this pre-release project are documented here in current operator-facing terms.

## [Unreleased]

### Changed
- Implemented stateful, terminal-state based OTA queue pacing in Flasher UI, keeping the queue locked (`remoteOtaBusyAddress`) until the current remote node reaches a terminal state (`ota_updated`, `ota_failed`, or `ota_no_reboot`).
- Replaced the short-term 3.0s post-UDP log delay in Flasher with the complete removal of automatic UDP log enablement before starting OTA.
- Refactored firmware LoRa telemetry suppression during OTA to be stateful (`ota_pull_active_` flag), guaranteeing complete silence throughout both control-frame assembly and the entire HTTP download loop, with explicit failsafe clearing on all validation failure and download error paths.
- Excluded purely diagnostic `ota_pull_control_orphan` log events from triggering Flasher UI OTA retry or failure decisions.
- Refactored PowerSave into a pure, binary model ("Full Power" vs "PowerSave") with no inactivity timers, boot grace periods, or delayed/instant modes. The node immediately enters LoRa-only low-power mode on boot or remote command, completely turning off WiFi, Serial Admin, OTA, MQTT, LEDs, and background services, and can only be woken back to Full Power by a remote LoRa command.
- Added a clear, premium BETA tag to the Monitor page title in the Flasher UI.
- Implemented smart sleep telemetry in Flasher: the WiFi column now transitions to a pulsing orange `"..."` (pending offline) state when a node's PowerSave goes ON, until the actual reported connection status drops or the node goes silent.
- Automatically clear the IP address field to `"-"` when a device's WiFi status is reported as Offline.
- Refactored Power command button names and mode titles in the Tauri Settings UI to professional standards (e.g. `"Enable Power Save"`, `"Disable Power Save"`).
- Replaced generic bottom status bar notifications with dynamic, context-aware messages indicating whether power configuration or sensor configuration is being transmitted to a remote.
- Renamed the Fleet table "LoRa" column to "Addr", shortened the WiFi status label from `"Connected"` to `"OK"` to conserve horizontal layout space, and dynamically hide the "IP" column if no active remote devices report a WiFi IP address.
- Colorized the Sensors column dry-contact input status values to render the `"Closed"` word in emerald green and `"Open"` in vibrant orange to highlight device states at first glance.
- Added a highly informative hover `ⓘ` tooltip to fleet status labels (`Unexpected reboot`, `Rebooted (OTA)`, `No reboot seen (OTA)`) in the Uptime column explaining their triggers and diagnostic context.
- Stored remote device address-to-chip ID pairings persistently in gateway config store, pre-populating runtime caches on boot to instantly preserve remote names (`lrs-XXXXXXXX`) across gateway power cycles or Flasher re-connections.
- Fixed remote maintenance sensor telemetry reporting by scheduling the cascading sequence of all maintenance pages (`kMaintenancePageSensors` and `kMaintenancePageDebug`) on every gateway maintenance request, restoring DS18B20 temperature and 4-20 mA tank level reporting.
- Improved serial admin peer serialization to unconditionally report input and relay state values when a remote node has checked in (so the frontend receives active `0` values and correctly displays `"Open"` instead of `"waiting"`).
- Refined Flasher UI fleet listing to omit age, last-seen, and sensor fields for unseen remote devices, rendering clean `"-"` placeholders instead of misleading `"0s"` ages, `"live"` statuses, or `"waiting"` labels.
- Firmware version is aligned to `0.8.17-dev`.
- Local maintenance is centered on Flasher over USB serial admin.
- Remote maintenance uses MQTT admin for online devices and gateway-mediated LoRa admin for bounded remote actions.
- OTA pull now requires SHA256 across serial, MQTT, and gateway-mediated LoRa paths.

### Added
- Added a `power_save_listen_only` configuration flag for remote transmitter nodes that enables an ultra-lean power save mode (disabling WiFi, background sensor polling, MQTT, status LEDs, and Serial Admin) immediately on boot or remote command.
- Added a `🗑️ Forget Device` action in the Flasher Fleet Actions dropdown with safety confirmation to cleanly remove outdated or replaced nodes.
- Flasher settings pane displays the decrypted Fleet Key with hide/show toggle, enabling direct local credential updates over USB serial admin.
- Firmware supports targeted, same-key encrypted LoRa key rollover commands (`MessageType::FleetKeyControl`), enabling secure over-the-air Fleet Key updates to remote nodes via gateway.
- Flasher Fleet table features a "Fleet Key" remote action modal with a stark operational warning banner and explicit safety checkbox confirmation to prevent remote node orphaning.
- Flasher Fleet Actions dropdown consolidates WiFi provisioning, Sensor configuration, and Fleet Key change into a single tabbed ⚙️ Settings modal with device info header, reducing the dropdown from 7 to 5 items (Flash, Logs, Settings, Reboot, Factory Reset).
- Flasher caches entered WiFi passwords per SSID in local storage, automatically pre-populating and updating passwords in Provision mode, Settings configuration, and Remote WiFi provisioning dialogs.
- Dev builds now use unique operator-visible build revisions such as `0.9.2~1`; use `python3 tools/bump_dev_build.py` before flashing or sharing a new development build.
- Firmware build metadata now rejects reusing the same `~DEVBUILD` version after the source tree changes, preventing accidental duplicate dev firmware versions.
- LoRa maintenance inventory now carries the compact dev-build revision separately from major/minor/patch, allowing Fleet and Monitor to show versions such as `0.9.2~1` instead of only `0.9.2`.
- Flasher identity reads now replace cached factory/admin passwords every time, and Provision/Fleet gateway loading always rereads the selected USB gateway instead of trusting stale cached details.
- Gateway peer-cache identity updates now preserve a known `~DEVBUILD` suffix while the major/minor/patch core is unchanged, so Fleet and Monitor do not drop remote firmware rows back to plain `X.Y.Z` between compact version-extension packets.
- Flasher now clears stale Flash-tab device status after a successful firmware write, waits for serial admin to return, and rereads device identity/status before showing the running firmware version or starting the serial monitor.
- Native PlatformIO unit tests now cover pure firmware helpers for fixed settings strings and mode/role parsing, with Python tests for build-version parsing.
- Flasher Settings provides local USB status, identity, settings fetch/save, WiFi scan, identify, reboot, and guarded factory reset workflows.
- Flasher Settings features a sticky, globally-accessible action footer with "Save config" and "Reboot" buttons, making saving settings visible from any settings sub-tab (including Sensors, MQTT, and Network) and improving operator UX.
- Flasher Fleet uses a selected USB gateway to read the gateway-owned peer cache, run explicit bounded LoRa inventory scans, trigger remote identify, enable temporary UDP logs, and start OTA-pull actions for WiFi-connected remotes.
- Flasher Monitor provides serial-gateway diagnostics and rejects non-gateway device selections.
- Firmware serial admin supports local status/config actions, WiFi scan, WiFi provisioning, Fleet inventory, UDP log control, OTA pull, identify, reboot, and guarded factory reset commands.
- Firmware LoRa maintenance inventory returns compact remote identity/status details, including chip ID, firmware version, WiFi state/IP, MQTT state, uptime, and link freshness.
- Remotes can report dry-contact input, DS18B20 temperature, and KIT0139-style 4-20 mA tank level telemetry to the gateway peer cache, Flasher Monitor/Fleet, and MQTT peer topics.

### Removed
- Device-hosted operator UI/admin services have been removed from normal firmware.
- Stale device-admin helper scripts and contract/spec documents have been removed from the repository.
- Orphaned Web UI extraction and local DevMon web helper scripts have been removed.
- Dormant onboard automation-rule runtime code has been removed until automation returns with a supported Flasher and serial-admin management surface.
- Pseudo-`mesh` mode aliases and settings audit fields have been removed because they implied unsupported routing and observability behavior.

### Fixed
- Gateway peer-cache snapshots now mark dry-contact input state as known only after a remote packet or maintenance sensor page actually reports it, preventing Fleet from displaying default `"Open"` / `"Closed"` values for unverified remotes.
- Flasher Fleet Sensors now falls back to the normal dry-contact `input_state` when optional debug-only `input_feedback` is absent, restoring `"Open"` / `"Closed"` display for remotes that are reporting standard sensor telemetry.
- Flasher now treats a remote row reporting the selected target firmware version as an OTA success, clearing stale `Downloading OTA...` / queued state even if reboot/status timing was missed.
- Receiver provisioning now explicitly disables paired-input gateway control on remotes, and config repair disables that incompatible flag instead of resetting the whole receiver back to default address `1`.
- Gateway peer cache now treats chip ID as the physical identity and collapses stale duplicate rows when the same chip reports from a different LoRa source address.
- Gateway `set_gateway_targets` now merges target addresses by default instead of destructively replacing the fleet list, preventing follow-up provisioning batches from silently removing existing remotes from Fleet. Explicit replacement now requires `replace: true`.
- Remote firmware checks the target destination address of `MessageType::MaintenanceRequest` packets before responding, preventing concurrent reply packet collisions when the gateway performs sequential fleet scans.
- Remote firmware packs the dynamic dev build number into the provisioning announce/verify packet revision bytes, allowing the Flasher's Provisioning tab to display the full beta version string (e.g. `0.9.2~11` or higher) during discovery.
- Flasher Fleet now treats the gateway peer cache as the primary view: it refreshes automatically, preserves cached rows across tab changes, removes the manual cache reload button, and rate-limits explicit LoRa Force Scan requests.
- Firmware version packet fields now use build-generated numeric macros instead of parsing `LRS_FW_VERSION` at runtime.
- Firmware avoids several transient heap-string helpers in WiFi static IP setup, hostname normalization, DS18B20 address formatting, and MQTT client ID creation.
- Firmware settings strings now use fixed inline buffers instead of long-lived heap-backed `String` fields, reducing ESP8266 heap fragmentation during settings load/save and serial-admin edits.
- Serial-admin settings patches avoid unnecessary temporary heap strings for fleet keys and admin password validation.
- OTA pull now verifies the same HTTP stream it flashes, so a checksum match cannot be followed by a second unchecked download.
- Gateway-mediated LoRa OTA sends the expected firmware SHA256 over encrypted LoRa control before the remote pulls `/firmware.bin`.
- Flasher keeps selected USB ports independently for Flash, Provision, Fleet, Monitor, and Settings.
- Flasher serial jobs are queued per port so monitoring, settings fetches, provisioning, Fleet scans, and flashing do not fight over the same USB adapter.
- Flasher Settings normalizes serial-admin config responses before binding fields, preventing partial loads from leaving select and numeric fields blank.
- Flasher release fetching continues when the optional repo-local `.pio/build/lrs_za/firmware.bin` artifact is unavailable, preserving downloaded release choices and the manual file picker on packaged installs.
- Flasher Settings writes loaded status/config back to the active Settings port cache and reloads accepted settings after save, so the System save action visibly reflects the firmware-accepted configuration.
- Flasher now displays exactly the firmware version reported by a device instead of inferring a suffix from the Flasher app version.
- EasyPair follow-up provisioning preserves the gateway's existing target list and reserves saved target addresses, so newly discovered remotes continue at the next free address instead of reusing address `1`.
- EasyPair discovery now gives dense 12-device scans a wider reply window with two well-spaced factory replies per target, reducing missed factory remotes during bulk pairing without creating extra reply collisions.
- Provision advanced `Prepare` and `Scan` now load the selected USB gateway automatically; `Scan` prepares the gateway with the current fleet key before starting discovery.
- Provision target saving now reads the gateway's existing target list, merges newly provisioned addresses in Flasher, and sends one explicit replacement list to firmware; applied-but-unverified remotes are kept in the target list with a clear log message.
- Provision scan wording now distinguishes the factory/unprovisioned scan count from total fleet size, and discovery status logs no longer present early in-progress polls as final found counts.
- Fleet cache reload now logs and notifies after rereading the gateway-owned peer cache, making clear that it does not start a LoRa probe scan.
- Fleet and Monitor maintenance scans are back to one compact response per probe, avoiding hidden sensor/debug response fan-out on the LoRa channel.
- Fleet and Monitor peer-cache snapshots now omit unknown/default row fields and the Flasher serial reader accepts larger bounded admin responses, preventing 12-row cache snapshots from being dropped as timeouts.
- Gateway-mediated remote OTA pull control is now paced over multiple firmware ticks instead of bursting all SHA256 control frames in one serial-admin call.
- Flasher Settings makes redacted secret-preserving saves explicit and clears cached state after reboot, factory reset, flash, or provisioning changes.
- Provisioning address allocation starts remotes at LoRa address `1`, logs chip IDs, and avoids stale serial cache where possible.
- Fleet scan defaults are bounded to the supported LRS remote range and refresh incomplete cached rows with focused LoRa probes.
- UDP logging in Flasher follows the latest line and includes copy plus expand/collapse controls.
- Gateway protects active remote OTA pull sequences by returning a `gateway_busy` error when another OTA trigger is sent during an ongoing LoRa frame sequence.
- Flasher Fleet clears `otaExpectedUntilMs` from device history once the device transitions to `ota_updated` status, preventing the UI status from reverting to "Waiting for reboot" after the 30-second update banner expires.
- Standard build target documentation in `README.md` and `docs/DEVELOPER_GUIDE.md` has been highly emphasized with explicit `[!IMPORTANT]` boxes to ensure development and automation tools focus only on `lrs_za`, `lrs_us`, and native tests.
- Flasher Fleet and Monitor listings display `-` under the MQTT column by default, and only show `Online` or `Offline` when MQTT is explicitly enabled on the device.
