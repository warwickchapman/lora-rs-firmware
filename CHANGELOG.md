# Changelog

All notable changes to this pre-release project are documented here in current operator-facing terms.

## [Unreleased]

### Changed
- Firmware version is aligned to `0.8.17-dev`.
- Local maintenance is centered on Flasher over USB serial admin.
- Remote maintenance uses MQTT admin for online devices and gateway-mediated LoRa admin for bounded remote actions.
- OTA pull now requires SHA256 across serial, MQTT, and gateway-mediated LoRa paths.

### Added
- Dev builds now use unique operator-visible build revisions such as `0.9.2~1`; use `python3 tools/bump_dev_build.py` before flashing or sharing a new development build.
- Firmware build metadata now rejects reusing the same `~DEVBUILD` version after the source tree changes, preventing accidental duplicate dev firmware versions.
- LoRa maintenance inventory now carries the compact dev-build revision separately from major/minor/patch, allowing Fleet and Monitor to show versions such as `0.9.2~1` instead of only `0.9.2`.
- Flasher identity reads now replace cached factory/admin passwords every time, and Provision/Fleet gateway loading always rereads the selected USB gateway instead of trusting stale cached details.
- Gateway peer-cache identity updates now preserve a known `~DEVBUILD` suffix while the major/minor/patch core is unchanged, so Fleet and Monitor do not drop remote firmware rows back to plain `X.Y.Z` between compact version-extension packets.
- Flasher now clears stale Flash-tab device status after a successful firmware write, waits for serial admin to return, and rereads device identity/status before showing the running firmware version or starting the serial monitor.
- Native PlatformIO unit tests now cover pure firmware helpers for fixed settings strings and mode/role parsing, with Python tests for build-version parsing.
- Flasher Settings provides local USB status, identity, settings fetch/save, WiFi scan, identify, reboot, and guarded factory reset workflows.
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
