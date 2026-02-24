#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Updater.h>
#include <cstring>

#include "build_info.h"
#include "config_store.h"
#include "logger.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "web_console_ui_assets.h"

#ifndef LRS_ENABLE_MDNS
#define LRS_ENABLE_MDNS 1
#endif

namespace {
constexpr uint32_t kMinHeartbeatMs = 60000;
constexpr uint32_t kMaxHeartbeatMs = 3600000;
constexpr uint32_t kMinAckTimeoutMs = 5 * 1000;
constexpr uint32_t kMaxAckTimeoutMs = 600 * 1000;
constexpr uint32_t kMinMqttRemoteRetryTimeoutMs = 5 * 1000;
constexpr uint32_t kMaxMqttRemoteRetryTimeoutMs = 3600 * 1000;
constexpr uint32_t kMinTxPollDefaultIntervalMs = 60 * 1000;
constexpr uint32_t kMaxTxPollDefaultIntervalMs = 3600 * 1000;
constexpr uint32_t kMinRxPushIntervalMs = 60 * 1000;
constexpr uint32_t kMaxRxPushIntervalMs = 3600 * 1000;
constexpr const char *kDefaultDeploymentKey = "lora-default-passphrase";
constexpr size_t kMinDeploymentKeyLen = 16;
constexpr const char *kHardwareVersion = "v1.2";
constexpr const char *kHardwareBatch = "251101";
constexpr uint32_t kLowHeapWarnThresholdBytes = 14000;
constexpr uint32_t kLowHeapWarnMinIntervalMs = 5000;
constexpr uint32_t kApiLightLowHeapRejectFreeBytes = 3000;
constexpr uint32_t kApiLightLowHeapRejectMaxBlockBytes = 1200;
constexpr uint32_t kApiLowHeapRejectFreeBytes = 6500;
constexpr uint32_t kApiLowHeapRejectMaxBlockBytes = 2500;
constexpr uint32_t kApiStatusLiveLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiStatusLiveLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiStatusStaticLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiStatusStaticLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiFleetLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiFleetLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiProvStatusCompactFreeBytes = 3500;
constexpr uint32_t kApiProvStatusCompactMaxBlockBytes = 1400;
constexpr uint32_t kIndexLowHeapRejectFreeBytes = 3800;
constexpr uint32_t kIndexLowHeapRejectMaxBlockBytes = 2400;
constexpr uint32_t kStatusLiveCacheTtlMs = 1500;
constexpr uint32_t kStatusStaticCacheTtlMs = 15000;
constexpr uint32_t kStatusLiteCacheTtlMs = 1000;
constexpr size_t kStatusCacheReserveBytes = 1600;
constexpr size_t kStatusLiveCacheReserveBytes = 1024;
constexpr size_t kStatusStaticCacheReserveBytes = 1024;
constexpr size_t kStatusLiteCacheReserveBytes = 384;
constexpr uint32_t kStatusLiveSseKeepAliveMs = 15000;
constexpr uint32_t kStatusLiveSseConnectMinFreeBytes = 5000;
constexpr uint32_t kStatusLiveSseConnectMinMaxBlockBytes = 2000;
constexpr uint32_t kStatusLiveSsePushHealthyMs = 2000;
constexpr uint32_t kStatusLiveSsePushWarnMs = 4000;
constexpr uint32_t kStatusLiveSsePushPressureMs = 8000;
constexpr uint32_t kStatusLiveSsePushSevereMs = 12000;
constexpr uint32_t kStatusLiveSsePressureWindowMs = 12000;
constexpr uint32_t kWebRequestPressureDurMs = 80;
constexpr int kStaTestMaxAttempts = 40;  // 40 * 100ms = 4s max blocking window (commissioning only)
constexpr size_t kFleetDocBaseBytes = 384;
constexpr size_t kFleetDocPerPeerBytes = 256;
constexpr size_t kFleetDocMinBytes = 1024;
constexpr size_t kFleetDocMaxBytes = 4096;

#ifdef REGION_US
constexpr long kMinFrequencyHz = 902000000L;
constexpr long kMaxFrequencyHz = 928000000L;
constexpr long kDefaultFrequencyHz = 915000000L;
#else
constexpr long kMinFrequencyHz = 433000000L;
constexpr long kMaxFrequencyHz = 434790000L;
constexpr long kDefaultFrequencyHz = 433000000L;
#endif

bool isOwnLrsSoftApLike(const String &ssid) {
  String s = ssid;
  s.toLowerCase();
  if (!s.startsWith("lrs-")) return false;
  if (s.endsWith("-tx") || s.endsWith("-rx")) return true;
  // New role-independent AP naming: lrs-<8 hex chars>
  if (s.length() != 12) return false;
  for (size_t i = 4; i < 12; ++i) {
    const char c = s.charAt(i);
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!isHex) return false;
  }
  return true;
}

const char *wifiStatusText(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_found";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
#ifdef WL_WRONG_PASSWORD
    case WL_WRONG_PASSWORD:
      return "wrong_password";
#endif
#ifdef WL_NO_SHIELD
    case WL_NO_SHIELD:
      return "no_shield";
#endif
    default:
      return "unknown";
  }
}

const char *httpMethodText(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    default:
      return "OTHER";
  }
}

const char *remoteAckStateText(PeerAckState s) {
  switch (s) {
    case PeerAckState::Pending:
      return "pending";
    case PeerAckState::Ok:
      return "ok";
    case PeerAckState::Timeout:
      return "timeout";
    case PeerAckState::Unknown:
    default:
      return "unknown";
  }
}

const char *provisioningSessionStateText(ProvisioningSessionState s) {
  switch (s) {
    case ProvisioningSessionState::Discovering:
      return "discovering";
    case ProvisioningSessionState::DiscoveryRetry:
      return "discovery_retry";
    case ProvisioningSessionState::Ready:
      return "ready";
    case ProvisioningSessionState::Provisioning:
      return "provisioning";
    case ProvisioningSessionState::Complete:
      return "complete";
    case ProvisioningSessionState::Error:
      return "error";
    case ProvisioningSessionState::Idle:
    default:
      return "idle";
  }
}

const char *provisioningDeviceStateText(ProvisioningDeviceState s) {
  switch (s) {
    case ProvisioningDeviceState::Assigned:
      return "assigned";
    case ProvisioningDeviceState::Keying:
      return "keying";
    case ProvisioningDeviceState::AwaitVerify:
      return "await_verify";
    case ProvisioningDeviceState::Verified:
      return "verified";
    case ProvisioningDeviceState::Failed:
      return "failed";
    case ProvisioningDeviceState::Skipped:
      return "skipped";
    case ProvisioningDeviceState::Discovered:
    default:
      return "discovered";
  }
}

uint8_t parseAddressField(const JsonVariantConst &value, uint8_t fallback) {
  if (value.isNull()) {
    return fallback;
  }
  if (value.is<uint8_t>() || value.is<int>()) {
    const int n = value.as<int>();
    if (n >= 1 && n <= 254) return static_cast<uint8_t>(n);
    return fallback;
  }
  String text = String(static_cast<const char *>(value.as<const char *>()));
  text.trim();
  if (text.length() == 0) return fallback;

  long n = -1;
  if (text.startsWith("0x") || text.startsWith("0X")) {
    n = strtol(text.c_str(), nullptr, 16);
  } else {
    n = strtol(text.c_str(), nullptr, 10);
  }
  if (n < 1 || n > 254) {
    return fallback;
  }
  return static_cast<uint8_t>(n);
}

uint8_t parseAddressText(const String &text, uint8_t fallback) {
  String t = text;
  t.trim();
  if (t.length() == 0) return fallback;
  long n = -1;
  if (t.startsWith("0x") || t.startsWith("0X")) {
    n = strtol(t.c_str(), nullptr, 16);
  } else {
    n = strtol(t.c_str(), nullptr, 10);
  }
  if (n < 1 || n > 254) return fallback;
  return static_cast<uint8_t>(n);
}

bool parseBoolField(const JsonVariantConst &value, bool fallback) {
  if (value.isNull()) return fallback;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<int>()) return value.as<int>() != 0;
  const char *raw = value.as<const char *>();
  if (!raw) return fallback;
  String text(raw);
  text.trim();
  text.toLowerCase();
  if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
  if (text == "false" || text == "0" || text == "no" || text == "off") return false;
  return fallback;
}

