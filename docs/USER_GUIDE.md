# User Guide

## What LRS Does
LRS extends dry-contact control over LoRa between ESP8266-based devices.

Operating modes:
- `Standalone`: one device works locally only. The input and relay are not split across LoRa peers.
- `Paired`: a `transmitter` reads the local input and sends control updates to a `receiver`. The receiver applies the relay state and returns ACK.

In paired-style control, the transmitter relay mirrors ACK-confirmed remote state after a short delay.

## Operator Tool
Use the desktop Flasher app for local setup and maintenance:

- Flash firmware over USB serial.
- Provision a TX/gateway and remotes.
- Observe a progressively refreshed Fleet through a USB or MQTT gateway.
- Monitor gateway/remote health.
- Edit local device Settings over USB serial admin.
- Trigger OTA pull for WiFi-connected devices from Fleet.

Normal local maintenance is performed with Flasher over USB serial.

## First Setup
1. Open Flasher.
2. Connect the device over USB serial.
3. In `Flash`, read identity and flash the target firmware if needed.
4. Choose the operating mode for the installation:
   - For a simple local-only device, set `Standalone` / `none` in Settings.
   - For one transmitter and one or more receivers, provision the TX/gateway first, then remotes.
5. In `Provision`, configure the TX/gateway with fleet key, role, address, and WiFi.
6. Power factory remotes, scan from the gateway, and provision selected devices.
7. Open `Fleet` and verify that configured remotes appear immediately and progressively report current identity/status.
8. In `Monitor`, confirm relay, input, WiFi, MQTT, heap, and link status.

Recommended defaults:
- TX/gateway local address: `254`
- Remote addresses: start at `1`
- Heartbeat: `60 s`
- ACK timeout: `5 s`
- TX command retry timeout: `180 s`
- RX fail-safe mode: `hold_last`
- RX fail-safe timeout: `180 s`
- Scheduled remote polling: disabled unless a specific diagnostic workflow needs it

## Settings
Use `Settings` in Flasher with a USB-connected device.

- `General`: role, addresses, and fleet identity.
- `Control`: choose the gateway's single relay-control source: MQTT or gateway dry-contact input. Remote devices expose only their controller-loss failsafe here.
- `Network`: WiFi SSID/password, hostname, TX power, static IP, fallback AP policy, and optional listen-only power save.
- `MQTT`: gateway broker connection, topic root, credentials, and optional low-priority remote-state refresh.
- `Sensors`: DS18B20 reporting and 4-20 mA tank level enablement.
- `System`: save config, reboot, guarded factory reset.

Secret fields are redacted on fetch. Leave password fields blank to preserve the stored value.

### PowerSave Mode
`power_save_listen_only` is for remote devices that should spend their time in low-power sleep mode with the least possible background activity.

When enabled, the device immediately transitions to deep sleeping listen-only mode on boot. In this state, local WiFi access, Serial Admin, MQTT, OTA, UDP logs, status LEDs, and background sensor polling are completely disabled to conserve power.

Recovery is performed remotely over LoRa. To wake a node back to Full Power, send the `Disable PowerSave` command from the gateway via the Flasher UI. This restarts all normal networking, Serial, and OTA services. Local WiFi/Serial access is intentionally unavailable while PowerSave is active.

## Fleet
Fleet uses a USB-connected TX/gateway as the source of truth.

- Fleet reads the gateway-owned peer cache immediately and refreshes one configured remote at a time while the view remains open.
- The gateway row is separate from remote rows.
- Remote rows show identity, firmware, role, WiFi/IP, MQTT, sensors, uptime, RSSI, age, and OTA eligibility.
- `Flash` on a remote row triggers OTA pull over WiFi; firmware bytes are not carried over LoRa. Flasher starts its temporary firmware server automatically and stops it 60 seconds after the final update settles.
- Remote factory reset is confirmed only after the remote saves its new configuration. A full reset removes the row automatically after confirmation; an unconfirmed reset retains the row with a Retry-safe warning. Selected remotes are processed one at a time and the sequence stops at the first unconfirmed result.
- Leaving **Keep WiFi Credentials** unchecked clears both the application credentials and the ESP8266 SDK station profile. **Reset but keep in fleet** preserves the fleet key, remote role, assigned address, and controller address so the remote remains reachable; it does not preserve WiFi.

