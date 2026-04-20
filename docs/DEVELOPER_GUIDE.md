# Developer Guide

## 1. Scope
Current production firmware for ESP8266 LRS devices (identical hardware; mode/role selected in commissioning), including:
- LoRa relay control and ACK logic
- Web configuration console
- MQTT bridge
- Sensor baseline (DS18B20 + dry-contact state)

Repo:
- `/Users/warwick/Code/LoRa/lora_rs`

## 2. Build Targets
Defined in `/Users/warwick/Code/LoRa/lora_rs/platformio.ini`:
- `lrs_za` (433 MHz, `REGION_ZA`)
- `lrs_us` (915 MHz, `REGION_US`)

Commands:
- `python3 -m platformio run -e lrs_za`
- `python3 -m platformio run -e lrs_us`

## 3. Runtime Modules
- `src/app.*`: orchestrator (networking, OTA, tick order)
- `src/config_store.*`: LittleFS settings + deterministic defaults/provisioning
- `src/radio_protocol.*`: LoRa packet encode/decode, crypto/MAC
- `src/state_machine.*`: TX/RX logic, timeout/ack, relay/input state
- `src/mqtt_bridge.*`: MQTT publish/subscribe bridge
- `src/sensor_manager.*`: DS18B20 detection/reads
- `src/web_console.*`: embedded UI + REST endpoints
- `src/runtime_utils.*`: shared pure helpers (role/mode parsing and WiFi status text) used by app + web/config paths to avoid cross-module coupling
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
- `mesh` mode: role `coordinator` or `node`
- Runtime behavior still branches on `role_tx` (`true` => TX/coordinator path, `false` => RX/node path).

## 5. Tick Order and Performance
In `App::tick`:
1. networking update
2. inject local temperature into state machine
3. state machine tick (LoRa control path)
4. MQTT tick
5. sensor manager tick
6. web tick

This ordering keeps LoRa control priority above MQTT.

## 6. Networking Behavior
- Mode: AP+STA
- AP starts immediately
- STA attempts if credentials exist
- On stable STA and `ap_always_on=false`, AP can be disabled
- On STA failure, AP fallback remains available
- WiFi power-save disabled; TX power set high for stable local-link behavior

## 7. Web API
- `GET /`
- `GET /setup`
- `POST /api/login`
- `POST /api/logout`
- `POST /api/setup/fleet-key`
- `POST /api/setup/commissioning`
- `GET /api/status-live`
- `GET /api/status-live/events` (SSE)
- `GET /api/status-static`
- `GET /api/status-lite`
- `GET /api/fleet`
- `POST /api/fleet/scan`
- `POST /api/fleet/:addr/actions/poll-now`
- `POST /api/fleet/:addr/actions/forget`
- `POST /api/fleet/:addr/actions/poll-interval`
- `POST /api/fleet/:addr/actions/schedule`
- `GET /api/automation-rules` (`LRS_ENABLE_AUTOMATIONS`)
- `POST /api/automation-rules` (`LRS_ENABLE_AUTOMATIONS`)
- `GET /api/settings`
- `POST /api/settings`
- `GET /api/settings/export`
- `POST /api/settings/import`
- `GET /api/factory`
- `GET /api/wifi/scan`
- `POST /api/network/test`
- `POST /api/network/provision-fleet`
- `POST /api/provisioning/start`
- `GET /api/provisioning/status`
- `POST /api/provisioning/provision-all`
- `POST /api/provisioning/cancel`
- `POST /api/mqtt/test`
- `POST /api/logging/udp`
- `POST /api/ota`
- `GET /api/logs.csv`
- `GET /api/logs.txt`
- `POST /api/system/factory-reset`
- `POST /api/reboot`

Fleet/Provisioning implementation notes:
- Fleet endpoints are disabled in `standalone` mode (`fleet_disabled_in_standalone`).
- Fleet devices response includes both live peers and cached known peers (bounded to 12) so Devices page can render before a fresh scan.
- Provisioning status no longer uses compact/count-only response mode; rows remain authoritative for UI state.
- `/api/network/provision-fleet` supports optional `target_address` for per-device sends (`255` broadcast default).

## 8. Packet and Compatibility
Current payload is 12 encrypted bytes with relay/input/flags/temp/sensor/time fields.
Older 8-byte payload firmware is not wire-compatible.

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

Published status topics (retained, every 10 s):
- `input`
- `dry_contact`
- `relay`
- `temp_c`
- `remote_temp_c`
- `type`
- `addr`
- `last_updated`
  - `addr` is published as `0xNN` (for example `0x9D`).