bool parseIpField(const JsonVariantConst &value, IPAddress &out) {
  if (value.isNull()) return false;
  const char *raw = value.as<const char *>();
  if (!raw) return false;
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
  if (*raw == '\0') return false;
  const char *end = raw + strlen(raw);
  while (end > raw && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
  char buf[32];
  const size_t n = static_cast<size_t>(end - raw);
  if (n == 0 || n >= sizeof(buf)) return false;
  memcpy(buf, raw, n);
  buf[n] = '\0';
  IPAddress ip;
  if (!ip.fromString(buf)) return false;
  out = ip;
  return true;
}

bool parseUint16Field(const JsonVariantConst &value, uint16_t &out) {
  if (value.isNull()) return false;
  long parsed = -1;
  if (value.is<uint16_t>() || value.is<int>()) {
    parsed = static_cast<long>(value.as<int>());
  } else {
    const char *raw = value.as<const char *>();
    if (!raw) return false;
    char *end = nullptr;
    parsed = strtol(raw, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (!end || *end != '\0') return false;
  }
  if (parsed < 1 || parsed > 65535) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
}

bool parseUint32FieldRange(const JsonVariantConst &value, uint32_t minValue, uint32_t maxValue, uint32_t &out) {
  if (value.isNull()) return false;
  long parsed = -1;
  if (value.is<uint32_t>() || value.is<int>()) {
    parsed = static_cast<long>(value.as<int>());
  } else {
    const char *raw = value.as<const char *>();
    if (!raw) return false;
    char *end = nullptr;
    parsed = strtol(raw, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (!end || *end != '\0') return false;
  }
  if (parsed < 0) return false;
  const uint32_t v = static_cast<uint32_t>(parsed);
  if (v < minValue || v > maxValue) return false;
  out = v;
  return true;
}

bool softApActiveNow() {
  const IPAddress apIp = WiFi.softAPIP();
  return apIp[0] != 0;
}

bool isDefaultDeploymentKey(const String &v) {
  String k = v;
  k.trim();
  return k == kDefaultDeploymentKey;
}

const char *linkStateText(LinkState st) {
  switch (st) {
    case LinkState::Boot:
      return "boot";
    case LinkState::Idle:
      return "idle";
    case LinkState::WaitAck:
      return "wait_ack";
    case LinkState::Timeout:
      return "timeout";
  }
  return "unknown";
}
}

bool WebConsole::begin(ConfigStore *config,
                       NodeStateMachine *sm,
                       SensorManager *sensors,
                       std::function<void(bool, bool)> onApply) {
  config_ = config;
  sm_ = sm;
  sensors_ = sensors;
  on_apply_ = onApply;
  server_.collectHeaders("Cookie", "User-Agent");
  initStatusCaches();

  routes();
  server_.begin();
  return true;
}

void WebConsole::tick() {
  server_.handleClient();
  tickStatusLiveSse();
}

void WebConsole::beginRequestLog(const char *path, bool api, bool poll, bool heapDiag) {
  request_log_.active = true;
  request_log_.api = api;
  request_log_.poll = poll;
  request_log_.heap_diag = heapDiag;
  request_log_.started_ms = millis();
  request_log_.status = 0;
  request_log_.path = path ? String(path) : server_.uri();
  request_log_.client_ip = server_.client().remoteIP().toString();
}

void WebConsole::finishRequestLog() {
  if (!request_log_.active) return;

  const uint32_t endMs = millis();
  const uint32_t durMs = endMs - request_log_.started_ms;
  const int status = request_log_.status;
  const String path = request_log_.path.length() ? request_log_.path : server_.uri();
  const String ip = request_log_.client_ip;
  const uint32_t freeHeap = lrslog::heapFree();
  const uint8_t heapFrag = lrslog::heapFragPercent();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const lrslog::Category cat = request_log_.api ? lrslog::Category::API : lrslog::Category::WEB;
  const lrslog::Level level = request_log_.poll ? lrslog::Level::DEBUG : lrslog::Level::INFO;

  if (request_log_.heap_diag) {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu",
                 httpMethodText(server_.method()),
                 path.c_str(),
                 status,
                 static_cast<unsigned long>(durMs),
                 ip.c_str(),
                 static_cast<unsigned long>(freeHeap),
                 static_cast<unsigned>(heapFrag),
                 static_cast<unsigned long>(maxBlock));
    if (freeHeap < kLowHeapWarnThresholdBytes &&
        (last_low_heap_warn_ms_ == 0 || (endMs - last_low_heap_warn_ms_) >= kLowHeapWarnMinIntervalMs)) {
      last_low_heap_warn_ms_ = endMs;
      LRS_LOGW(API,
               "event=low_heap path=%s heap_free=%lu heap_frag=%u max_free_block=%lu dur_ms=%lu",
               path.c_str(),
               static_cast<unsigned long>(freeHeap),
               static_cast<unsigned>(heapFrag),
               static_cast<unsigned long>(maxBlock),
               static_cast<unsigned long>(durMs));
    }
  } else {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s",
                 httpMethodText(server_.method()),
                 path.c_str(),
                 status,
                 static_cast<unsigned long>(durMs),
                 ip.c_str());
  }

  if (durMs >= kWebRequestPressureDurMs) {
    // Local proxy for "recent slow-phase pressure" used by SSE pacing.
    // We don't currently have app-level slow-phase state exposed to WebConsole.
    last_web_pressure_ms_ = endMs;
  }

  request_log_ = RequestLogState{};
}

void WebConsole::markResponseStatus(int status) {
  if (request_log_.active) request_log_.status = status;
}

