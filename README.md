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
  - Windows: `py -m esptool --port COM7 --baud 460800 write_flash 0x00000 firmware-lrs_za-v0.4.3-alpha.bin`
  - macOS: `python3 -m esptool --port /dev/cu.usbserial-XXXX --baud 460800 write_flash 0x00000 firmware-lrs_za-v0.4.3-alpha.bin`

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

## Release Automation Script
Use `/Users/warwick/Code/LoRa/lora_rs/tools/release_manager.py` to run the same release flow end-to-end:
- builds fresh `lrs_za` + `lrs_us` firmware
- generates named assets + SHA256 checksums
- creates/updates GitHub release from `VERSION`
- enforces non-repeated George Bernard Shaw quote + one-word release name

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
