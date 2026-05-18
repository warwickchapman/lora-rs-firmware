# User Guide

## What LRS Does
LRS extends dry-contact control over LoRa between ESP8266-based devices.

Operating modes:
- `Standalone`: one device works locally only. The input and relay are not split across LoRa peers.
- `Paired`: a `transmitter` reads the local input and sends control updates to a `receiver`. The receiver applies the relay state and returns ACK.
- `Mesh`: uses `coordinator` and `node` role names for gateway-style operation. Internally, the coordinator follows the TX/gateway control path and nodes follow the RX/remote path.

In paired-style control, the transmitter/coordinator relay mirrors ACK-confirmed remote state after a short delay.

## Operator Tool
Use the desktop Flasher app for local setup and maintenance:

- Flash firmware over USB serial.
- Provision a TX/gateway and remotes.
- Scan Fleet inventory through a USB-connected gateway.
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
   - For gateway/node naming, use `Mesh` with a coordinator and nodes.
5. In `Provision`, configure the TX/gateway or coordinator with fleet key, role, address, and WiFi.
6. Power factory remotes/nodes, scan from the gateway, and provision selected devices.
7. In `Fleet`, scan the gateway peer cache and verify remotes appear with current identity/status.
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

- `General`: role/address and LoRa timing/fail-safe controls.
- `Network`: WiFi SSID/password, hostname, PHY/TX power, static IP, fallback AP policy.
- `MQTT`: broker, topic root, MQTT client/control, controller addresses.
- `Sensors`: DS18B20 reporting enablement.
- `System`: save config, reboot, guarded factory reset.

Secret fields are redacted on fetch. Leave password fields blank to preserve the stored value.

## Fleet
Fleet uses a USB-connected TX/gateway as the source of truth.

- `Scan Fleet` reads the gateway-owned peer cache and sends bounded LoRa probes.
- The gateway row is separate from remote rows.
- Remote rows show identity, firmware, role, WiFi/IP, MQTT, uptime, RSSI, age, and OTA eligibility.
- `Flash` on a remote row triggers OTA pull over WiFi; firmware bytes are not carried over LoRa.

## Monitor
Monitor is for gateway diagnostics over USB serial.

- It requires a TX/gateway USB device.
- It shows gateway health and the gateway peer cache.
- It can start temporary UDP logs for WiFi-connected devices.

## MQTT
MQTT is optional and requires STA WiFi.

Configure it in Flasher `Settings > MQTT`:
- Enable MQTT client.
- Set broker host, port, topic root, and credentials as needed.
- Enable MQTT control only when the broker and ACLs are trusted for control traffic.

Discovery topic:
- `<root>/discovery/lrs-<chipid>`

Per-device topics include:
- `<root>/lrs-<chipid>/input`
- `<root>/lrs-<chipid>/dry_contact`
- `<root>/lrs-<chipid>/relay`
- `<root>/lrs-<chipid>/temp_c`
- `<root>/lrs-<chipid>/addr`
- `<root>/lrs-<chipid>/control`

## Recovery
USB serial is the guaranteed local maintenance path.

If a device cannot be reached remotely:
1. Connect USB serial.
2. Use Flasher `Flash` to read identity/status.
3. Use `Settings` to inspect or correct config.
4. Reflash over USB if config is unknown or the admin password is lost.
