#pragma once

#include <Arduino.h>
#include <array>
#include <functional>

struct LogItem {
  static constexpr size_t kEventBytes = 32;
  uint32_t ms;
  uint32_t unix_time_s;
  char event[kEventBytes];
  int rssi;
  uint32_t counter;
  uint8_t state;
};

class LogBuffer {
 public:
  using TimeProvider = std::function<bool(uint32_t &)>;

  void setTimeProvider(TimeProvider provider);
  void add(const char *event, int rssi, uint32_t counter, uint8_t state);
  void add(const String &event, int rssi, uint32_t counter, uint8_t state);
  String asCsv() const;
  String asText() const;
  template <typename Fn>
  void forEachEntry(Fn fn) const {
    for (size_t i = 0; i < count_; ++i) {
      const size_t idx = (head_ + i) % kMaxEntries;
      fn(entries_[idx]);
    }
  }

 private:
  TimeProvider time_provider_;
  static constexpr size_t kMaxEntries = 48;
  std::array<LogItem, kMaxEntries> entries_{};
  size_t head_ = 0;   // oldest entry index
  size_t count_ = 0;  // number of valid entries
};
