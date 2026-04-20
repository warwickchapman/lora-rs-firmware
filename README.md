# Thanda LoRa Remote Switch (LRS) Firmware

This repository contains ESP8266 firmware for LoRa relay control devices with mode-aware role semantics.

Both hardware units are identical. Behavior is selected by commissioning mode/role:
- `Standalone` mode: role `none` (local-only control, no paired LoRa role split)
- `Paired` mode: roles `transmitter` and `receiver`
- `Mesh` mode: roles `coordinator` and `node`

The codebase has been rebuilt as a modular state-machine firmware with a protected web console, LittleFS settings, OTA support, and factory metadata generation.

## Release Flashing (No VSCode/PlatformIO)
For shipped release `.bin` files, use `esptool` and the included helper:

- Helper script (recommended):
  - Windows: `python tools/flash_release.py --port COM7 --bin firmware-lrs_za-v0.4.3-alpha.bin`
  - macOS: `python3 tools/flash_release.py --port /dev/cu.usbserial-XXXX --bin firmware-lrs_za-v0.4.3-alpha.bin`

The helper reads chip ID, flashes firmware, and prints:
- SoftAP SSID: `lrs-<chipid>`
- SoftAP password
- Admin password

Direct `esptool` fallback:
- Read chip ID:
  - Windows: `py -m esptool --port COM7 chip_id`
  - macOS: `python3 -m esptool --port /dev/cu.usbserial-XXXX chip_id`
- Flash at address `0x00000`:
  - Windows: `py -m esptool --port COM7 --baud 460800 write-flash 0x00000 firmware-lrs_za-v0.4.3-alpha.bin`
  - macOS: `python3 -m esptool --port /dev/cu.usbserial-XXXX --baud 460800 write-flash 0x00000 firmware-lrs_za-v0.4.3-alpha.bin`

Password derivation is deterministic per device/chip ID, so users can recover credentials without PlatformIO tooling.

## Desktop Flasher App
The repository now includes a desktop flasher utility at `/Users/warwick/Code/LoRa/lora_rs/tools/flasher` for installers who want a GUI workflow.

