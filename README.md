# Thanda LoRa Remote Switch (LRS) Firmware

This repository contains ESP8266 firmware for LoRa relay control devices with mode-aware role semantics.

Both hardware units are identical. Behavior is selected by commissioning mode/role:
- `Standalone` mode: role `none` (local-only control, no paired LoRa role split)
- `Paired` mode: roles `transmitter` and `receiver`

The codebase is modular state-machine firmware with LittleFS settings, USB serial admin for the desktop Flasher app, MQTT support, OTA pull support, and factory metadata generation.

The former device-hosted Web UI, REST API, and captive-portal admin flow have been removed. Local maintenance now runs through the desktop Flasher app over USB serial admin; remote maintenance uses MQTT admin or gateway-mediated encrypted LoRa admin where supported.

## Release Flashing (No VSCode/PlatformIO)
For shipped release `.bin` files, use `esptool` and the included helper:

- Helper script (recommended):
  - Windows: `python tools/flash_release.py --port COM7 --bin lrs-firmware-0.10.1~1-za.bin`
  - macOS: `python3 tools/flash_release.py --port /dev/cu.usbserial-XXXX --bin lrs-firmware-0.10.1~1-za.bin`

The helper reads chip ID, flashes firmware, and prints:
- device name: `lrs-<chipid>`
- Admin password

Direct `esptool` fallback:
- Read chip ID:
  - Windows: `py -m esptool --port COM7 chip_id`
  - macOS: `python3 -m esptool --port /dev/cu.usbserial-XXXX chip_id`
- Flash at address `0x00000`:
  - Windows: `py -m esptool --port COM7 --baud 460800 write-flash 0x00000 lrs-firmware-0.10.1~1-za.bin`
  - macOS: `python3 -m esptool --port /dev/cu.usbserial-XXXX --baud 460800 write-flash 0x00000 lrs-firmware-0.10.1~1-za.bin`

Password derivation is deterministic per device/chip ID, so users can recover credentials without PlatformIO tooling.

## Desktop Flasher App
The repository includes a desktop flasher utility at `tools/flasher` for installers who want a GUI workflow.