void WebConsole::sendTracked(int code, const char *contentType, const char *body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::sendTracked(int code, const char *contentType, const String &body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::initStatusCaches() {
  // Keep cache allocation lazy on low-RAM boards; reserve() here can OOM during boot.
  status_live_cache_.built_ms = 0;
  status_static_cache_.built_ms = 0;
  status_lite_cache_.built_ms = 0;
}

bool WebConsole::apiHeapHealthy(uint32_t minFreeBytes, uint32_t minMaxBlockBytes) const {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  return freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes);
}

bool WebConsole::tryServeCachedJson(const char *path,
                                    uint32_t minFreeBytes,
                                    uint32_t minMaxBlockBytes,
                                    uint32_t ttlMs,
                                    JsonResponseCache &cache) {
  const uint32_t now = millis();
  if (cache.body.length() > 0 && ttlMs > 0 && (now - cache.built_ms) < ttlMs) {
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  if (apiHeapHealthy(minFreeBytes, minMaxBlockBytes)) {
    return false;
  }

  if (cache.body.length() > 0) {
    LRS_LOGW(API,
             "event=api_cached_stale path=%s heap_free=%lu heap_frag=%u max_free_block=%lu age_ms=%lu",
             path ? path : server_.uri().c_str(),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
             static_cast<unsigned long>(now - cache.built_ms));
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  return rejectApiIfLowHeap(path, minFreeBytes, minMaxBlockBytes);
}

void WebConsole::setUiNoStoreHeaders() {
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Expires", "0");
}

void WebConsole::closeStatusLiveSse() {
  if (status_live_sse_client_) {
    status_live_sse_client_.flush();
    status_live_sse_client_.stop();
  }
  status_live_sse_client_ = WiFiClient();
  status_live_sse_active_ = false;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;
  status_live_sse_last_interval_ms_ = 0;
}

uint32_t WebConsole::computeStatusLiveSseIntervalMs() const {
  const uint32_t now = millis();
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const uint32_t ratioPct = (freeHeap == 0U) ? 0U : ((maxBlock * 100U) / freeHeap);
  const bool recentPressure =
      (last_web_pressure_ms_ != 0U) && (static_cast<uint32_t>(now - last_web_pressure_ms_) < kStatusLiveSsePressureWindowMs);

  if (maxBlock < 1200U || ratioPct < 30U || freeHeap < 3000U) return kStatusLiveSsePushSevereMs;
  if (maxBlock < 1700U || ratioPct < 40U || (recentPressure && maxBlock < 2600U)) return kStatusLiveSsePushPressureMs;
  if (maxBlock < 2400U || ratioPct < 55U || recentPressure) return kStatusLiveSsePushWarnMs;
  return kStatusLiveSsePushHealthyMs;
}

bool WebConsole::buildStatusLiveCache() {
  if (!config_ || !sm_) return false;

  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  const wl_status_t st = WiFi.status();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["link_state"] = linkStateText(sm_->linkState());
  doc["relay_state"] = sm_->relayState();
  doc["input_state"] = sm_->inputState();
  doc["local_input_state"] = sm_->localDryContactState();
  doc["lora_last_rssi"] = sm_->lastPacketRssi();
  doc["lora_last_packet_ms"] = sm_->lastPacketMs();
  doc["lora_last_tx_ms"] = sm_->lastTxMs();
  doc["lora_remote_temp_valid"] = sm_->remoteTemperatureValid();
  doc["lora_remote_temp_c"] = sm_->remoteTemperatureC();
  doc["lora_remote_temp_ms"] = sm_->remoteTemperatureMs();
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["sta_ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  String relayReason = "boot";
  if (cfg.role_tx) {
    if (sm_->relayState() == 0) {
      if (sm_->inputState() == 0) {
        relayReason = "input_open";
      } else if (sm_->linkState() == LinkState::Timeout) {
        relayReason = "ack_timeout";
      } else if (sm_->linkState() == LinkState::WaitAck) {
        relayReason = "wait_ack";
      } else {
        relayReason = "no_lora_link";
      }
    } else {
      relayReason = "ok";
    }
  } else {
    const bool relayOn = sm_->relayState() != 0;
    switch (sm_->lastRxControlSource()) {
      case RxControlSource::Mqtt:
        relayReason = relayOn ? "mqtt_on" : "mqtt_off";
        break;
      case RxControlSource::LoRa:
        relayReason = relayOn ? "lora_on" : "lora_off";
        break;
      default:
        relayReason = "boot";
        break;
    }
  }
  doc["relay_reason"] = relayReason;

  if (sensors_) {
    const TempSensorStatus &ts = sensors_->tempStatus();
    doc["sensor_temp_enabled"] = ts.enabled;
    doc["sensor_temp_detected"] = ts.detected;
    doc["sensor_temp_valid"] = ts.valid;
    doc["sensor_temp_c"] = ts.celsius;
    doc["sensor_temp_addr"] = ts.address;
    doc["sensor_temp_error"] = ts.error;
    doc["sensor_temp_last_read_ms"] = ts.last_read_ms;
  }

  status_live_cache_.body = "";
  serializeJson(doc, status_live_cache_.body);
  status_live_cache_.built_ms = millis();
  return status_live_cache_.body.length() > 0;
}

bool WebConsole::buildStatusStaticCache() {
  if (!config_) return false;

  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["sta_target_ssid"] = cfg.wifi_sta_ssid;
  doc["deployment_key"] = cfg.fleet_passphrase.length() ? lrslog::maskSecret(cfg.fleet_passphrase) : String("");
  doc["deployment_key_set"] = (cfg.fleet_passphrase.length() > 0);
  doc["deployment_key_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["fleet_setup_required"] = needsFleetSetupPrompt();
  doc["ap_ssid"] = config_->apSsid();
  doc["ap_ip"] = WiFi.softAPIP().toString();
#if LRS_ENABLE_MDNS
  doc["mdns_ap"] = "lrs.local";
  doc["mdns_lan"] = config_->settings().lan_hostname + ".local";
#endif
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  const String fwVersion = String(LRS_FW_VERSION);
  if (LRS_GIT_DIRTY == 0) {
    doc["fw_display"] = fwVersion + " (" + String(LRS_GIT_SHA) + ")";
  } else {
    doc["fw_display"] = fwVersion + " (" + String(LRS_GIT_SHA) + ", dirty)";
  }
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["session_remaining_s"] = sessionRemainingS();
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  status_static_cache_.body = "";
  serializeJson(doc, status_static_cache_.body);
  status_static_cache_.built_ms = millis();
  return status_static_cache_.body.length() > 0;
}

bool WebConsole::buildStatusLiteCache() {
  if (!config_) return false;

  DynamicJsonDocument doc(384);
  auto &cfg = config_->settings();
  doc["chip_id"] = config_->chipIdHex();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["relay_state"] = sm_ ? sm_->relayState() : 0;
  doc["lora_last_rssi"] = sm_ ? sm_->lastPacketRssi() : 0;
  doc["lora_last_packet_ms"] = sm_ ? sm_->lastPacketMs() : 0;
  doc["lora_last_tx_ms"] = sm_ ? sm_->lastTxMs() : 0;
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  status_lite_cache_.body = "";
  serializeJson(doc, status_lite_cache_.body);
  status_lite_cache_.built_ms = millis();
  return status_lite_cache_.body.length() > 0;
}

void WebConsole::tickStatusLiveSse() {
  if (!status_live_sse_active_) return;
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }

  const uint32_t now = millis();
  const uint32_t pushIntervalMs = computeStatusLiveSseIntervalMs();
  status_live_sse_last_interval_ms_ = pushIntervalMs;

  if (status_live_sse_last_keepalive_ms_ == 0 || (now - status_live_sse_last_keepalive_ms_) >= kStatusLiveSseKeepAliveMs) {
    if (status_live_sse_client_.print(F(": keepalive\n\n")) == 0) {
      closeStatusLiveSse();
      return;
    }
    status_live_sse_last_keepalive_ms_ = now;
  }

  if (status_live_sse_last_push_ms_ != 0 && (now - status_live_sse_last_push_ms_) < pushIntervalMs) {
    return;
  }

  bool cacheUpdated = false;
  if (status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) {
    if (apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
      cacheUpdated = buildStatusLiveCache();
    }
  }

  if (status_live_cache_.body.length() == 0) {
    return;
  }

  if (!cacheUpdated && status_live_cache_.built_ms == status_live_sse_last_sent_cache_ms_) {
    return;
  }

  if (status_live_sse_client_.print(F("event: status\nid: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.built_ms) == 0 ||
      status_live_sse_client_.print(F("\ndata: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.body) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }

  status_live_sse_last_push_ms_ = now;
  status_live_sse_last_sent_cache_ms_ = status_live_cache_.built_ms;
}

bool WebConsole::rejectApiIfLowHeap(const char *path, uint32_t minFreeBytes, uint32_t minMaxBlockBytes) {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  if (freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes)) {
    return false;
  }
  LRS_LOGW(API,
           "event=api_low_heap_reject path=%s heap_free=%lu heap_frag=%u max_free_block=%lu need_free=%lu need_block=%lu",
           path ? path : server_.uri().c_str(),
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock),
           static_cast<unsigned long>(minFreeBytes),
           static_cast<unsigned long>(minMaxBlockBytes));
  char body[160];
  snprintf(body,
           sizeof(body),
           "{\"ok\":false,\"error\":\"low_heap\",\"heap_free\":%lu,\"heap_frag\":%u,\"max_free_block\":%lu}",
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock));
  sendTracked(503, "application/json", body);
  return true;
}

bool WebConsole::hasSession() const {
  if (session_token_.length() == 0) return false;
  if (session_expires_ms_ == 0) return false;
  return static_cast<int32_t>(session_expires_ms_ - millis()) > 0;
}

void WebConsole::clearSession() {
  session_token_ = "";
  session_expires_ms_ = 0;
}

String WebConsole::cookieValue(const String &name) const {
  const String raw = server_.header("Cookie");
  if (raw.length() == 0) return "";
  const String needle = name + "=";
  int p = raw.indexOf(needle);
  if (p < 0) return "";
  p += needle.length();
  int e = raw.indexOf(';', p);
  if (e < 0) e = raw.length();
  String v = raw.substring(p, e);
  v.trim();
  return v;
}

String WebConsole::randomToken() const {
  char out[33];
  for (size_t i = 0; i < 16; i++) {
    const uint8_t b = static_cast<uint8_t>(::random(0, 256));
    snprintf(out + (i * 2), 3, "%02x", b);
  }
  out[32] = '\0';
  return String(out);
}

void WebConsole::startSession() {
  session_token_ = randomToken();
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  server_.sendHeader("Set-Cookie", "lrs_session=" + session_token_ + "; Path=/; HttpOnly; SameSite=Lax");
}

uint32_t WebConsole::sessionRemainingS() const {
  if (!hasSession()) return 0;
  return static_cast<uint32_t>((session_expires_ms_ - millis()) / 1000UL);
}

bool WebConsole::requireAuth(bool api) {
  if (locked_until_ms_ != 0 && static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    if (api) {
      sendTracked(429, "application/json", "{\"error\":\"too_many_failed_logins\"}");
    } else {
      sendTracked(429, "text/plain", "Too many failed logins. Try again shortly.");
    }
    LRS_LOGW(API, "event=auth_locked ip=%s", server_.client().remoteIP().toString().c_str());
    return false;
  }

  if (!hasSession()) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    return false;
  }

  const String cookie = cookieValue("lrs_session");
  if (cookie.length() == 0 || cookie != session_token_) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    LRS_LOGW(API,
             "event=auth_cookie_invalid ip=%s has_cookie=%u",
             server_.client().remoteIP().toString().c_str(),
             static_cast<unsigned>(cookie.length() ? 1U : 0U));
    return false;
  }
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  failed_auth_ = 0;
  return true;
}

bool WebConsole::isSoftApActive() const { return softApActiveNow(); }

bool WebConsole::needsFleetSetupPrompt() const {
  if (config_ == nullptr) return false;
  const auto &cfg = config_->settings();
  return isDefaultDeploymentKey(cfg.fleet_passphrase) && !cfg.fleet_setup_prompt_dismissed;
}

void WebConsole::handleCaptiveProbe() {
  if (!isSoftApActive()) {
    sendTracked(204, "text/plain", "");
    return;
  }
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Location", "http://192.168.4.1/");
  sendTracked(302, "text/plain", "Redirecting to LRS console");
}

void WebConsole::routes() {
  server_.on("/", HTTP_GET, [this]() {
    beginRequestLog("/", false, false, true);
    handleIndex();
    finishRequestLog();
  });
  server_.on("/login", HTTP_GET, [this]() {
    beginRequestLog("/login", false, false, true);
    handleLoginPage();
    finishRequestLog();
  });
  server_.on("/setup", HTTP_GET, [this]() {
    beginRequestLog("/setup", false, false, true);
    handleFleetSetupPage();
    finishRequestLog();
  });
  server_.on("/api/login", HTTP_POST, [this]() {
    beginRequestLog("/api/login", true, false, true);
    handleLoginApi();
    finishRequestLog();
  });
  server_.on("/api/setup/fleet-key", HTTP_POST, [this]() {
    beginRequestLog("/api/setup/fleet-key", true, false, true);
    handleFleetSetupApi();
    finishRequestLog();
  });
  server_.on("/api/logout", HTTP_POST, [this]() {
    beginRequestLog("/api/logout", true, false, false);
    handleLogoutApi();
    finishRequestLog();
  });
  server_.on("/api/session", HTTP_GET, [this]() {
    beginRequestLog("/api/session", true, true, true);
    handleSessionApi();
    finishRequestLog();
  });
  server_.on("/generate_204", HTTP_GET, [this]() { handleCaptiveProbe(); });       // Android
  server_.on("/gen_204", HTTP_GET, [this]() { handleCaptiveProbe(); });            // Android (variant)
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptiveProbe(); });  // Apple
  server_.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptiveProbe(); });           // Windows
  server_.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptiveProbe(); });    // Windows
  server_.on("/fwlink", HTTP_GET, [this]() { handleCaptiveProbe(); });             // Windows
  server_.on("/api/status", HTTP_GET, [this]() {
    beginRequestLog("/api/status", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatus();
    finishRequestLog();
  });
  server_.on("/api/status-live", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLive();
    finishRequestLog();
  });
  server_.on("/api/status-live/events", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live/events", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLiveEvents();
    finishRequestLog();
  });
  server_.on("/api/status-static", HTTP_GET, [this]() {
    beginRequestLog("/api/status-static", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusStatic();
    finishRequestLog();
  });
  server_.on("/api/status-lite", HTTP_GET, [this]() {
    beginRequestLog("/api/status-lite", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLite();
    finishRequestLog();
  });
  server_.on("/api/fleet", HTTP_GET, [this]() {
    beginRequestLog("/api/fleet", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleFleet();
    finishRequestLog();
  });
  server_.on("/api/factory", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleFactory();
  });
  server_.on("/api/diagnostics", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleDiagnostics();
  });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("networks");
    const int count = WiFi.scanNetworks(false, true);
    for (int i = 0; i < count; i++) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      if (isOwnLrsSoftApLike(ssid)) continue;
      JsonObject n = arr.createNestedObject();
      n["ssid"] = ssid;
      n["rssi"] = WiFi.RSSI(i);
      n["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  });
  server_.on("/api/network/test", HTTP_POST, [this]() { handleTestSta(); });
  server_.on("/api/network/provision-fleet", HTTP_POST, [this]() {
    beginRequestLog("/api/network/provision-fleet", true, false, true);
    handleProvisionFleetWifi();
    finishRequestLog();
  });
  server_.on("/api/provisioning/start", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/start", true, false, true);
    handleProvisioningStart();
    finishRequestLog();
  });
  server_.on("/api/provisioning/status", HTTP_GET, [this]() {
    beginRequestLog("/api/provisioning/status", true, true, true);
    handleProvisioningStatus();
    finishRequestLog();
  });
  server_.on("/api/provisioning/provision-all", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/provision-all", true, false, true);
    handleProvisioningProvisionAll();
    finishRequestLog();
  });
  server_.on("/api/provisioning/cancel", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/cancel", true, false, true);
    handleProvisioningCancel();
    finishRequestLog();
  });
  server_.on("/api/mqtt/test", HTTP_POST, [this]() { handleTestMqtt(); });
  server_.on("/api/logging/udp", HTTP_POST, [this]() {
    beginRequestLog("/api/logging/udp", true, false, true);
    handleUdpLogging();
    finishRequestLog();
  });
  server_.on("/api/settings", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleGetSettings();
  });
  server_.on("/api/settings", HTTP_POST, [this]() { handlePostSettings(); });
  server_.on("/api/settings/export", HTTP_GET, [this]() { handleExportSettings(); });
  server_.on("/api/settings/import", HTTP_POST, [this]() { handleImportSettings(); });
  server_.on(
      "/api/ota", HTTP_POST, [this]() { handleOtaUpload(); }, [this]() { handleOtaUploadChunk(); });
  server_.on("/api/logs.csv", HTTP_GET, [this]() { handleLogsCsv(); });
  server_.on("/api/logs.txt", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/system/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.onNotFound([this]() {
    if (server_.method() == HTTP_POST && handleFleetDeviceActionRoute(server_.uri())) {
      return;
    }
    if (isSoftApActive()) {
      handleCaptiveProbe();
      return;
    }
    server_.send(404, "text/plain", "not found");
  });
}

