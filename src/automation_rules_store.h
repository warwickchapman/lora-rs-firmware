#pragma once

#include <Arduino.h>

namespace automation_rules {

constexpr size_t kMaxPayloadBytes = 6144;
constexpr size_t kMaxRules = 8;
constexpr size_t kMaxPredicatesPerRule = 4;
constexpr size_t kMaxActionsPerRule = 4;

class Store {
 public:
  static const char *rulesPath();
  static bool exists();
};

}  // namespace automation_rules
