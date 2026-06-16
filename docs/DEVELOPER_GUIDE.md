# Developer Guide

## 1. Scope
Current production firmware for ESP8266 LRS devices (identical hardware; mode/role selected in commissioning), including:
- LoRa relay control and ACK logic
- USB serial admin via the desktop Flasher app
- MQTT bridge
- Sensor baseline (DS18B20 + dry-contact state)

Repo:
- `/Users/warwick/Code/LoRa/lora_rs`

## 2. Build Targets

> [!IMPORTANT]
> **We ONLY build `lrs_za` (South Africa), `lrs_us` (USA), and run the native tests (`native`).**
> Any new automated process, compilation test, or developer must focus strictly on these core commands:
> - South Africa (433 MHz): `python3 -m platformio run -e lrs_za`
> - USA (915 MHz): `python3 -m platformio run -e lrs_us`
> - Unit Test Suite: `python3 -m platformio test -e native`
> - Version Metadata Verification: `python3 tools/test_version_metadata.py`
>
> All other environments defined in `platformio.ini` (such as the local `_ota` targets) are custom local, deployment-specific, or diagnostic environments. They are not part of the standard build or test pipeline and must be disregarded.


Native unit tests intentionally cover only pure firmware helpers that do not need ESP8266 hardware, WiFi, LoRa, SPI, or Arduino mocks. Use firmware builds and bench/soak tests for hardware timing, OTA, WiFi reconnect, LoRa ACK behavior, and Flasher serial workflows.

## 3. Runtime Modules
- `src/app.*`: orchestrator (networking, OTA, tick order)
- `src/config_store.*`: LittleFS settings + deterministic defaults/provisioning
- `src/radio_protocol.*`: LoRa packet encode/decode, crypto/MAC
- `src/state_machine.*`: TX/RX logic, timeout/ack, relay/input state
- `src/mqtt_bridge.*`: MQTT publish/subscribe bridge
- `src/sensor_manager.*`: DS18B20 detection/reads
- `src/serial_admin.*`: local USB maintenance, provisioning, Settings, Fleet, Monitor, and OTA-pull commands for Flasher
- `src/runtime_utils.*`: shared pure helpers (role/mode parsing and WiFi status text) used by app and admin/config paths to avoid cross-module coupling
- `src/logger.*`: structured serial logs + optional UDP mirror

## 4. Critical Defaults
- AP SSID: `lrs-<chipid>`
- Heartbeat default: 60 s
- ACK timeout default: 5 s
- TX command retry timeout default: 180 s
- RX fail-safe default: `hold_last` (timeout 180 s; optional `force_off` / `force_on`)
- DS18B20 default pin: GPIO0
- MQTT host default: `venus`

Mode/role mapping:
- `standalone` mode: role `none`
- `paired` mode: role `transmitter` or `receiver`
- Runtime behavior still branches on `role_tx` (`true` => TX path, `false` => RX path).

## 5. Tick Order and Performance
In `App::tick`:
1. USB serial admin
2. networking update
3. time sync
4. inject local temperature into state machine
5. state machine tick (LoRa control path)
6. MQTT tick
7. sensor manager tick

This ordering keeps LoRa control priority above MQTT.

When `power_save_listen_only` is enabled (representing binary PowerSave), the node immediately enters deep sleeping listen-only mode on boot or remote command. In this state, the node completely disables its WiFi stack, MQTT, Serial Admin, OTA, UDP logs, status LEDs, and nonessential sensor polling, leaving only the LoRa receiver active. It remains unpingable and unreachable locally until a remote LoRa command disables PowerSave, returning the node to Full Power.

## 6. Networking Behavior
- Mode: AP+STA
- AP starts immediately
- STA attempts if credentials exist
- On STA connection, Soft AP is disabled
- On STA disconnect/failure, Soft AP fallback is re-enabled unless `wifi_ap_fallback_policy=secure_sta_only`
- WiFi power-save disabled; TX power set high for stable local-link behavior
- `power_save_listen_only` is disabled by default. Enable it only for installed remote nodes where remote LoRa recovery is preferred and local WiFi/Serial access is not required during normal field operation.

## 7. Local Admin and Fleet Control
Local maintenance is via USB serial admin in Flasher. Remote maintenance is via MQTT admin where online, plus gateway-mediated LoRa admin for bounded remote actions.
The firmware no longer hosts an onboard Web UI, REST API, captive portal, or `ESP8266WebServer` admin surface; returning contributors should use Flasher/serial admin for local work.

