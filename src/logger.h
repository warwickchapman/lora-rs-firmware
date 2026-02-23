#pragma once

#include <Arduino.h>

#include <functional>

namespace lrslog {

enum class Level : uint8_t {
  ERROR = 0,
  WARN = 1,
  INFO = 2,
  DEBUG = 3,
};

enum class Category : uint8_t {
  SYS,
  WIFI,
  NTP,
  MDNS,
  LORA,
  SENSOR,
  WEB,
  API,
  FS,
};

using UnixTimeProvider = std::function<bool(uint32_t &)>;

void setLevel(Level level);
Level level();
bool enabled(Level level);

void setUnixTimeProvider(UnixTimeProvider provider);

void logf(Level level, Category cat, const char *fmt, ...);
void logAtf(Level level, Category cat, uint32_t ms, uint32_t unixTimeS, const char *fmt, ...);

uint32_t heapFree();
uint8_t heapFragPercent();
uint32_t heapMaxFreeBlock();

String maskSecret(const String &value, size_t keepPrefix = 2, size_t keepSuffix = 2);
String redact(const String &value);

}  // namespace lrslog

#define LRS_LOGE(CAT, FMT, ...) ::lrslog::logf(::lrslog::Level::ERROR, ::lrslog::Category::CAT, FMT, ##__VA_ARGS__)
#define LRS_LOGW(CAT, FMT, ...) ::lrslog::logf(::lrslog::Level::WARN, ::lrslog::Category::CAT, FMT, ##__VA_ARGS__)
#define LRS_LOGI(CAT, FMT, ...) ::lrslog::logf(::lrslog::Level::INFO, ::lrslog::Category::CAT, FMT, ##__VA_ARGS__)
#define LRS_LOGD(CAT, FMT, ...) ::lrslog::logf(::lrslog::Level::DEBUG, ::lrslog::Category::CAT, FMT, ##__VA_ARGS__)

