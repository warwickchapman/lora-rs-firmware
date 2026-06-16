#include "mqtt_bridge.h"
#include "admin_executor.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <cstring>

#include "build_info.h"
#include "logger.h"
#include "ota_pull.h"
#include "state_machine.h"

namespace {
constexpr uint32_t kPublishIntervalMs = 10000;
constexpr uint32_t kDiscoveryPublishIntervalMs = 60000;
constexpr uint8_t kStatusPublishYieldEveryOps = 4;
constexpr uint8_t kStatusPeerSnapshotsPerTick = 1;
constexpr size_t kMqttTopicBufBytes = 192;

const char *peerAckStateText(PeerAckState s) {
  switch (s) {
    case PeerAckState::Pending:
      return "pending";
    case PeerAckState::Ok:
      return "ok";
    case PeerAckState::Timeout:
      return "timeout";
    default:
      return "unknown";
  }
}

bool knownPeerAddress(NodeStateMachine *sm, uint8_t addr) {
  if (sm == nullptr) return false;
  const size_t n = sm->peerCount();
  for (size_t i = 0; i < n; ++i) {
    PeerStatusSnapshot node{};
    if (sm->peerByIndex(i, node) && node.address == addr) return true;
  }
  return false;
}

bool isSha256Hex(const char *hex) {
  if (hex == nullptr || strlen(hex) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char c = hex[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

bool parseHexAddressSegmentCstr(const char *segment, size_t len, uint8_t &out) {
  if (segment == nullptr || len == 0 || len > 8) return false;
  char buf[9];
  memcpy(buf, segment, len);
  buf[len] = '\0';
  char *end = nullptr;
  long parsed = strtol(buf, &end, 16);
  if (end == nullptr || *end != '\0') return false;
  if (parsed < 1 || parsed > 254) return false;
  out = static_cast<uint8_t>(parsed);
  return true;
}

bool parseDecAddressSegmentCstr(const char *segment, size_t len, uint8_t &out) {
  if (segment == nullptr || len == 0 || len > 8) return false;
  char buf[9];
  memcpy(buf, segment, len);
  buf[len] = '\0';
  char *end = nullptr;
  long parsed = strtol(buf, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  if (parsed < 1 || parsed > 254) return false;
  out = static_cast<uint8_t>(parsed);
  return true;
}

uint32_t parseChipIdFromSegment(const char *segment, size_t len) {
  if (segment == nullptr || len == 0) return 0;
  // Look for "_lrs-"
  const char *p = nullptr;
  for (size_t i = 0; i + 5 <= len; ++i) {
    if (strncmp(segment + i, "_lrs-", 5) == 0) {
      p = segment + i + 5;
      break;
    }
  }
  if (p == nullptr) return 0;
  // Parse hex
  char hexBuf[16]{};
  size_t hexLen = len - (p - segment);
  if (hexLen > 15) hexLen = 15;
  memcpy(hexBuf, p, hexLen);
  hexBuf[hexLen] = '\0';
  char *end = nullptr;
  uint32_t val = static_cast<uint32_t>(strtoul(hexBuf, &end, 16));
  if (end == nullptr || *end != '\0') return 0;
  return val;
}

bool parsePeerAddressSegmentCstr(const char *segment, size_t len, NodeStateMachine *sm, uint8_t &out) {
  if (segment == nullptr || len == 0) return false;

  // Compound segment: "NN_lrs-XXXXXXXX" — extract the decimal prefix before '_'
  for (size_t i = 0; i < len; ++i) {
    if (segment[i] == '_') {
      return i > 0 && parseDecAddressSegmentCstr(segment, i, out);
    }
  }

  if (len >= 2 && segment[0] == '0' && (segment[1] == 'x' || segment[1] == 'X')) {
    return parseHexAddressSegmentCstr(segment + 2, len - 2, out);
  }

  bool hasHexAlpha = false;
  for (size_t i = 0; i < len; ++i) {
    const char c = segment[i];
    if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      hasHexAlpha = true;
      break;
    }
  }
  if (hasHexAlpha) {
    return parseHexAddressSegmentCstr(segment, len, out);
  }

  uint8_t decAddr = 0;
  uint8_t hexAddr = 0;
  const bool decOk = parseDecAddressSegmentCstr(segment, len, decAddr);
  const bool hexOk = parseHexAddressSegmentCstr(segment, len, hexAddr);
  if (!decOk && !hexOk) return false;

  if (decOk && hexOk && decAddr != hexAddr) {
    const bool decKnown = knownPeerAddress(sm, decAddr);
    const bool hexKnown = knownPeerAddress(sm, hexAddr);
    if (decKnown && !hexKnown) {
      out = decAddr;
      return true;
    }
    if (hexKnown && !decKnown) {
      out = hexAddr;
      return true;
    }
    if (len <= 2) {
      out = hexAddr;
      return true;
    }
  }

  if (decOk) {
    out = decAddr;
    return true;
  }
  out = hexAddr;
  return true;
}

// Format peer address topic segment: "01_lrs-8829ca6f" when chip_id is known, "01" otherwise.
void formatPeerAddrSegment(char *out, size_t outLen, uint8_t addr, uint32_t chipId) {
  if (chipId != 0) {
    snprintf(out, outLen, "%02u_lrs-%08lx",
             static_cast<unsigned>(addr), static_cast<unsigned long>(chipId));
  } else {
    snprintf(out, outLen, "%02u", static_cast<unsigned>(addr));
  }
}

// Format canonical peer address topic segment: "01_lrs-8829ca6f". Returns false if chipId is 0.
bool formatCanonicalPeerAddrSegment(char *out, size_t outLen, uint8_t addr, uint32_t chipId) {
  if (chipId == 0) {
    return false;
  }
  snprintf(out, outLen, "%02u_lrs-%08lx",
           static_cast<unsigned>(addr), static_cast<unsigned long>(chipId));
  return true;
}


bool parseSignedPayloadLong(const uint8_t *payload, unsigned int length, long &out) {
  if (payload == nullptr) return false;
  if (length == 0) {
    out = 0;
    return true;
  }
  if (length >= 24) return false;
  char buf[24];
  unsigned int n = 0;
  for (unsigned int i = 0; i < length && n < (sizeof(buf) - 1); ++i) {
    const char c = static_cast<char>(payload[i]);
    if (c == '\r' || c == '\n' || c == '\t') continue;
    buf[n++] = c;
  }
  while (n > 0 && buf[n - 1] == ' ') --n;
  size_t start = 0;
  while (start < n && buf[start] == ' ') ++start;
  if (start > 0 && start < n) memmove(buf, buf + start, n - start);
  if (start > 0) n = (start < n) ? (n - start) : 0;
  buf[n] = '\0';
  if (n == 0) {
    out = 0;
    return true;
  }
  char *end = nullptr;
  long parsed = strtol(buf, &end, 10);
  if (end == nullptr || *end != '\0') return false;
  out = parsed;
  return true;
}

bool parseBoolPayload(const uint8_t *payload, unsigned int length, bool &out) {
  if (payload == nullptr || length == 0 || length >= 32) return false;
  char buf[32];
  unsigned int n = 0;
  for (unsigned int i = 0; i < length && n < sizeof(buf) - 1; ++i) {
    const char c = static_cast<char>(payload[i]);
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ') continue;
    buf[n++] = static_cast<char>(tolower(c));
  }
  buf[n] = '\0';
  if (strcmp(buf, "1") == 0 || strcmp(buf, "on") == 0 || strcmp(buf, "enable") == 0 || strcmp(buf, "enabled") == 0 ||
      strcmp(buf, "true") == 0) {
    out = true;
    return true;
  }
  if (strcmp(buf, "0") == 0 || strcmp(buf, "off") == 0 || strcmp(buf, "disable") == 0 || strcmp(buf, "disabled") == 0 ||
      strcmp(buf, "false") == 0) {
    out = false;
    return true;
  }
  JsonDocument doc;
  auto err = deserializeJson(doc, payload, length);
  if (!err && doc["enabled"].is<bool>()) {
    out = doc["enabled"].as<bool>();
    return true;
  }
  return false;
}

bool parseUdpLogControlPayload(const uint8_t *payload, unsigned int length, bool &enabled, IPAddress &host, uint16_t &port, uint32_t &ttlS) {
  if (payload == nullptr || length == 0 || length > 192) return false;
  bool boolValue = false;
  if (parseBoolPayload(payload, length, boolValue)) {
    enabled = boolValue;
    host = IPAddress();
    port = 0;
    ttlS = 0;
    return true;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, payload, length);
  if (err) return false;

  enabled = true;
  if (doc["enabled"].is<bool>()) {
    enabled = doc["enabled"].as<bool>();
  } else if (doc["enabled"].is<const char *>()) {
    const char *raw = doc["enabled"];
    if (raw == nullptr || !parseBoolPayload(reinterpret_cast<const uint8_t *>(raw), strlen(raw), enabled)) return false;
  } else {
    enabled = true;
  }

  port = static_cast<uint16_t>(doc["port"] | 5514);
  ttlS = doc["ttl_s"] | 300UL;
  if (ttlS > 3600UL) ttlS = 3600UL;

  const char *hostStr = doc["host"] | "";
  if (enabled && (port == 0 || !host.fromString(hostStr))) return false;
  if (!enabled) {
    host = IPAddress();
    port = 0;
    ttlS = 0;
  }
  return true;
}

bool parseOtaPullPayload(const uint8_t *payload, unsigned int length, String &url, String &sha256) {
  if (payload == nullptr || length == 0 || length > 256) return false;
  JsonDocument doc;
  auto err = deserializeJson(doc, payload, length);
  if (err) return false;
  url = String(static_cast<const char *>(doc["url"] | ""));
  sha256 = String(static_cast<const char *>(doc["sha256"] | ""));
  return url.length() > 0 && isSha256Hex(sha256.c_str());
}
}

MqttBridge *MqttBridge::instance_ = nullptr;

bool MqttBridge::begin(const Settings &cfg, const String &chipIdHex, NodeStateMachine *sm, AdminExecutor *executor) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  chip_id_hex_ = chipIdHex;
  sm_ = sm;
  executor_ = executor;

  rebuildTopics();

  instance_ = this;
  // Keep connection attempts short so MQTT outages do not stall control loop timing.
  mqtt_client_.setSocketTimeout(1);
  mqtt_client_.setBufferSize(768);
  mqtt_client_.setCallback(MqttBridge::staticCallback);
  resetPeerPublishCache();
  resetFibonacci();
  return true;
}

void MqttBridge::clearPeerRetained(uint8_t addr, uint32_t chipId) {
  if (instance_ != nullptr) {
    instance_->clearPeerRetainedTopics(addr, chipId);
  }
}

void MqttBridge::applyConfig(const Settings &cfg, const String &chipIdHex) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  chip_id_hex_ = chipIdHex;
  rebuildTopics();

  if (mqtt_client_.connected()) {
    mqtt_client_.disconnect();
  }
  resetPeerPublishCache();
  resetFibonacci();
  status_publish_in_progress_ = false;
  status_publish_locals_done_ = false;
  status_publish_peer_index_ = 0;
}

void MqttBridge::tick(bool wifiConnected) {
  if (!settings_ || !runtime_.mqtt_client_enabled || settings_->mqtt_host.length() == 0) {
    status_publish_in_progress_ = false;
    status_publish_locals_done_ = false;
    status_publish_peer_index_ = 0;
    return;
  }

  if (!wifiConnected) {
    status_publish_in_progress_ = false;
    status_publish_locals_done_ = false;
    status_publish_peer_index_ = 0;
    return;
  }

  if (!connectIfNeeded()) {
    status_publish_in_progress_ = false;
    status_publish_locals_done_ = false;
    status_publish_peer_index_ = 0;
    return;
  }

  mqtt_client_.loop();

  const uint32_t now = millis();
  if (!status_publish_in_progress_ && (now - last_publish_ms_) >= kPublishIntervalMs) {
    last_publish_ms_ = now;
    status_publish_in_progress_ = true;
    status_publish_locals_done_ = false;
    status_publish_peer_index_ = 0;
  }

  if (status_publish_in_progress_) {
    publishStatus();
  }
}

bool MqttBridge::connected() { return mqtt_client_.connected(); }

void MqttBridge::disconnect() {
  if (mqtt_client_.connected()) {
    mqtt_client_.disconnect();
  }
}

void MqttBridge::staticCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (instance_ != nullptr) {
    instance_->mqttCallback(topic, payload, length);
  }
}

