#include "logger.h"

#include <ESP8266WiFi.h>
#include <cstdarg>

namespace lrslog {
namespace {

#ifndef LRS_LOG_LEVEL_DEFAULT
#define LRS_LOG_LEVEL_DEFAULT 2
#endif

Level g_level = static_cast<Level>(LRS_LOG_LEVEL_DEFAULT);
UnixTimeProvider g_unix_provider;

const char *levelText(Level level) {
  switch (level) {
    case Level::ERROR:
      return "ERROR";
    case Level::WARN:
      return "WARN";
    case Level::DEBUG:
      return "DEBUG";
    case Level::INFO:
    default:
      return "INFO";
  }
}

const char *categoryText(Category cat) {
  switch (cat) {
    case Category::SYS:
      return "SYS";
    case Category::WIFI:
      return "WIFI";
    case Category::NTP:
      return "NTP";
    case Category::MDNS:
      return "MDNS";
    case Category::LORA:
      return "LORA";
    case Category::SENSOR:
      return "SENSOR";
    case Category::WEB:
      return "WEB";
    case Category::API:
      return "API";
    case Category::FS:
      return "FS";
    default:
      return "SYS";
  }
}

void logWithVaList(Level level, Category cat, uint32_t ms, uint32_t unixTimeS, const char *fmt, va_list ap) {
  if (!enabled(level)) return;
  char msg[384];
  msg[0] = '\0';
  if (fmt != nullptr && fmt[0] != '\0') {
    vsnprintf(msg, sizeof(msg), fmt, ap);
  }
  if (msg[0] != '\0') {
    if (unixTimeS != 0) {
      Serial.printf("[%s][%s] t=%lu unix=%lu %s\n",
                    levelText(level),
                    categoryText(cat),
                    static_cast<unsigned long>(ms),
                    static_cast<unsigned long>(unixTimeS),
                    msg);
      return;
    }
    Serial.printf("[%s][%s] t=%lu %s\n",
                  levelText(level),
                  categoryText(cat),
                  static_cast<unsigned long>(ms),
                  msg);
    return;
  }

  if (unixTimeS != 0) {
    Serial.printf("[%s][%s] t=%lu unix=%lu\n",
                  levelText(level),
                  categoryText(cat),
                  static_cast<unsigned long>(ms),
                  static_cast<unsigned long>(unixTimeS));
    return;
  }
  Serial.printf("[%s][%s] t=%lu\n", levelText(level), categoryText(cat), static_cast<unsigned long>(ms));
}

}  // namespace

void setLevel(Level level) { g_level = level; }

Level level() { return g_level; }

bool enabled(Level level) { return static_cast<uint8_t>(level) <= static_cast<uint8_t>(g_level); }

void setUnixTimeProvider(UnixTimeProvider provider) { g_unix_provider = provider; }

void logf(Level level, Category cat, const char *fmt, ...) {
  if (!enabled(level)) return;
  uint32_t unixTimeS = 0;
  if (g_unix_provider) {
    uint32_t candidate = 0;
    if (g_unix_provider(candidate)) {
      unixTimeS = candidate;
    }
  }
  va_list ap;
  va_start(ap, fmt);
  logWithVaList(level, cat, millis(), unixTimeS, fmt, ap);
  va_end(ap);
}

void logAtf(Level level, Category cat, uint32_t ms, uint32_t unixTimeS, const char *fmt, ...) {
  if (!enabled(level)) return;
  va_list ap;
  va_start(ap, fmt);
  logWithVaList(level, cat, ms, unixTimeS, fmt, ap);
  va_end(ap);
}

uint32_t heapFree() { return ESP.getFreeHeap(); }

uint8_t heapFragPercent() { return static_cast<uint8_t>(ESP.getHeapFragmentation()); }

uint32_t heapMaxFreeBlock() { return ESP.getMaxFreeBlockSize(); }

String redact(const String &value) { return value.length() ? String("<redacted>") : String(""); }

String maskSecret(const String &value, size_t keepPrefix, size_t keepSuffix) {
  if (value.length() == 0) return "";
  if (value.length() <= (keepPrefix + keepSuffix + 1U)) return "<redacted>";
  String out;
  out.reserve(value.length());
  out += value.substring(0, keepPrefix);
  out += "***";
  out += value.substring(value.length() - keepSuffix);
  return out;
}

}  // namespace lrslog