Fleet/Provisioning implementation notes:
- Fleet scans are explicit serial-admin commands sent to a selected USB TX/gateway.
- Fleet inventory reads the gateway-owned peer cache and can send bounded encrypted maintenance probes.
- ESP8266 provisioning supports up to 12 remotes per gateway, matching `Settings::kAddressListCap` and `LRS_MAX_PEERS`.
- `identify` flashes the local LED with the Identify pattern for physical unit lookup.

USB serial admin protocol:
- Flasher commands are line-delimited JSON prefixed with `LRS:` so responses can be separated from normal serial logs.
- Replies use the same `LRS:` prefix and include `ok`, `cmd`, optional `id`, and command-specific fields.
- Read-only commands: `hello`, `identity`, `status`, `provisioning_status`.
- Password-gated commands: `get_config`, `set_config`, `factory_reset`, `configure_gateway`, `set_gateway_targets`, `start_discovery`, `provision_all`, `cancel_provisioning`, `wifi_scan`, `configure_wifi`, `provision_fleet_wifi`, `identify`, `reboot`, `udp_log_control`, `remote_udp_log_control`, `ota_pull`, `remote_ota_pull`, `start_lora_inventory`, `lora_inventory_status`, and `cancel_lora_inventory`.
- `get_config` returns redacted secrets by default. `set_config` accepts a partial `config` object, validates safety bounds, saves atomically, and applies runtime changes through the normal config reload hook.
- `factory_reset` supports `keep_shared_fleet_key` and `keep_wifi_credentials`, then reboots after acknowledging the command.
- `ota_pull` and `remote_ota_pull` require a 64-character SHA256 for the firmware payload. The remote LoRa trigger sends that digest over the encrypted LoRa control channel before the target downloads `/firmware.bin`.
- `identify` flashes the local LED with a distinct 3 fast flashes, pause, 3 fast flashes pattern; clients should animate the same pattern in the UI.
- Lost admin passwords are not reset in place; physical recovery is erase-and-reflash.

## 8. Packet Format
Current payload is 12 encrypted bytes with relay/input/flags/temp/sensor/time fields.

Current message types include control/status (`A/C/H/M/S/P/R`) plus provisioning/reset extensions (`W`/`X`).
`W` (WiFi provisioning) and `X` (factory reset) reuse the same encrypted 12-byte payload slot with custom byte layouts via the radio layer raw-payload send path.
- `Ack` frames carry acknowledged command counter in payload `b8..b11` and TX validates this before clearing pending command state.

If payload semantics change again, add protocol-version signaling first.

## 9. MQTT Summary
MQTT runs only when:
- enabled in config
- host set
- STA connected

Base path:
- `<root>/lrs-<chipid>/...`

### Availability

| Topic | Values | Description |
|-------|--------|-------------|
| `availability` | `online` / `offline` | LWT — broker publishes `offline` (retained) on disconnect; gateway publishes `online` (retained) on connect. |

### Discovery

| Topic | Format | Description |
|-------|--------|-------------|
| `<root>/discovery/lrs-<chipid>` | JSON | Device metadata published on connect (chip_id, firmware, role, address). |

### Gateway (local) status topics

Published retained every ~10 s under `<root>/lrs-<chipid>/`:

| Topic | Type | Description |
|-------|------|-------------|
| `input` | `0`/`1` | Dry contact / digital input state. |
| `relay` | `0`/`1` | Local relay state. |
| `relay_feedback` | `0`/`1` | Hardware GPIO readback of relay driver. |
| `type` | `tx`/`rx` | Device role. |
| `addr` | `0xNN` | LoRa address (hex). |
| `sensor/<kind>/<instance>/value` | string | Normalized value (float or int) for the given sensor. |
| `sensor/<kind>/<instance>/state` | string | State for the given sensor (`ok`, `missing`, `fault`, etc.). |
| `diagnostics/tank_current_ma` | float | 4–20 mA loop current diagnostic (empty string if disabled/invalid). |
| `diagnostics/tank_voltage_mv` | int | ADC reference voltage in mV (empty string if disabled/invalid). |
| `last_updated` | int | `millis()` at publish time — use to detect stale data. |
| `uptime_ms` | int | Gateway uptime in milliseconds. |
| `heap_free` | int | Free heap bytes (diagnostic). |
| `heap_max_block` | int | Largest contiguous free block (diagnostic). |
| `heap_frag_pct` | int | Heap fragmentation percentage (diagnostic). |
| `ota_status` | string | Gateway OTA upgrade status (`downloading`, `failed:<code>`, `rebooting`, or empty/cleared). |