void MqttBridge::refreshRuntimeCfg(const Settings &cfg) {
  runtime_.mqtt_client_enabled = cfg.mqtt_client_enabled;
  runtime_.mqtt_control_enabled = cfg.mqtt_control_enabled;
  runtime_.role_tx = cfg.role_tx;
  runtime_.local_address = cfg.local_address;
  runtime_.remote_address = cfg.remote_address;
  runtime_.mqtt_port = cfg.mqtt_port;
  runtime_.tx_mqtt_remote_polling_enabled = cfg.tx_mqtt_remote_polling_enabled;
}

void MqttBridge::advanceFibonacci() {
  uint32_t next = fib_prev_s_ + fib_curr_s_;
  if (next > kFibMaxDelayS) next = kFibMaxDelayS;
  fib_prev_s_ = fib_curr_s_;
  fib_curr_s_ = next;
}

void MqttBridge::resetFibonacci() {
  fib_prev_s_ = 0;
  fib_curr_s_ = 1;
}

void MqttBridge::resetPeerPublishCache() {
  for (size_t i = 0; i < kPeerPublishCacheSize; ++i) {
    peer_publish_cache_[i] = PeerPublishCacheEntry{};
  }
}

MqttBridge::PeerPublishCacheEntry *MqttBridge::findPeerPublishCache(uint8_t addr) {
  for (size_t i = 0; i < kPeerPublishCacheSize; ++i) {
    if (peer_publish_cache_[i].in_use && peer_publish_cache_[i].addr == addr) return &peer_publish_cache_[i];
  }
  return nullptr;
}