void WebConsole::handleIndex() {
  if (!requireAuth(false)) return;
  if (needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/setup");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t maxBlockBefore = lrslog::heapMaxFreeBlock();
  const uint8_t fragBefore = lrslog::heapFragPercent();
  const bool forceFull = server_.hasArg("force_full") && server_.arg("force_full") != "0";
  LRS_LOGI(WEB,
           "event=index_send_start heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned>(fragBefore),
           static_cast<unsigned long>(maxBlockBefore));
  if (!forceFull && (heapBefore < kIndexLowHeapRejectFreeBytes || maxBlockBefore < kIndexLowHeapRejectMaxBlockBytes)) {
    LRS_LOGW(WEB,
             "event=index_send_reject_low_heap heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
    LRS_LOGI(WEB, "event=index_send_low_heap_fallback");
    markResponseStatus(200);
    setUiNoStoreHeaders();
    server_.send_P(200, "text/html", kIndexLowHeapHtml);
    return;
  }
  if (forceFull && (heapBefore < kIndexLowHeapRejectFreeBytes || maxBlockBefore < kIndexLowHeapRejectMaxBlockBytes)) {
    LRS_LOGW(WEB,
             "event=index_send_force_low_heap heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kIndexHtml);
  LRS_LOGI(WEB,
           "event=index_send_done heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
}

void WebConsole::handleLoginPage() {
  if (hasSession() && cookieValue("lrs_session") == session_token_) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", needsFleetSetupPrompt() ? "/setup" : "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kLoginHtml);
}

void WebConsole::handleFleetSetupPage() {
  if (!requireAuth(false)) return;
  if (!needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kFleetSetupHtml);
}

void WebConsole::handleLoginApi() {
  if (locked_until_ms_ != 0 && static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    sendTracked(429, "application/json", "{\"error\":\"Too many failed logins. Try again shortly.\"}");
    LRS_LOGW(API, "event=login_blocked ip=%s", server_.client().remoteIP().toString().c_str());
    return;
  }

  DynamicJsonDocument doc(256);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  const String posted = String(static_cast<const char *>(doc["password"] | ""));
  if (posted != config_->settings().admin_password) {
    failed_auth_++;
    if (failed_auth_ >= 5) {
      locked_until_ms_ = millis() + 60000;
      failed_auth_ = 0;
    }
    sendTracked(401, "application/json", "{\"error\":\"Invalid password\"}");
    LRS_LOGW(API,
             "event=login_failed ip=%s remaining_lock_attempts=%u",
             server_.client().remoteIP().toString().c_str(),
             static_cast<unsigned>((failed_auth_ < 5) ? (5 - failed_auth_) : 0));
    return;
  }

  failed_auth_ = 0;
  locked_until_ms_ = 0;
  startSession();
  DynamicJsonDocument out(128);
  out["ok"] = true;
  out["setup_required"] = needsFleetSetupPrompt();
  String body;
  serializeJson(out, body);
  sendTracked(200, "application/json", body);
  LRS_LOGI(API,
           "event=login_ok ip=%s setup_required=%u",
           server_.client().remoteIP().toString().c_str(),
           needsFleetSetupPrompt() ? 1U : 0U);
}

void WebConsole::handleFleetSetupApi() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument doc(384);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const bool skip = parseBoolField(doc["skip"], false);
  if (skip) {
    cfg.fleet_setup_prompt_dismissed = true;
    cfg.audit_last_saved_by = "first_login_skip";
    cfg.audit_last_saved_ms = millis();
    if (!config_->save()) {
      sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
      return;
    }
    sendTracked(200, "application/json", "{\"ok\":true,\"skipped\":true}");
    LRS_LOGI(API, "event=fleet_setup_skip");
    return;
  }

  String fleetKey = String(static_cast<const char *>(doc["fleet_passphrase"] | ""));
  fleetKey.trim();
  if (fleetKey.length() < kMinDeploymentKeyLen) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"deployment_key_too_short\"}");
    return;
  }
  if (isDefaultDeploymentKey(fleetKey)) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"deployment_key_default_blocked\"}");
    return;
  }

  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  cfg.audit_last_saved_by = "first_login_setup";
  cfg.audit_last_saved_ms = millis();
  if (!config_->save()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  if (on_apply_) on_apply_(false, false);
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=fleet_key_set key=%s", lrslog::maskSecret(fleetKey).c_str());
}

void WebConsole::handleLogoutApi() {
  clearSession();
  server_.sendHeader("Set-Cookie", "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=logout ip=%s", server_.client().remoteIP().toString().c_str());
}