### Peer status topics

Published retained under `<root>/lrs-<chipid>/peers/<NN_lrs-peerchipid>/`.
Topic path uses zero-padded decimal address + chip_id (e.g. `peers/03_lrs-804a9c27/relay`). Peer topics are withheld and NOT published until the peer's chip ID is known (from identity/version telemetry or persisted config).


> **Stale data protection:** When `ack_state` is `timeout`, all operational topics
> (`relay`, `input`, `wifi`, `relay_feedback`, `input_feedback`) are published as empty strings so automations do not act on
> stale values. Status/metadata/polling topics continue updating normally.

**Operational** — use these for automations and integrations:

| Topic | Type | Description |
|-------|------|-------------|
| `relay` | `0`/`1` | Remote relay state. |
| `input` | `0`/`1` | Remote dry contact state (from sensors telemetry, change-detected). |
| `ack_state` | string | `acked`, `pending`, `timeout`, `unknown`. |
| `sensor/<kind>/<instance>/value` | string | Remote sensor normalized value. |
| `sensor/<kind>/<instance>/state` | string | Remote sensor state. |
| `wifi` | `0`/`1` | Remote WiFi enabled state (empty string if unknown). |
| `uptime_ms` | int | Remote uptime in milliseconds (from version telemetry; cleared/published as empty when non-uptime status packet is received). |
| `uplink_rssi_dbm` | int | RSSI of last received packet from this peer (dBm). |
| `downlink_rssi_dbm` | int | RSSI reported by peer for gateway's signal (dBm, empty if unknown). |

**Polling / timing** — gateway-managed peer polling state:

| Topic | Type | Description |
|-------|------|-------------|
| `poll_interval_s` | int | Configured poll interval in seconds (0 = disabled). |
| `poll_state` | `idle`/`pending` | Whether a poll request is in flight. |
| `last_poll_tx_ms` | int | `millis()` of last poll request sent. |
| `last_seen_ms` | int | `millis()` of last received packet from peer. |
| `last_seen_age_s` | int | Seconds since last received packet — recomputed every publish cycle. Use to detect stale peers without knowing gateway absolute time. |
| `last_cmd_counter` | int | Monotonic command counter for change detection. |

**Cross-reference** — address lookup helpers:

| Topic | Type | Description |
|-------|------|-------------|
| `addr_hex` | string | Two-character hex address (e.g. `03`). |
| `addr_dec` | string | Zero-padded decimal address (e.g. `03`). |

**Diagnostic** — raw hardware state from debug maintenance telemetry:

| Topic | Type | Description |
|-------|------|-------------|
| `relay_feedback` | `0`/`1` | Raw GPIO readback of remote relay driver pin (hidden from normal UI). |
| `input_feedback` | `0`/`1` | Raw GPIO readback of remote input pin (hidden from normal UI). May differ from `input` due to telemetry page timing. |
| `heap_free` | int | Remote free heap bytes. |
| `heap_max_block` | int | Remote largest contiguous free block. |
| `heap_frag_pct` | int | Remote heap fragmentation percentage. |

### Subscribed control topics

- `relay`: sets local relay directly on that node.
- `control`: TX-only JSON control for remote LoRa relay send.
- `control.addr` parsing: JSON number = decimal address, JSON string = hex address.
- TX can be commanded to poll peers via:
  - `<root>/lrs-<tx_chipid>/peers/<NN_lrs-chipid>/poll_interval_s`
  - `<root>/lrs-<tx_chipid>/peers/<NN_lrs-chipid>/poll_now`
  - `<root>/lrs-<tx_chipid>/peers/<NN_lrs-chipid>/wifi` (payload `1`/`0`)
  - `<root>/lrs-<tx_chipid>/peers/<NN_lrs-chipid>/forget` (payload `1` removes runtime node and clears retained peer subtree topics)

TX input-to-LoRa control gate:
- Setting: `input_control_paired_lora_enabled` (LoRa tab).
- **Mutual Exclusivity:** Gateway physical input-control and MQTT relay control are mutually exclusive.
  - `true`: Physical gateway input transitions and state synchronization own relay authority. The gateway ignores and blocks incoming MQTT relay commands (both local `relay` and remote `control` topics, logging `relay_local_mqtt_blocked` and `mqtt_remote_relay_blocked` respectively) to prevent conflicting state loops. Remote nodes in slave mode will also block LoRa-bridged MQTT command execution (logging `rx_slave_block_mqtt`).
  - `false`: Gateway input state is reported but does not trigger LoRa relay commands. Local and remote MQTT relay controls are active and processed.


