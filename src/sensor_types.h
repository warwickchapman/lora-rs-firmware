#pragma once
#ifdef UNIT_TEST
#include <stdint.h>
#else
#include <Arduino.h>
#endif

enum class SensorKind : uint8_t {
  Input = 1,
  TemperatureC = 2,
  TankLevel = 3,
};

enum class SensorState : uint8_t {
  Disabled = 0,
  Missing = 1,
  Fault = 2,
  Ok = 3,
  Overrange = 4,
  Waiting = 5,
};

struct SensorReading {
  int32_t value;
  SensorKind kind;
  SensorState state;
  uint8_t instance;
  uint8_t scale;
};

static_assert(sizeof(SensorReading) == 8, "SensorReading must stay compact");

inline const char* sensorKindToString(SensorKind kind) {
  switch (kind) {
    case SensorKind::Input:
      return "input";
    case SensorKind::TemperatureC:
      return "temperature";
    case SensorKind::TankLevel:
      return "tank_level";
    default:
      return "unknown";
  }
}

inline const char* sensorStateToString(SensorState state) {
  switch (state) {
    case SensorState::Disabled:
      return "disabled";
    case SensorState::Missing:
      return "missing";
    case SensorState::Fault:
      return "fault";
    case SensorState::Ok:
      return "ok";
    case SensorState::Overrange:
      return "overrange";
    case SensorState::Waiting:
      return "waiting";
    default:
      return "unknown";
  }
}

inline const char* sensorKindToUnit(SensorKind kind) {
  switch (kind) {
    case SensorKind::Input:
      return "bool";
    case SensorKind::TemperatureC:
      return "c";
    case SensorKind::TankLevel:
      return "mm";
    default:
      return "";
  }
}
