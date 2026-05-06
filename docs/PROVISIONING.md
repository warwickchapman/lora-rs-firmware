# Provisioning and Factory Flow

## Goals
- Flash firmware for target region.
- Capture deterministic device metadata for sticker/traceability.
- Ensure first field login is predictable.
- Commission mode/role and fleet key correctly on first login.

## Tooling
Factory script:
- `/Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py`

API-first provisioning helper:
- `/Users/warwick/Code/LoRa/lora_rs/tools/lrs_provisioning_cli.py`

Firmware USB serial admin protocol:
- Flasher-facing commands are newline-delimited JSON prefixed with `LRS:`.
- The selected USB-connected device can be configured as the TX/gateway with `configure_gateway`.
- The gateway can then run LoRa discovery/provisioning through `start_discovery`, `provisioning_status`, `provision_all`, and `cancel_provisioning`.
- `expected_remotes` is scan capacity only. `configure_gateway` must not leave speculative runtime targets behind.
- After provisioning, Flasher should call `set_gateway_targets` with only verified remote addresses that should remain in the gateway's paired/known target list.
- Flasher can scan WiFi from the selected USB gateway with `wifi_scan`, save the gateway STA credentials with `configure_wifi`, and send the same credentials to remotes over LoRa with `provision_fleet_wifi`.
- Flasher can call `identify` to flash the selected USB device LED with the same 3 fast flashes, pause, 3 fast flashes pattern shown in the desktop UI.
- Mutating commands require the current admin password. If that password is lost, use physical erase-and-reflash recovery rather than resetting the password in place.

Example:
- `python3 /Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py --port /dev/cu.usbserial-XXXX --env lrs_za --flash --csv factory_sticker.csv`
- Optional: generate deployment key per batch automatically by passing `--batch-id <YYMMDD>` (or explicit `--deployment-key <value>`).

Example (API-first coordinator workflow):
- `python3 /Users/warwick/Code/LoRa/lora_rs/tools/lrs_provisioning_cli.py run --host 192.168.4.1 --admin-password <pwd> --fleet-key <key> --role tx --local-address 254 --remote-address 1`

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
- `mesh` mode -> roles `coordinator` / `node`

Notes:
- Runtime still uses `role_tx` internally (`true` = transmitter/coordinator behavior, `false` = receiver/node behavior).
- `input_control_paired_lora_enabled` is valid only for paired TX.
- Current LoRa provisioning apply path configures `mode=paired` with role `transmitter` or `receiver`.

## Pair Provisioning (Current Manual)
For a TX/RX pair:
1. Provision device A as TX.
2. Provision device B as RX in web console.
3. Set addresses as inverse pair:
- TX local=`A`, remote=`B`
- RX local=`B`, remote=`A`
4. Set identical fleet passphrase and radio parameters.
5. Validate ACK behavior and relay mirror.

## Fleet Provisioning APIs (Coordinator/TX)
Provisioning endpoints in current firmware:
- `POST /api/provisioning/start`
- `GET /api/provisioning/status`
- `POST /api/provisioning/provision-all`
- `POST /api/provisioning/cancel`
- `POST /api/network/provision-fleet` (broadcast or targeted STA WiFi credentials over LoRa)

`/api/network/provision-fleet` request fields:
- `wifi_sta_ssid` (optional override; defaults to stored STA SSID)
- `wifi_sta_password` (optional override; defaults to stored STA password)
- `target_address` (optional; `1..254`; default `255` for broadcast)

Web Console operator cues (Fleet -> Manage -> LoRa):
- Provisioning results table uses explicit addressing/version labels: `Cur Addr`, `New Addr`, `FW Ver`.
- `Provision All` now shows an inline reason whenever it is disabled (for example: discovery still running, no discovered devices, provisioning already in progress).
- A compact session line is shown during active sessions with phase/progress and elapsed time (for example: `Discovering...`, `Verified x/y`, `Provisioned x/y`, `elapsed mm:ss`).
- Discovery reliability: factory-key targets now transmit two announce frames per discover command (short jitter before the second frame). Coordinator device list remains deduped by `chip_id`.
- Discovery timing model is two-phase: coordinator sends a short `DiscoverStart` burst first, then remains silent while targets reply on randomized jitter within the declared reply window.
- Discovery is now single-pass (no automatic retry cycle). If another scan is desired, the operator explicitly presses `Start Discovery` again.
- Discovery start prompts for expected device count (`1..12` on ESP8266) and stops on expected count reached or `120s`.
- During discovery/readiness UI updates, discovered rows are sticky by `chip_id` and remain visible until `Provision All` is started (or session is cancelled).
- Address auto-assignment for provisioning is constrained to `1..32`.
- If `verify` is missed after apply, coordinator performs a fleet-key probe on the assigned address before final classification; status may show `applied_unconfirmed` when apply likely succeeded but confirmation was not observed.
- Provisioning status responses always include row data (no low-memory count-only mode).

Mode gating:
- Fleet APIs are blocked in `standalone` mode (`fleet_disabled_in_standalone`).

CLI mirrors:
- `run`, `status`, `cancel`
- `udp-log-start`, `udp-log-stop` (temporary UDP log mirror while provisioning/debugging)

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
- Web API: `/api/status-lite`, `/api/settings`, `/api/factory`, `/api/wifi/scan`, `/api/logs.csv`
- Priority task: `<describe task>`
- Constraints: pre-release firmware; favor clean/small implementation over backward-compat layers.
