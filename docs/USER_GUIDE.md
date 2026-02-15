# User Guide

## What the Device Does
LRS extends a dry-contact control signal over LoRa.

- TX reads local dry-contact input and sends LoRa updates.
- RX applies relay state and returns ACK.
- TX relay mirrors ACK-confirmed remote state (500 ms delayed).

## First Access
1. Power device.
2. Connect to SoftAP SSID: `lrs-<chipid>`.
3. Open `http://192.168.4.1`.
4. Login: `admin` + factory-derived password.

SoftAP captive portal is enabled. On most phones/laptops, joining the device AP will auto-open the login/console page.

## Required Setup
Set these in `LoRa` tab:
- Role (`Transmitter` / `Receiver`)
- Local address
- Remote address
- Deployment Key (Encryption)
- `Input drives LoRa relay control` (shown when role is TX, in Advanced)

`Deployment Key (Encryption)` must be unique per installation to prevent cross-control with nearby systems.
Use at least 16 characters.
Examples:
- `fairview-generator-start-line-alpha42`
- `smith-load-management-south-basin-27`
- `farm-pump-control-west-field-9k`

Recommended defaults:
- Heartbeat: 60 s
- ACK timeout: 5 s
- `Input drives LoRa relay control`: enabled for classic paired dry-contact mode

## Network Setup
In `Network` tab:
- Set STA SSID/password.
- Set LAN hostname (`<name>.local`).
- Choose whether AP remains enabled after STA connects.

## OTA Update Test
Prerequisites:
- Device is already flashed once over USB.
- Device is connected to STA WiFi.
- Your computer is on the same LAN.
- You know the admin password (used for OTA auth).

Steps:
1. In UI `Network`, confirm LAN mDNS (example: `lrs-004a9c27.local`).
2. In `/Users/warwick/Code/LoRa/lora_rs/platformio.ini`, set OTA environment `upload_port` to that hostname.
3. Uncomment and set `upload_flags = --auth=<admin_password>` for that environment.
4. Run upload:
   - ZA: `python3 -m platformio run -e lrs_za_ota -t upload`
   - US: `python3 -m platformio run -e lrs_us_ota -t upload`
5. Verify serial/logs show OTA reboot and normal boot events.

If OTA fails:
- Check STA is connected and has LAN IP.
- Try hostname and IP (`upload_port = <sta_ip>`).
- Ensure firewall allows local UDP/TCP discovery/upload.

## Status Page
Status is split into:
- `LoRa`: role, link state, LoRa RSSI, last packet age
- `WiFi Station`: STA state/IP/RSSI/mDNS
- `Soft AP`: AP SSID/IP/mDNS
- `Sensors`: local DS18B20, remote LoRa temperature, dry-contact OPEN/CLOSED

Header badges show:
- Relay state
- LoRa signal
- WiFi connection state

## MQTT (Optional)
In `MQTT` tab:
- Enable MQTT
- Set broker host/port/user/pass/topic root

MQTT is active only when STA is connected and MQTT is enabled.
MQTT host must also be set.

Per-device topics:
- `<root>/lrs-<chipid>/input`
- `<root>/lrs-<chipid>/dry_contact`
- `<root>/lrs-<chipid>/relay`
- `<root>/lrs-<chipid>/temp_c`
- `<root>/lrs-<chipid>/remote_temp_c`
- `<root>/lrs-<chipid>/type`
- `<root>/lrs-<chipid>/addr`
- `<root>/lrs-<chipid>/control`
- `<root>/lrs-<chipid>/last_updated`

Discovery topic (retained JSON):
- `<root>/discovery/lrs-<chipid>`

Discovery payload includes:
- `serial`, `chip_id`, `role`, `addr`, `remote_addr`
- `mac`, `sta_ip`, `ap_ip`, `sta_ssid`, `lan_mdns`
- `uptime_ms`, `fw`, `fw_version`, `fw_git_sha`, `fw_git_branch`, `fw_dirty`
- `build_date`, `build_time`

`last_updated` carries device uptime milliseconds at last publish.
`relay` and `control` are subscribed topics. Other listed topics are status publishes.

`control` payload typing for `addr`:
- JSON number (`"addr": 40`) means decimal address 40.
- JSON string (`"addr": "0x28"` or `"addr": "28"`) is parsed as hexadecimal.
- Important: `"addr": "40"` is hex `0x40` (decimal 64), not decimal 40.

Examples:
```json
{"addr":40,"relay":1}
{"addr":40,"relay":0}
{"addr":"0x28","relay":1}
{"addr":"28","relay":0}
```

