#pragma once

#include <Arduino.h>
#include <functional>
#include <deque>

struct LogItem {
  uint32_t ms;
  uint32_t unix_time_s;
  String event;
  int rssi;
  uint32_t counter;
  uint8_t state;
};

class LogBuffer {
 public:
  using TimeProvider = std::function<bool(uint32_t &)>;

  void setTimeProvider(TimeProvider provider);
  void add(const String &event, int rssi, uint32_t counter, uint8_t state);
  String asCsv() const;
  String asText() const;
  const std::deque<LogItem> &entries() const;

 private:
  TimeProvider time_provider_;
  std::deque<LogItem> entries_;
  static constexpr size_t kMaxEntries = 200;
};
