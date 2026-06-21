#include "sensor_manager.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#include "logger.h"

namespace {
constexpr float kInvalidTemp = -127.0f;
constexpr uint8_t kTankAnalogPin = A0;
constexpr uint8_t kTankSamples = 8;
constexpr float kTankZeroMa = 4.0f;
constexpr float kTankSpanMa = 16.0f;
constexpr float kTankFaultLowMa = 3.8f;
constexpr float kTankOverrangeMa = 20.0f;

uint16_t deriveTempIntervalS(uint32_t heartbeatMs) {
  uint32_t sec = heartbeatMs / 2000U;
  if (sec < 2U) sec = 2U;
  if (sec > 300U) sec = 300U;
  return static_cast<uint16_t>(sec);
}

uint16_t clampU16(float value) {
  if (value <= 0.0f) return 0;
  if (value >= 65535.0f) return 65535;
  return static_cast<uint16_t>(value + 0.5f);
}
}

bool SensorManager::begin(const Settings &cfg) {
  applyConfig(cfg);
  return true;
}

void SensorManager::applyConfig(const Settings &cfg) {
  settings_ = &cfg;
  runtime_.sensor_temp_enabled = cfg.sensor_temp_enabled;
  runtime_.sensor_temp_pin = cfg.sensor_temp_pin;
  runtime_.temp_interval_s = deriveTempIntervalS(cfg.heartbeat_ms);
  runtime_.sensor_tank_enabled = cfg.sensor_tank_enabled;
  runtime_.tank_range_mm = cfg.sensor_tank_range_mm == 0 ? 5000 : cfg.sensor_tank_range_mm;
  runtime_.tank_vref_mv = cfg.sensor_tank_vref_mv == 0 ? 3553 : cfg.sensor_tank_vref_mv;
  runtime_.tank_sense_ohms = cfg.sensor_tank_sense_ohms == 0 ? 120 : cfg.sensor_tank_sense_ohms;
  runtime_.tank_interval_s = cfg.sensor_tank_interval_s == 0 ? 5 : cfg.sensor_tank_interval_s;
  temp_.enabled = runtime_.sensor_temp_enabled;
  temp_.pin = runtime_.sensor_temp_pin;
  temp_.interval_s = runtime_.temp_interval_s;
  temp_.detected = false;
  temp_.valid = false;
  temp_.celsius = NAN;
  temp_.last_read_ms = 0;
  last_read_ms_ = 0;
  temp_conversion_pending_ = false;
  temp_conversion_started_ms_ = 0;
  temp_conversion_wait_ms_ = 750;
  has_addr_ = false;
  tank_.enabled = runtime_.sensor_tank_enabled;
  tank_.valid = false;
  tank_.state = tank_.enabled ? TankSensorState::FaultLow : TankSensorState::Disabled;
  tank_.depth_mm = 0;
  tank_.voltage_mv = 0;
  tank_.current_centi_ma = 0;
  tank_.range_mm = runtime_.tank_range_mm;
  tank_.vref_mv = runtime_.tank_vref_mv;
  tank_.sense_ohms = runtime_.tank_sense_ohms;
  tank_.interval_s = runtime_.tank_interval_s;
  tank_.raw_adc = 0;
  tank_.last_read_ms = 0;
  if (tank_.enabled) {
    pinMode(kTankAnalogPin, INPUT);
  }
  setupBus();
}

const SensorRegistry &SensorManager::readings() const { return registry_; }

void SensorManager::tick() {
  const uint32_t now = millis();
  tickTemperature(now);
  tickTank(now);

  registry_.clear();

  // Temperature
  SensorReading t_reading{};
  t_reading.kind = SensorKind::TemperatureC;
  t_reading.instance = 0;
  t_reading.scale = 1;
  if (!temp_.enabled) {
    t_reading.state = SensorState::Disabled;
    t_reading.value = 0;
  } else if (!temp_.detected) {
    t_reading.state = SensorState::Missing;
    t_reading.value = 0;
  } else if (temp_.last_read_ms == 0) {
    t_reading.state = SensorState::Waiting;
    t_reading.value = 0;
  } else if (!temp_.valid) {
    t_reading.state = SensorState::Fault;
    t_reading.value = 0;
  } else {
    t_reading.state = SensorState::Ok;
    t_reading.value = static_cast<int32_t>(temp_.celsius * 10.0f + (temp_.celsius >= 0.0f ? 0.5f : -0.5f));
  }
  registry_.upsert(t_reading);

  // Tank Level
  SensorReading tank_reading{};
  tank_reading.kind = SensorKind::TankLevel;
  tank_reading.instance = 0;
  tank_reading.scale = 0;
  if (!tank_.enabled) {
    tank_reading.state = SensorState::Disabled;
    tank_reading.value = 0;
  } else if (!tank_.valid) {
    if (tank_.state == TankSensorState::FaultLow) {
      tank_reading.state = SensorState::Fault;
    } else {
      tank_reading.state = SensorState::Missing;
    }
    tank_reading.value = 0;
  } else {
    if (tank_.state == TankSensorState::Overrange) {
      tank_reading.state = SensorState::Overrange;
    } else {
      tank_reading.state = SensorState::Ok;
    }
    tank_reading.value = tank_.depth_mm;
  }
  registry_.upsert(tank_reading);
}

