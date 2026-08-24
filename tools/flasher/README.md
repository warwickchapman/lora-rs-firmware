# Thanda LoRa Flasher

Desktop utility for LRS firmware updates, commissioning, and maintenance.

## Modes

### Flash

Flash mode is the USB firmware and local maintenance workflow:

- auto-detect serial ports
- select release firmware or choose a local `.bin`
- flash over USB serial
- optionally start serial monitoring immediately after flashing
- remember the last monitor-after-flash and erase-before-flash checkbox states
- read and copy device identity/factory-password details

### Settings

Settings is an explicit, one-device maintenance surface. Its target is independent
of the Fleet/Monitor Gateway Session, so an operator can choose either a USB device
or a gateway discovered through the shared MQTT broker without changing the active
operational gateway:

- load status and redacted configuration through USB serial admin or authenticated MQTT admin
- edit local role/address, LoRa timing/failsafe/debug settings, WiFi STA/AP/static-IP settings, MQTT client/control settings, and DS18B20 settings
- leave secret fields blank to preserve existing WiFi, MQTT, fleet, or admin secrets
- identify, reboot, or guarded factory-reset the selected device

### Fleet

Fleet mode is a LoRa/MQTT admin helper:

- shares one persistent Gateway Session with Monitor, with an explicit USB Serial Gateway, Remote MQTT Broker, or Local MQTT Broker transport and gateway target
- opens that Gateway Session from the compact Connections control in the application header instead of reserving vertical Fleet/Monitor space; its labelled **S** (Serial) and **M** (MQTT) tags show both transport states, with the selected transport bold and an available background transport muted
- automatically closes the Connections popover when a newly selected gateway becomes active, but leaves it open on failures and while connection settings are being edited
- keeps broker connection state separate from the selected operational transport, allowing MQTT Settings work to remain connected while Fleet/Monitor uses USB serial
- immediately connects with the saved parameters when Remote MQTT Broker is selected, or starts and connects the local service when Local MQTT Broker is selected; Connection settings is the fallback for errors or edits
- applies that same automatic activation when a saved Remote or Local MQTT selection is restored at startup, after broker event listeners and current backend state are hydrated
- remembers the operational MQTT gateway separately for each broker host, port, topic root, and username; restores the matching target on reconnect, selects a sole discovered gateway automatically when the saved target is absent, and requires an explicit choice when several unmatched gateways are available
- changes gateway context without probing unused LoRa addresses or loading diagnostics; configured-peer inventory refresh begins only while Fleet or Monitor is observed
- reads the selected gateway's existing peer cache when the Fleet session becomes usable, then refreshes one configured peer every ten seconds through a shared low-priority cursor
- loads the selected gateway, including the factory-derived or entered admin password needed for Fleet actions
- keeps the gateway visible as its own Fleet panel with load, identify, and USB flash actions, while remote counts remain remote-only
- reads the TX/gateway-owned peer cache over serial admin
- keeps address-range discovery separate from normal Fleet refresh; explicit unlisted-device discovery belongs in Provision/recovery
- shows cached remotes in a dense fleet table with LoRa address, chip ID, firmware version, role/mode, WiFi state/IP, MQTT state, link RSSI/freshness, and OTA eligibility
- starts a temporary local firmware file server automatically when an OTA needs the selected release/local `.bin`, keeps it available across queued/active updates, and stops it 60 seconds after the last update settles
- defaults the shared firmware picker to the repo-local `.pio/build/lrs_433_za/firmware.bin` artifact when that PlatformIO build output exists
- calculates the firmware SHA256 before devices are commanded to pull it and records the digest in the activity log
- shows the active firmware filename and URL in Fleet while serving, and records reachable URLs for the local machine's active LAN interfaces in the activity log
- can trigger a discovered remote's OTA pull over LoRa from the inventory `Flash` action; the gateway sends the temporary firmware server host/port plus SHA256, and the remote downloads `/firmware.bin` over WiFi only after receiving the digest
- can request **Flash LED** from a remote's Actions menu through either a USB or MQTT gateway; the request uses one correlated LoRa transaction with one retry and reports acknowledged, unconfirmed, or unavailable in Power Save
- performs remote factory reset as a correlated save-confirm-reboot transaction; selected remotes run sequentially and unconfirmed devices remain in the gateway for safe retry
- supports OTA firmware upgrades of the gateway itself over MQTT when the Fleet tab is configured in Remote MQTT Broker mode, routing the `ota_pull` command via secure MQTT admin command topics and verifying reconnection
- shows the MQTT `ota_pull` payload shape for online devices; payloads must include both `url` and `sha256`
- routes gateway and remote diagnostic logs through the dedicated **Logs** tab; it owns Flasher's single UDP listener on fixed port `5514`, while Fleet Actions open Logs with the relevant source preselected

The TX/gateway owns the peer runtime cache. While Fleet or Monitor is observed, Flasher reads that cache and requests compact encrypted maintenance status from one configured remote every ten seconds; leaving the views stops this inventory traffic. The cursor never probes unused addresses, catches up in a burst, or outranks control traffic. Online devices can also be commanded through MQTT admin. A USB-connected gateway can forward `ota_pull` and `remote_udp_log_control` over LoRa only to remotes that already have WiFi connectivity/IP eligibility; remotes without WiFi cannot pull firmware or stream UDP logs. Gateway-side UDP logging control is strictly MQTT-only.

### Logs