MqttBridge::PeerPublishCacheEntry *MqttBridge::upsertPeerPublishCache(uint8_t addr) {
  PeerPublishCacheEntry *entry = findPeerPublishCache(addr);
  if (entry != nullptr) return entry;
  for (size_t i = 0; i < kPeerPublishCacheSize; ++i) {
    if (!peer_publish_cache_[i].in_use) {
      peer_publish_cache_[i].in_use = true;
      peer_publish_cache_[i].addr = addr;
      return &peer_publish_cache_[i];
    }
  }
  return nullptr;
}

void MqttBridge::clearPeerPublishCache(uint8_t addr) {
  PeerPublishCacheEntry *entry = findPeerPublishCache(addr);
  if (entry != nullptr) {
    *entry = PeerPublishCacheEntry{};
  }
}

void MqttBridge::rebuildTopics() {
  if (!settings_) return;
  snprintf(host_name_, sizeof(host_name_), "lrs-%s", chip_id_hex_.c_str());
  snprintf(topic_base_, sizeof(topic_base_), "%s/%s", settings_->mqtt_topic_root.c_str(), host_name_);
  snprintf(relay_topic_, sizeof(relay_topic_), "%s/relay", topic_base_);
  snprintf(control_topic_, sizeof(control_topic_), "%s/control", topic_base_);
  snprintf(udp_log_control_topic_, sizeof(udp_log_control_topic_), "%s/udp_log_control", topic_base_);
  snprintf(ota_pull_topic_, sizeof(ota_pull_topic_), "%s/ota_pull", topic_base_);
  snprintf(admin_command_topic_, sizeof(admin_command_topic_), "%s/admin_command", topic_base_);
  snprintf(admin_response_topic_, sizeof(admin_response_topic_), "%s/admin_response", topic_base_);
  snprintf(remote_prefix_, sizeof(remote_prefix_), "%s/peers/", topic_base_);
  snprintf(discovery_topic_, sizeof(discovery_topic_), "%s/discovery/%s", settings_->mqtt_topic_root.c_str(), host_name_);
  snprintf(availability_topic_, sizeof(availability_topic_), "%s/availability", topic_base_);

  mqtt_client_.setServer(settings_->mqtt_host.c_str(), runtime_.mqtt_port);
}

bool MqttBridge::buildLocalTopic(char *out, size_t outLen, const char *leaf) const {
  if (out == nullptr || outLen == 0) return false;
  const int n = snprintf(out, outLen, "%s/%s", topic_base_, leaf);
  return n > 0 && static_cast<size_t>(n) < outLen;
}

