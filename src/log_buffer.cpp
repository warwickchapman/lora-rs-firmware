#include "log_buffer.h"

#include <Arduino.h>

void LogBuffer::add(const String &event, int rssi, uint32_t counter, uint8_t state) {
  if (entries_.size() >= kMaxEntries) {
    entries_.pop_front();
  }
  const LogItem item{millis(), event, rssi, counter, state};
  entries_.push_back(item);

  // Mirror structured log events to serial for live field diagnostics.
  Serial.printf("[LRS] t=%lu event=%s rssi=%d counter=%lu state=%u\n",
                static_cast<unsigned long>(item.ms),
                item.event.c_str(),
                item.rssi,
                static_cast<unsigned long>(item.counter),
                static_cast<unsigned>(item.state));
}

String LogBuffer::asCsv() const {
  String out = "millis,event,rssi,counter,state\n";
  for (const auto &e : entries_) {
    out += String(e.ms) + "," + e.event + "," + String(e.rssi) + "," + String(e.counter) + "," + String(e.state) + "\n";
  }
  return out;
}

String LogBuffer::asText() const {
  String out;
  for (const auto &e : entries_) {
    out += "[LRS] t=" + String(e.ms) + " event=" + e.event + " rssi=" + String(e.rssi) + " counter=" + String(e.counter) +
           " state=" + String(e.state) + "\n";
  }
  return out;
}

const std::deque<LogItem> &LogBuffer::entries() const { return entries_; }
