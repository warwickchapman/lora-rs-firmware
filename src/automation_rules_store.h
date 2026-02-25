#pragma once

#include <Arduino.h>

class Print;

namespace automation_rules {

constexpr size_t kMaxPayloadBytes = 6144;
constexpr size_t kMaxRules = 8;
constexpr size_t kMaxPredicatesPerRule = 4;
constexpr size_t kMaxActionsPerRule = 4;

struct SaveResult {
  bool ok = false;
  uint16_t http_status = 500;
  const char *error_code = "internal";
  size_t saved_bytes = 0;
  char detail[96] = {0};
};

class Store {
 public:
  static const char *rulesPath();
  static bool exists();
  static size_t defaultJsonLength();
  static void writeDefaultJson(Print &out);
  static SaveResult validateAndSave(const String &body);
};

}  // namespace automation_rules