## Monitor
Monitor is for gateway diagnostics over USB serial.

- It requires a TX/gateway USB device.
- It shows gateway health and the gateway peer cache, including remote input,
  temperature, and tank telemetry when reported.

## Logs

Use **Fleet > Actions > View Logs** for a gateway or WiFi-connected remote. Logs can focus one source, stack selected sources, or merge them by receive time. Use filters to show all lines, parsed events, or warnings/errors, then Copy, Clear, or export the bounded session as JSONL. USB gateway logs use the existing coordinated serial stream; MQTT gateway and remote logs use temporary UDP forwarding. UDP is best-effort: a missing line must never be treated as device health, relay state, input state, or a failed command. Leaving Logs does not stop capture; use **Stop forwarding** when finished.

## Gateway Session Connection & Local Broker
Transport communication across Fleet, Monitor, and Settings is unified under the global **Gateway Session Connection** banner at the top of the interface:
- **USB Serial Gateway**: Direct USB-connected gateway node.
- **MQTT**: Uses an external cloud or network MQTT broker.
- **Local MQTT Broker**: Starts an embedded `rumqttd` broker inside Flasher. Default port is `1883`. Selecting this option automatically starts the local broker (if not already running) and connects Flasher's MQTT client to it. Once started, the broker runs persistently until Flasher exits. If the default port is busy, the Configure Session Connection panel will automatically open to display the error, allowing you to select an alternative port before retrying. You can copy the host settings and manually write them to your gateway device over USB.

> [!TIP]
> **MQTT Explorer Troubleshooting**:
> If you are using MQTT Explorer to connect to the local `rumqttd` broker, do not subscribe to wildcard topic patterns containing `$SYS` (e.g., remove `$SYS/#` from the default subscription list). Instead, subscribe explicitly to the root topic, e.g., `lora/#`.

Configure MQTT parameters on a gateway under Settings:
- Select **MQTT** on the Control tab. This enables the MQTT client and MQTT relay control, and disables gateway-input control.
- Select **Gateway input** on the Control tab to disable MQTT relay control while leaving the MQTT client available for retained status telemetry.
- Set broker host, port, topic root, and credentials as needed.
- Optionally enable Remote refresh and choose its interval when confirmed remote relay/input state is needed without an explicit control command. This polling is low priority and does not compete with relay control.

Only gateways run MQTT clients. Remote nodes report and receive control through their paired gateway over LoRa.

Discovery topic:
- `<root>/discovery/lrs-<chipid>`

Per-device topics include:
- `<root>/lrs-<chipid>/input`
- `<root>/lrs-<chipid>/relay`
- `<root>/lrs-<chipid>/sensor/<kind>/<instance>/value`
- `<root>/lrs-<chipid>/sensor/<kind>/<instance>/state`
- `<root>/lrs-<chipid>/addr`
- `<root>/lrs-<chipid>/control`

Gateway peer topics use `<root>/lrs-<tx_chipid>/peers/<NN_lrs-peer_chipid>/...` (where `NN` is the two-digit decimal address, and `peer_chipid` is the hexadecimal chip ID of the remote peer) with matching
sensor leaves such as `input`, `sensor/<kind>/<instance>/value`, and `sensor/<kind>/<instance>/state`.

## Recovery
USB serial is the guaranteed local maintenance path.

If a device cannot be reached remotely:
1. Connect USB serial.
2. Use Flasher `Flash` to read identity/status.
3. Use `Settings` to inspect or correct config.
4. Reflash over USB if config is unknown or the admin password is lost.
