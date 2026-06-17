#pragma once

#include <Arduino.h>

#include "config_store.h"
#include "sensor_status.h"
#include "sensor_registry.h"

struct TempSensorStatus {
  bool enabled = false;
  bool detected = false;
  bool valid = false;
  float celsius = NAN;
  uint8_t pin = 0;
  uint16_t interval_s = 10;
  uint32_t last_read_ms = 0;
};

struct TankSensorStatus {
  bool enabled = false;
  bool valid = false;
  TankSensorState state = TankSensorState::Disabled;
  uint16_t depth_mm = 0;
  uint16_t voltage_mv = 0;
  uint16_t current_centi_ma = 0;
  uint16_t range_mm = 5000;
  uint16_t vref_mv = 3553;
  uint16_t sense_ohms = 120;
  uint16_t interval_s = 5;
  uint16_t raw_adc = 0;
  uint32_t last_read_ms = 0;
};

class SensorManager {
 public:
  bool begin(const Settings &cfg);
  void applyConfig(const Settings &cfg);
  void tick();
  const TempSensorStatus &tempStatus() const;
  const TankSensorStatus &tankStatus() const;
  const SensorRegistry &readings() const;

 private:
  struct RuntimeCfg {
    bool sensor_temp_enabled = false;
    uint8_t sensor_temp_pin = 0;
    uint16_t temp_interval_s = 10;
    bool sensor_tank_enabled = false;
    uint16_t tank_range_mm = 5000;
    uint16_t tank_vref_mv = 3553;
    uint16_t tank_sense_ohms = 120;
    uint16_t tank_interval_s = 5;
  };

  const Settings *settings_ = nullptr;
  RuntimeCfg runtime_{};

  class OneWire *ow_ = nullptr;
  class DallasTemperature *ds_ = nullptr;
  uint8_t addr_[8]{};
  bool has_addr_ = false;
  uint32_t last_read_ms_ = 0;
  bool temp_conversion_pending_ = false;
  uint32_t temp_conversion_started_ms_ = 0;
  uint16_t temp_conversion_wait_ms_ = 750;
  TempSensorStatus temp_;
  TankSensorStatus tank_;
  SensorRegistry registry_;

  void tickTemperature(uint32_t now);
  void tickTank(uint32_t now);
  void teardownBus();
  void setupBus();
};
