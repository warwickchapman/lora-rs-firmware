# Thanda LoRa Flasher

Desktop utility for LRS firmware updates and logs.

## Modes

### Serial

Serial mode is the original guided workflow:

- auto-detect serial ports
- select release firmware or choose a local `.bin`
- flash over USB serial
- optionally start serial monitoring immediately after flashing
- remember the last monitor-after-flash and erase-before-flash checkbox states
- read and copy device identity/factory-password details

### Network

Network mode handles one LRS device at a time over the current LAN:

- discovers LRS devices by scanning IPv4 ranges derived from the local network adapter netmasks
- lists devices as they are found during the scan, while the final scan result reconciles the complete set
- identifies devices by direct HTTP probing only; mDNS and `lrs-*.local` hostnames are not used
- accepts both current login pages with device identity metadata and older LRS login pages that require manual password entry before version details are available
- tries the derived factory/admin password from the device identity where available
- allows a per-device admin password override when the default password is no longer valid
- remembers a successfully tested non-factory admin password for that device and prefers it over the derived password on future scans
- shows authenticated firmware version/build details when login succeeds
- keeps per-device actions in the discovered-device row: direct Web UI opening, password check, OTA upload, and UDP logs
- listens for UDP logs on fixed port `5514` after OTA or on demand, renewing the device-side UDP logging lease while monitoring is active, with the expanded log view retaining copy-log, copy-password, open-Web-UI, and stop controls

Network mode intentionally does not include bulk OTA yet. The UI can enumerate multiple devices, but update and UDP-log actions target the selected device only.

## EasyPair foundation

New firmware exposes a USB serial admin protocol for Flasher-driven pairing:

- commands are newline-delimited JSON prefixed with `LRS:`
- the generic Tauri command is `serial_admin_command`
- the selected USB device is configured as the TX/gateway
- remotes are discovered and provisioned over LoRa by that selected gateway
- final gateway target lists should be written with `set_gateway_targets`
- Pair Devices and USB Cable include an Identify LED action, shown only after the selected port confirms LRS serial-admin support, that triggers the device's 3 fast flashes, pause, 3 fast flashes pattern and animates the same pattern in the app

Flasher coordinates USB-port ownership between flashing, device-info reads, serial monitoring, and EasyPair. Switching away from USB Cable stops the serial monitor so Pair Devices can take the selected gateway port cleanly. USB Cable and Pair Devices use one shared serial device state per selected USB port: device details, serial-admin support/status/config, gateway WiFi state, and scanned WiFi networks all live in that per-port record until the port is unplugged. Pair Devices WiFi reads the selected gateway status before scanning; if the gateway is already connected to WiFi, the app shows it as connected without asking the operator to scan or connect again.

The app opens on Pair Devices. The existing Serial and Network tools remain available as USB Cable and Network maintenance modes for flashing, OTA, log viewing, password checks, and Web UI access.

Network mode supports one-device OTA and sequential bulk OTA for discovered devices with known admin passwords. Bulk OTA only uploads firmware; it does not start UDP logging after each update, so operators should rescan after the batch to confirm devices returned.
