# LoRa Remote Switch (LRS) Firmware

This repository contains ESP8266 firmware for paired LoRa relay control devices.

Both hardware units are identical. Behavior is selected by configuration:
- `TX` unit: reads digital input and transmits state.
- `RX` unit: receives state and drives relay output.

The codebase has been rebuilt as a modular state-machine firmware with a protected web console, LittleFS settings, OTA support, and factory metadata generation.

## Start Here
- User/operator quickstart: `/Users/warwick/Code/LoRa/lora_rs/docs/USER_GUIDE.md`
- Developer onboarding and architecture: `/Users/warwick/Code/LoRa/lora_rs/docs/DEVELOPER_GUIDE.md`
- Protocol details: `/Users/warwick/Code/LoRa/lora_rs/docs/PROTOCOL.md`
- Factory/provisioning flow: `/Users/warwick/Code/LoRa/lora_rs/docs/PROVISIONING.md`
- Product manual draft: `/Users/warwick/Code/LoRa/lora_rs/docs/PRODUCT_MANUAL.md`
- Legacy manual alignment notes: `/Users/warwick/Code/LoRa/lora_rs/docs/MANUAL_ALIGNMENT.md`
- Legacy branch parity map: `/Users/warwick/Code/LoRa/lora_rs/docs/FEATURE_PARITY.md`
- Deferred scope TODO list: `/Users/warwick/Code/LoRa/lora_rs/docs/TODO.md`

## Build Targets
- South Africa (433 MHz): `python3 -m platformio run -e lrs_za`
- USA (915 MHz): `python3 -m platformio run -e lrs_us`

## Flash
- `python3 -m platformio run -e lrs_za -t upload --upload-port <PORT>`
- `python3 -m platformio run -e lrs_us -t upload --upload-port <PORT>`

## Current Status
- `lrs_za` compiles successfully.
- `lrs_us` compiles successfully.

## Notes for New Contributors
- Main runtime entrypoint: `/Users/warwick/Code/LoRa/lora_rs/src/main.cpp`
- Application orchestrator: `/Users/warwick/Code/LoRa/lora_rs/src/app.cpp`
- Runtime modules are split under `/Users/warwick/Code/LoRa/lora_rs/src/` (`app`, `state_machine`, `radio_protocol`, `web_console`, `mqtt_bridge`, `sensor_manager`, `config_store`, `log_buffer`).