What it does:
- Detects available serial ports.
- Reads chip identity and derives installer fields (`ssid`, admin/AP password, local/remote addresses).
- Lists available firmware binaries from the public firmware release repository ([lora-rs-firmware](https://github.com/warwickchapman/lora-rs-firmware)).
- Supports local `.bin` override selection.
- Flashes selected firmware via `esptool` and shows live operation logs.
- Provisions gateways/remotes, scans Fleet inventory through a USB gateway, triggers OTA pull for WiFi-connected devices, and edits local Settings over USB serial admin.
- **Linux Users**: Ensure you are in the `dialout` group (`sudo usermod -a -G dialout $USER`) and log out/in.
- **Linux AppImage note**: Prefer the `*.AppImage.tar.gz` release asset. Extracting it preserves executable permissions.

Main project:
- `tools/flasher`

## Documentation & Code Navigation
- User/operator quickstart: `docs/USER_GUIDE.md`
- Developer onboarding and architecture: `docs/DEVELOPER_GUIDE.md`
- Protocol details: `docs/PROTOCOL.md`
- Factory/provisioning flow: `docs/PROVISIONING.md`
- Product manual draft: `docs/PRODUCT_MANUAL.md`
- Field technician manual: `docs/FIELD_TECHNICIAN_MANUAL.md`
- Deferred scope TODO list: `docs/TODO.md`

## Build Targets

> [!IMPORTANT]
> **We ONLY build `lrs_za` (South Africa), `lrs_us` (USA), and run the native tests (`native`).**
> Any new automated process, compilation test, or developer must focus strictly on these core commands:
> - South Africa (433 MHz): `python3 -m platformio run -e lrs_za`
> - USA (915 MHz): `python3 -m platformio run -e lrs_us`
> - Unit Test Suite: `python3 -m platformio test -e native`
>
> All other environments defined in `platformio.ini` (such as the local `_ota` targets) are custom local, deployment-specific, or diagnostic environments. They are not part of the standard build or test pipeline and must be disregarded.

## Release Version Source
- Single source of truth: `VERSION`
- Firmware build metadata (`fw_version` reported through serial admin/MQTT), flasher app version label, and factory/release helper scripts all read from this file.
- For a new release, bump `VERSION` once (for example `0.10.0-beta`) and keep release tag/title/assets aligned to that value.

Local non-release flasher build policy:
- Do not reuse the last released version string for one-off local test builds.
- Release builds use the exact value from `VERSION`.
- After a successful release, the release script auto-advances `VERSION` to the next patch `-dev`, commits it, and pushes it to `main` (default behavior).
- Example: releasing `0.10.0-beta` auto-bumps `VERSION` to `0.10.1-dev`.
- Local ad-hoc builds can then append git identity in artifact labels as needed:
  - clean tree: `<version>.<shortsha>` or `<version>-<shortsha>`
  - dirty tree: `<version>.<shortsha>.dirty`
- This keeps bug reports and screenshots unambiguous and prevents newer test builds from appearing older than the last shipped release.
- Dev-build version bumps should be deliberate:
  - If producing a Flasher artifact for sharing, flashing, or field testing, commit version files as a separate version bump commit.
  - If not producing an artifact, restore incidental Flasher version-file changes before committing unrelated work.
  - Check this with `python3 tools/flasher/version_guard.py` or `npm run check:version-workflow --prefix tools/flasher`.

## Release Automation Script
Use `tools/release_manager.py` to run the same release flow end-to-end:
- Runs mandatory pre-release validation gate (release blocks unless all pass):
  - `pio test -e native`
  - `python3 tools/test_version_metadata.py`
  - `pio run -e lrs_za`
  - `pio run -e lrs_us`
- Builds fresh `lrs_za` + `lrs_us` firmware
- Captures firmware RAM/Flash usage for both environments and prints deltas vs previous release in the release run output
- Generates named assets + SHA256 checksums
- Creates/updates GitHub release from `VERSION`
- After successful publish to both repos, auto-bumps `VERSION` to next patch `-dev`, commits, and pushes (disable with `--no-post-bump-dev`)
- Applies George Bernard Shaw quote + one-word release name (name reuse allowed when the quote bank is exhausted)
- Supports firmware-only releases that reuse prior flasher binaries:
  - `--reuse-flasher vX.Y.Z-alpha` reuses flasher assets from that release
  - `--reuse-flasher-keep-names` keeps original flasher filenames (for example `0.6.3-alpha` names in a `0.6.4-alpha` release)
- Optional verification mode:
  - `--verify-firmware-only-assets` for firmware-first publish
  - `--verify-full-assets` only after flasher assets are present

Pre-release gate notes:
- Native tests are intentionally limited to pure firmware helpers (no ESP8266 hardware mocks).
- Bench/soak validation is still required for LoRa ACK behavior, WiFi reconnect, OTA pull, serial-admin/Flasher workflows, and long-running heap stability.
- Flasher `npm run build` is not part of this specific gate unless `tools/flasher/**` changed.
- `pio test -e native` may install PlatformIO native platform/toolchain on first run.
- Release metrics files (`docs/release_build_metrics.csv`, `docs/release_build_metrics.md`) are generated artifacts and should remain out of source commits unless explicitly updating release history.

Release build metrics history:
- Historical table: `docs/release_build_metrics.md`
- Canonical data source: `docs/release_build_metrics.csv`
- Updated automatically by `tools/release_manager.py` after publish.

Flasher rebuild policy (mandatory):
- If any file under `tools/flasher/` changed since the source release, flasher binaries must be rebuilt from current source.
- Reusing flasher binaries from an older tag is allowed only when `tools/flasher/**` is unchanged.
- If in doubt, rebuild flasher binaries.
- If flasher code is unchanged and you intentionally want firmware-only release cadence, reuse is allowed:
  - Use `--reuse-flasher <source-tag> --reuse-flasher-keep-names`.
  - This copies prior flasher binaries into the new release without version renaming.

Release safety guardrails (mandatory):
- Do not run `gh workflow run package_flasher.yml` with `platform=all` or `platform=macos` for releases.
- In release mode, CI is Windows/Linux only; macOS portable ZIPs are local-only.
- `package_flasher.yml` now hard-fails release dispatches that try `platform=all` or `platform=macos` with `create_release=true`.
- `create_release=true` is tag-locked: dispatch must use `--ref v<release-version>`, never `main`.
- Enforce this order:
  1. Confirm workflow policy is already correct before tagging (tag runs must not include macOS CI).
  2. Run `python3 tools/flasher/sync_version.py` and then build/verify macOS app bundles locally.
  3. Push tag/release so CI builds Linux/Windows assets only, or dispatch only these two from the release tag ref:
     - `python3 tools/release_flasher_assets.py dispatch-ci --tag v<version>`
  4. Upload local macOS portable ZIP assets to the same release.
     - `python3 tools/release_flasher_assets.py upload-macos --tag v<version> --arm64 <arm64-portable.zip> --x64 <x86_64-portable.zip>`
  5. Verify full asset contract:
     - `python3 tools/release_flasher_assets.py verify --tag v<version>`
- If any unintended manual run starts, cancel it immediately and verify release assets were not mutated.

When flasher files changed (`tools/flasher/**`) in a release:
- Rebuild flasher installers from current source (Windows x64 MSI + portable ZIP, Linux x64, macOS arm64/x86_64).
- Before any flasher build (local or CI), run `python3 tools/flasher/sync_version.py` so `package.json`, `Cargo.toml`, and `tauri.conf.json` are aligned to `VERSION` (prevents stale metadata leakage).
- Fail fast on drift with `python3 tools/flasher/sync_version.py --check` (this also checks `package-lock.json` when present).
- Verify local macOS `.app` outputs with `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2` before zipping.
- Update `lora-rs-firmware` README with:
  - brief flasher summary (what it is),
  - supported platforms list,
  - at least one current screenshot.
- Ensure README platform list matches actual uploaded assets.

Release asset contract:
- Firmware binaries: 3 (`za`, `us`, `eu`)
- Flasher binaries: 7 (`windows msi`, `windows portable zip`, `linux deb`, `linux rpm`, `linux AppImage.tar.gz`, `macos arm64 portable zip`, `macos x86_64 portable zip`)
- Total binaries per release: 10
- macOS portable ZIPs should contain `Thanda LoRa Flasher.app`; the app handles first-run move-to-Applications or run-once behavior itself.
- In reuse-with-keep-names mode, firmware assets use the new release version and flasher assets retain source-tag version in filenames.

Changelog/release-notes rule:
- Release notes and `CHANGELOG.md` entries must describe deltas from the immediately previous release, not generic project capability lists.
- Include all non-trivial firmware/flasher changes since the previous release as the baseline draft for release announcements.

Deterministic one-command release (recommended):
- Prepare release notes in a file first (for example `docs/release_notes/v0.9.0-beta.md`).
- Then run one command:
```bash
python3 tools/release_one_shot.py   --notes-file /absolute/path/to/release-notes.md   --title "vX.Y.Z-suffix OneWordName"
```
- This script performs the full flow:
  - firmware build + publish to both repos,
  - flasher Windows/Linux CI dispatch from the release tag and wait-for-success,
  - local macOS portable ZIP builds from the release tag,
  - macOS portable ZIP upload to both repos,
  - full 10-asset verification in both repos,
  - GitHub Actions run cleanup for `package_flasher.yml` (keeps latest 10 completed runs by default).
- Release notes are used exactly as provided (no auto-generated summary/highlights).
- Optional: `--keep-workflow-runs N` (default `10`, set `0` to disable pruning for that run).

## Direct Flash Commands
- `python3 -m platformio run -e lrs_za -t upload --upload-port <PORT>`
- `python3 -m platformio run -e lrs_us -t upload --upload-port <PORT>`

## Notes for New Contributors
- Main runtime entrypoint: `src/main.cpp`
- Application orchestrator: `src/app.cpp`
- Runtime modules are split under `src/` (`app`, `state_machine`, `radio_protocol`, `serial_admin`, `mqtt_bridge`, `sensor_manager`, `config_store`, `logger`).
