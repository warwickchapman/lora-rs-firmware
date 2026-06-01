# Provisioning and Factory Flow

## Goals
- Flash firmware for target region.
- Capture deterministic device metadata for sticker/traceability.
- Ensure first field maintenance via Flasher/USB serial is predictable.
- Commission mode/role and fleet key correctly on first login.

## Tooling
Factory script:
- `/Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py`

Firmware USB serial admin protocol:
- Flasher-facing commands are newline-delimited JSON prefixed with `LRS:`.
- Flasher can inspect a USB-connected device using `status`.
- Flasher can load and save local device configuration with authenticated `get_config` and `set_config`. Secret fields are redacted by default; blank password fields in Flasher preserve existing stored passwords. Settings > Network can scan WiFi from the selected USB device with serial admin `wifi_scan`, and Settings > Sensors only exposes DS18B20 enablement because the current hardware fixes the data pin and runtime cadence.
- Flasher can trigger authenticated `factory_reset` with `keep_shared_fleet_key` and `keep_wifi_credentials` options, and can use authenticated `reboot` for local recovery/admin flows.
- The selected USB-connected device can be configured as the TX/gateway with `configure_gateway`.
- The gateway can then run LoRa discovery/provisioning through `start_discovery`, `provisioning_status`, `provision_all`, and `cancel_provisioning`.
- `expected_remotes` is scan capacity only. `configure_gateway` must not leave speculative runtime targets behind.
- After provisioning, Flasher should call `set_gateway_targets` with verified remote addresses to add to the gateway's paired/known target list. The command merges by default so follow-up batches cannot erase existing remotes; callers must pass `replace: true` only for an intentional full target-list replacement.
- Flasher can scan WiFi from the selected USB gateway with `wifi_scan`, save the gateway STA credentials with `configure_wifi`, and send the same credentials to remotes over LoRa with `provision_fleet_wifi`.
- Flasher can call `identify` to flash the selected USB device LED with the same 3 fast flashes, pause, 3 fast flashes pattern shown in the desktop UI.
- Mutating commands require the current admin password. If that password is lost, use physical erase-and-reflash recovery rather than resetting the password in place.

Example:
- `python3 /Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py --port /dev/cu.usbserial-XXXX --env lrs_za --flash --csv factory_sticker.csv`
- Optional: generate deployment key per batch automatically by passing `--batch-id <YYMMDD>` (or explicit `--deployment-key <value>`).

## What the Script Produces
A CSV row with:
- `serial`
- `chip_id`
- `mac`
- `factory_role`
- `factory_local_address`
- `factory_remote_address`
- `factory_ap_ssid`
- `factory_ap_password`
- `factory_admin_password`
- `deployment_key`
- `deployment_key_is_default`
- `batch_id`
- `region_env`
- `date`

## Current Default Policy
- Factory role: TX
- Addresses:
  - TX/GW defaults to local `254` with first remote target `1`
  - RX units are assigned from `1` upwards during fleet provisioning (`1..32` address range; 12 remotes per ESP8266 gateway currently)
- AP/admin password: deterministic by chip ID + product secret
- AP SSID: `lrs-<chipid>` (role-independent so TX/RX changes do not force AP SSID changes)
- Deployment key: generated as readable three-word key per batch run unless explicitly provided

## Commissioning Modes and Roles
Commissioning enforces canonical mode/role pairs:
- `standalone` mode -> role `none`
- `paired` mode -> roles `transmitter` / `receiver`

Notes:
- Runtime still uses `role_tx` internally (`true` = transmitter behavior, `false` = receiver behavior).
- `input_control_paired_lora_enabled` is valid only for paired TX.
- Current LoRa provisioning apply path configures `mode=paired` with role `transmitter` or `receiver`.

## Pair Provisioning (Flasher)
For a TX/RX pair:
1. Open Flasher > Provision.
2. Select the USB gateway/TX device and provision it with the fleet key, gateway role, WiFi, and local address.
3. Scan for powered factory remotes and provision selected remotes.
4. Flasher assigns inverse addressing:
- TX local=`A`, remote=`B`
- RX local=`B`, remote=`A`
5. Validate ACK behavior and relay mirror.

## Fleet Provisioning Over Serial Admin
Flasher uses the selected USB TX/gateway and `LRS:` serial admin commands:
- `configure_gateway`
- `start_discovery`
- `provisioning_status`
- `provision_all`
- `cancel_provisioning`
- `provision_fleet_wifi`

Discovery is explicit and bounded to the supported ESP8266 remote count. The gateway owns peer truth; Flasher reads serial-admin status/cache data and triggers LoRa probes only when the operator asks.

## Planned Improvement
Add explicit pair mode in factory script:
- Scan/flash first unit -> assign TX + address pair.
- Scan/flash second unit -> assign RX + inverse addresses.
- Output two linked sticker entries with same pair ID.

## Briefing Template for New Developer/Codex
When handing over, include:
1. Repo path and active branch.
2. Hardware and region target (`lrs_za` or `lrs_us`).
3. Build command and current compile status.
4. Device role/address policy in production.
5. Security policy (password derivation + secret handling).
6. Required deliverable for this cycle (bugfix, feature, release prep).

Suggested handoff block:
- Product: LRS ESP8266 LoRa relay pair firmware
- Build target: `<lrs_za|lrs_us>`
- Hardware: ESP-12F + `<Ra-01|Ra-01H>`
- Current state: state-machine runtime under `src/app.*`
- Config path: LittleFS `/config.json`
- Local admin: Flasher over USB serial admin (`status`, `get_config`, `set_config`, `wifi_scan`, `factory_reset`, Fleet/Monitor commands)
- Priority task: `<describe task>`
- Constraints: pre-release firmware; favor clean/small implementation over backward-compat layers.
