#pragma once
#include "sensor_types.h"

class SensorRegistry {
 public:
  static constexpr uint8_t MAX_SENSORS = 6;

  void clear();
  bool upsert(const SensorReading& reading);
  bool find(SensorKind kind, uint8_t instance, SensorReading& outReading) const;
  uint8_t count() const { return count_; }
  bool byIndex(uint8_t index, SensorReading& outReading) const;

 private:
  SensorReading readings_[MAX_SENSORS]{};
  uint8_t count_ = 0;
};
