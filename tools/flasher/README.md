# Thanda LoRa Flasher

Desktop utility for LRS firmware updates, commissioning, and maintenance.

## Modes

### Serial

Serial mode is the original guided workflow:

- auto-detect serial ports
- select release firmware or choose a local `.bin`
- flash over USB serial
- optionally start serial monitoring immediately after flashing
- remember the last monitor-after-flash and erase-before-flash checkbox states
- read and copy device identity/factory-password details

### Fleet

Fleet mode is now a LoRa/MQTT admin helper, not a device-side Web UI client:

- loads a selected USB-connected LRS device as the LoRa gateway, including the factory-derived admin password needed for Fleet actions
- scans the supported LRS remote range through the gateway with `start_lora_inventory`
- refuses Fleet scans from factory-default gateways until Pair Devices has commissioned the gateway with a secure fleet key
- shows discovered remotes in a dense inventory table with LoRa address, role/mode, WiFi state, link RSSI/freshness, and OTA eligibility
- starts a temporary local firmware file server from the selected release/local `.bin`
- calculates and displays the firmware SHA256 before devices are commanded to pull it
- shows reachable firmware URLs for the local machine's active LAN interfaces
- can trigger a discovered remote's OTA pull over LoRa from the inventory `Flash` action; the gateway sends the temporary firmware server host/port and the remote downloads `/firmware.bin` over WiFi
- shows the MQTT `ota_pull` payload shape for online devices
- listens for UDP logs on fixed port `5514`

Fleet mode intentionally does not scan devices by HTTP, open device Web UIs, log into REST endpoints, or upload firmware through `/api/ota`. The current inventory table uses the compact encrypted LoRa poll response, so firmware version, chip ID, IP address, and MQTT state remain `unsupported`/`unknown` until the bounded maintenance-status response is added. Online devices can still be commanded through MQTT admin. A USB-connected gateway can forward `udp_log_control` and `ota_pull` over LoRa only to remotes that already have WiFi connectivity; remotes without WiFi cannot emit UDP logs or pull firmware.

## EasyPair foundation

New firmware exposes a USB serial admin protocol for Flasher-driven pairing:

- commands are newline-delimited JSON prefixed with `LRS:`
- the generic Tauri command is `serial_admin_command`
- the selected USB device is configured as the TX/gateway
- remotes are discovered and provisioned over LoRa by that selected gateway
- final gateway target lists should be written with `set_gateway_targets`
- Pair Devices and USB Cable include an Identify LED action, shown only after the selected port confirms LRS serial-admin support, that triggers the device's 3 fast flashes, pause, 3 fast flashes pattern and animates the same pattern in the app

Flasher coordinates USB-port ownership between flashing, device-info reads, serial monitoring, and EasyPair. Switching away from USB Cable stops the serial monitor so Pair Devices can take the selected gateway port cleanly. USB Cable and Pair Devices use one shared serial device state per selected USB port: device details, serial-admin support/status/config, gateway WiFi state, and scanned WiFi networks all live in that per-port record until the port is unplugged. Pair Devices WiFi reads the selected gateway status before scanning; if the gateway is already connected to WiFi, the app shows it as connected without asking the operator to scan or connect again.

The app opens on Pair Devices. The existing Serial and Fleet tools remain available as USB Cable and Fleet maintenance modes for flashing, local admin, firmware serving, and log viewing.
