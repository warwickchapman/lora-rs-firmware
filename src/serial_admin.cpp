#include "serial_admin.h"

namespace {
constexpr size_t kMaxLineBytes = 4096;
}

bool SerialAdmin::begin(AdminExecutor *executor) {
  executor_ = executor;
  input_.reserve(256);
  return true;
}

void SerialAdmin::tick() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (input_.length() > 0) {
        handleLine(input_);
        input_ = "";
      }
      continue;
    }
    if (input_.length() >= kMaxLineBytes) {
      input_ = "";
      Serial.println(F("LRS:{\"ok\":false,\"cmd\":\"unknown\",\"error\":\"line_too_long\"}"));
      continue;
    }
    input_ += c;
  }
}

void SerialAdmin::handleLine(const String &line) {
  if (!line.startsWith("LRS:"))
    return;
  if (executor_ == nullptr) {
    Serial.println(F("LRS:{\"ok\":false,\"cmd\":\"unknown\",\"error\":\"executor_unavailable\"}"));
    return;
  }
  String cmd = line.substring(4);
  executor_->execute(cmd.c_str(), cmd.length(), [](const String &response) {
    Serial.print(F("LRS:"));
    Serial.println(response);
  }, false);
}