MQTT remote retry control:
- Setting: `mqtt_remote_retry_timeout_ms` (default 300000 ms / 300 s).
- TX retries `Mqtt` command sends with bounded Fibonacci-like backoff until timeout.
- RX replies with `MqttStatus` (`'S'`) including applied state and telemetry.
- TX polling uses `PollRequest` (`'P'`) / `PollResponse` (`'R'`) with the same retry-timeout window.
- TX scheduled polling controls:
  - `tx_mqtt_remote_polling_enabled` (default `false`)
  - `tx_mqtt_remote_default_poll_interval_ms` (default `60000`, enforced range `60000..3600000`)
  - `poll_interval_s` MQTT command is clamped to `0` (disable) or `60..3600` seconds.
- RX push-on-change controls:
  - `rx_push_on_change_enabled` (default `false`)
  - `rx_push_min_interval_ms` (default `60000`, enforced range `60000..3600000`)
  - when enabled, RX sends unsolicited `PollResponse` on debounced local input change, rate-limited by `rx_push_min_interval_ms`.
- Paired TX retry controls:
  - `tx_command_retry_timeout_ms` (default `180000`, enforced range `5000..3600000`)
  - retry spacing remains Fibonacci-like with added small jitter to reduce synchronization collisions.
- RX fail-safe controls:
  - `rx_failsafe_mode` (`hold_last` default; optional `force_off`, `force_on`)
  - `rx_failsafe_timeout_ms` (default `180000`, enforced range `5000..3600000`)

Discovery:
- Topic: `<root>/discovery/lrs-<chipid>`
- Retained JSON payload with identity/network/build fields.
- Published on connect and then every 60 s.

Reconnect/perf guards:
- Reconnect attempt interval: 30 s.
- MQTT socket timeout: 1 s.
- LoRa state machine tick executes before MQTT tick.

`last_updated` value is uptime milliseconds at publish time.

## 10. Hardware Input Notes (ESP8266)
CN1:
- +3V3, GND, GPIO0, GPIO2, GND, AIN

Constraints:
- GPIO0/GPIO2 are boot-sensitive / shared-purpose pins.
- AIN path is limited and board-conditioned.
- Advanced analog calibration UI remains staged work.

## 11. Current Sensor Support
- Local DS18B20 read
- Remote LoRa temperature decode/display
- Dry-contact status included in sensor digital field
- 4-20 mA tank level sampled on A0 and sent through maintenance telemetry

Future fields already reserved in payload for additional sensor telemetry.

## 12. Firmware Versioning Policy
- Use SemVer: `MAJOR.MINOR.PATCH[-PRERELEASE]`.
- `PATCH`: bug fixes, UI fixes, no protocol/config breaking change.
- `MINOR`: new backward-compatible features (new pages, MQTT fields, sensors).
- `MAJOR`: any breaking protocol/config behavior change.
- `PRERELEASE`: use suffixes like `-alpha`, `-beta`, `-rc.1` for non-final builds.

Build identity at runtime includes:
- `fw_version` (from repo root `VERSION`; `platformio.ini` value is fallback-only)
- `fw_git_sha`, `fw_git_branch`, `fw_dirty`
- `build_date`, `build_time`

Visibility:
- Flasher Flash/Settings/Monitor/Fleet panes via USB serial admin
- MQTT discovery topic (`<root>/discovery/lrs-<chipid>`)

Release alignment policy:
- Keep firmware, flasher UI label, docs/release notes, and factory helper scripts on the same value from `VERSION`.
- Use release tags in `v<version>` form (for example `v0.4.3-alpha`) while `VERSION` remains plain (for example `0.4.3-alpha`).
- Changelog and release notes must be written as a delta from the immediately previous release.
- For each release, include all non-trivial firmware/flasher changes since the previous release as the baseline announcement content.
- Flasher reuse guard: if `tools/flasher/**` changed, do not reuse prior flasher assets; rebuild all flasher installers from current source.
- Flasher asset reuse is permitted only when `tools/flasher/**` is unchanged (for example firmware-only/documentation-only releases).
- Firmware-only release mode is supported:
  - `--reuse-flasher <source-tag>` to copy prior flasher assets
  - `--reuse-flasher-keep-names` to keep source-tag flasher filenames in the new release (no binary rename)