Minimal Node-RED flow example (TX control topic):
```json
[
  {
    "id": "inject_dec_on",
    "type": "inject",
    "name": "Unit 40 ON (decimal)",
    "topic": "lora/lrs-00fc4f9c/control",
    "payload": "{\"addr\":40,\"relay\":1}",
    "payloadType": "json",
    "wires": [["mqtt_out"]]
  },
  {
    "id": "inject_hex_on",
    "type": "inject",
    "name": "Unit 0x28 ON (hex)",
    "topic": "lora/lrs-00fc4f9c/control",
    "payload": "{\"addr\":\"0x28\",\"relay\":1}",
    "payloadType": "json",
    "wires": [["mqtt_out"]]
  },
  {
    "id": "mqtt_out",
    "type": "mqtt out",
    "name": "TX control publish",
    "topic": "",
    "broker": "your_broker_id",
    "wires": []
  }
]
```

MQTT control vs paired LoRa address:
- If TX `Input drives LoRa relay control` is enabled, heartbeat and input-driven LoRa traffic control the TX-paired `remote_address`.
- In that mode, avoid using MQTT `control` for RX nodes that share the TX-paired `remote_address`, or heartbeat can overwrite MQTT state.
- If you need MQTT control on the TX-paired address, disable `Input drives LoRa relay control` on TX.

## MQTT Control Test Matrix (Copy/Paste)
Prerequisites:
- TX and RX are online and paired over LoRa.
- MQTT is enabled on both nodes with the same topic root.
- You know chip IDs and LoRa addresses for TX and RX.

Set test variables:
```bash
export MQTT_HOST="venus.local"
export MQTT_PORT="1883"
export ROOT="lora"
export CHIP_TX="004a9c27"
export CHIP_RX="00bb12ef"
export RX_ADDR_HEX="02"
```

Watch MQTT traffic in one terminal:
```bash
mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/#" -v
```

1. RX local relay control via `relay` topic:
```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_RX/relay" -m "1"
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_RX/relay" -m "0"
```
Expected relay behavior:
- RX relay changes immediately ON then OFF.
- TX relay does not follow from this MQTT command.
Expected logs:
- RX: `mqtt_relay_topic`, `mqtt_local_relay`.

2. TX local relay control via `relay` topic:
```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_TX/relay" -m "1"
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_TX/relay" -m "0"
```
Expected relay behavior:
- TX relay changes immediately ON then OFF.
- RX relay does not follow from this MQTT command.
Expected logs:
- TX: `mqtt_relay_topic`, `mqtt_local_relay`.

3. Remote RX control via TX `control` topic:
```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_TX/control" -m "{\"addr\":\"$RX_ADDR_HEX\",\"relay\":1}"
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_TX/control" -m "{\"addr\":\"$RX_ADDR_HEX\",\"relay\":0}"
```
Expected relay behavior:
- TX sends LoRa `Mqtt` packet to RX address.
- RX relay changes ON then OFF.
- TX relay does not auto-follow this path.
Expected logs:
- TX: `mqtt_control_topic`, `mqtt_remote_relay_tx`.
- RX: `rx_apply_mqtt`.

4. Negative test: invalid JSON on TX `control`:
```bash
mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/lrs-$CHIP_TX/control" -m "{bad-json"
```
Expected logs:
- TX: `mqtt_control_json_err`.

5. Discovery topic check:
```bash
mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/discovery/lrs-$CHIP_TX" -C 1 -v
mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$ROOT/discovery/lrs-$CHIP_RX" -C 1 -v
```
Expected:
- Retained JSON payload with identity/build fields for each node.

## Logs
Use `Logs` tab for live view or download CSV.

Useful events:
- `tx_change`, `tx_heartbeat`, `tx_ack`, `tx_ack_timeout`
- `rx_apply_and_ack`, replay/addr filters
- `sta_connected`, `sta_connect_failed_fallback_ap`
- MQTT connect/status events

## DS18B20
In `Sensors` tab:
- Enable DS18B20

DS18B20 data pin is fixed to `GPIO0` on current hardware.
Temperature sampling interval is automatic: `heartbeat / 2` (minimum 2 seconds).

Current firmware also transports temperature over LoRa so paired nodes can display remote temperature.

## Safety
- Use relay for control signaling, not high-current power switching.
- Validate polarity and terminal wiring before commissioning.
- GPIO0/GPIO2 are boot-sensitive pins; wire external sensors accordingly.
