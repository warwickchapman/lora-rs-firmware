# Architecture Map (Takeover Baseline)

## Purpose
Document the current subsystem boundaries and runtime/admin paths before monolith splitting.

## Composition Root
- `App` owns lifecycle and orchestration of all subsystems.
- Entry point: `/Users/warwick/Code/LoRa/lora_rs/src/main.cpp`
- Main orchestration: `/Users/warwick/Code/LoRa/lora_rs/src/app.cpp`

## Subsystems and Ownership
- `ConfigStore`
  - Owns persisted settings, defaults, migrations, and local factory reset persistence behavior.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/config_store.h`, `/Users/warwick/Code/LoRa/lora_rs/src/config_store.cpp`

- `RadioProtocol`
  - Owns LoRa packet format, encryption/MAC, raw send/receive, default-key gating.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/radio_protocol.h`, `/Users/warwick/Code/LoRa/lora_rs/src/radio_protocol.cpp`

- `NodeStateMachine`
  - Owns runtime control semantics, TX/RX behavior, replay protection, peer state, provisioning state machines, pending admin actions.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/state_machine.h`, `/Users/warwick/Code/LoRa/lora_rs/src/state_machine.cpp`

- `MqttBridge`
  - Owns MQTT transport/topic schema and maps MQTT commands/status to/from `NodeStateMachine`.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/mqtt_bridge.h`, `/Users/warwick/Code/LoRa/lora_rs/src/mqtt_bridge.cpp`

- `SensorManager`
  - Owns DS18B20 lifecycle and sampling cadence.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/sensor_manager.h`, `/Users/warwick/Code/LoRa/lora_rs/src/sensor_manager.cpp`

- `WebConsole`
  - Owns HTTP UI/API, auth/session, status caches/SSE, diagnostics, OTA upload, admin actions, and embedded UI assets.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/web_console.h`, `/Users/warwick/Code/LoRa/lora_rs/src/web_console.cpp`

- `logger`
  - Owns serial log formatting, heap telemetry helpers, UDP log mirroring.
  - Files: `/Users/warwick/Code/LoRa/lora_rs/src/logger.h`, `/Users/warwick/Code/LoRa/lora_rs/src/logger.cpp`

## Hot Paths (Timing/Heap Sensitive)
- `App::tick()` phase ordering and pacing
- `NodeStateMachine::tick()`, `tickReceive()`, TX/RX branches
- `RadioProtocol::receive()` and LoRa send paths
- `MqttBridge::tick()` and incremental publish flow
- `SensorManager::tick()`

## Admin / Cold Paths (Can still destabilize runtime)
- `WebConsole` request handlers and SSE/status caching
- `ConfigStore::save()` / `factoryReset()`
- `App::applyUpdatedConfig()` cross-subsystem reconfiguration boundary

## Immediate Refactor Strategy (Approved Direction)
- Same-class multi-`.cpp` splitting first
- `WebConsole` before `NodeStateMachine`
- No framework/abstraction introduction during early split phases
- Behavior-preserving refactors only unless covered by explicit exception policy