Logs is an application-level diagnostic session, not Fleet health data. It stores a bounded in-memory set of normalized records, supports focused, stacked, and merged views, plus All/Events/Warnings & Errors filters, Copy, Clear, and JSONL export. Gateway and remote **Actions > View Logs** select the source and request temporary forwarding only when required. Remote forwarding enables are sequential and low priority; relay control, provisioning, OTA, and same-port USB work take priority. Leaving Logs does not stop a session. Stop, expiry, gateway/transport change, and application closure release the listener and request forwarding shutdown where the original authenticated target is still available; otherwise firmware's bounded TTL is the final cleanup. Lost UDP lines are best-effort diagnostics, not proof of an offline device.

### Read-only Codex diagnostics MCP (development)

Flasher owns a local-user-only diagnostic socket while it is running. Its companion exposes bounded OTA captures, timelines, anomalies, and raw host evidence only; it never owns a port/listener or sends a device command.

```toml
[mcp_servers.flasherDiagnostics]
command = "cargo"
args = ["run", "--quiet", "--manifest-path", "/absolute/path/to/lora_rs/tools/flasher/src-tauri/Cargo.toml", "--bin", "flasher_diagnostics_mcp"]
```

Start Flasher first and open a new Codex task after registering the server. The companion fails closed if Flasher is unavailable. Packaging it for release is a separate follow-up.

### Navigation and resource ownership

Changing tabs never cancels Provision, relay control, a queued/active OTA, MQTT connection, or an explicit Logs session. Fleet and Monitor are different: their periodic refresh is an observer lease and intentionally ends when their view closes, so it cannot create background LoRa traffic after the operator leaves the view. The retained cache and completed results remain visible when the operator returns.

| Situation | Result and owner |
| --- | --- |
| Serial Provision, then Fleet or Logs | Provision continues. Fleet observation stays separate; Logs reports the USB port as unavailable until the serial coordinator releases it. |
| MQTT Provision, then Fleet or Logs | Provision continues through per-gateway MQTT command ordering. UDP capture can run, but forwarding enable waits behind transactional work. |
| Logging, then control or OTA | Control and transactional work take priority. Serial logging yields the same USB port; UDP capture is passive and remote enables remain low priority. |
| Gateway/transport changes | Existing Gateway Session guards reject changes during OTA, scans, admin work, or connection setup. Active forwarding is stopped against its recorded target rather than silently retargeted. |
| Leave Logs | Logging continues until Stop, TTL expiry, gateway/transport change, or application exit. |
| Application exit | The local UDP listener is stopped and Flasher makes a best-effort forwarding disable; firmware TTL is the final safety boundary. |
| Same USB port | `SerialPortCoordinator` owns one port at a time. Flash, provisioning, device-info, and serial-admin commands stop the monitor before acquiring it. |
| Serial and MQTT, different targets | They may proceed concurrently; they have independent coordinators. |

### Monitor

Monitor uses the same Gateway Session and selected gateway as Fleet. Its refresh
loop starts automatically when the tab is observed and stops when the operator
leaves it or the gateway context becomes unavailable. Refresh work remains
low-priority and interruptible. Broker settings are available from the Gateway
Session instead of being owned by the Monitor tab.

## Provision foundation

New firmware exposes a USB serial admin protocol for Flasher-driven pairing:

- commands are newline-delimited JSON prefixed with `LRS:`
- the generic Tauri command is `serial_admin_command`
- the selected USB device is configured as the TX/gateway
- remotes are discovered and provisioned over LoRa by that selected gateway
- gateway target additions should be written with `set_gateway_targets`; the firmware merges by default, and destructive full-list replacement requires `replace: true`
- Provision and Flash include an Identify LED action, shown only after the selected port confirms LRS serial-admin support, that triggers the device's 3 fast flashes, pause, 3 fast flashes pattern and animates the same pattern in the app

Provision can also target a gateway through the shared MQTT broker connection. Its
MQTT broker badge and Broker settings button remain visible on both the Pair and
WiFi views. MQTT gateway loading, LoRa discovery, and WiFi scanning stay disabled
until the broker is connected; connecting here does not change the Fleet/Monitor
Gateway Session target.

Flasher coordinates USB-port ownership between flashing, device-info reads, serial monitoring, and Provision. Switching away from Flash stops the serial monitor so Provision can take the selected gateway port cleanly. Flash and Provision use one shared serial device state per selected USB port: device details, serial-admin support/status/config, gateway WiFi state, and scanned WiFi networks all live in that per-port record until the port is unplugged. Provision WiFi reads the selected gateway status before scanning; if the gateway is already connected to WiFi, the app shows it as connected without asking the operator to scan or connect again.

The app restores the last active tab on launch. The main tabs are ordered Flash, Provision, Fleet, Monitor, Logs, and Settings.
The header Connections control is contextual: Fleet and Monitor edit their shared Gateway Session there, while Flash, Provision, and Settings show a compact connection summary and retain their explicit target selector beside the operation to reduce wrong-device mistakes.

## Remote OTA Handoff & Watchdog Design
Remote OTA updates serialize only the short LoRa manifest handoff. As soon as a remote acknowledges its manifest, Flasher starts the next queued handoff while accepted remotes download, reboot, and confirm independently:
- **Handoff Stages**: Transitions are driven by matching the destination address and transfer ID: `ota_sending` -> `ota_awaiting_ack` -> `ota_downloading`. The remote sends its accepted status only after the gateway's receive-turnaround guard. Non-matching status reports are ignored.
- **Reboot & Confirmation**: Upon handoff completion, the RF slot is released and the row continues independently. After a 45-second reboot delay window, Flasher paces targeted `refresh_lora_peer` requests one at a time. Successful update requires the expected version and a remote uptime lower than the pre-upgrade baseline.