void WebConsole::handleSessionApi() {
  DynamicJsonDocument doc(128);
  const bool ok = hasSession() && cookieValue("lrs_session") == session_token_;
  doc["ok"] = ok;
  doc["remaining_s"] = ok ? sessionRemainingS() : 0;
  doc["setup_required"] = ok ? needsFleetSetupPrompt() : false;
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleStatus() {
  LRS_LOGW(API,
           "event=status_compat_removed ip=%s",
           server_.client().remoteIP().toString().c_str());
  sendTracked(410,
              "application/json",
              "{\"ok\":false,\"error\":\"deprecated\",\"use\":[\"/api/status-live\",\"/api/status-static\"]}");
}

void WebConsole::handleStatusLive() {
  if (tryServeCachedJson("/api/status-live",
                         kApiStatusLiveLowHeapRejectFreeBytes,
                         kApiStatusLiveLowHeapRejectMaxBlockBytes,
                         kStatusLiveCacheTtlMs,
                         status_live_cache_))
    return;
  if (!buildStatusLiveCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_live_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_live_cache_.body);
}

void WebConsole::handleStatusLiveEvents() {
  const bool connectHeapHealthy =
      apiHeapHealthy(kStatusLiveSseConnectMinFreeBytes, kStatusLiveSseConnectMinMaxBlockBytes);

  if (status_live_sse_active_) {
    closeStatusLiveSse();
  }

  const uint32_t now = millis();
  if ((status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) &&
      apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
    buildStatusLiveCache();
  }

  WiFiClient client = server_.client();
  client.setNoDelay(true);
  client.setSync(true);
  status_live_sse_client_ = client;
  status_live_sse_active_ = true;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  markResponseStatus(200);
  server_.sendContent_P(PSTR("HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
                             "Pragma: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "X-Accel-Buffering: no\r\n"
                             "\r\n"));
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }
  if (status_live_sse_client_.print(F("retry: ")) == 0 ||
      status_live_sse_client_.print(connectHeapHealthy ? 3000U : 10000U) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }
  tickStatusLiveSse();
  LRS_LOGI(API,
           "event=status_live_sse_open ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu heap_ok=%u",
           server_.client().remoteIP().toString().c_str(),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
           connectHeapHealthy ? 1U : 0U);
}

void WebConsole::handleStatusStatic() {
  if (tryServeCachedJson("/api/status-static",
                         kApiStatusStaticLowHeapRejectFreeBytes,
                         kApiStatusStaticLowHeapRejectMaxBlockBytes,
                         kStatusStaticCacheTtlMs,
                         status_static_cache_))
    return;
  if (!buildStatusStaticCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_static_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_static_cache_.body);
}

void WebConsole::handleStatusLite() {
  if (tryServeCachedJson("/api/status-lite",
                         kApiLightLowHeapRejectFreeBytes,
                         kApiLightLowHeapRejectMaxBlockBytes,
                         kStatusLiteCacheTtlMs,
                         status_lite_cache_))
    return;
  if (!buildStatusLiteCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_lite_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_lite_cache_.body);
}

void WebConsole::handleFleet() {
  if (rejectApiIfLowHeap("/api/fleet", kApiFleetLowHeapRejectFreeBytes, kApiFleetLowHeapRejectMaxBlockBytes)) return;
  auto &cfg = config_->settings();
  const size_t peerCount = (cfg.role_tx && sm_ != nullptr) ? sm_->peerCount() : 0;
  size_t docCapacity = kFleetDocBaseBytes + (peerCount * kFleetDocPerPeerBytes);
  if (docCapacity < kFleetDocMinBytes) docCapacity = kFleetDocMinBytes;
  if (docCapacity > kFleetDocMaxBytes) docCapacity = kFleetDocMaxBytes;
  DynamicJsonDocument doc(docCapacity);
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["tx_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  const uint32_t now = millis();
  doc["uptime_ms"] = now;
  JsonArray arr = doc.createNestedArray("devices");
  if (cfg.role_tx && sm_ != nullptr) {
    for (size_t i = 0; i < peerCount; ++i) {
      PeerStatusSnapshot node{};
      if (!sm_->peerByIndex(i, node)) continue;
      JsonObject r = arr.createNestedObject();
      r["address"] = node.address;
      char addrHex[5];
      snprintf(addrHex, sizeof(addrHex), "0x%02X", node.address);
      r["addr_hex"] = addrHex;
      r["relay_state"] = node.relay_state;
      r["input_state"] = node.input_state;
      r["temp_valid"] = node.temp_valid;
      r["temp_c"] = node.temp_c;
      r["uplink_rssi"] = node.uplink_rssi;
      r["downlink_rssi_valid"] = node.downlink_rssi_valid;
      r["downlink_rssi"] = node.downlink_rssi;
      r["last_seen_ms"] = node.last_seen_ms;
      const uint32_t seenAgeMs = (node.last_seen_ms > 0 && now >= node.last_seen_ms) ? (now - node.last_seen_ms) : 0;
      r["last_seen_age_ms"] = seenAgeMs;
      r["last_cmd_counter"] = node.last_cmd_counter;
      r["ack_state"] = remoteAckStateText(node.ack_state);
      r["poll_interval_ms"] = node.poll_interval_ms;
      r["poll_interval_s"] = node.poll_interval_ms / 1000U;
      r["last_poll_tx_ms"] = node.last_poll_tx_ms;
      const uint32_t pollAgeMs = (node.last_poll_tx_ms > 0 && now >= node.last_poll_tx_ms) ? (now - node.last_poll_tx_ms) : 0;
      r["last_poll_age_ms"] = pollAgeMs;
      r["poll_pending"] = node.poll_pending;
      r["poll_state"] = node.poll_pending ? "pending" : "idle";
      const uint32_t expectedIntervalMs = (node.poll_interval_ms > 0) ? node.poll_interval_ms : 300000U;
      uint32_t staleAfterMs = expectedIntervalMs * 3U;
      if (staleAfterMs < 180000U) staleAfterMs = 180000U;
      r["expected_interval_ms"] = expectedIntervalMs;
      r["stale_after_ms"] = staleAfterMs;
      r["stale_threshold_ms"] = staleAfterMs;
      r["stale"] = (node.last_seen_ms == 0) || (seenAgeMs > staleAfterMs);
    }
  }
  if (doc.overflowed()) {
    LRS_LOGW(API, "event=fleet_json_overflow peers=%u cap=%u",
             static_cast<unsigned>(peerCount),
             static_cast<unsigned>(docCapacity));
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

bool WebConsole::handleFleetDeviceActionRoute(const String &uri) {
  const String prefix = "/api/fleet/";
  if (!uri.startsWith(prefix)) return false;
  const String suffix = uri.substring(prefix.length());
  const int slash = suffix.indexOf('/');
  if (slash <= 0) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!requireAuth(true)) return true;
  auto &cfg = config_->settings();
  if (!cfg.role_tx || sm_ == nullptr) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"tx_only\"}");
    return true;
  }

  const uint8_t addr = parseAddressText(suffix.substring(0, slash), 0);
  if (addr == 0 || addr == 255) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_address\"}");
    return true;
  }

  const String actionPrefix = "actions/";
  const String tail = suffix.substring(slash + 1);
  if (!tail.startsWith(actionPrefix)) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }
  const String action = tail.substring(actionPrefix.length());

  DynamicJsonDocument doc(256);
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return true;
    }
  }

  bool ok = false;
  if (action == "poll-now") {
    ok = sm_->mqttPollPeerNow(addr);
  } else if (action == "forget") {
    ok = sm_->mqttForgetPeer(addr);
  } else if (action == "poll-interval") {
    uint32_t sec = doc["interval_s"] | 0;
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, sec * 1000U);
  } else if (action == "schedule") {
    const bool enabled = parseBoolField(doc["enabled"], true);
    uint32_t sec = doc["interval_s"] | (cfg.tx_mqtt_remote_default_poll_interval_ms / 1000U);
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, enabled ? (sec * 1000U) : 0U);
  } else if (action == "factory-reset") {
    const bool keepSharedFleetKey = parseBoolField(doc["keep_shared_fleet_key"], true);
    ok = sm_->sendPeerFactoryReset(addr, keepSharedFleetKey);
  } else {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!ok) {
    server_.send(409, "application/json", "{\"ok\":false,\"error\":\"action_failed\"}");
    return true;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
  return true;
}

