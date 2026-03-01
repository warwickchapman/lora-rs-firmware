# Legacy Manual vs Current Firmware Alignment

Sources reviewed:
- `/Users/warwick/Downloads/Sensible LoRa Switcher Manual.docx`
- `/Users/warwick/Downloads/Sensible LoRa Switcher Manual (2).pdf` (image/manual reference)
- User-provided board and topology images

## Aligned with Manual Intent
- TX sends control state from dry contact input.
- RX applies relay state and ACKs TX.
- TX relay reflects ACK-confirmed remote state (500 ms delay).
- 1-to-many and many-to-1 topologies are achieved by address configuration.
- Link quality/status visibility provided in UI.

## Additions Beyond Legacy Manual
- Web configuration console with auth.
- LittleFS persistence.
- MQTT bridge and control topics.
- OTA support.
- Factory/sticker automation scripts.
- Log CSV/text endpoints.
- DS18B20 local sensing and remote temperature transfer over LoRa.

## Important Behavior Differences
- AP SSID is now role-independent: `lrs-<chipid>`.
- Default credentials and serial are deterministic, derived from chip ID + product secret.
- Packet payload format has expanded (12-byte encrypted payload with sensor/time fields).

## Hardware Constraint Notes Added
- CN1 includes GPIO0/GPIO2/AIN but GPIO0/GPIO2 are strap/LED-linked pins.
- ESP8266 analog path is limited; advanced multi-sensor analog support remains future work.