Subscribed control topics:
- `relay`: sets local relay directly on that node.
- `control`: TX-only JSON control for remote LoRa relay send.
- `control.addr` parsing: JSON number = decimal address, JSON string = hex address.
- TX publishes per-peer child state under `<root>/lrs-<tx_chipid>/peer/0xNN/...` (canonical path).
- TX can be commanded to poll peers via:
  - `<root>/lrs-<tx_chipid>/peer/0xNN/poll_interval_s`
  - `<root>/lrs-<tx_chipid>/peer/0xNN/poll_now`
  - `<root>/lrs-<tx_chipid>/peer/0xNN/forget` (payload `1` removes runtime node and clears retained peer subtree topics)

TX input-to-LoRa control gate:
- Setting: `input_control_paired_lora_enabled` (LoRa tab).
- `true`: TX input transitions send `Change`; heartbeat relay field follows TX input state.
- `false`: TX still reports local input status, but does not send paired input-driven `Change`/`Heartbeat`; MQTT remote control remains active.
- MQTT deployments should avoid targeting RX nodes at the TX-paired `remote_address` unless this gate is `false`.

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
- Advanced analog sensor support remains staged work.

## 11. Current Sensor Support
- Local DS18B20 read
- Remote LoRa temperature decode/display
- Dry-contact status included in sensor digital field

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
- Web UI status table
- `/api/status`
- `/api/factory`
- MQTT discovery topic (`<root>/discovery/lrs-<chipid>`)

Release alignment policy:
- Keep firmware, flasher UI label, docs/release notes, and factory helper scripts on the same value from `VERSION`.
- Use release tags in `v<version>` form (for example `v0.4.3-alpha`) while `VERSION` remains plain (for example `0.4.3-alpha`).
- Flasher reuse guard: if `tools/flasher/**` changed, do not reuse prior flasher assets; rebuild all flasher installers from current source.
- Flasher asset reuse is permitted only when `tools/flasher/**` is unchanged (for example firmware-only/documentation-only releases).

Local non-release flasher version policy:
- Do not label local one-off test builds with the last shipped release version.
- Release builds use the exact root `VERSION`.
- Local ad-hoc flasher builds should derive from the next patch version and current git state:
  - clean tree: `<next-patch>-dev.<shortsha>`
  - dirty tree: `<next-patch>-dev.<shortsha>.dirty`
- Example: if the last release is `0.6.0`, local test DMGs should use `0.6.1-dev.<shortsha>` instead of `0.6.0` or an older prerelease label.
- This rule applies to local DMGs and other flasher test artifacts; it is there to keep support/debugging truthful and avoid stale version leakage.

Release execution guardrails:
- Never trigger `package_flasher.yml` manually on `main` for standard releases.
- Standard release path is tag-driven CI plus local macOS builds.
- Mandatory release order:
  1. Verify workflow matrix policy before tag push (tag-triggered path must exclude macOS CI).
  2. Run `python3 tools/flasher/sync_version.py`, then build and validate macOS installers locally (`arm64` and `x86_64`) with architecture + `codesign --verify --deep --strict`.
  3. Push release tag and let CI publish Linux/Windows flasher artifacts.
  4. Upload local macOS DMGs to the same release.
- Manual `workflow_dispatch` is incident-recovery only and requires explicit project owner approval.

Conditional checklist: when `tools/flasher/**` changed in the release:
- Rebuild flasher installers from current source for all supported targets (Windows x64 MSI + portable ZIP, Linux x64, macOS arm64/x86_64).
- Ensure flasher metadata is synchronized first via `python3 tools/flasher/sync_version.py` so `package.json`, `Cargo.toml`, and `tauri.conf.json` match root `VERSION`.
- Verify local macOS DMGs pass `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2`.
- Update the public firmware repository (`lora-rs-firmware`) README with:
  - A brief "what the desktop flasher is" summary.
  - Current supported OS/architecture list.
  - At least one current UI screenshot (replace stale screenshot if UI changed).
- Confirm release assets and README platform matrix stay aligned (no platform listed without a downloadable artifact).

Release binary set contract:
- Firmware: `za`, `us`, `eu` (`3` files)
- Flasher: `windows msi`, `windows portable zip`, `linux deb`, `linux rpm`, `linux AppImage.tar.gz`, `macos arm64 dmg`, `macos x64 dmg` (`7` files)
- Total release binaries: `10`

Apple signing/notarization policy for flasher macOS artifacts:
- Non-release/dev builds may use ad-hoc signing (`codesign -`) for rapid iteration.
- Standard release path currently uses local macOS builds outside CI; verify the resulting DMGs with `spctl` and `codesign`.
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
  - Sign DMG.
  - Submit DMG with `xcrun notarytool submit --wait`.
  - Staple with `xcrun stapler staple`.
  - Verify with `spctl -a -vv` and `codesign --verify --deep --strict --verbose=2`.