Local non-release flasher version policy:
- Do not label local one-off test builds with the last shipped release version.
- Release builds use the exact root `VERSION`.
- After successful release publish to both repos, `tools/release_manager.py` auto-bumps `VERSION` to the next patch `-dev`, commits it, and pushes to `main` (default behavior).
- Example: release `0.6.1-alpha` -> automatic `VERSION` bump to `0.6.2-dev`.
- Local ad-hoc flasher artifact labels can then add git identity:
  - clean tree: `<version>.<shortsha>` (for example `0.6.2-dev.abc1234`)
  - dirty tree: `<version>.<shortsha>.dirty`
- This rule applies to local flasher test artifacts; it is there to keep support/debugging truthful and avoid stale version leakage.

### Dev Build Versioning
- The repo-root `VERSION` file is the operator-visible firmware and Flasher version source of truth.
- Every development build that will be flashed, shared, or used for field testing must get a unique dev build revision before building.
- Use `MAJOR.MINOR.PATCH~DEVBUILD`, for example `0.9.2~1`, `0.9.2~2`, and `0.9.2~3`.
- The `~DEVBUILD` suffix implies a development build; do not add `-dev`.
- If `VERSION` is `0.9.2-dev`, the next development build is `0.9.2~1`.
- Increment only the dev build revision while staying on the same base patch.
- Do not reuse a dev build version for a different firmware or Flasher artifact.
- Run `python3 tools/bump_dev_build.py` before a new development build.
- Firmware builds record the source tree used for each `~DEVBUILD` value and reject reusing the same dev build number after code changes.

Release execution guardrails:
- Never trigger `package_flasher.yml` in release mode with `platform=all` or `platform=macos`.
- Standard release path is tag-driven CI plus local macOS builds.
- CI release mode is Windows/Linux only; local macOS portable ZIPs are uploaded after CI.
- `package_flasher.yml` now blocks `create_release=true` when `platform=all` or `platform=macos`.
- `create_release=true` is allowed only on tag refs (`refs/tags/v*`); release dispatches from `main` are blocked.
- Mandatory release order:
  1. Verify workflow matrix policy before tag push (tag-triggered path must exclude macOS CI).
  2. Run `python3 tools/flasher/sync_version.py`, then build and validate macOS app bundles locally (`arm64` and `x86_64`) with architecture + `codesign --verify --deep --strict`.
  3. Push release tag and let CI publish Linux/Windows flasher artifacts, or dispatch explicitly from the release tag ref:
     - `python3 tools/release_flasher_assets.py dispatch-ci --tag v<version>`
  4. Upload local macOS portable ZIPs to the same release:
     - `python3 tools/release_flasher_assets.py upload-macos --tag v<version> --arm64 <arm64-portable.zip> --x64 <x86_64-portable.zip>`
  5. Verify complete assets in both repos:
     - `python3 tools/release_flasher_assets.py verify --tag v<version>`
- Manual `workflow_dispatch` is incident-recovery only and requires explicit project owner approval.

Deterministic release mode (preferred):
1. Draft final release notes first (delta-only vs previous release) and save to a file.
2. Run:
   - `python3 tools/release_one_shot.py --notes-file /absolute/path/to/release-notes.md --title "vX.Y.Z-suffix OneWordName"`
3. The script handles end-to-end:
   - firmware release publish (`lora-rs` + `lora-rs-firmware`),
   - Windows/Linux flasher CI release dispatch from the release tag and completion wait,
   - local macOS arm64/x86_64 portable ZIP builds from the release tag,
   - macOS asset upload to both repos,
   - full 10-asset contract verification in both repos,
   - workflow run pruning for `package_flasher.yml` (keeps latest 10 completed runs by default).
4. Release notes are deterministic in this mode: the file content is published verbatim.
5. Optional pruning control: `--keep-workflow-runs <N>` (default `10`; use `0` to disable for a specific run).

Mandatory pre-release validation gate:
- A release is not ready unless all four pass:
  - `pio test -e native`
  - `python3 tools/test_version_metadata.py`
  - `pio run -e lrs_za`
  - `pio run -e lrs_us`
