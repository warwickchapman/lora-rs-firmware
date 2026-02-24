#include "mqtt_bridge.h"

#include <ArduinoJson.h>

#include "build_info.h"
#include "logger.h"
#include "state_machine.h"

namespace {
constexpr uint32_t kReconnectIntervalMs = 30000;
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

bool parseHexAddressSegment(const String &segment, uint8_t &out) {
  if (segment.length() == 0) return false;
  char *end = nullptr;
  long parsed = strtol(segment.c_str(), &end, 16);
  if (end == nullptr || *end != '\0') return false;
  if (parsed < 1 || parsed > 254) return false;
  out = static_cast<uint8_t>(parsed);
  return true;
}

bool parseDecAddressSegment(const String &segment, uint8_t &out) {
  if (segment.length() == 0) return false;
  char *end = nullptr;
  long parsed = strtol(segment.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return false;
  if (parsed < 1 || parsed > 254) return false;
  out = static_cast<uint8_t>(parsed);
  return true;
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

bool parsePeerAddressSegment(const String &segment, NodeStateMachine *sm, uint8_t &out) {
  // Explicit hex forms stay hex for backward compatibility.
  if (segment.startsWith("0x") || segment.startsWith("0X")) {
    return parseHexAddressSegment(segment.substring(2), out);
  }

  bool hasHexAlpha = false;
  for (size_t i = 0; i < segment.length(); ++i) {
    const char c = segment.charAt(i);
    if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
      hasHexAlpha = true;
      break;
    }
  }
  if (hasHexAlpha) {
    return parseHexAddressSegment(segment, out);
  }

  uint8_t decAddr = 0;
  uint8_t hexAddr = 0;
  const bool decOk = parseDecAddressSegment(segment, decAddr);
  const bool hexOk = parseHexAddressSegment(segment, hexAddr);
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
    // Ambiguous and unknown: keep old behavior for short IDs (hex).
    if (segment.length() <= 2) {
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
}

MqttBridge *MqttBridge::instance_ = nullptr;

bool MqttBridge::begin(const Settings &cfg, const String &chipIdHex, NodeStateMachine *sm) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  chip_id_hex_ = chipIdHex;
  sm_ = sm;

  rebuildTopics();

  instance_ = this;
  // Keep connection attempts short so MQTT outages do not stall control loop timing.
  mqtt_client_.setSocketTimeout(1);
  mqtt_client_.setBufferSize(768);
  mqtt_client_.setCallback(MqttBridge::staticCallback);
  memset(peer_last_seen_published_, 0, sizeof(peer_last_seen_published_));
  memset(peer_last_cmd_published_, 0, sizeof(peer_last_cmd_published_));
  memset(peer_published_once_, 0, sizeof(peer_published_once_));
  memset(peer_input_published_, 0, sizeof(peer_input_published_));
  memset(peer_input_value_, 0, sizeof(peer_input_value_));
  return true;
}

void MqttBridge::applyConfig(const Settings &cfg, const String &chipIdHex) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  chip_id_hex_ = chipIdHex;
  rebuildTopics();

  if (mqtt_client_.connected()) {
    mqtt_client_.disconnect();
  }
  memset(peer_last_seen_published_, 0, sizeof(peer_last_seen_published_));
  memset(peer_last_cmd_published_, 0, sizeof(peer_last_cmd_published_));
  memset(peer_published_once_, 0, sizeof(peer_published_once_));
  memset(peer_input_published_, 0, sizeof(peer_input_published_));
  memset(peer_input_value_, 0, sizeof(peer_input_value_));
  status_publish_in_progress_ = false;
  status_publish_locals_done_ = false;
  status_publish_peer_index_ = 0;
}

void MqttBridge::tick(bool wifiConnected) {
  if (!settings_ || !runtime_.mqtt_enabled || settings_->mqtt_host.length() == 0) {
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

void MqttBridge::staticCallback(char *topic, uint8_t *payload, unsigned int length) {
  if (instance_ != nullptr) {
    instance_->mqttCallback(topic, payload, length);
  }
}

void MqttBridge::refreshRuntimeCfg(const Settings &cfg) {
  runtime_.mqtt_enabled = cfg.mqtt_enabled;
  runtime_.role_tx = cfg.role_tx;
  runtime_.local_address = cfg.local_address;
  runtime_.remote_address = cfg.remote_address;
  runtime_.mqtt_port = cfg.mqtt_port;
  runtime_.tx_mqtt_remote_polling_enabled = cfg.tx_mqtt_remote_polling_enabled;
}

void MqttBridge::rebuildTopics() {
  if (!settings_) return;
  snprintf(host_name_, sizeof(host_name_), "lrs-%s", chip_id_hex_.c_str());
  snprintf(topic_base_, sizeof(topic_base_), "%s/%s", settings_->mqtt_topic_root.c_str(), host_name_);
  snprintf(relay_topic_, sizeof(relay_topic_), "%s/relay", topic_base_);
  snprintf(control_topic_, sizeof(control_topic_), "%s/control", topic_base_);
  snprintf(remote_prefix_, sizeof(remote_prefix_), "%s/peer/", topic_base_);
  snprintf(discovery_topic_, sizeof(discovery_topic_), "%s/discovery/%s", settings_->mqtt_topic_root.c_str(), host_name_);

  mqtt_client_.setServer(settings_->mqtt_host.c_str(), runtime_.mqtt_port);
}

bool MqttBridge::buildLocalTopic(char *out, size_t outLen, const char *leaf) const {
  if (out == nullptr || outLen == 0) return false;
  const int n = snprintf(out, outLen, "%s/%s", topic_base_, leaf);
  return n > 0 && static_cast<size_t>(n) < outLen;
}

bool MqttBridge::buildPeerTopic(char *out, size_t outLen, const char *addrSegment, const char *leaf) const {
  if (out == nullptr || outLen == 0) return false;
  const int n = snprintf(out, outLen, "%s/peer/%s/%s", topic_base_, addrSegment, leaf);
  return n > 0 && static_cast<size_t>(n) < outLen;
}

void MqttBridge::mqttCallback(char *topic, uint8_t *payload, unsigned int length) {
  String topicStr(topic);

  if (topicStr == relay_topic_) {
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

  if (topicStr == control_topic_) {
    if (!runtime_.role_tx || sm_ == nullptr) {
      return;
    }

    DynamicJsonDocument doc(256);
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
      if (addrStr == nullptr || !parsePeerAddressSegment(String(addrStr), sm_, addr)) {
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

  if (runtime_.role_tx && sm_ != nullptr) {
    if (!topicStr.startsWith(remote_prefix_)) {
      return;
    }

    const String suffix = topicStr.substring(strlen(remote_prefix_));
    const int slash = suffix.indexOf('/');
    if (slash <= 0) {
      return;
    }

    uint8_t addr = 0;
    if (!parsePeerAddressSegment(suffix.substring(0, slash), sm_, addr)) {
      return;
    }

    const String leaf = suffix.substring(slash + 1);
    if (leaf == "poll_interval_s") {
      String value;
      for (unsigned int i = 0; i < length; ++i) value += static_cast<char>(payload[i]);
      value.trim();
      long sec = value.toInt();
      if (sec < 0) sec = 0;
      if (sec > 0 && sec < 60) sec = 60;
      if (sec > 3600) sec = 3600;
      sm_->mqttSetPeerPollIntervalMs(addr, static_cast<uint32_t>(sec) * 1000U);
      {
        lrslog::event("mqtt_remote_poll_interval", 0, static_cast<uint32_t>(sec), addr);
      }
      return;
    }

    if (leaf == "poll_now") {
      sm_->mqttPollPeerNow(addr);
      {
        lrslog::event("mqtt_remote_poll_now", 0, 0, addr);
      }
      return;
    }

    if (leaf == "forget") {
      const bool forget = (length > 0 && payload[0] != '0');
      if (!forget) return;
      const bool removed = sm_->mqttForgetPeer(addr);
      clearPeerRetainedTopics(addr);
      {
        lrslog::event(removed ? "mqtt_remote_forget_ok" : "mqtt_remote_forget_missing", 0, 0, addr);
      }
      return;
    }
  }
}

void MqttBridge::clearPeerRetainedTopics(uint8_t addr) {
  if (!mqtt_client_.connected()) return;
  peer_last_seen_published_[addr] = 0;
  peer_last_cmd_published_[addr] = 0;
  peer_published_once_[addr] = false;
  peer_input_published_[addr] = false;
  peer_input_value_[addr] = 0;

  char addrHex[3];
  snprintf(addrHex, sizeof(addrHex), "%02X", addr);
  char addrHexPrefixed[5];
  snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
  char addrDec[4];
  snprintf(addrDec, sizeof(addrDec), "%u", static_cast<unsigned>(addr));

  const String bases[] = {
      String(topic_base_) + "/peer/" + String(addrHexPrefixed),
      String(topic_base_) + "/peer/" + String(addrHex),
      String(topic_base_) + "/peer/" + String(addrDec),
  };

  const char *leaves[] = {
      "relay",           "input",          "ack_state",        "addr_hex",         "addr_dec",
      "uplink_rssi_dbm", "downlink_rssi_dbm", "last_seen_ms",     "last_cmd_counter", "poll_interval_s",
      "last_poll_tx_ms", "poll_state",     "temp_c",           "forget",           "poll_now",
  };

  for (const String &base : bases) {
    for (const char *leaf : leaves) {
      const String topic = base + "/" + leaf;
      mqtt_client_.publish(topic.c_str(), "", true);
    }
  }
}

bool MqttBridge::connectIfNeeded() {
  if (mqtt_client_.connected()) {
    return true;
  }

  const uint32_t now = millis();
  if ((now - last_reconnect_attempt_ms_) < kReconnectIntervalMs) {
    return false;
  }
  last_reconnect_attempt_ms_ = now;

  String clientId = String("LRS-") + chip_id_hex_;
  bool ok = false;
  if (!settings_) return false;
  if (settings_->mqtt_user.length() > 0) {
    ok = mqtt_client_.connect(clientId.c_str(), settings_->mqtt_user.c_str(), settings_->mqtt_password.c_str());
  } else {
    ok = mqtt_client_.connect(clientId.c_str());
  }

  if (!ok) {
    {
      lrslog::event("mqtt_connect_failed", 0, 0, 0);
    }
    return false;
  }

  mqtt_client_.subscribe(relay_topic_);
  mqtt_client_.subscribe(control_topic_);
  char topic[kMqttTopicBufBytes];
  if (buildPeerTopic(topic, sizeof(topic), "+", "poll_interval_s")) mqtt_client_.subscribe(topic);
  if (buildPeerTopic(topic, sizeof(topic), "+", "poll_now")) mqtt_client_.subscribe(topic);
  if (buildPeerTopic(topic, sizeof(topic), "+", "forget")) mqtt_client_.subscribe(topic);

  {
    lrslog::event("mqtt_connected", 0, 0, 0);
  }
  publishDiscovery();
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
    if (buildLocalTopic(topic, sizeof(topic), "type")) publishRetained(topic, runtime_.role_tx ? "tx" : "rx");

    char addrHex[3];
    snprintf(addrHex, sizeof(addrHex), "%02X", runtime_.local_address);
    char addrHexPrefixed[5];
    snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
    if (buildLocalTopic(topic, sizeof(topic), "addr")) publishRetained(topic, addrHexPrefixed);

    char tempBuf[16];
    if (sm_->localTemperatureValid()) {
      dtostrf(sm_->localTemperatureC(), 0, 1, tempBuf);
      if (buildLocalTopic(topic, sizeof(topic), "temp_c")) publishRetained(topic, tempBuf);
    } else {
      if (buildLocalTopic(topic, sizeof(topic), "temp_c")) publishRetained(topic, "");
    }

    if (sm_->remoteTemperatureValid()) {
      dtostrf(sm_->remoteTemperatureC(), 0, 1, tempBuf);
      if (buildLocalTopic(topic, sizeof(topic), "remote_temp_c")) publishRetained(topic, tempBuf);
    } else {
      if (buildLocalTopic(topic, sizeof(topic), "remote_temp_c")) publishRetained(topic, "");
    }

    char updatedMs[16];
    snprintf(updatedMs, sizeof(updatedMs), "%lu", static_cast<unsigned long>(millis()));
    if (buildLocalTopic(topic, sizeof(topic), "last_updated")) publishRetained(topic, updatedMs);
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
      const uint8_t addr = node.address;
      if (!runtime_.tx_mqtt_remote_polling_enabled) {
        const bool changed = !peer_published_once_[addr] || peer_last_seen_published_[addr] != node.last_seen_ms ||
                             peer_last_cmd_published_[addr] != node.last_cmd_counter;
        if (!changed) {
          continue;
        }
      }

      char addrHex[3];
      snprintf(addrHex, sizeof(addrHex), "%02X", node.address);
      char addrHexPrefixed[5];
      snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
      char addrDec[4];
      snprintf(addrDec, sizeof(addrDec), "%u", static_cast<unsigned>(node.address));

      auto publishRemote = [&](const char *addrSeg) {
        char topic[kMqttTopicBufBytes];
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "relay")) publishRetainedTopic(topic, node.relay_state ? "1" : "0");
        const uint8_t inputValue = node.input_state ? 1 : 0;
        if (!peer_input_published_[addr] || peer_input_value_[addr] != inputValue) {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "input")) publishRetainedTopic(topic, inputValue ? "1" : "0");
          peer_input_published_[addr] = true;
          peer_input_value_[addr] = inputValue;
        }
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "ack_state")) publishRetainedTopic(topic, peerAckStateText(node.ack_state));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "addr_hex")) publishRetainedTopic(topic, addrHex);
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "addr_dec")) publishRetainedTopic(topic, addrDec);

        char numBuf[24];
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
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_cmd_counter));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_cmd_counter")) publishRetainedTopic(topic, numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.poll_interval_ms / 1000U));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "poll_interval_s")) publishRetainedTopic(topic, numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_poll_tx_ms));
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "last_poll_tx_ms")) publishRetainedTopic(topic, numBuf);
        if (buildPeerTopic(topic, sizeof(topic), addrSeg, "poll_state")) publishRetainedTopic(topic, node.poll_pending ? "pending" : "idle");
        if (node.temp_valid) {
          dtostrf(static_cast<float>(node.temp_c), 0, 1, numBuf);
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "temp_c")) publishRetainedTopic(topic, numBuf);
        } else {
          if (buildPeerTopic(topic, sizeof(topic), addrSeg, "temp_c")) publishRetainedTopic(topic, "");
        }
      };

      publishRemote(addrHexPrefixed);
      peer_last_seen_published_[addr] = node.last_seen_ms;
      peer_last_cmd_published_[addr] = node.last_cmd_counter;
      peer_published_once_[addr] = true;
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

  DynamicJsonDocument doc(512);
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
  doc["lan_mdns"] = settings_->lan_hostname + ".local";
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
