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

Settings is the local USB maintenance surface for a selected device. It uses the
`LRS:` serial-admin protocol rather than the on-device Web UI:

- load status and redacted configuration from a USB-connected LRS device
- edit local role/address, LoRa timing/failsafe/debug settings, WiFi STA/AP/static-IP settings, MQTT client/control settings, and DS18B20 settings
- leave secret fields blank to preserve existing WiFi, MQTT, fleet, or admin secrets
- identify, reboot, or guarded factory-reset the selected device

### Fleet

Fleet mode is now a LoRa/MQTT admin helper, not a device-side Web UI client:

- loads a selected USB-connected LRS device as the LoRa gateway, including the factory-derived admin password needed for Fleet actions
- keeps the gateway visible as its own Fleet panel with load, identify, and USB flash actions, while remote counts remain remote-only
- reads the TX/gateway-owned peer cache over serial admin
- scans the supported LRS remote range through the gateway with explicit `start_lora_inventory` operator action
- refuses Fleet scans from factory-default gateways until Provision has commissioned the gateway with a secure fleet key
- shows cached remotes in a dense fleet table with LoRa address, chip ID, firmware version, role/mode, WiFi state/IP, MQTT state, link RSSI/freshness, and OTA eligibility
- starts a temporary local firmware file server from the selected release/local `.bin`
- defaults the shared firmware picker to the repo-local `.pio/build/lrs_za/firmware.bin` artifact when that PlatformIO build output exists
- calculates and displays the firmware SHA256 before devices are commanded to pull it
- shows reachable firmware URLs for the local machine's active LAN interfaces
- can trigger a discovered remote's OTA pull over LoRa from the inventory `Flash` action; the gateway sends the temporary firmware server host/port and the remote downloads `/firmware.bin` over WiFi
- shows the MQTT `ota_pull` payload shape for online devices
- listens for UDP logs on fixed port `5514`

Fleet mode intentionally does not scan devices by HTTP, open device Web UIs, log into REST endpoints, or upload firmware through `/api/ota`. The TX/gateway owns the serial-mode peer runtime cache; Flasher reads that cache and only starts LoRa probing when the operator clicks Scan Fleet. Peer state is updated from compact encrypted LoRa maintenance-status responses and does not expose secrets. Online devices can still be commanded through MQTT admin. A USB-connected gateway can forward `udp_log_control` and `ota_pull` over LoRa only to remotes that already have WiFi connectivity; remotes without WiFi cannot emit UDP logs or pull firmware.

## Provision foundation

New firmware exposes a USB serial admin protocol for Flasher-driven pairing:

- commands are newline-delimited JSON prefixed with `LRS:`
- the generic Tauri command is `serial_admin_command`
- the selected USB device is configured as the TX/gateway
- remotes are discovered and provisioned over LoRa by that selected gateway
- final gateway target lists should be written with `set_gateway_targets`
- Provision and Flash include an Identify LED action, shown only after the selected port confirms LRS serial-admin support, that triggers the device's 3 fast flashes, pause, 3 fast flashes pattern and animates the same pattern in the app

Flasher coordinates USB-port ownership between flashing, device-info reads, serial monitoring, and Provision. Switching away from Flash stops the serial monitor so Provision can take the selected gateway port cleanly. Flash and Provision use one shared serial device state per selected USB port: device details, serial-admin support/status/config, gateway WiFi state, and scanned WiFi networks all live in that per-port record until the port is unplugged. Provision WiFi reads the selected gateway status before scanning; if the gateway is already connected to WiFi, the app shows it as connected without asking the operator to scan or connect again.

The app restores the last active tab on launch. The main tabs are ordered Flash, Provision, Fleet, Monitor, and Settings.