bool MqttBridge::buildPeerTopic(char *out, size_t outLen, const char *addrSegment, const char *leaf) const {
  if (out == nullptr || outLen == 0) return false;
  const int n = snprintf(out, outLen, "%s/peers/%s/%s", topic_base_, addrSegment, leaf);
  return n > 0 && static_cast<size_t>(n) < outLen;
}

void MqttBridge::mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (topic == nullptr) return;

  if (strcmp(topic, relay_topic_) == 0) {
    if (!runtime_.mqtt_control_enabled) {
      lrslog::event("mqtt_control_blocked_mode", 0, 0, 0);
      return;
    }
    if (length == 0 || sm_ == nullptr) {
      return;
    }

    bool targetRelay = payload[0] == '1';
    sm_->mqttSetLocalRelay(targetRelay ? 1 : 0);

    {
      lrslog::event("mqtt_relay_topic", 0, 0, targetRelay ? 1 : 0);
    }
    return;
  }

  if (strcmp(topic, control_topic_) == 0) {
    if (!runtime_.mqtt_control_enabled) {
      lrslog::event("mqtt_control_blocked_mode", 0, 0, 0);
      return;
    }
    if (!runtime_.role_tx || sm_ == nullptr) {
      return;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, payload, length);
    if (err) {
      {
        lrslog::event("mqtt_control_json_err", 0, 0, 0);
      }
      return;
    }

    uint8_t addr = 0;
    if (doc["addr"].is<const char *>()) {
      const char *addrStr = doc["addr"];
      if (addrStr == nullptr || !parsePeerAddressSegmentCstr(addrStr, strlen(addrStr), sm_, addr)) {
        return;
      }
    } else if (doc["addr"].is<int>()) {
      addr = static_cast<uint8_t>(doc["addr"].as<int>());
    }

    const bool relay = (doc["relay"] | 0) != 0;
    if (addr == 0 || addr == 255) {
      return;
    }

    sm_->mqttSendPeerRelay(addr, relay ? 1 : 0);
    {
      lrslog::event("mqtt_control_topic", 0, 0, relay ? 1 : 0);
    }
    return;
  }

  if (strcmp(topic, udp_log_control_topic_) == 0) {
    if (!runtime_.mqtt_control_enabled) {
      lrslog::event("mqtt_control_blocked_mode", 0, 0, 0);
      return;
    }

    bool enabled = false;
    IPAddress host;
    uint16_t port = 0;
    uint32_t ttlS = 0;
    if (!parseUdpLogControlPayload(payload, length, enabled, host, port, ttlS)) {
      lrslog::event("mqtt_udp_log_control_json_err", 0, 0, 0);
      return;
    }

    if (enabled) {
      lrslog::setUdpMirror(host, port, ttlS * 1000UL);
    } else {
      lrslog::disableUdpMirror();
    }
    lrslog::event(enabled ? "mqtt_udp_log_control_enable" : "mqtt_udp_log_control_disable", 0, 0, static_cast<uint8_t>(port & 0xFFU));
    return;
  }

  if (strcmp(topic, admin_command_topic_) == 0) {
    if (length == 0 || executor_ == nullptr) {
      return;
    }
    if (!runtime_.mqtt_control_enabled) {
      lrslog::event("mqtt_control_blocked_mode", 0, 0, 0);
      JsonDocument reqDoc;
      deserializeJson(reqDoc, payload, length);
      const char *reqId = reqDoc["id"] | "";
      const char *cmd = reqDoc["cmd"] | "";

      JsonDocument respDoc;
      respDoc["ok"] = false;
      respDoc["cmd"] = cmd;
      if (reqId[0] != '\0') {
        respDoc["id"] = reqId;
      }
      respDoc["error"] = "mqtt_control_disabled";
      String respStr;
      serializeJson(respDoc, respStr);
      if (mqtt_client_.connected()) {
        mqtt_client_.publish(admin_response_topic_, respStr.c_str(), false);
      }
      return;
    }
    String cmdPayload;
    cmdPayload.reserve(length + 1);
    for (unsigned int i = 0; i < length; ++i) {
      cmdPayload += static_cast<char>(payload[i]);
    }
    executor_->execute(cmdPayload, [this](const String &response) {
      if (mqtt_client_.connected()) {
        mqtt_client_.publish(admin_response_topic_, response.c_str(), false);
      }
    }, true);
    return;
  }

  if (strcmp(topic, ota_pull_topic_) == 0) {
    if (!runtime_.mqtt_control_enabled) {
      lrslog::event("mqtt_control_blocked_mode", 0, 0, 0);
      return;
    }
    String url;
    String sha256;
    if (!parseOtaPullPayload(payload, length, url, sha256)) {
      lrslog::event("mqtt_ota_pull_json_err", 0, 0, 0);
      return;
    }
    String error;
    if (!otaPullFromUrl(url.c_str(), sha256.c_str(), error)) {
      LRS_LOGW(SYS, "event=mqtt_ota_pull_failed error=%s", error.c_str());
      return;
    }
    lrslog::event("mqtt_ota_pull_reboot", 0, 0, 0);
    delay(150);
    ESP.restart();
    return;
  }

  if (runtime_.role_tx && sm_ != nullptr) {
    if (!runtime_.mqtt_control_enabled) {
      return;
    }
    const size_t remotePrefixLen = strlen(remote_prefix_);
    if (strncmp(topic, remote_prefix_, remotePrefixLen) != 0) {
      return;
    }

    const char *suffix = topic + remotePrefixLen;
    const char *slash = strchr(suffix, '/');
    if (slash == nullptr || slash == suffix) {
      return;
    }

    uint8_t addr = 0;
    const size_t addrLen = static_cast<size_t>(slash - suffix);
    if (!parsePeerAddressSegmentCstr(suffix, addrLen, sm_, addr)) {
      return;
    }

    const char *leaf = slash + 1;
    if (strcmp(leaf, "poll_interval_s") == 0) {
      long sec = 0;
      if (!parseSignedPayloadLong(payload, length, sec)) return;
      if (sec < 0) sec = 0;
      if (sec > 0 && sec < 60) sec = 60;
      if (sec > 3600) sec = 3600;
      sm_->mqttSetPeerPollIntervalMs(addr, static_cast<uint32_t>(sec) * 1000U);
      {
        lrslog::event("mqtt_remote_poll_interval", 0, static_cast<uint32_t>(sec), addr);
      }
      return;
    }

    if (strcmp(leaf, "poll_now") == 0) {
      sm_->mqttPollPeerNow(addr);
      {
        lrslog::event("mqtt_remote_poll_now", 0, 0, addr);
      }
      return;
    }

    if (strcmp(leaf, "wifi") == 0) {
      bool enabled = true;
      if (!parseBoolPayload(payload, length, enabled)) return;
      sm_->mqttSetPeerWifi(addr, enabled);
      {
        lrslog::event(enabled ? "mqtt_remote_wifi_enable" : "mqtt_remote_wifi_disable", 0, 0, addr);
      }
      return;
    }

    if (strcmp(leaf, "udp_log_control") == 0) {
      bool enabled = false;
      IPAddress host;
      uint16_t port = 0;
      uint32_t ttlS = 0;
      if (!parseUdpLogControlPayload(payload, length, enabled, host, port, ttlS)) return;
      if (sm_->mqttSetPeerUdpLogControl(addr, enabled, host, port, ttlS)) {
        lrslog::event(enabled ? "mqtt_remote_udp_log_control_enable" : "mqtt_remote_udp_log_control_disable", 0, 0, addr);
      }
      return;
    }

    if (strcmp(leaf, "forget") == 0) {
      const bool forget = (length > 0 && payload[0] != '0');
      if (!forget) return;

      // Extract the chip ID from the topic address segment if it's canonical
      uint32_t chipId = 0;
      const char *suffix = topic + remotePrefixLen;
      const char *slash = strchr(suffix, '/');
      if (slash != nullptr) {
        const size_t addrLen = static_cast<size_t>(slash - suffix);
        chipId = parseChipIdFromSegment(suffix, addrLen);
      }

      clearPeerRetainedTopics(addr, chipId);
      const bool removed = sm_->mqttForgetPeer(addr);
      {
        lrslog::event(removed ? "mqtt_remote_forget_ok" : "mqtt_remote_forget_missing", 0, 0, addr);
      }
      return;
    }
  }
}

