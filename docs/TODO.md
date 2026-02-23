# TODO

## Sensors Roadmap (ESP8266 Track)
- Add sensor type selection for dry-contact input semantics (`float switch`, `start/stop`, generic dry contact).
- Add UI and payload mapping for tank level model(s).
- Define flow sensor model and units.
- Implement reserved `sensor_analog0` usage and scaling conventions.
- Add sensor alarm/status thresholds in UI.

## Hardware and Architecture
- Document CN1 usage and strap-pin caveats in production manuals.
- Keep ESP8266 support primary for current product.
- Evaluate ESP32 baseboard migration path separately (no current firmware migration committed).

## Protocol and Migration
- Introduce explicit protocol version field in packet.
- Add backward/compatibility migration policy for future payload changes.

## Provisioning
- Add pair-mode provisioning flow (first unit TX, second unit RX, linked output records).

## UX / API Cleanup
- Unify `deployment_key` and `fleet_passphrase` terminology under user-facing `Shared Fleet Key` (short form: `Fleet Key` where space is tight); place helper text directly under the key input explaining it is the shared passphrase used to derive LoRa encryption/authentication keys; choose one canonical API field name and treat old names as temporary input aliases only.

## Logging / Observability
- Phase 1 (minimal patch, ESP8266-safe): keep structured logging focused on diagnosability with low overhead: levels (`ERROR/WARN/INFO/DEBUG`), categories (`SYS/WIFI/NTP/MDNS/LORA/SENSOR/WEB/API/FS`), redaction helpers, web/API request summaries (status + duration), and heap diagnostics on high-risk endpoints (`/api/status`, `/api/fleet`, `/api/provisioning/status`); keep default level at `INFO`; keep polling endpoints (`/api/status`, `/api/session`) at `DEBUG`.
- Phase 2 (later mass refactor): convert remaining ad-hoc prints across modules to the shared logging API; standardize event names/fields; add state-change/rate-limited logging patterns; review LoRa/web/API logs for spam/noise; expand structured coverage for provisioning/fleet workflows and automation engine paths; document log taxonomy and operational/debug logging policy.
- Phase 2 guardrails: no secret leakage (fleet keys, passwords, tokens), avoid heap-heavy log string construction in hot paths, and preserve current runtime timing priorities (LoRa control path before MQTT).

## Fleet (Fleet-Wide Tools / Actions)
- Broadcast WiFi provisioning (current feature; keep as anchor item).
- Add fleet-wide remote factory reset for `selected` or `all` devices, with `keep fleet key` option.
- Add staged fleet key rotation workflow.
- Add broadcast poll / discovery refresh.
- Add fleet-wide schedule defaults push.
- Add batch firmware rollout trigger (future; depends on OTA over LoRa / coordination support).
- Add fleet health summary (stale/offline counts, last-seen distribution).
- Add fleet export (device list, RSSI snapshot, statuses).

## System / Diagnostics (Device-Level Tools)
- Add `System > Diagnostics` sub-menu to group troubleshooting tools and avoid clutter in top-level System settings.
- Add remote serial console / TCP serial monitor (VS Code-style monitor over TCP/IP) under `System > Diagnostics`.

## WiFi Provisioning UX
- When clicking `Use` on a scanned WiFi network, auto-fill SSID and focus the password field.
- Scroll password field into view after `Use` (especially on mobile).
- If network is open (no password), skip password focus and move to connect/save action.
- If password is already prefilled/stored, prefer focusing connect/save instead of forcing password edit.

## Firmware Runtime Behavior
- DS18B20 (GPIO0): split persistent `enabled` config from runtime `present` detection state.
- If DS18B20 is enabled but not detected, report `missing` / `not present`, skip readings/publish, and retry detection periodically.
- If DS18B20 is detected later, activate automatically without requiring a reboot or config rewrite.
- Do not auto-disable DS18B20 config on failed detection (avoid boot-time false negatives becoming sticky state).

## Observability / Logging
- Implement structured logging with levels: `ERROR`, `WARN`, `INFO` (default), `DEBUG`, `TRACE`.
- Standardize categories/tags (e.g. `SYS`, `WIFI`, `NTP`, `MDNS`, `LORA`, `SENSOR`, `WEB`, `API`, `FS`).
- Standardize one-line log format with level/category, `t=<millis>`, optional `unix=<epoch>`, `event=<name>`, and key-value fields.
- Log web page/path requests and API calls with method, path, status, duration (`dur_ms`), and client IP (if available).
- Add mandatory secret redaction in logs (WiFi passwords, fleet keys, tokens, other secrets).
- Keep default logging at `INFO`; make `DEBUG` / `TRACE` opt-in diagnostics modes.
- Avoid log spam in tight loops (prefer state-change logging and/or repeated-warning rate limiting).
