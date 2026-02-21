# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows SemVer.

## [0.2.2-alpha] - 2026-02-21

### Added
- Complete implementation spec document: `docs/LRS_COMPLETE_IMPLEMENTATION_SPEC.json`.

### Changed
- Firmware version updated to `0.2.2-alpha`.
- Documentation updates across developer guide, protocol, and user guide.
- Runtime behavior updates in app/config/MQTT/state machine/radio/web console modules.

## [0.2.1-alpha] - 2026-02-15

### Added
- Standalone release flasher helper: `tools/flash_release.py` (reads chip ID, flashes binary, prints AP/admin password).
- No-VSCode flashing guidance in `docs/USER_GUIDE.md`.

### Changed
- Firmware version updated to `0.2.1-alpha`.
- Radio/state-machine behavior tightened when default deployment key is active.
- Web console/mobile UX and diagnostics display improvements.

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