void MqttBridge::clearPeerRetainedTopics(uint8_t addr, uint32_t passedChipId) {
  if (!mqtt_client_.connected()) return;

  // Read chip_id from cache/SM/Settings before clearing
  const PeerPublishCacheEntry *entry = findPeerPublishCache(addr);
  uint32_t chipId = passedChipId;
  if (chipId == 0 && entry != nullptr) {
    chipId = entry->chip_id;
  }
  if (chipId == 0 && sm_ != nullptr) {
    chipId = sm_->resolveChipIdForAddress(addr);
    if (chipId == 0) chipId = sm_->activePeerChipIdForAddress(addr);
  }
  if (chipId == 0 && settings_ != nullptr) {
    for (uint8_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
      if (settings_->known_peer_addresses[i] == addr) {
        chipId = settings_->known_peer_chip_ids[i];
        break;
      }
    }
  }
  clearPeerPublishCache(addr);

  const char *leaves[] = {
      "relay",           "input",              "dry_contact",      "ack_state",
      "addr_hex",        "addr_dec",
      "uplink_rssi_dbm", "downlink_rssi_dbm",  "last_seen_ms",     "last_seen_age_s", "last_cmd_counter",
      "poll_interval_s", "last_poll_tx_ms",    "poll_state",       "temp_c",           "tank_status",
      "tank_depth_mm",   "tank_current_ma",    "tank_voltage_mv",  "forget",           "poll_now",
      "wifi",            "input_feedback",     "uptime_ms",        "heap_free",        "heap_max_block",
      "heap_frag_pct",   "relay_feedback",
  };

  if (chipId != 0) {
    char canonicalSeg[24];
    if (formatCanonicalPeerAddrSegment(canonicalSeg, sizeof(canonicalSeg), addr, chipId)) {
      char topic[kMqttTopicBufBytes];
      for (const char *leaf : leaves) {
        if (buildPeerTopic(topic, sizeof(topic), canonicalSeg, leaf)) {
          mqtt_client_.publish(topic, "", true);
        }
      }
    }
  }
}

