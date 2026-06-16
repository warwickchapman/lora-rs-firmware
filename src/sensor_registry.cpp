#include "sensor_registry.h"

void SensorRegistry::clear() {
  count_ = 0;
}

bool SensorRegistry::upsert(const SensorReading& reading) {
  for (uint8_t i = 0; i < count_; ++i) {
    if (readings_[i].kind == reading.kind && readings_[i].instance == reading.instance) {
      readings_[i] = reading;
      return true;
    }
  }

  if (count_ >= MAX_SENSORS) {
    return false;
  }

  readings_[count_++] = reading;
  return true;
}

bool SensorRegistry::find(SensorKind kind, uint8_t instance, SensorReading& outReading) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (readings_[i].kind == kind && readings_[i].instance == instance) {
      outReading = readings_[i];
      return true;
    }
  }
  return false;
}

bool SensorRegistry::byIndex(uint8_t index, SensorReading& outReading) const {
  if (index >= count_) {
    return false;
  }
  outReading = readings_[index];
  return true;
}
