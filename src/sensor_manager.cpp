#include "sensor_manager.h"

#include <DallasTemperature.h>
#include <OneWire.h>

#include "logger.h"

namespace {
constexpr float kInvalidTemp = -127.0f;

uint16_t deriveTempIntervalS(uint32_t heartbeatMs) {
  uint32_t sec = heartbeatMs / 2000U;
  if (sec < 2U) sec = 2U;
  if (sec > 300U) sec = 300U;
  return static_cast<uint16_t>(sec);
}
}

bool SensorManager::begin(const Settings &cfg, LogBuffer *logs) {
  logs_ = logs;
  applyConfig(cfg);
  return true;
}

void SensorManager::applyConfig(const Settings &cfg) {
  cfg_ = cfg;
  temp_.enabled = cfg_.sensor_temp_enabled;
  temp_.pin = cfg_.sensor_temp_pin;
  temp_.interval_s = deriveTempIntervalS(cfg_.heartbeat_ms);
  temp_.detected = false;
  temp_.valid = false;
  temp_.celsius = NAN;
  temp_.address = "";
  temp_.error = "";
  temp_.last_read_ms = 0;
  last_read_ms_ = 0;
  has_addr_ = false;
  setupBus();
}

void SensorManager::tick() {
  if (!temp_.enabled || !ds_ || !has_addr_) {
    return;
  }

  const uint32_t now = millis();
  if (now - last_read_ms_ < static_cast<uint32_t>(temp_.interval_s) * 1000UL) {
    return;
  }
  last_read_ms_ = now;

  ds_->requestTemperaturesByAddress(addr_);
  const float c = ds_->getTempC(addr_);
  temp_.last_read_ms = now;

  if (c == DEVICE_DISCONNECTED_C || c <= kInvalidTemp) {
    temp_.valid = false;
    temp_.error = "read_failed";
    if (logs_) logs_->add("temp_read_failed", 0, 0, 0);
    LRS_LOGW(SENSOR, "event=temp_read_failed pin=%u", static_cast<unsigned>(temp_.pin));
    return;
  }

  temp_.valid = true;
  temp_.celsius = c;
  temp_.error = "";
  if (logs_) logs_->add("temp_read_ok", 0, 0, static_cast<uint8_t>(c));
}

const TempSensorStatus &SensorManager::tempStatus() const { return temp_; }

void SensorManager::teardownBus() {
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
    temp_.error = "disabled";
    return;
  }

  ow_ = new OneWire(temp_.pin);
  ds_ = new DallasTemperature(ow_);
  ds_->begin();

  uint8_t found[8];
  if (!ow_->search(found)) {
    temp_.detected = false;
    temp_.error = "not_detected";
    ow_->reset_search();
    if (logs_) logs_->add("temp_not_detected", 0, 0, temp_.pin);
    LRS_LOGW(SENSOR, "event=temp_not_detected pin=%u", static_cast<unsigned>(temp_.pin));
    return;
  }
  ow_->reset_search();

  memcpy(addr_, found, 8);
  has_addr_ = true;
  temp_.detected = true;
  temp_.address = formatAddress();
  temp_.error = "";
  ds_->setResolution(addr_, 12);
  if (logs_) logs_->add("temp_detected", 0, 0, temp_.pin);
  LRS_LOGI(SENSOR, "event=temp_detected pin=%u addr=%s", static_cast<unsigned>(temp_.pin), temp_.address.c_str());
}

String SensorManager::formatAddress() const {
  String out;
  for (size_t i = 0; i < 8; i++) {
    if (i) out += ":";
    if (addr_[i] < 16) out += "0";
    out += String(addr_[i], HEX);
  }
  out.toLowerCase();
  return out;
}