void WebConsole::handleFactory() {
  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["serial"] = cfg.factory_serial;
  doc["chip_id"] = config_->chipIdHex();
  doc["mac"] = WiFi.softAPmacAddress();
  doc["factory_role"] = cfg.role_tx ? "tx" : "rx";
  doc["factory_local_address"] = cfg.local_address;
  doc["factory_remote_address"] = cfg.remote_address;
  doc["factory_ap_ssid"] = config_->apSsid();
  doc["factory_ap_password"] = config_->apPassword();
  doc["hardware_version"] = kHardwareVersion;
  doc["hardware_batch"] = kHardwareBatch;
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleGetSettings() {
  DynamicJsonDocument doc(1024);
  auto &cfg = config_->settings();
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["tx_input_lora_control_enabled"] = cfg.tx_input_lora_control_enabled;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = "";
  doc["wifi_sta_password_set"] = (cfg.wifi_sta_password.length() > 0);
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = "";
  doc["fleet_passphrase_set"] = (cfg.fleet_passphrase.length() > 0);
  doc["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = "";
  doc["admin_password_set"] = (cfg.admin_password.length() > 0);
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_enabled"] = cfg.mqtt_enabled;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = "";
  doc["mqtt_password_set"] = (cfg.mqtt_password.length() > 0);
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handlePostSettings() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument doc(1536);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    server_.send(400, "text/plain", "invalid json");
    return;
  }

  auto &cfg = config_->settings();
  const Settings prev = cfg;
  Settings next = cfg;
  const String prevStaSsid = prev.wifi_sta_ssid;
  const String prevStaPassword = prev.wifi_sta_password;
  const String prevLanHost = prev.lan_hostname;
  const bool prevApAlwaysOn = prev.ap_always_on;
  const String prevAdminPassword = prev.admin_password;
  const bool oldRoleTx = prev.role_tx;
  const String oldDefaultHost = config_->defaultLanHostnameForRole(oldRoleTx);
  const String oldLegacyDefaultHost = String("lrs-") + config_->chipIdHex();
  const String oldLegacyRoleTxHost = oldLegacyDefaultHost + "-tx";
  const String oldLegacyRoleRxHost = oldLegacyDefaultHost + "-rx";
  next.role_tx = parseBoolField(doc["role_tx"], next.role_tx);
  next.local_address = parseAddressField(doc["local_address"], next.local_address);
  next.remote_address = parseAddressField(doc["remote_address"], next.remote_address);
  next.lora_frequency_hz = doc["lora_frequency_hz"] | next.lora_frequency_hz;
  next.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | next.lora_tx_power);
  next.lora_spreading_factor = static_cast<uint8_t>(doc["lora_spreading_factor"] | next.lora_spreading_factor);
  next.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | next.lora_bandwidth_hz;
  next.lora_coding_rate = static_cast<uint8_t>(doc["lora_coding_rate"] | next.lora_coding_rate);
  next.heartbeat_ms = doc["heartbeat_ms"] | next.heartbeat_ms;
  next.ack_timeout_ms = doc["ack_timeout_ms"] | next.ack_timeout_ms;
  next.mqtt_remote_retry_timeout_ms = doc["mqtt_remote_retry_timeout_ms"] | next.mqtt_remote_retry_timeout_ms;
  next.tx_mqtt_remote_polling_enabled = parseBoolField(doc["tx_mqtt_remote_polling_enabled"], next.tx_mqtt_remote_polling_enabled);
  next.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] | next.tx_mqtt_remote_default_poll_interval_ms;
  next.rx_push_on_change_enabled = parseBoolField(doc["rx_push_on_change_enabled"], next.rx_push_on_change_enabled);
  next.rx_push_min_interval_ms = doc["rx_push_min_interval_ms"] | next.rx_push_min_interval_ms;
  next.tx_input_lora_control_enabled = parseBoolField(doc["tx_input_lora_control_enabled"], next.tx_input_lora_control_enabled);
  next.wifi_sta_ssid = String(static_cast<const char *>(doc["wifi_sta_ssid"] | next.wifi_sta_ssid.c_str()));
  next.wifi_sta_password = String(static_cast<const char *>(doc["wifi_sta_password"] | next.wifi_sta_password.c_str()));
  const String postedLanHost = String(static_cast<const char *>(doc["lan_hostname"] | next.lan_hostname.c_str()));
  const bool wasDefaultHostname = (prev.lan_hostname == oldDefaultHost) || (prev.lan_hostname == oldLegacyDefaultHost) ||
                                  (prev.lan_hostname == oldLegacyRoleTxHost) || (prev.lan_hostname == oldLegacyRoleRxHost);
  if (wasDefaultHostname && (postedLanHost == oldDefaultHost || postedLanHost == oldLegacyDefaultHost ||
                             postedLanHost == oldLegacyRoleTxHost || postedLanHost == oldLegacyRoleRxHost)) {
    next.lan_hostname = config_->defaultLanHostnameForRole(next.role_tx);
  } else {
    next.lan_hostname = postedLanHost;
  }
  next.fleet_passphrase = String(static_cast<const char *>(doc["fleet_passphrase"] | next.fleet_passphrase.c_str()));
  next.ap_always_on = parseBoolField(doc["ap_always_on"], next.ap_always_on);
  next.mqtt_enabled = parseBoolField(doc["mqtt_enabled"], next.mqtt_enabled);
  next.mqtt_host = String(static_cast<const char *>(doc["mqtt_host"] | next.mqtt_host.c_str()));
  next.mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | next.mqtt_port);
  next.mqtt_user = String(static_cast<const char *>(doc["mqtt_user"] | next.mqtt_user.c_str()));
  next.mqtt_password = String(static_cast<const char *>(doc["mqtt_password"] | next.mqtt_password.c_str()));
  next.mqtt_topic_root = String(static_cast<const char *>(doc["mqtt_topic_root"] | next.mqtt_topic_root.c_str()));
  next.sensor_temp_enabled = parseBoolField(doc["sensor_temp_enabled"], next.sensor_temp_enabled);

  String newAdmin = String(static_cast<const char *>(doc["admin_password"] | next.admin_password.c_str()));
  if (newAdmin.length() >= 8) {
    next.admin_password = newAdmin;
  }

  if (next.local_address < 1) next.local_address = 1;
  if (next.local_address > 254) next.local_address = 254;
  if (next.remote_address < 1) next.remote_address = 1;
  if (next.remote_address > 254) next.remote_address = 254;
  next.fleet_passphrase.trim();
  const bool hasFleetPassphraseField = !doc["fleet_passphrase"].isNull();
  const bool allowDefaultDeploymentKey = parseBoolField(doc["allow_default_deployment_key"], false);
  if (hasFleetPassphraseField && next.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    server_.send(400, "text/plain",
                 String("deployment key too short (min ") + String(static_cast<unsigned>(kMinDeploymentKeyLen)) +
                     " chars)");
    return;
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey && isDefaultDeploymentKey(next.fleet_passphrase)) {
    server_.send(400, "text/plain", "deployment key cannot be default; set unique key");
    return;
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(next.fleet_passphrase)) {
    next.fleet_setup_prompt_dismissed = true;
  }
  if (next.lora_frequency_hz < kMinFrequencyHz || next.lora_frequency_hz > kMaxFrequencyHz) {
    next.lora_frequency_hz = kDefaultFrequencyHz;
  }
  if (next.heartbeat_ms < kMinHeartbeatMs) next.heartbeat_ms = kMinHeartbeatMs;
  if (next.heartbeat_ms > kMaxHeartbeatMs) next.heartbeat_ms = kMaxHeartbeatMs;
  if (next.ack_timeout_ms < kMinAckTimeoutMs) next.ack_timeout_ms = kMinAckTimeoutMs;
  if (next.ack_timeout_ms > kMaxAckTimeoutMs) next.ack_timeout_ms = kMaxAckTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms < kMinMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMinMqttRemoteRetryTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms > kMaxMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMaxMqttRemoteRetryTimeoutMs;
  if (next.tx_mqtt_remote_default_poll_interval_ms < kMinTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMinTxPollDefaultIntervalMs;
  }
  if (next.tx_mqtt_remote_default_poll_interval_ms > kMaxTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMaxTxPollDefaultIntervalMs;
  }
  if (next.rx_push_min_interval_ms < kMinRxPushIntervalMs) next.rx_push_min_interval_ms = kMinRxPushIntervalMs;
  if (next.rx_push_min_interval_ms > kMaxRxPushIntervalMs) next.rx_push_min_interval_ms = kMaxRxPushIntervalMs;
  if (next.mqtt_port == 0) next.mqtt_port = 1883;
  if (next.mqtt_topic_root.length() == 0) next.mqtt_topic_root = "lora";
  next.audit_last_saved_by = "admin";
  next.audit_last_saved_ms = millis();

  const bool networkChanged = (next.wifi_sta_ssid != prevStaSsid) ||
                              (next.wifi_sta_password != prevStaPassword) ||
                              (next.lan_hostname != prevLanHost) ||
                              (next.ap_always_on != prevApAlwaysOn);
  const bool otaAuthChanged = (next.admin_password != prevAdminPassword);

  cfg = next;
  if (!config_->save()) {
    cfg = prev;
    server_.send(500, "text/plain", "save failed");
    return;
  }

  server_.send(200, "text/plain", "saved");
  if (on_apply_) on_apply_(networkChanged, otaAuthChanged);
}