What it does:
- Detects available serial ports.
- Reads chip identity and derives installer fields (`ssid`, admin/AP password, local/remote addresses).
- Lists available firmware binaries from the public firmware release repository ([lora-rs-firmware](https://github.com/warwickchapman/lora-rs-firmware)).
- Supports local `.bin` override selection.
- Flashes selected firmware via `esptool` and shows live operation logs.
- **Linux Users**: Ensure you are in the `dialout` group (`sudo usermod -a -G dialout $USER`) and log out/in.
- **Linux AppImage note**: prefer the `*.AppImage.tar.gz` release asset. Extracting it preserves executable permissions.

Main entrypoint:
- `/Users/warwick/Code/LoRa/lora_rs/tools/flasher/main.py`

## Start Here
- User/operator quickstart: `/Users/warwick/Code/LoRa/lora_rs/docs/USER_GUIDE.md`
- Developer onboarding and architecture: `/Users/warwick/Code/LoRa/lora_rs/docs/DEVELOPER_GUIDE.md`
- Protocol details: `/Users/warwick/Code/LoRa/lora_rs/docs/PROTOCOL.md`
- Factory/provisioning flow: `/Users/warwick/Code/LoRa/lora_rs/docs/PROVISIONING.md`
- Product manual draft: `/Users/warwick/Code/LoRa/lora_rs/docs/PRODUCT_MANUAL.md`
- Legacy manual alignment notes: `/Users/warwick/Code/LoRa/lora_rs/docs/MANUAL_ALIGNMENT.md`
- Legacy branch parity map: `/Users/warwick/Code/LoRa/lora_rs/docs/FEATURE_PARITY.md`
- Deferred scope TODO list: `/Users/warwick/Code/LoRa/lora_rs/docs/TODO.md`

## Build Targets
- South Africa (433 MHz): `python3 -m platformio run -e lrs_za`
- USA (915 MHz): `python3 -m platformio run -e lrs_us`

## Release Version Source
- Single source of truth: `/Users/warwick/Code/LoRa/lora_rs/VERSION`
- Firmware build metadata (`fw_version` shown in Web UI/API), flasher app version label, and factory/release helper scripts all read from this file.
- For a new release, bump `VERSION` once (for example `0.4.4-alpha`) and keep release tag/title/assets aligned to that value.

Local non-release flasher build policy:
- Do not reuse the last released version string for one-off local test builds.
- Release builds use the exact value from `VERSION`.
- Local ad-hoc flasher builds after a release should identify themselves as:
  - clean tree: `<next-patch>-dev.<shortsha>`
  - dirty tree: `<next-patch>-dev.<shortsha>.dirty`
- Example: after release `0.6.0`, a local test build from commit `abc1234` should be labeled `0.6.1-dev.abc1234` (or `0.6.1-dev.abc1234.dirty` if the tree is modified).
- This keeps bug reports and screenshots unambiguous and prevents newer test builds from appearing older than the last shipped release.

## Release Automation Script
Use `/Users/warwick/Code/LoRa/lora_rs/tools/release_manager.py` to run the same release flow end-to-end:
- builds fresh `lrs_za` + `lrs_us` firmware
- generates named assets + SHA256 checksums
- creates/updates GitHub release from `VERSION`
- applies George Bernard Shaw quote + one-word release name (name reuse allowed when the quote bank is exhausted)

Flasher rebuild policy (mandatory):
- If any file under `/Users/warwick/Code/LoRa/lora_rs/tools/flasher/` changed since the source release, flasher binaries must be rebuilt from current source.
- Reusing flasher binaries from an older tag is allowed only when `tools/flasher/**` is unchanged.
- If in doubt, rebuild flasher binaries.

Release safety guardrails (mandatory):
- Do not run `gh workflow run package_flasher.yml` on `main` during normal releases.
- Tag-triggered CI is the default release path; manual workflow dispatch is exception-only and requires explicit owner approval.
- Enforce this order:
  1. Confirm workflow policy is already correct before tagging (tag runs must not include macOS CI).
  2. Run `python3 tools/flasher/sync_version.py` and then build/verify macOS installers locally.
  3. Push tag/release so CI builds Linux/Windows assets only.
  4. Upload local macOS assets to the same release.
- If any unintended manual run starts, cancel it immediately and verify release assets were not mutated.

When flasher files changed (`tools/flasher/**`) in a release:
- Rebuild flasher installers from current source (Windows x64 MSI + portable ZIP, Linux x64, macOS arm64/x86_64).
- Before any flasher build (local or CI), run `python3 tools/flasher/sync_version.py` so `package.json`, `Cargo.toml`, and `tauri.conf.json` are aligned to `VERSION` (prevents stale `0.5.0`/`0.5.5` metadata leakage).
- Verify local macOS outputs with `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2`.
- Update `lora-rs-firmware` README with:
  - brief flasher summary (what it is),
  - supported platforms list,
  - at least one current screenshot.
- Ensure README platform list matches actual uploaded assets.

Release asset contract:
- Firmware binaries: 3 (`za`, `us`, `eu`)
- Flasher binaries: 7 (`windows msi`, `windows portable zip`, `linux deb`, `linux rpm`, `linux AppImage.tar.gz`, `macos arm64 dmg`, `macos x64 dmg`)
- Total binaries per release: 10

Example:
```bash
cd /Users/warwick/Code/LoRa/lora_rs
python3 tools/release_manager.py \
  --summary "Short release summary here." \
  --highlight "Feature highlight one" \
  --highlight "Feature highlight two"
```

## Flash
- `python3 -m platformio run -e lrs_za -t upload --upload-port <PORT>`
- `python3 -m platformio run -e lrs_us -t upload --upload-port <PORT>`

## Current Status
- `lrs_za` compiles successfully.
- `lrs_us` compiles successfully.

## Notes for New Contributors
- Main runtime entrypoint: `/Users/warwick/Code/LoRa/lora_rs/src/main.cpp`
- Application orchestrator: `/Users/warwick/Code/LoRa/lora_rs/src/app.cpp`
- Runtime modules are split under `/Users/warwick/Code/LoRa/lora_rs/src/` (`app`, `state_machine`, `radio_protocol`, `web_console`, `mqtt_bridge`, `sensor_manager`, `config_store`, `log_buffer`).
