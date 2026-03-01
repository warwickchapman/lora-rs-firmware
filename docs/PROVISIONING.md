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

Example:
- `python3 /Users/warwick/Code/LoRa/lora_rs/tools/factory_provision.py --port /dev/cu.usbserial-XXXX --env lrs_za --flash --csv factory_sticker.csv`
- Optional: generate deployment key per batch automatically by passing `--batch-id <YYMMDD>` (or explicit `--deployment-key <value>`).

Example (API-first coordinator workflow):
- `python3 /Users/warwick/Code/LoRa/lora_rs/tools/lrs_provisioning_cli.py run --host 192.168.4.1 --admin-password <pwd> --fleet-key <key> --role tx --local-address 1 --remote-address 2`

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
- Addresses: deterministic by chip ID
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
- `POST /api/network/provision-fleet` (broadcast STA WiFi credentials over LoRa)

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
- Constraints: maintain protocol compatibility unless explicitly approved