void WebConsole::handleExportSettings() {
  if (!requireAuth(true)) return;
  server_.sendHeader("Content-Disposition", "attachment; filename=lrs-config.json");
  DynamicJsonDocument doc(2048);
  auto &cfg = config_->settings();
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["tx_input_lora_control_enabled"] = cfg.tx_input_lora_control_enabled;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg.wifi_sta_password;
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = cfg.fleet_passphrase;
  doc["admin_password"] = cfg.admin_password;
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_enabled"] = cfg.mqtt_enabled;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = cfg.mqtt_password;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  String out;
  serializeJsonPretty(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleImportSettings() { handlePostSettings(); }

void WebConsole::handleDiagnostics() {
  DynamicJsonDocument doc(1024);
  auto &cfg = config_->settings();
  const wl_status_t st = WiFi.status();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["fw_display"] = String(LRS_FW_VERSION) + " (" + String(LRS_GIT_SHA) + (LRS_GIT_DIRTY == 0 ? "" : ", dirty") + ")";
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["uptime_ms"] = millis();
  doc["free_heap_bytes"] = ESP.getFreeHeap();
  doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  doc["chip_id"] = config_->chipIdHex();
  doc["flash_real_size"] = ESP.getFlashChipRealSize();
  doc["flash_ide_size"] = ESP.getFlashChipSize();
  doc["sdk_version"] = ESP.getSdkVersion();
  doc["core_version"] = ESP.getCoreVersion();
  doc["lora_tx_packets"] = nullptr;
  doc["ack_ok"] = nullptr;
  doc["ack_timeout"] = nullptr;
  doc["replay_drop"] = nullptr;
  doc["wifi_connect_attempts"] = nullptr;
  doc["wifi_connect_fail"] = nullptr;
  doc["wifi_disconnects"] = nullptr;
  doc["log_history_available"] = false;
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleTestSta() {
  if (!requireAuth(true)) return;
  if (!needsFleetSetupPrompt()) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"setup_only\"}");
    return;
  }
  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  const String ssid = String(static_cast<const char *>(body["wifi_sta_ssid"] | config_->settings().wifi_sta_ssid.c_str()));
  const String pass = String(static_cast<const char *>(body["wifi_sta_password"] | config_->settings().wifi_sta_password.c_str()));
  if (ssid.length() == 0) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }

  const auto &cfg = config_->settings();
  const bool alreadyConnectedSameSsid = WiFi.isConnected() && WiFi.SSID() == ssid;
  const bool sameAsConfigured = (ssid == cfg.wifi_sta_ssid) && (pass == cfg.wifi_sta_password);
  if (alreadyConnectedSameSsid && sameAsConfigured) {
    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    doc["status_code"] = static_cast<int>(WL_CONNECTED);
    doc["status_text"] = wifiStatusText(WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
    return;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  wl_status_t st = WL_IDLE_STATUS;
  for (int i = 0; i < kStaTestMaxAttempts; i++) {
    delay(100);
    yield();
    st = WiFi.status();
    if (st == WL_CONNECTED) break;
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
  }

  DynamicJsonDocument doc(256);
  const bool ok = (st == WL_CONNECTED);
  doc["ok"] = ok;
  doc["status_code"] = static_cast<int>(st);
  doc["status_text"] = wifiStatusText(st);
  if (ok) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  }

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);

  // If test credentials differ from persisted settings, restore configured STA
  // after replying so the HTTP response has a chance to reach the browser.
  if (!sameAsConfigured) {
    delay(80);
    WiFi.disconnect();
    delay(20);
    if (cfg.wifi_sta_ssid.length() > 0) {
      WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str());
    }
  }
}

void WebConsole::handleProvisionFleetWifi() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }

  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  const String ssid = String(static_cast<const char *>(body["wifi_sta_ssid"] | config_->settings().wifi_sta_ssid.c_str()));
  const String pass = String(static_cast<const char *>(body["wifi_sta_password"] | config_->settings().wifi_sta_password.c_str()));
  if (ssid.length() == 0) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }
  if (ssid.length() > 32 || pass.length() > 64) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"credentials_too_long\"}");
    return;
  }
  if (isDefaultDeploymentKey(config_->settings().fleet_passphrase)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"fleet_key_default\"}");
    return;
  }

  const uint32_t cooldownRemainingMs = sm_->fleetWifiProvisionCooldownRemainingMs();
  if (cooldownRemainingMs > 0) {
    DynamicJsonDocument cooldown(160);
    cooldown["ok"] = false;
    cooldown["error"] = "cooldown_active";
    cooldown["retry_after_ms"] = cooldownRemainingMs;
    cooldown["retry_after_s"] = (cooldownRemainingMs + 999U) / 1000U;
    String out;
    serializeJson(cooldown, out);
    sendTracked(429, "application/json", out);
    return;
  }

  if (!sm_->sendFleetWifiProvision(ssid, pass)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"send_failed\"}");
    return;
  }

  const size_t totalLen = static_cast<size_t>(ssid.length() + pass.length());
  const size_t chunks = (totalLen + 6U) / 7U;
  DynamicJsonDocument out(128);
  out["ok"] = true;
  out["packets"] = static_cast<uint32_t>(chunks + 2U);  // start + chunks + commit
  String json;
  serializeJson(out, json);
  sendTracked(200, "application/json", json);
  LRS_LOGI(API,
           "event=fleet_wifi_provision_tx ssid=%s password=%s packets=%lu",
           ssid.c_str(),
           lrslog::maskSecret(pass).c_str(),
           static_cast<unsigned long>(chunks + 2U));
}

