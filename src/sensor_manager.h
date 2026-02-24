#pragma once

#include <Arduino.h>

#include "config_store.h"

struct TempSensorStatus {
  bool enabled = false;
  bool detected = false;
  bool valid = false;
  float celsius = NAN;
  String address;
  String error;
  uint8_t pin = 0;
  uint16_t interval_s = 10;
  uint32_t last_read_ms = 0;
};

class SensorManager {
 public:
  bool begin(const Settings &cfg);
  void applyConfig(const Settings &cfg);
  void tick();
  const TempSensorStatus &tempStatus() const;

 private:
  Settings cfg_{};

  class OneWire *ow_ = nullptr;
  class DallasTemperature *ds_ = nullptr;
  uint8_t addr_[8]{};
  bool has_addr_ = false;
  uint32_t last_read_ms_ = 0;
  bool temp_conversion_pending_ = false;
  uint32_t temp_conversion_started_ms_ = 0;
  uint16_t temp_conversion_wait_ms_ = 750;
  TempSensorStatus temp_;

  void teardownBus();
  void setupBus();
  String formatAddress() const;
};
