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

