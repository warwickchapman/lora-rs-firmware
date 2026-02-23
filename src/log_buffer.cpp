#include "log_buffer.h"

#include <Arduino.h>
#include <cstring>

#include "logger.h"

namespace {
bool startsWith(const char *event, const char *prefix);

void copyEventName(char *dst, size_t dstLen, const char *src) {
  if (dstLen == 0) return;
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

lrslog::Category classifyCategory(const char *event) {
  if (event == nullptr) return lrslog::Category::SYS;
  if (startsWith(event, "sta_") || startsWith(event, "wifi_")) return lrslog::Category::WIFI;
  if (startsWith(event, "ntp_")) return lrslog::Category::NTP;
  if (startsWith(event, "mdns_")) return lrslog::Category::MDNS;
  if (startsWith(event, "temp_")) return lrslog::Category::SENSOR;

  if (strcmp(event, "time_sync_ntp") == 0) return lrslog::Category::NTP;
  if (strcmp(event, "time_sync_peer") == 0) return lrslog::Category::LORA;

  if (startsWith(event, "tx_") || startsWith(event, "rx_") || startsWith(event, "mqtt_remote_") || startsWith(event, "prov_") ||
      startsWith(event, "wifi_prov_") || startsWith(event, "factory_reset_") || strcmp(event, "radio_start_failed") == 0) {
    return lrslog::Category::LORA;
  }
  return lrslog::Category::SYS;
}

bool startsWith(const char *event, const char *prefix) {
  if (event == nullptr || prefix == nullptr) return false;
  const size_t n = strlen(prefix);
  return strncmp(event, prefix, n) == 0;
}

bool containsAny(const char *event, const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr) {
  if (event == nullptr) return false;
  if (a && strstr(event, a) != nullptr) return true;
  if (b && strstr(event, b) != nullptr) return true;
  if (c && strstr(event, c) != nullptr) return true;
  if (d && strstr(event, d) != nullptr) return true;
  return false;
}

lrslog::Level classifyLevel(const char *event) {
  if (event != nullptr && strcmp(event, "radio_start_failed") == 0) return lrslog::Level::ERROR;
  if (containsAny(event, "invalid_size", "bad_mac", "wrong_address", "wrong_source") || containsAny(event, "filtered_source", "replay_drop")) {
    return lrslog::Level::DEBUG;  // frequent noise in the field; available when needed
  }
  if (event != nullptr &&
      (strcmp(event, "tx_packet") == 0 || strcmp(event, "rx_packet") == 0 || strcmp(event, "temp_read_ok") == 0)) {
    return lrslog::Level::DEBUG;
  }
  if (containsAny(event, "_failed", "_fail", "_timeout", "_bad") ||
      containsAny(event, "_orphan", "_hash_fail", "_incomplete")) {
    return lrslog::Level::WARN;
  }
  return lrslog::Level::INFO;
}
}  // namespace

void LogBuffer::setTimeProvider(TimeProvider provider) { time_provider_ = provider; }

void LogBuffer::add(const char *event, int rssi, uint32_t counter, uint8_t state) {
  uint32_t unixTimeS = 0;
  if (time_provider_) {
    uint32_t candidate = 0;
    if (time_provider_(candidate)) {
      unixTimeS = candidate;
    }
  }
  LogItem item{};
  item.ms = millis();
  item.unix_time_s = unixTimeS;
  copyEventName(item.event, LogItem::kEventBytes, event);
  item.rssi = rssi;
  item.counter = counter;
  item.state = state;
  if (count_ < kMaxEntries) {
    const size_t idx = (head_ + count_) % kMaxEntries;
    entries_[idx] = item;
    ++count_;
  } else {
    entries_[head_] = item;
    head_ = (head_ + 1) % kMaxEntries;
  }

  // Mirror ring-buffer events to serial using structured logs for live diagnostics.
  if (strcmp(item.event, "tx_prov") == 0) {
    if (item.state == 0xFF) {
      lrslog::logAtf(classifyLevel(item.event),
                     classifyCategory(item.event),
                     item.ms,
                     item.unix_time_s,
                     "event=%s target=bcast rssi=%d counter=%lu state=%u",
                     item.event,
                     item.rssi,
                     static_cast<unsigned long>(item.counter),
                     static_cast<unsigned>(item.state));
    } else {
      lrslog::logAtf(classifyLevel(item.event),
                     classifyCategory(item.event),
                     item.ms,
                     item.unix_time_s,
                     "event=%s target=0x%02X rssi=%d counter=%lu state=%u",
                     item.event,
                     static_cast<unsigned>(item.state),
                     item.rssi,
                     static_cast<unsigned long>(item.counter),
                     static_cast<unsigned>(item.state));
    }
    return;
  }

  lrslog::logAtf(classifyLevel(item.event),
                 classifyCategory(item.event),
                 item.ms,
                 item.unix_time_s,
                 "event=%s rssi=%d counter=%lu state=%u",
                 item.event,
                 item.rssi,
                 static_cast<unsigned long>(item.counter),
                 static_cast<unsigned>(item.state));
}

void LogBuffer::add(const String &event, int rssi, uint32_t counter, uint8_t state) {
  add(event.c_str(), rssi, counter, state);
}

String LogBuffer::asCsv() const {
  String out = "millis,unix_time_s,event,rssi,counter,state\n";
  forEachEntry([&out](const LogItem &e) {
    out += String(e.ms) + "," + String(e.unix_time_s) + "," + String(e.event) + "," + String(e.rssi) + "," + String(e.counter) + "," +
           String(e.state) + "\n";
  });
  return out;
}

String LogBuffer::asText() const {
  String out;
  forEachEntry([&out](const LogItem &e) {
    out += "[LRS] t=" + String(e.ms);
    if (e.unix_time_s != 0) {
      out += " unix=" + String(e.unix_time_s);
    }
    out += " event=" + String(e.event) + " rssi=" + String(e.rssi) + " counter=" + String(e.counter) + " state=" + String(e.state) + "\n";
  });
  return out;
}
