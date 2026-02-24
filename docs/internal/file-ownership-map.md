# File Ownership Map (Current + Split Targets)

## Current Ownership
- `/Users/warwick/Code/LoRa/lora_rs/src/app.cpp`
  - Composition root, lifecycle, runtime orchestration, apply boundary, networking/NTP/OTA/mDNS coordination.

- `/Users/warwick/Code/LoRa/lora_rs/src/web_console.cpp`
  - `WebConsole` core/common methods: begin/tick/auth/session/request logging/common cache guards/captive probe.
- `/Users/warwick/Code/LoRa/lora_rs/src/web_console_routes.cpp`
  - `WebConsole::routes()` only.
- `/Users/warwick/Code/LoRa/lora_rs/src/web_console_ui_assets.cpp`
  - Embedded UI HTML/CSS/JS `PROGMEM` blobs only.
- `/Users/warwick/Code/LoRa/lora_rs/src/web_console_status.cpp` ... `/Users/warwick/Code/LoRa/lora_rs/src/web_console_system.cpp`
  - `WebConsole` feature handlers split by concern (status/pages/settings/fleet/provisioning/network/system).

- `/Users/warwick/Code/LoRa/lora_rs/src/state_machine.cpp`
  - Runtime control engine, replay protection, peer state, provisioning coordinator/target, RX/TX dispatch.

## Planned Split Ownership (Same Class, Multi-`.cpp`)
### `WebConsole`
- `web_console_ui_assets.cpp` + `.h`: UI HTML/CSS/JS `PROGMEM` blobs only
- `web_console_routes.cpp`: route registration only
- `web_console_core.cpp`: begin/tick/auth/session/request logging/common helpers
- `web_console_status.cpp`: status JSON caches, SSE, status endpoints
- `web_console_pages.cpp`: `/`, `/login`, `/setup`, captive probes
- `web_console_settings.cpp`: settings get/post/export/import
- `web_console_fleet.cpp`: fleet list + fleet device action routes
- `web_console_provisioning.cpp`: provisioning and fleet WiFi provisioning APIs
- `web_console_network.cpp`: WiFi scan/test
- `web_console_system.cpp`: diagnostics/logging/OTA/factory reset/reboot

### `NodeStateMachine` (Milestone 5 in progress)
- `state_machine_core.cpp`
- `state_machine_replay.cpp`
- `state_machine_tx.cpp`
- `state_machine_rx.cpp`
- `state_machine_peers.cpp`
- `state_machine_wifi_factory.cpp`
- `state_machine_provisioning_coord.cpp`
- `state_machine_provisioning_target.cpp`
- `state_machine_time_led.cpp`

Current Milestone 5 target (safe first moves):
- `state_machine_replay.cpp`
- `state_machine_time_led.cpp`
- Keep TX/RX/provisioning dispatch in `state_machine.cpp` for now

## Split Ownership Rules
- Each split file owns one concern and only methods/helpers for that concern.
- No behavior changes mixed with file moves in split milestones.
- TX/RX/provisioning tick ordering changes may only happen in explicitly planned work, not in file moves.
