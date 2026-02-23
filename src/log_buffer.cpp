#include "log_buffer.h"

#include <Arduino.h>

#include "logger.h"

namespace {
lrslog::Category classifyCategory(const String &event) {
  if (event.startsWith("sta_") || event.startsWith("wifi_")) return lrslog::Category::WIFI;
  if (event.startsWith("ntp_")) return lrslog::Category::NTP;
  if (event.startsWith("mdns_")) return lrslog::Category::MDNS;
  if (event.startsWith("temp_")) return lrslog::Category::SENSOR;

  if (event == "time_sync_ntp") return lrslog::Category::NTP;
  if (event == "time_sync_peer") return lrslog::Category::LORA;

  if (event.startsWith("tx_") || event.startsWith("rx_") || event.startsWith("mqtt_remote_") || event.startsWith("prov_") ||
      event.startsWith("wifi_prov_") || event.startsWith("factory_reset_") || event == "radio_start_failed") {
    return lrslog::Category::LORA;
  }
  return lrslog::Category::SYS;
}

bool containsAny(const String &event, const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr) {
  if (a && event.indexOf(a) >= 0) return true;
  if (b && event.indexOf(b) >= 0) return true;
  if (c && event.indexOf(c) >= 0) return true;
  if (d && event.indexOf(d) >= 0) return true;
  return false;
}

lrslog::Level classifyLevel(const String &event) {
  if (event == "radio_start_failed") return lrslog::Level::ERROR;
  if (containsAny(event, "invalid_size", "bad_mac", "wrong_address", "wrong_source") || containsAny(event, "filtered_source", "replay_drop")) {
    return lrslog::Level::DEBUG;  // frequent noise in the field; available when needed
  }
  if (event == "tx_packet" || event == "rx_packet" || event == "temp_read_ok") return lrslog::Level::DEBUG;
  if (containsAny(event, "_failed", "_fail", "_timeout", "_bad") ||
      containsAny(event, "_orphan", "_hash_fail", "_incomplete")) {
    return lrslog::Level::WARN;
  }
  return lrslog::Level::INFO;
}
}  // namespace

void LogBuffer::setTimeProvider(TimeProvider provider) { time_provider_ = provider; }

void LogBuffer::add(const String &event, int rssi, uint32_t counter, uint8_t state) {
  if (entries_.size() >= kMaxEntries) {
    entries_.pop_front();
  }
  uint32_t unixTimeS = 0;
  if (time_provider_) {
    uint32_t candidate = 0;
    if (time_provider_(candidate)) {
      unixTimeS = candidate;
    }
  }
  const LogItem item{millis(), unixTimeS, event, rssi, counter, state};
  entries_.push_back(item);

  // Mirror ring-buffer events to serial using structured logs for live diagnostics.
  lrslog::logAtf(classifyLevel(item.event),
                 classifyCategory(item.event),
                 item.ms,
                 item.unix_time_s,
                 "event=%s rssi=%d counter=%lu state=%u",
                 item.event.c_str(),
                 item.rssi,
                 static_cast<unsigned long>(item.counter),
                 static_cast<unsigned>(item.state));
}

String LogBuffer::asCsv() const {
  String out = "millis,unix_time_s,event,rssi,counter,state\n";
  for (const auto &e : entries_) {
    out += String(e.ms) + "," + String(e.unix_time_s) + "," + e.event + "," + String(e.rssi) + "," + String(e.counter) + "," +
           String(e.state) + "\n";
  }
  return out;
}

String LogBuffer::asText() const {
  String out;
  for (const auto &e : entries_) {
    out += "[LRS] t=" + String(e.ms);
    if (e.unix_time_s != 0) {
      out += " unix=" + String(e.unix_time_s);
    }
    out += " event=" + e.event + " rssi=" + String(e.rssi) + " counter=" + String(e.counter) + " state=" + String(e.state) + "\n";
  }
  return out;
}

const std::deque<LogItem> &LogBuffer::entries() const { return entries_; }