void WebConsole::handleProvisioningStatus() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  const uint32_t heapFree = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const bool veryLowHeap =
      (heapFree < kApiProvStatusCompactFreeBytes || maxBlock < kApiProvStatusCompactMaxBlockBytes);
  const bool lowHeapForFull =
      (heapFree < kApiLowHeapRejectFreeBytes || maxBlock < kApiLowHeapRejectMaxBlockBytes);
  if (lowHeapForFull) {
    if (veryLowHeap &&
        rejectApiIfLowHeap("/api/provisioning/status",
                           kApiProvStatusCompactFreeBytes,
                           kApiProvStatusCompactMaxBlockBytes)) {
      return;
    }
    StaticJsonDocument<1536> doc;
    doc["ok"] = true;
    ProvisioningSessionSnapshot sess{};
    sm_->provisioningSession(sess);
    const size_t totalDevices = sm_->provisioningDeviceCount();
    const size_t maxCompactRows = 8;
    const size_t returnedDevices = (totalDevices < maxCompactRows) ? totalDevices : maxCompactRows;
    JsonObject s = doc.createNestedObject("session");
    s["active"] = sess.active;
    s["state"] = provisioningSessionStateText(sess.state);
    s["session_nonce"] = sess.session_nonce;
    s["estimated_count"] = sess.estimated_count;
    s["started_ms"] = sess.started_ms;
    s["phase_deadline_ms"] = sess.phase_deadline_ms;
    s["retry_enabled"] = sess.retry_enabled;
    s["retry_used"] = sess.retry_used;
    s["paused_normal_tx"] = sess.paused_normal_tx;
    s["discovered_count"] = sess.discovered_count;
    s["selected_count"] = sess.selected_count;
    s["conflict_count"] = sess.conflict_count;
    s["verified_count"] = sess.verified_count;
    s["failed_count"] = sess.failed_count;
    s["now_ms"] = millis();
    s["devices_total"] = totalDevices;
    s["devices_returned"] = returnedDevices;
    s["devices_truncated"] = (returnedDevices < totalDevices);
    s["compact"] = true;
    JsonArray arr = doc.createNestedArray("devices");
    for (size_t i = 0; i < returnedDevices; ++i) {
      ProvisioningDeviceSnapshot d{};
      if (!sm_->provisioningDeviceByIndex(i, d)) continue;
      JsonObject o = arr.createNestedObject();
      char chipHex[11];
      snprintf(chipHex, sizeof(chipHex), "0x%08lX", static_cast<unsigned long>(d.chip_id));
      o["chip_id_hex"] = chipHex;
      o["current_address"] = d.current_address;
      o["assigned_address"] = d.assigned_address;
      o["fw_major"] = d.fw_major;
      o["fw_minor"] = d.fw_minor;
      o["fw_patch"] = d.fw_patch;
      o["rssi"] = d.rssi;
      o["state"] = provisioningDeviceStateText(d.state);
      o["address_conflict"] = d.address_conflict;
    }

    if (last_logged_prov_state_ != static_cast<uint8_t>(sess.state)) {
      last_logged_prov_state_ = static_cast<uint8_t>(sess.state);
      LRS_LOGI(API,
               "event=provisioning_phase state=%s discovered=%u conflicts=%u verified=%u failed=%u",
               provisioningSessionStateText(sess.state),
               static_cast<unsigned>(sess.discovered_count),
               static_cast<unsigned>(sess.conflict_count),
               static_cast<unsigned>(sess.verified_count),
               static_cast<unsigned>(sess.failed_count));
    }

    const size_t len = measureJson(doc);
    server_.setContentLength(len);
    markResponseStatus(200);
    server_.send(200, "application/json", "");
    serializeJson(doc, server_.client());
    LRS_LOGW(API,
             "event=provisioning_status_compact heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapFree),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(maxBlock));
    return;
  }
  const size_t totalDevices = sm_->provisioningDeviceCount();
  const size_t maxDevicesReturned = 64;
  const size_t returnedDevices = (totalDevices < maxDevicesReturned) ? totalDevices : maxDevicesReturned;
  size_t docCap = 1024U + (returnedDevices * 192U);
  if (docCap < 2048U) docCap = 2048U;
  if (docCap > 12288U) docCap = 12288U;
  DynamicJsonDocument doc(docCap);
  doc["ok"] = true;
  ProvisioningSessionSnapshot sess{};
  sm_->provisioningSession(sess);
  JsonObject s = doc.createNestedObject("session");
  s["active"] = sess.active;
  s["state"] = provisioningSessionStateText(sess.state);
  s["session_nonce"] = sess.session_nonce;
  s["estimated_count"] = sess.estimated_count;
  s["started_ms"] = sess.started_ms;
  s["phase_deadline_ms"] = sess.phase_deadline_ms;
  s["retry_enabled"] = sess.retry_enabled;
  s["retry_used"] = sess.retry_used;
  s["paused_normal_tx"] = sess.paused_normal_tx;
  s["discovered_count"] = sess.discovered_count;
  s["selected_count"] = sess.selected_count;
  s["conflict_count"] = sess.conflict_count;
  s["verified_count"] = sess.verified_count;
  s["failed_count"] = sess.failed_count;
  s["now_ms"] = millis();
  s["devices_total"] = totalDevices;
  s["devices_returned"] = returnedDevices;
  s["devices_truncated"] = (returnedDevices < totalDevices);
  if (last_logged_prov_state_ != static_cast<uint8_t>(sess.state)) {
    last_logged_prov_state_ = static_cast<uint8_t>(sess.state);
    LRS_LOGI(API,
             "event=provisioning_phase state=%s discovered=%u conflicts=%u verified=%u failed=%u",
             provisioningSessionStateText(sess.state),
             static_cast<unsigned>(sess.discovered_count),
             static_cast<unsigned>(sess.conflict_count),
             static_cast<unsigned>(sess.verified_count),
             static_cast<unsigned>(sess.failed_count));
  }

  JsonArray arr = doc.createNestedArray("devices");
  for (size_t i = 0; i < returnedDevices; ++i) {
    ProvisioningDeviceSnapshot d{};
    if (!sm_->provisioningDeviceByIndex(i, d)) continue;
    JsonObject o = arr.createNestedObject();
    o["chip_id"] = d.chip_id;
    char chipHex[11];
    snprintf(chipHex, sizeof(chipHex), "0x%08lX", static_cast<unsigned long>(d.chip_id));
    o["chip_id_hex"] = chipHex;
    o["current_address"] = d.current_address;
    o["assigned_address"] = d.assigned_address;
    o["role"] = d.role_tx ? "tx" : "rx";
    o["fw_major"] = d.fw_major;
    o["fw_minor"] = d.fw_minor;
    o["fw_patch"] = d.fw_patch;
    o["rssi"] = d.rssi;
    o["selected"] = d.selected;
    o["address_conflict"] = d.address_conflict;
    o["state"] = provisioningDeviceStateText(d.state);
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleProvisioningStart() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  DynamicJsonDocument body(256);
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(body, server_.arg("plain"));
    if (err) {
      sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return;
    }
  }
  uint16_t estimated = 10;
  if (!body["estimated_count"].isNull()) {
    int v = body["estimated_count"].as<int>();
    if (v < 1) v = 1;
    if (v > 250) v = 250;
    estimated = static_cast<uint16_t>(v);
  }
  const bool retryOnce = parseBoolField(body["retry_once"], true);
  if (!sm_->provisioningStartDiscovery(estimated, retryOnce)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"start_failed\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_start estimated=%u retry_once=%u heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned>(estimated),
           retryOnce ? 1U : 0U,
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningProvisionAll() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  if (!sm_->provisioningStartProvisionAll()) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"invalid_state\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_provision_all heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningCancel() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  sm_->provisioningCancel();
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=provisioning_cancel");
}

void WebConsole::handleTestMqtt() {
  if (!requireAuth(true)) return;
  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }
  const String host = String(static_cast<const char *>(body["mqtt_host"] | config_->settings().mqtt_host.c_str()));
  uint16_t port = config_->settings().mqtt_port;
  if (!body["mqtt_port"].isNull()) {
    long parsed = -1;
    if (body["mqtt_port"].is<uint16_t>()) {
      parsed = static_cast<long>(body["mqtt_port"].as<uint16_t>());
    } else {
      const String raw = String(static_cast<const char *>(body["mqtt_port"] | ""));
      if (raw.length() > 0) parsed = raw.toInt();
    }
    if (parsed < 1 || parsed > 65535) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mqtt_port\"}");
      return;
    }
    port = static_cast<uint16_t>(parsed);
  }
  const String user = String(static_cast<const char *>(body["mqtt_user"] | config_->settings().mqtt_user.c_str()));
  const String pass = String(static_cast<const char *>(body["mqtt_password"] | config_->settings().mqtt_password.c_str()));
  if (!WiFi.isConnected()) {
    server_.send(200, "application/json", "{\"ok\":false,\"state\":-2}");
    return;
  }
  WiFiClient client;
  PubSubClient mqtt(client);
  mqtt.setServer(host.c_str(), port);
  mqtt.setSocketTimeout(2);
  const String clientId = "lrs-test-" + config_->chipIdHex();
  bool ok = false;
  if (user.length() > 0) {
    ok = mqtt.connect(clientId.c_str(), user.c_str(), pass.c_str());
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  DynamicJsonDocument doc(256);
  doc["ok"] = ok;
  doc["state"] = mqtt.state();
  doc["host"] = host;
  doc["port"] = port;
  if (ok) mqtt.disconnect();
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleUdpLogging() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument body(256);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const bool enabled = parseBoolField(body["enabled"], true);
  if (!enabled) {
    lrslog::disableUdpMirror();
    const IPAddress rip = server_.client().remoteIP();
    char ripbuf[16];
    snprintf(ripbuf,
             sizeof(ripbuf),
             "%u.%u.%u.%u",
             static_cast<unsigned>(rip[0]),
             static_cast<unsigned>(rip[1]),
             static_cast<unsigned>(rip[2]),
             static_cast<unsigned>(rip[3]));
    LRS_LOGI(API, "event=udp_log_mirror_disabled ip=%s", ripbuf);
    sendTracked(200, "application/json", "{\"ok\":true,\"enabled\":false,\"ttl_ms\":0}");
    return;
  }

  IPAddress target = server_.client().remoteIP();
  if (!parseIpField(body["host"], target) && !body["host"].isNull()) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_host\"}");
    return;
  }

  uint16_t port = 5514;
  if (!body["port"].isNull() && !parseUint16Field(body["port"], port)) {
      sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_port\"}");
      return;
  }

  uint32_t ttlS = 300;
  if (!body["ttl_s"].isNull() && !parseUint32FieldRange(body["ttl_s"], 1, 1800, ttlS)) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_ttl_s\"}");
    return;
  }

  lrslog::setUdpMirror(target, port, ttlS * 1000UL);

  char ipbuf[16];
  snprintf(ipbuf,
           sizeof(ipbuf),
           "%u.%u.%u.%u",
           static_cast<unsigned>(target[0]),
           static_cast<unsigned>(target[1]),
           static_cast<unsigned>(target[2]),
           static_cast<unsigned>(target[3]));
  LRS_LOGI(API,
           "event=udp_log_mirror_enabled host=%s port=%u ttl_s=%lu",
           ipbuf,
           static_cast<unsigned>(port),
           static_cast<unsigned long>(ttlS));

  char resp[192];
  snprintf(resp,
           sizeof(resp),
           "{\"ok\":true,\"enabled\":true,\"host\":\"%s\",\"port\":%u,\"ttl_ms\":%lu,\"remaining_ms\":%lu}",
           ipbuf,
           static_cast<unsigned>(port),
           static_cast<unsigned long>(ttlS * 1000UL),
           static_cast<unsigned long>(lrslog::udpMirrorRemainingMs()));
  sendTracked(200, "application/json", resp);
}

void WebConsole::handleOtaUpload() {
  if (!requireAuth(true)) return;
  if (!ota_upload_ok_) {
    server_.send(500, "text/plain", ota_upload_error_.length() ? ota_upload_error_ : "OTA failed");
    return;
  }
  server_.send(200, "text/plain", "ok");
  delay(150);
  ESP.restart();
}

void WebConsole::handleOtaUploadChunk() {
  if (!requireAuth(true)) return;
  HTTPUpload &upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    ota_upload_ok_ = false;
    ota_upload_error_ = "";
    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      ota_upload_error_ = "Cannot start OTA";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      ota_upload_error_ = "Write failed";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      ota_upload_error_ = "Finalize failed";
      ota_upload_ok_ = false;
      return;
    }
    ota_upload_ok_ = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    ota_upload_error_ = "Upload aborted";
    ota_upload_ok_ = false;
  }
}

void WebConsole::handleLogsCsv() {
  if (!requireAuth()) return;
  server_.send(410, "text/plain", "in-memory log history disabled");
}

void WebConsole::handleLogsText() {
  if (!requireAuth()) return;
  server_.send(410, "text/plain", "in-memory log history disabled");
}

void WebConsole::handleFactoryReset() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const String postedPassword = String(static_cast<const char *>(body["admin_password"] | ""));
  if (postedPassword != cfg.admin_password) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"invalid_password\"}");
    return;
  }
  const bool keepSharedFleetKey = parseBoolField(body["keep_shared_fleet_key"], false);
  const bool keepWifiCredentials = parseBoolField(body["keep_wifi_credentials"], false);

  if (!config_->factoryReset(keepSharedFleetKey, keepWifiCredentials)) {
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"reset_save_failed\"}");
    return;
  }

  clearSession();
  server_.sendHeader("Set-Cookie", "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(120);
  ESP.restart();
}

void WebConsole::handleReboot() {
  if (!requireAuth(true)) return;
  auto &cfg = config_->settings();
  cfg.audit_last_reboot_reason = "web_reboot";
  cfg.audit_last_reboot_ms = millis();
  config_->save();
  server_.send(200, "text/plain", "rebooting");
  delay(150);
  ESP.restart();
}
