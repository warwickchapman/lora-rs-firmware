# Changelog

All notable changes to this pre-release project are documented here in current operator-facing terms.

## [Unreleased]

### Changed
- Firmware version is aligned to `0.8.17-dev`.
- Local maintenance is centered on Flasher over USB serial admin.
- Remote maintenance uses MQTT admin for online devices and gateway-mediated LoRa admin for bounded remote actions.
- OTA pull now requires SHA256 across serial, MQTT, and gateway-mediated LoRa paths.

### Added
- Flasher Settings provides local USB status, identity, settings fetch/save, WiFi scan, identify, reboot, and guarded factory reset workflows.
- Flasher Fleet uses a selected USB gateway to read the gateway-owned peer cache, run explicit bounded LoRa inventory scans, trigger remote identify, enable temporary UDP logs, and start OTA-pull actions for WiFi-connected remotes.
- Flasher Monitor provides serial-gateway diagnostics and rejects non-gateway device selections.
- Firmware serial admin supports local status/config actions, WiFi scan, WiFi provisioning, Fleet inventory, UDP log control, OTA pull, identify, reboot, and guarded factory reset commands.
- Firmware LoRa maintenance inventory returns compact remote identity/status details, including chip ID, firmware version, WiFi state/IP, MQTT state, uptime, and link freshness.

### Removed
- Device-hosted operator UI/admin services have been removed from normal firmware.
- Stale device-admin helper scripts and contract/spec documents have been removed from the repository.
- Orphaned Web UI extraction and local DevMon web helper scripts have been removed.

### Fixed
- OTA pull now verifies the same HTTP stream it flashes, so a checksum match cannot be followed by a second unchecked download.
- Gateway-mediated LoRa OTA sends the expected firmware SHA256 over encrypted LoRa control before the remote pulls `/firmware.bin`.
- Flasher keeps selected USB ports independently for Flash, Provision, Fleet, Monitor, and Settings.
- Flasher serial jobs are queued per port so monitoring, settings fetches, provisioning, Fleet scans, and flashing do not fight over the same USB adapter.
- Flasher Settings normalizes serial-admin config responses before binding fields, preventing partial loads from leaving select and numeric fields blank.
- Flasher release fetching continues when the optional repo-local `.pio/build/lrs_za/firmware.bin` artifact is unavailable, preserving downloaded release choices and the manual file picker on packaged installs.
- Flasher Settings writes loaded status/config back to the active Settings port cache and reloads accepted settings after save, so the System save action visibly reflects the firmware-accepted configuration.
- Flasher Settings makes redacted secret-preserving saves explicit and clears cached state after reboot, factory reset, flash, or provisioning changes.
- Provisioning address allocation starts remotes at LoRa address `1`, logs chip IDs, and avoids stale serial cache where possible.
- Fleet scan defaults are bounded to the supported LRS remote range and refresh incomplete cached rows with focused LoRa probes.
- UDP logging in Flasher follows the latest line and includes copy plus expand/collapse controls.
