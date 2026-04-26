# Thanda LoRa Flasher

Desktop utility for LRS firmware updates and logs.

## Modes

### Serial

Serial mode is the original guided workflow:

- auto-detect serial ports
- select release firmware or choose a local `.bin`
- flash over USB serial
- optionally start serial monitoring immediately after flashing
- read and copy device identity/password details

### Network

Network mode handles one LRS device at a time over the current LAN:

- discovers LRS devices by scanning IPv4 ranges derived from the local network adapter netmasks
- identifies devices by direct HTTP probing only; mDNS and `lrs-*.local` hostnames are not used
- tries the derived factory/admin password from the device identity where available
- allows a per-device admin password override when the default password is no longer valid
- shows authenticated firmware version/build details when login succeeds
- opens the selected device Web UI by direct IP
- uploads firmware through the authenticated HTTP OTA endpoint
- listens for UDP logs on fixed port `5514` after OTA or on demand

Network mode intentionally does not include bulk OTA yet. The UI can enumerate multiple devices, but update and UDP-log actions target the selected device only.
