#pragma once

#include <Arduino.h>
#include <deque>

struct LogItem {
  uint32_t ms;
  String event;
  int rssi;
  uint32_t counter;
  uint8_t state;
};

class LogBuffer {
 public:
  void add(const String &event, int rssi, uint32_t counter, uint8_t state);
  String asCsv() const;
  String asText() const;
  const std::deque<LogItem> &entries() const;

 private:
  std::deque<LogItem> entries_;
  static constexpr size_t kMaxEntries = 200;
};
