#pragma once

#include <Arduino.h>

enum class TankSensorState : uint8_t {
  Disabled = 0,
  Ok = 1,
  FaultLow = 2,
  Overrange = 3,
};

inline const char *tankSensorStateText(TankSensorState state) {
  switch (state) {
    case TankSensorState::Ok:
      return "ok";
    case TankSensorState::FaultLow:
      return "fault_low";
    case TankSensorState::Overrange:
      return "overrange";
    default:
      return "disabled";
  }
}