bool MqttBridge::connectIfNeeded() {
  if (mqtt_client_.connected()) {
    return true;
  }

  const uint32_t now = millis();
  if ((now - last_reconnect_attempt_ms_) < (fib_curr_s_ * 1000UL)) {
    return false;
  }
  last_reconnect_attempt_ms_ = now;

  char clientId[32]{};
  snprintf(clientId, sizeof(clientId), "LRS-%s", chip_id_hex_.c_str());
  bool ok = false;
  if (!settings_) return false;
  if (settings_->mqtt_user.length() > 0) {
    ok = mqtt_client_.connect(clientId, settings_->mqtt_user.c_str(), settings_->mqtt_password.c_str(),
                              availability_topic_, 0, true, "offline");
  } else {
    ok = mqtt_client_.connect(clientId, availability_topic_, 0, true, "offline");
  }

  if (!ok) {
    {
      lrslog::event("mqtt_connect_failed", 0, fib_curr_s_ * 1000UL, 0);
    }
    advanceFibonacci();
    return false;
  }

  resetFibonacci();

  if (runtime_.mqtt_control_enabled) {
    mqtt_client_.subscribe(relay_topic_);
    mqtt_client_.subscribe(control_topic_);
    mqtt_client_.subscribe(udp_log_control_topic_);
    mqtt_client_.subscribe(ota_pull_topic_);
    mqtt_client_.subscribe(admin_command_topic_);
    char topic[kMqttTopicBufBytes];
    if (buildPeerTopic(topic, sizeof(topic), "+", "poll_interval_s")) mqtt_client_.subscribe(topic);
    if (buildPeerTopic(topic, sizeof(topic), "+", "poll_now")) mqtt_client_.subscribe(topic);
    if (buildPeerTopic(topic, sizeof(topic), "+", "wifi")) mqtt_client_.subscribe(topic);
    if (buildPeerTopic(topic, sizeof(topic), "+", "udp_log_control")) mqtt_client_.subscribe(topic);
    if (buildPeerTopic(topic, sizeof(topic), "+", "forget")) mqtt_client_.subscribe(topic);
  }

  {
    lrslog::event("mqtt_connected", 0, 0, 0);
  }
  publishDiscovery();
  mqtt_client_.publish(availability_topic_, "online", true);
  return true;
}

