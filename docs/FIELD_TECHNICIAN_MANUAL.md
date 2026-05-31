# Field Technician Manual

This guide is for technicians installing or servicing Thanda LoRa Remote Switch devices in the field.

## What The System Does
The system lets one device control another device over LoRa radio.

- The gateway or transmitter is usually the device near the control panel.
- Remote devices are usually near pumps, tanks, relays, or sensors.
- The desktop Flasher app is the main service tool.
- USB is the reliable local service connection.
- WiFi is used only when configured, mainly for MQTT, logs, or firmware pull updates.

## What To Bring
- Laptop with the Thanda LoRa Flasher app installed.
- USB cable for the device.
- The correct firmware release for the country or region.
- Site WiFi name and password if WiFi will be used.
- The site Fleet Key if the devices are already commissioned.
- A simple label or notebook for device names and addresses.

## Normal First Setup
1. Open Flasher.
2. Plug the gateway device into USB.
3. In `Flash`, read the device identity.
4. Flash the correct firmware if needed.
5. In `Provision`, set the gateway address. The normal gateway address is `254`.
6. Set the Fleet Key.
7. Add WiFi details if this site uses WiFi.
8. Power the remote devices.
9. Scan for factory remotes.
10. Provision remotes starting at address `1`.
11. Open `Fleet` and scan the fleet.
12. Confirm every expected remote appears with the right address.

## Normal Address Rules
- Gateway address: `254`
- First remote address: `1`
- Next remotes: `2`, `3`, `4`, and so on
- Do not give two devices the same LoRa address.

## What The Fleet Key Is
The Fleet Key is the shared secret for the site.

Devices with the same Fleet Key can talk to each other. Devices with a different Fleet Key cannot understand or control the fleet.

Keep the Fleet Key private. If you change it remotely, make sure the target devices are online and correctly selected before you send the change.

## Checking A Site
Use `Fleet` in Flasher from the USB-connected gateway.

Check:
- Gateway is detected.
- Remotes appear in the list.
- Firmware versions are shown.
- Input and relay state make sense.
- WiFi and MQTT show `Online` only when those features are enabled and expected.
- Sensor values are present only when the hardware is installed and enabled.

## Updating Firmware
Use USB flashing when you are next to the device.

For remote updates:
1. Confirm the remote is visible in `Fleet`.
2. Confirm the remote has WiFi.
3. Select the correct firmware.
4. Start the remote flash action.
5. Wait for the remote to reboot and report the new firmware version.

Firmware is not sent over LoRa. LoRa only sends the update command and checksum. The remote downloads the firmware over WiFi.

## Listen-Only Power Save
Some remote devices may use listen-only power save.

In this mode the device waits 10 minutes after boot. If no technician uses USB and no UDP logging is active, it turns off WiFi work, MQTT, sensor polling, Serial Admin, OTA handling, and status LEDs. LoRa control keeps running.

To service this type of device:
1. Power-cycle the device.
2. Connect USB.
3. Open Flasher within 10 minutes.
4. Make the required changes before the service window closes.

If you miss the 10 minute window, power-cycle the device and try again.

## Recovery
If a device does not respond remotely:
1. Go to the device.
2. Connect USB.
3. Use `Flash` to read identity.
4. Use `Settings` to check role, address, Fleet Key, WiFi, and sensors.
5. Reflash over USB if the configuration is unknown.

If the admin password is lost, recover by erase-and-reflash over USB.

## Common Problems
Wrong Fleet Key:
The device appears missing or does not respond. Reconnect over USB and set the correct Fleet Key.

Duplicate address:
Two remotes behave strangely or one disappears. Give every remote a unique address.

Wrong firmware region:
Devices do not communicate. Flash the correct regional firmware for the installation.

No WiFi:
Remote OTA, MQTT, and UDP logs will not work. LoRa control can still work.

Power-save service window missed:
Power-cycle the device and connect Flasher within 10 minutes.
