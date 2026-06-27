#include "logger.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <cstdarg>
#include <cstring>

namespace lrslog {
namespace {

#ifndef LRS_LOG_LEVEL_DEFAULT
#define LRS_LOG_LEVEL_DEFAULT 2
#endif

Level g_level = static_cast<Level>(LRS_LOG_LEVEL_DEFAULT);
UnixTimeProviderFn g_unix_provider = nullptr;
void *g_unix_provider_ctx = nullptr;
WiFiUDP g_udp;
bool g_udp_enabled = false;
IPAddress g_udp_host;
uint16_t g_udp_port = 0;
uint32_t g_udp_until_ms = 0;

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

bool startsWith(const char *s, const char *prefix) {
  if (s == nullptr || prefix == nullptr) return false;
  const size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

bool containsAny(const char *s, const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr) {
  if (s == nullptr) return false;
  if (a && strstr(s, a) != nullptr) return true;
  if (b && strstr(s, b) != nullptr) return true;
  if (c && strstr(s, c) != nullptr) return true;
  if (d && strstr(s, d) != nullptr) return true;
  return false;
}

lrslog::Category classifyEventCategory(const char *event) {
  if (event == nullptr) return lrslog::Category::SYS;
  if (startsWith(event, "sta_") || startsWith(event, "wifi_")) return lrslog::Category::WIFI;
  if (startsWith(event, "ntp_")) return lrslog::Category::NTP;
  if (startsWith(event, "temp_")) return lrslog::Category::SENSOR;
  if (strcmp(event, "time_sync_ntp") == 0) return lrslog::Category::NTP;
  if (strcmp(event, "time_sync_peer") == 0) return lrslog::Category::LORA;
  if (startsWith(event, "tx_") || startsWith(event, "rx_") || startsWith(event, "mqtt_remote_") || startsWith(event, "prov_") ||
      startsWith(event, "wifi_prov_") || startsWith(event, "factory_reset_") || strcmp(event, "radio_start_failed") == 0) {
    return lrslog::Category::LORA;
  }
  return lrslog::Category::SYS;
}

lrslog::Level classifyEventLevel(const char *event) {
  if (event != nullptr && strcmp(event, "radio_start_failed") == 0) return lrslog::Level::ERROR;
  if (containsAny(event, "invalid_size", "bad_mac", "wrong_address", "wrong_source") || containsAny(event, "filtered_source", "replay_drop")) {
    return lrslog::Level::DEBUG;
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

void emitUdpMirrorLine(const char *line) {
  if (!g_udp_enabled || line == nullptr || line[0] == '\0') return;
  if (g_udp_until_ms != 0 && static_cast<int32_t>(millis() - g_udp_until_ms) >= 0) {
    g_udp_enabled = false;
    g_udp_port = 0;
    g_udp_until_ms = 0;
    return;
  }
  if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0) return;
  if (!g_udp.beginPacket(g_udp_host, g_udp_port)) return;
  g_udp.write(reinterpret_cast<const uint8_t *>(line), strlen(line));
  g_udp.endPacket();
}

void logWithVaList(Level level, Category cat, uint32_t ms, uint32_t unixTimeS, const char *fmt, va_list ap) {
  if (!enabled(level)) return;
  char msg[384];
  char line[512];
  msg[0] = '\0';
  line[0] = '\0';
  if (fmt != nullptr && fmt[0] != '\0') {
    vsnprintf(msg, sizeof(msg), fmt, ap);
  }
  if (msg[0] != '\0') {
    if (unixTimeS != 0) {
      snprintf(line,
               sizeof(line),
               "[%s][%s] t=%lu unix=%lu %s\n",
               levelText(level),
               categoryText(cat),
               static_cast<unsigned long>(ms),
               static_cast<unsigned long>(unixTimeS),
               msg);
      Serial.print(line);
      emitUdpMirrorLine(line);
      return;
    }
    snprintf(line,
             sizeof(line),
             "[%s][%s] t=%lu %s\n",
             levelText(level),
             categoryText(cat),
             static_cast<unsigned long>(ms),
             msg);
    Serial.print(line);
    emitUdpMirrorLine(line);
    return;
  }

  if (unixTimeS != 0) {
    snprintf(line,
             sizeof(line),
             "[%s][%s] t=%lu unix=%lu\n",
             levelText(level),
             categoryText(cat),
             static_cast<unsigned long>(ms),
             static_cast<unsigned long>(unixTimeS));
    Serial.print(line);
    emitUdpMirrorLine(line);
    return;
  }
  snprintf(line, sizeof(line), "[%s][%s] t=%lu\n", levelText(level), categoryText(cat), static_cast<unsigned long>(ms));
  Serial.print(line);
  emitUdpMirrorLine(line);
}

}  // namespace

void setLevel(Level level) { g_level = level; }

Level level() { return g_level; }

bool enabled(Level level) { return static_cast<uint8_t>(level) <= static_cast<uint8_t>(g_level); }

void setUnixTimeProvider(UnixTimeProviderFn provider, void *context) {
  g_unix_provider = provider;
  g_unix_provider_ctx = context;
}

void setUdpMirror(const IPAddress &host, uint16_t port, uint32_t ttlMs) {
  g_udp_host = host;
  g_udp_port = port;
  g_udp_enabled = (port != 0);
  g_udp_until_ms = (g_udp_enabled && ttlMs != 0) ? (millis() + ttlMs) : 0;
}

void disableUdpMirror() {
  g_udp_enabled = false;
  g_udp_port = 0;
  g_udp_until_ms = 0;
}

bool udpMirrorEnabled() {
  if (!g_udp_enabled) return false;
  if (g_udp_until_ms != 0 && static_cast<int32_t>(millis() - g_udp_until_ms) >= 0) {
    g_udp_enabled = false;
    g_udp_port = 0;
    g_udp_until_ms = 0;
    return false;
  }
  return true;
}

uint32_t udpMirrorRemainingMs() {
  if (!udpMirrorEnabled()) return 0;
  if (g_udp_until_ms == 0) return 0;
  return g_udp_until_ms - millis();
}

void logf(Level level, Category cat, const char *fmt, ...) {
  if (!enabled(level)) return;
  uint32_t unixTimeS = 0;
  if (g_unix_provider) {
    uint32_t candidate = 0;
    if (g_unix_provider(g_unix_provider_ctx, candidate)) {
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

void event(const char *eventName, int rssi, uint32_t counter, uint8_t state) {
  const uint32_t ms = millis();
  uint32_t unixTimeS = 0;
  if (g_unix_provider) {
    uint32_t candidate = 0;
    if (g_unix_provider(g_unix_provider_ctx, candidate)) unixTimeS = candidate;
  }

  if (eventName != nullptr && strcmp(eventName, "tx_prov") == 0) {
    if (state == 0xFF) {
      logAtf(classifyEventLevel(eventName),
             classifyEventCategory(eventName),
             ms,
             unixTimeS,
             "event=%s target=bcast rssi=%d counter=%lu state=%u",
             eventName,
             rssi,
             static_cast<unsigned long>(counter),
             static_cast<unsigned>(state));
      return;
    }
    logAtf(classifyEventLevel(eventName),
           classifyEventCategory(eventName),
           ms,
           unixTimeS,
           "event=%s target=0x%02X rssi=%d counter=%lu state=%u",
           eventName,
           static_cast<unsigned>(state),
           rssi,
           static_cast<unsigned long>(counter),
           static_cast<unsigned>(state));
    return;
  }

  logAtf(classifyEventLevel(eventName),
         classifyEventCategory(eventName),
         ms,
         unixTimeS,
         "event=%s rssi=%d counter=%lu state=%u",
         (eventName != nullptr ? eventName : ""),
         rssi,
         static_cast<unsigned long>(counter),
         static_cast<unsigned>(state));
}

void event(const String &eventName, int rssi, uint32_t counter, uint8_t state) { event(eventName.c_str(), rssi, counter, state); }

uint32_t heapFree() { return ESP.getFreeHeap(); }

uint8_t heapFragPercent() { return static_cast<uint8_t>(ESP.getHeapFragmentation()); }

uint32_t heapMaxFreeBlock() { return ESP.getMaxFreeBlockSize(); }


void maskSecret(char *dest, size_t destSize, const char *src, size_t keepPrefix, size_t keepSuffix) {
  if (destSize == 0) return;
  if (src == nullptr || strlen(src) == 0) {
    dest[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len <= (keepPrefix + keepSuffix + 1U)) {
    strlcpy(dest, "<redacted>", destSize);
    return;
  }
  size_t pos = 0;
  for (size_t i = 0; i < keepPrefix && pos < destSize - 1; ++i) {
    dest[pos++] = src[i];
  }
  const char *stars = "***";
  for (size_t i = 0; i < 3 && pos < destSize - 1; ++i) {
    dest[pos++] = stars[i];
  }
  size_t suffixStart = len - keepSuffix;
  for (size_t i = 0; i < keepSuffix && pos < destSize - 1; ++i) {
    dest[pos++] = src[suffixStart + i];
  }
  dest[pos] = '\0';
}

}  // namespace lrslog