- This gate is wired into `tools/release_manager.py` and therefore also enforced by `tools/release_one_shot.py`.
- Native tests are intentionally scoped to pure firmware helpers (`FixedSettingString`, mode/role parsing, version metadata parsing).
- Hardware behavior still requires bench/soak validation (LoRa ACK behavior, WiFi reconnect, OTA pull, serial-admin/Flasher workflows, long-running heap stability).
- Do not add flasher `npm run build` to this specific gate unless `tools/flasher/**` changed in the release.

Release/CI machine bootstrap requirements:
- PlatformIO CLI installed and available as `pio`.
- Native test toolchain availability for first run of `pio test -e native` (PlatformIO may install `native` platform dependencies automatically).
- Python 3 available for `tools/test_version_metadata.py`.
- Keep generated release metrics (`docs/release_build_metrics.csv`, `docs/release_build_metrics.md`) separate from source commits unless explicitly updating release-history artifacts.

Conditional checklist: when `tools/flasher/**` changed in the release:
- Rebuild flasher installers from current source for all supported targets (Windows x64 MSI + portable ZIP, Linux x64, macOS arm64/x86_64).
- Ensure flasher metadata is synchronized first via `python3 tools/flasher/sync_version.py` so `package.json`, `Cargo.toml`, and `tauri.conf.json` match root `VERSION`.
- Run `python3 tools/flasher/sync_version.py --check` to fail fast if any metadata (or `package-lock.json`, when present) drifts from `VERSION`.
- Verify local macOS app bundles pass `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2` before zipping.
- Update the public firmware repository (`lora-rs-firmware`) README with:
  - A brief "what the desktop flasher is" summary.
  - Current supported OS/architecture list.
  - At least one current UI screenshot (replace stale screenshot if UI changed).
- Confirm release assets and README platform matrix stay aligned (no platform listed without a downloadable artifact).

Release binary set contract:
- Firmware: `za`, `us`, `eu` (`3` files)
- Flasher: `windows msi`, `windows portable zip`, `linux deb`, `linux rpm`, `linux AppImage.tar.gz`, `macos arm64 portable zip`, `macos x86_64 portable zip` (`7` files)
- Total release binaries: `10`
- macOS portable ZIPs should contain `Thanda LoRa Flasher.app`; the app handles first-run move-to-Applications or run-once behavior itself.

Release tooling notes:
- `tools/release_manager.py` now defaults to no asset verification unless explicitly requested:
  - `--verify-firmware-only-assets` checks only firmware files (`za/us/eu`) in both repos.
  - `--verify-full-assets` checks full 10-file contract (use after flasher upload is complete).
- In firmware-only reuse mode (`--reuse-flasher --reuse-flasher-keep-names`), `--verify-full-assets` validates firmware at new version and flasher filenames at the reused source-tag version.
- `tools/release_manager.py` captures firmware memory/flash stats for `lrs_za` and `lrs_us` from PlatformIO output, prints deltas vs the previous release in the release run output, and appends them to:
  - `/Users/warwick/Code/LoRa/lora_rs/docs/release_build_metrics.csv` (canonical)
  - `/Users/warwick/Code/LoRa/lora_rs/docs/release_build_metrics.md` (human-readable table)

Apple signing/notarization policy for flasher macOS artifacts:
- Non-release/dev builds may use ad-hoc signing (`codesign -`) for rapid iteration.
- Standard release path currently uses local macOS builds outside CI; verify the resulting app bundles with `spctl` and `codesign` before zipping.
- Developer ID signing + notarization remains the future hardening path if release distribution policy changes.
- Required certificate secrets:
  - `APPLE_DEVELOPER_ID_CERT_P12_BASE64`
  - `APPLE_DEVELOPER_ID_CERT_PASSWORD`
  - Optional override: `APPLE_DEVELOPER_ID_IDENTITY` (certificate common name).
- Required notarization secrets (choose one auth mode):
  - Apple ID mode: `APPLE_ID`, `APPLE_APP_SPECIFIC_PASSWORD`, `APPLE_TEAM_ID`
  - API key mode: `APPLE_API_KEY_ID`, `APPLE_API_ISSUER_ID`, `APPLE_API_PRIVATE_KEY_BASE64`
- Packaging/signing verification checklist:
  - Sign app with hardened runtime + timestamp.
  - Sign the app bundle or ZIP according to the chosen Apple signing path.
  - Submit the signed artifact with `xcrun notarytool submit --wait` when notarization is enabled.
  - Staple with `xcrun stapler staple`.
  - Verify with `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2`.