void MqttBridge::publishStatus() {
  if (!mqtt_client_.connected() || sm_ == nullptr) {
    status_publish_in_progress_ = false;
    status_publish_locals_done_ = false;
    status_publish_peer_index_ = 0;
    return;
  }

  uint8_t publishOpsSinceYield = 0;
  auto maybeYield = [&]() {
    ++publishOpsSinceYield;
    if (publishOpsSinceYield >= kStatusPublishYieldEveryOps) {
      publishOpsSinceYield = 0;
      yield();
    }
  };
  auto publishRetained = [&](const char *topic, const char *payload) {
    mqtt_client_.publish(topic, payload, true);
    maybeYield();
  };
  auto publishRetainedTopic = [&](const char *topic, const char *payload) {
    mqtt_client_.publish(topic, payload, true);
    maybeYield();
  };

  if (!status_publish_locals_done_) {
    char topic[kMqttTopicBufBytes];
    const bool localInput = sm_->localDryContactState() != 0;
    if (buildLocalTopic(topic, sizeof(topic), "input")) publishRetained(topic, localInput ? "1" : "0");
    if (buildLocalTopic(topic, sizeof(topic), "dry_contact")) publishRetained(topic, localInput ? "1" : "0");
    publishRetained(relay_topic_, sm_->relayState() ? "1" : "0");
    if (buildLocalTopic(topic, sizeof(topic), "relay_feedback")) publishRetained(topic, sm_->relayFeedbackState() ? "1" : "0");
    if (buildLocalTopic(topic, sizeof(topic), "type")) publishRetained(topic, runtime_.role_tx ? "tx" : "rx");

    char addrHex[3];
    snprintf(addrHex, sizeof(addrHex), "%02X", runtime_.local_address);
    char addrHexPrefixed[5];
    snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
    if (buildLocalTopic(topic, sizeof(topic), "addr")) publishRetained(topic, addrHexPrefixed);

    const SensorRegistry &localReg = sm_->localSensors();
    for (uint8_t i = 0; i < localReg.count(); ++i) {
      SensorReading r{};
      if (localReg.byIndex(i, r)) {
        char valBuf[32];
        char topicSuffix[64];
        snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/value", sensorKindToString(r.kind), r.instance);
        if (r.state == SensorState::Ok || r.state == SensorState::Overrange) {
          if (r.scale == 0) {
            snprintf(valBuf, sizeof(valBuf), "%d", static_cast<int>(r.value));
          } else {
            float divisor = 1.0f;
            for (uint8_t s = 0; s < r.scale; ++s) divisor *= 10.0f;
            dtostrf(static_cast<float>(r.value) / divisor, 0, r.scale, valBuf);
          }
          if (buildLocalTopic(topic, sizeof(topic), topicSuffix)) publishRetained(topic, valBuf);
        } else {
          if (buildLocalTopic(topic, sizeof(topic), topicSuffix)) publishRetained(topic, "");
        }
        snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/state", sensorKindToString(r.kind), r.instance);
        if (buildLocalTopic(topic, sizeof(topic), topicSuffix)) publishRetained(topic, sensorStateToString(r.state));
      }
    }

    SensorReading tankReading{};
    const bool tankValid = sm_->localSensors().find(SensorKind::TankLevel, 0, tankReading) &&
                           (tankReading.state == SensorState::Ok || tankReading.state == SensorState::Overrange);

    if (tankValid) {
      char diagBuf[16];
      dtostrf(static_cast<float>(sm_->localTankCurrentCentiMa()) / 100.0f, 0, 2, diagBuf);
      if (buildLocalTopic(topic, sizeof(topic), "diagnostics/tank_current_ma")) publishRetained(topic, diagBuf);
      snprintf(diagBuf, sizeof(diagBuf), "%u", static_cast<unsigned>(sm_->localTankVoltageMv()));
      if (buildLocalTopic(topic, sizeof(topic), "diagnostics/tank_voltage_mv")) publishRetained(topic, diagBuf);
    } else {
      if (buildLocalTopic(topic, sizeof(topic), "diagnostics/tank_current_ma")) publishRetained(topic, "");
      if (buildLocalTopic(topic, sizeof(topic), "diagnostics/tank_voltage_mv")) publishRetained(topic, "");
    }

    char updatedMs[16];
    snprintf(updatedMs, sizeof(updatedMs), "%lu", static_cast<unsigned long>(millis()));
    if (buildLocalTopic(topic, sizeof(topic), "last_updated")) publishRetained(topic, updatedMs);

    char numBuf[24];
    snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(lrslog::heapFree()));
    if (buildLocalTopic(topic, sizeof(topic), "heap_free")) publishRetained(topic, numBuf);
    snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    if (buildLocalTopic(topic, sizeof(topic), "heap_max_block")) publishRetained(topic, numBuf);
    snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(lrslog::heapFragPercent()));
    if (buildLocalTopic(topic, sizeof(topic), "heap_frag_pct")) publishRetained(topic, numBuf);
    snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(millis()));
    if (buildLocalTopic(topic, sizeof(topic), "uptime_ms")) publishRetained(topic, numBuf);
    status_publish_locals_done_ = true;
    if (runtime_.role_tx) {
      return;
    }
  }

  if (runtime_.role_tx) {
    const size_t nodeCount = sm_->peerCount();
    if (status_publish_peer_index_ > nodeCount) {
      status_publish_peer_index_ = 0;
    }

    uint8_t peerSnapshotsPublished = 0;
    for (; status_publish_peer_index_ < nodeCount; ++status_publish_peer_index_) {
      PeerStatusSnapshot node{};
      if (!sm_->peerByIndex(status_publish_peer_index_, node)) {
        continue;
      }
      if (node.chip_id == 0) {
        continue;
      }
      const uint8_t addr = node.address;
      PeerPublishCacheEntry *peerCache = upsertPeerPublishCache(addr);
      if (peerCache == nullptr) {
        continue;
      }
      if (!runtime_.tx_mqtt_remote_polling_enabled) {
        const bool changed =
            !peerCache->published_once || peerCache->last_seen_ms != node.last_seen_ms || peerCache->last_cmd_counter != node.last_cmd_counter;
        if (!changed) {
          continue;
        }
      }

      char addrHex[3];
      snprintf(addrHex, sizeof(addrHex), "%02X", node.address);
      char addrHexPrefixed[5];
      snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
      char addrDec[4];
      snprintf(addrDec, sizeof(addrDec), "%02u", static_cast<unsigned>(node.address));
      char addrSeg[24];
      if (!formatCanonicalPeerAddrSegment(addrSeg, sizeof(addrSeg), node.address, node.chip_id)) {
        continue;
      }

      auto publishRemote = [&](const char *addrSeg) {
        char topic[kMqttTopicBufBytes];
        char numBuf[24];

        const bool timedOut = (node.ack_state == PeerAckState::Timeout);

        // --- Operational topics: blank when timed out ---
        if (timedOut) {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "relay")) publishRetainedTopic(topic, "");
          if (!peerCache->input_published || peerCache->input_value != 0xFF) {
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "input")) publishRetainedTopic(topic, "");
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "dry_contact")) publishRetainedTopic(topic, "");
            peerCache->input_published = true;
            peerCache->input_value = 0xFF; // sentinel: stale
          }
          for (uint8_t j = 0; j < node.sensors.count(); ++j) {
            SensorReading r{};
            if (node.sensors.byIndex(j, r)) {
              char topicSuffix[64];
              snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/value", sensorKindToString(r.kind), r.instance);
              if (buildPeerTopic(topic, sizeof(topic), addrSeg, topicSuffix)) publishRetainedTopic(topic, "");
              snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/state", sensorKindToString(r.kind), r.instance);
              if (buildPeerTopic(topic, sizeof(topic), addrSeg, topicSuffix)) publishRetainedTopic(topic, "");
            }
          }
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "wifi")) publishRetainedTopic(topic, "");
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "relay_feedback")) publishRetainedTopic(topic, "");
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "input_feedback")) publishRetainedTopic(topic, "");
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "relay")) publishRetainedTopic(topic, node.relay_state ? "1" : "0");
          const uint8_t inputValue = node.input_state ? 1 : 0;
          if (!peerCache->input_published || peerCache->input_value != inputValue) {
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "input")) publishRetainedTopic(topic, inputValue ? "1" : "0");
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "dry_contact")) publishRetainedTopic(topic, inputValue ? "1" : "0");
            peerCache->input_published = true;
            peerCache->input_value = inputValue;
          }
          for (uint8_t j = 0; j < node.sensors.count(); ++j) {
            SensorReading r{};
            if (node.sensors.byIndex(j, r)) {
              char valBuf[32];
              char topicSuffix[64];
              snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/value", sensorKindToString(r.kind), r.instance);
              if (r.state == SensorState::Ok || r.state == SensorState::Overrange) {
                if (r.scale == 0) {
                  snprintf(valBuf, sizeof(valBuf), "%d", static_cast<int>(r.value));
                } else {
                  float divisor = 1.0f;
                  for (uint8_t s = 0; s < r.scale; ++s) divisor *= 10.0f;
                  dtostrf(static_cast<float>(r.value) / divisor, 0, r.scale, valBuf);
                }
                if (buildPeerTopic(topic, sizeof(topic), addrSeg, topicSuffix)) publishRetainedTopic(topic, valBuf);
              } else {
                if (buildPeerTopic(topic, sizeof(topic), addrSeg, topicSuffix)) publishRetainedTopic(topic, "");
              }
              snprintf(topicSuffix, sizeof(topicSuffix), "sensor/%s/%u/state", sensorKindToString(r.kind), r.instance);
              if (buildPeerTopic(topic, sizeof(topic), addrSeg, topicSuffix)) publishRetainedTopic(topic, sensorStateToString(r.state));
            }
          }
          if (node.wifi_state_known) {
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "wifi")) publishRetainedTopic(topic, node.wifi_enabled ? "1" : "0");
          } else {
            if (buildPeerTopic(topic, sizeof(topic), addrSeg, "wifi")) publishRetainedTopic(topic, "");
          }
        }

        // --- Always published: status/metadata/diagnostic ---
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "ack_state")) publishRetainedTopic(topic, peerAckStateText(node.ack_state));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "addr_hex")) publishRetainedTopic(topic, addrHex);
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "addr_dec")) publishRetainedTopic(topic, addrDec);

        snprintf(numBuf, sizeof(numBuf), "%d", node.uplink_rssi);
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "uplink_rssi_dbm")) publishRetainedTopic(topic, numBuf);
        if (node.downlink_rssi_valid) {
          snprintf(numBuf, sizeof(numBuf), "%d", node.downlink_rssi);
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "downlink_rssi_dbm")) publishRetainedTopic(topic, numBuf);
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "downlink_rssi_dbm")) publishRetainedTopic(topic, "");
        }
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_seen_ms));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_seen_ms")) publishRetainedTopic(topic, numBuf);
        const uint32_t ageS = (node.last_seen_ms > 0) ? (millis() - node.last_seen_ms) / 1000U : 0;
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(ageS));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_seen_age_s")) publishRetainedTopic(topic, numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_cmd_counter));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_cmd_counter")) publishRetainedTopic(topic, numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.poll_interval_ms / 1000U));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "poll_interval_s")) publishRetainedTopic(topic, numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_poll_tx_ms));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_poll_tx_ms")) publishRetainedTopic(topic, numBuf);
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "poll_state")) publishRetainedTopic(topic, node.poll_pending ? "pending" : "idle");

        if (!timedOut && node.maintenance_debug_known) {
          snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.heap_free));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "heap_free")) publishRetainedTopic(topic, numBuf);
          snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.heap_max_block));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "heap_max_block")) publishRetainedTopic(topic, numBuf);
          snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(node.heap_frag_pct));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "heap_frag_pct")) publishRetainedTopic(topic, numBuf);
          snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(node.relay_feedback));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "relay_feedback")) publishRetainedTopic(topic, numBuf);
          snprintf(numBuf, sizeof(numBuf), "%u", static_cast<unsigned>(node.input_feedback));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "input_feedback")) publishRetainedTopic(topic, numBuf);
        }
        if (!timedOut && node.uptime_ms > 0) {
          snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.uptime_ms));
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "uptime_ms")) publishRetainedTopic(topic, numBuf);
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "uptime_ms")) publishRetainedTopic(topic, "");
        }

        // --- Additional Peer Telemetry Leaves ---
        snprintf(numBuf, sizeof(numBuf), "%08lx", static_cast<unsigned long>(node.chip_id));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "chip_id")) publishRetainedTopic(topic, numBuf);

        if (node.fw_major != 0 || node.fw_minor != 0 || node.fw_patch != 0) {
          if (node.fw_build > 0) {
            snprintf(numBuf, sizeof(numBuf), "%u.%u.%u~%u", node.fw_major, node.fw_minor, node.fw_patch, node.fw_build);
          } else {
            snprintf(numBuf, sizeof(numBuf), "%u.%u.%u", node.fw_major, node.fw_minor, node.fw_patch);
          }
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "fw_version")) publishRetainedTopic(topic, numBuf);
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "fw_version")) publishRetainedTopic(topic, "");
        }

        if (node.wifi_connected && node.ip[0] != 0) {
          snprintf(numBuf, sizeof(numBuf), "%u.%u.%u.%u", node.ip[0], node.ip[1], node.ip[2], node.ip[3]);
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "ip")) publishRetainedTopic(topic, numBuf);
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "ip")) publishRetainedTopic(topic, "");
        }

        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "power_save_listen_only")) {
          publishRetainedTopic(topic, node.power_save_listen_only ? "1" : "0");
        }
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "power_save_active")) {
          publishRetainedTopic(topic, node.power_save_active ? "1" : "0");
        }
      };

      publishRemote(addrSeg);
      peerCache->chip_id = node.chip_id;
      peerCache->last_seen_ms = node.last_seen_ms;
      peerCache->last_cmd_counter = node.last_cmd_counter;
      peerCache->published_once = true;
      ++peerSnapshotsPublished;
      ++status_publish_peer_index_;
      if (peerSnapshotsPublished >= kStatusPeerSnapshotsPerTick) {
        return;
      }
    }
    status_publish_peer_index_ = 0;
  }

  status_publish_in_progress_ = false;
  status_publish_locals_done_ = false;
  const uint32_t now = millis();
  if ((now - last_discovery_publish_ms_) >= kDiscoveryPublishIntervalMs) {
    publishDiscovery();
  }

}

void MqttBridge::publishDiscovery() {
  if (!mqtt_client_.connected()) {
    return;
  }

  JsonDocument doc;
  if (!settings_) return;
  doc["serial"] = settings_->factory_serial;
  doc["chip_id"] = chip_id_hex_;
  doc["role"] = runtime_.role_tx ? "tx" : "rx";
  doc["addr"] = runtime_.local_address;
  doc["remote_addr"] = runtime_.remote_address;
  doc["mac"] = WiFi.macAddress();
  doc["sta_ip"] = WiFi.localIP().toString();
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["sta_ssid"] = settings_->wifi_sta_ssid;
  doc["uptime_ms"] = millis();
  doc["fw"] = "lrs";
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;

  char payload[512];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n > 0) {
    if (mqtt_client_.publish(discovery_topic_, payload, true)) {
      last_discovery_publish_ms_ = millis();
    } else {
      lrslog::event("mqtt_discovery_publish_failed", 0, 0, 0);
    }
  }
}


