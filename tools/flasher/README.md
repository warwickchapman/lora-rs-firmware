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
- opens that Gateway Session from the compact Connections control in the application header instead of reserving vertical Fleet/Monitor space; the control separately indicates the selected task target and shared MQTT broker state
- automatically closes the Connections popover when a newly selected gateway becomes active, but leaves it open on failures and while connection settings are being edited
- keeps broker connection state separate from the selected operational transport, allowing MQTT Settings work to remain connected while Fleet/Monitor uses USB serial
- immediately connects with the saved parameters when Remote MQTT Broker is selected, or starts and connects the local service when Local MQTT Broker is selected; Connection settings is the fallback for errors or edits
- changes gateway context without automatically scanning LoRa or loading diagnostics; those observability actions remain operator initiated
- loads the selected gateway, including the factory-derived or entered admin password needed for Fleet actions
- keeps the gateway visible as its own Fleet panel with load, identify, and USB flash actions, while remote counts remain remote-only
- reads the TX/gateway-owned peer cache over serial admin
- scans the supported LRS remote range through the gateway with explicit `start_lora_inventory` operator action
- refuses Fleet scans from factory-default gateways until Provision has commissioned the gateway with a secure fleet key
- shows cached remotes in a dense fleet table with LoRa address, chip ID, firmware version, role/mode, WiFi state/IP, MQTT state, link RSSI/freshness, and OTA eligibility
- starts a temporary local firmware file server from the selected release/local `.bin`
- defaults the shared firmware picker to the repo-local `.pio/build/lrs_za/firmware.bin` artifact when that PlatformIO build output exists
- calculates and displays the firmware SHA256 before devices are commanded to pull it
- shows reachable firmware URLs for the local machine's active LAN interfaces
- can trigger a discovered remote's OTA pull over LoRa from the inventory `Flash` action; the gateway sends the temporary firmware server host/port plus SHA256, and the remote downloads `/firmware.bin` over WiFi only after receiving the digest
- performs remote factory reset as a correlated save-confirm-reboot transaction; selected remotes run sequentially and unconfirmed devices remain in the gateway for safe retry
- supports OTA firmware upgrades of the gateway itself over MQTT when the Fleet tab is configured in Remote MQTT Broker mode, routing the `ota_pull` command via secure MQTT admin command topics and verifying reconnection
- shows the MQTT `ota_pull` payload shape for online devices; payloads must include both `url` and `sha256`
- listens for UDP logs on fixed port `5514` with a dedicated Start/Stop UDP Listener workflow
- supports enabling UDP log mirroring on the gateway over MQTT, and targeted remote nodes over serial or MQTT gateway command bridge

The TX/gateway owns the serial-mode peer runtime cache; Flasher reads that cache and only starts LoRa probing when the operator clicks Scan Fleet. Peer state is updated from compact encrypted LoRa maintenance-status responses and does not expose secrets. Online devices can also be commanded through MQTT admin. A USB-connected gateway can forward `ota_pull` and `remote_udp_log_control` over LoRa only to remotes that already have WiFi connectivity/IP eligibility; remotes without WiFi cannot pull firmware or stream UDP logs. Gateway-side UDP logging control is strictly MQTT-only.

### Monitor

Monitor uses the same Gateway Session and selected gateway as Fleet. Its refresh
loop starts only when the operator clicks Monitor and stops when the gateway context
changes. Broker settings are available from the Gateway Session instead of being
owned by the Monitor tab.

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

The app restores the last active tab on launch. The main tabs are ordered Flash, Provision, Fleet, Monitor, and Settings.
The header Connections control is contextual: Fleet and Monitor edit their shared Gateway Session there, while Flash, Provision, and Settings show a compact connection summary and retain their explicit target selector beside the operation to reduce wrong-device mistakes.

## Remote OTA Handoff & Watchdog Design
Remote OTA updates serialize only the short LoRa manifest handoff. As soon as a remote acknowledges its manifest, Flasher starts the next queued handoff while accepted remotes download, reboot, and confirm independently:
- **Handoff Stages**: Transitions are driven by matching the destination address and transfer ID: `ota_sending` -> `ota_awaiting_ack` -> `ota_downloading`. The remote sends its accepted status only after the gateway's receive-turnaround guard. Non-matching status reports are ignored.
- **Reboot & Confirmation**: Upon handoff completion, the RF slot is released and the row continues independently. After a 45-second reboot delay window, Flasher paces targeted `refresh_lora_peer` requests one at a time. Successful update requires the expected version and a remote uptime lower than the pre-upgrade baseline.
