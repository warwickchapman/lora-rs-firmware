#pragma once

#include <Arduino.h>
#include "admin_executor.h"

class SerialAdmin {
public:
  bool begin(AdminExecutor *executor);
  void tick();
private:
  AdminExecutor *executor_ = nullptr;
  String input_;
  void handleLine(const String &line);
};