void SensorManager::tickTemperature(uint32_t now) {
  if (!temp_.enabled || !ds_ || !has_addr_) {
    return;
  }

  if (temp_conversion_pending_) {
    if (now - temp_conversion_started_ms_ < temp_conversion_wait_ms_) {
      return;
    }
    temp_conversion_pending_ = false;

    const float c = ds_->getTempC(addr_);
    temp_.last_read_ms = now;

    if (c == DEVICE_DISCONNECTED_C || c <= kInvalidTemp) {
      temp_.valid = false;
      lrslog::event("temp_read_failed", 0, 0, 0);
      LRS_LOGW(SENSOR, "event=temp_read_failed pin=%u", static_cast<unsigned>(temp_.pin));
      return;
    }

    temp_.valid = true;
    temp_.celsius = c;
    lrslog::event("temp_read_ok", 0, 0, static_cast<uint8_t>(c));
    return;
  }

  if (last_read_ms_ != 0 &&
      now - last_read_ms_ < static_cast<uint32_t>(temp_.interval_s) * 1000UL) {
    return;
  }
  last_read_ms_ = now;

  const auto req = ds_->requestTemperaturesByAddress(addr_);
  if (!req.result) {
    temp_.last_read_ms = now;
    temp_.valid = false;
    lrslog::event("temp_read_failed", 0, 0, 0);
    LRS_LOGW(SENSOR, "event=temp_read_failed pin=%u", static_cast<unsigned>(temp_.pin));
    return;
  }

  temp_conversion_pending_ = true;
  temp_conversion_started_ms_ = req.timestamp;
}

const TempSensorStatus &SensorManager::tempStatus() const { return temp_; }

const TankSensorStatus &SensorManager::tankStatus() const { return tank_; }

void SensorManager::tickTank(uint32_t now) {
  if (!tank_.enabled) {
    return;
  }
  if (tank_.last_read_ms != 0 &&
      now - tank_.last_read_ms < static_cast<uint32_t>(tank_.interval_s) * 1000UL) {
    return;
  }

  uint32_t total = 0;
  for (uint8_t i = 0; i < kTankSamples; ++i) {
    total += static_cast<uint16_t>(analogRead(kTankAnalogPin));
  }
  const uint16_t raw = static_cast<uint16_t>((total + (kTankSamples / 2U)) / kTankSamples);
  const float voltageMv = (static_cast<float>(raw) * static_cast<float>(tank_.vref_mv)) / 1024.0f;
  const float currentMa = voltageMv / static_cast<float>(tank_.sense_ohms);
  float depthMm = (currentMa - kTankZeroMa) *
                  (static_cast<float>(tank_.range_mm) / kTankSpanMa);
  if (depthMm < 0.0f) {
    depthMm = 0.0f;
  }

  tank_.raw_adc = raw;
  tank_.voltage_mv = clampU16(voltageMv);
  tank_.current_centi_ma = clampU16(currentMa * 100.0f);
  tank_.depth_mm = clampU16(depthMm);
  tank_.last_read_ms = now;

  if (currentMa < kTankFaultLowMa) {
    tank_.valid = false;
    tank_.state = TankSensorState::FaultLow;
  } else if (currentMa > kTankOverrangeMa) {
    tank_.valid = true;
    tank_.state = TankSensorState::Overrange;
  } else {
    tank_.valid = true;
    tank_.state = TankSensorState::Ok;
  }
}

void SensorManager::teardownBus() {
  temp_conversion_pending_ = false;
  temp_conversion_started_ms_ = 0;
  if (ds_) {
    delete ds_;
    ds_ = nullptr;
  }
  if (ow_) {
    delete ow_;
    ow_ = nullptr;
  }
}

void SensorManager::setupBus() {
  teardownBus();
  if (!temp_.enabled) {
    return;
  }

  ow_ = new OneWire(temp_.pin);
  ds_ = new DallasTemperature(ow_);
  ds_->begin();
  ds_->setWaitForConversion(false);
  ds_->setCheckForConversion(false);

  uint8_t found[8];
  if (!ow_->search(found)) {
    temp_.detected = false;
    ow_->reset_search();
    lrslog::event("temp_not_detected", 0, 0, temp_.pin);
    LRS_LOGW(SENSOR, "event=temp_not_detected pin=%u", static_cast<unsigned>(temp_.pin));
    return;
  }
  ow_->reset_search();

  memcpy(addr_, found, 8);
  has_addr_ = true;
  temp_.detected = true;
  ds_->setResolution(addr_, 12);
  temp_conversion_wait_ms_ = DallasTemperature::millisToWaitForConversion(12);
  lrslog::event("temp_detected", 0, 0, temp_.pin);
  char addrStr[24]{};
  snprintf(addrStr, sizeof(addrStr), "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
           addr_[0], addr_[1], addr_[2], addr_[3],
           addr_[4], addr_[5], addr_[6], addr_[7]);
  LRS_LOGI(SENSOR, "event=temp_detected pin=%u addr=%s", static_cast<unsigned>(temp_.pin), addrStr);
}
