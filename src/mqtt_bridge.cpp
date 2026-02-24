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
  cfg_ = cfg;
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
  cfg_ = cfg;
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
  if (!cfg_.mqtt_enabled || cfg_.mqtt_host.length() == 0) {
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

void MqttBridge::rebuildTopics() {
  host_name_ = String("lrs-") + chip_id_hex_;
  topic_base_ = cfg_.mqtt_topic_root + "/" + host_name_;
  relay_topic_ = topic_base_ + "/relay";
  input_topic_ = topic_base_ + "/input";
  dry_contact_topic_ = topic_base_ + "/dry_contact";
  temp_topic_ = topic_base_ + "/temp_c";
  remote_temp_topic_ = topic_base_ + "/remote_temp_c";
  node_topic_ = topic_base_ + "/type";
  addr_topic_ = topic_base_ + "/addr";
  control_topic_ = topic_base_ + "/control";
  last_updated_topic_ = topic_base_ + "/last_updated";
  discovery_topic_ = cfg_.mqtt_topic_root + "/discovery/" + host_name_;

  mqtt_client_.setServer(cfg_.mqtt_host.c_str(), cfg_.mqtt_port);
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
    if (!cfg_.role_tx || sm_ == nullptr) {
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
      addr = static_cast<uint8_t>(strtoul(addrStr, nullptr, 16));
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

  if (cfg_.role_tx && sm_ != nullptr) {
    const String remotePrefix = topic_base_ + "/peer/";
    if (!topicStr.startsWith(remotePrefix)) {
      return;
    }

    const String suffix = topicStr.substring(remotePrefix.length());
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
      topic_base_ + "/peer/" + String(addrHexPrefixed),
      topic_base_ + "/peer/" + String(addrHex),
      topic_base_ + "/peer/" + String(addrDec),
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
  if (cfg_.mqtt_user.length() > 0) {
    ok = mqtt_client_.connect(clientId.c_str(), cfg_.mqtt_user.c_str(), cfg_.mqtt_password.c_str());
  } else {
    ok = mqtt_client_.connect(clientId.c_str());
  }

  if (!ok) {
    {
      lrslog::event("mqtt_connect_failed", 0, 0, 0);
    }
    return false;
  }

  mqtt_client_.subscribe(relay_topic_.c_str());
  mqtt_client_.subscribe(control_topic_.c_str());
  mqtt_client_.subscribe((topic_base_ + "/peer/+/poll_interval_s").c_str());
  mqtt_client_.subscribe((topic_base_ + "/peer/+/poll_now").c_str());
  mqtt_client_.subscribe((topic_base_ + "/peer/+/forget").c_str());

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
  auto publishRetainedString = [&](const String &topic, const char *payload) {
    mqtt_client_.publish(topic.c_str(), payload, true);
    maybeYield();
  };

  if (!status_publish_locals_done_) {
    const bool localInput = sm_->localDryContactState() != 0;
    publishRetained(input_topic_.c_str(), localInput ? "1" : "0");
    publishRetained(dry_contact_topic_.c_str(), localInput ? "1" : "0");
    publishRetained(relay_topic_.c_str(), sm_->relayState() ? "1" : "0");
    publishRetained(node_topic_.c_str(), cfg_.role_tx ? "tx" : "rx");

    char addrHex[3];
    snprintf(addrHex, sizeof(addrHex), "%02X", cfg_.local_address);
    char addrHexPrefixed[5];
    snprintf(addrHexPrefixed, sizeof(addrHexPrefixed), "0x%s", addrHex);
    publishRetained(addr_topic_.c_str(), addrHexPrefixed);

    char tempBuf[16];
    if (sm_->localTemperatureValid()) {
      dtostrf(sm_->localTemperatureC(), 0, 1, tempBuf);
      publishRetained(temp_topic_.c_str(), tempBuf);
    } else {
      publishRetained(temp_topic_.c_str(), "");
    }

    if (sm_->remoteTemperatureValid()) {
      dtostrf(sm_->remoteTemperatureC(), 0, 1, tempBuf);
      publishRetained(remote_temp_topic_.c_str(), tempBuf);
    } else {
      publishRetained(remote_temp_topic_.c_str(), "");
    }

    char updatedMs[16];
    snprintf(updatedMs, sizeof(updatedMs), "%lu", static_cast<unsigned long>(millis()));
    publishRetained(last_updated_topic_.c_str(), updatedMs);
    status_publish_locals_done_ = true;
    if (cfg_.role_tx) {
      return;
    }
  }

  if (cfg_.role_tx) {
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
      if (!cfg_.tx_mqtt_remote_polling_enabled) {
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

      const String baseHex = topic_base_ + "/peer/" + String(addrHexPrefixed);

      auto publishRemote = [&](const String &base) {
        publishRetainedString(base + "/relay", node.relay_state ? "1" : "0");
        const uint8_t inputValue = node.input_state ? 1 : 0;
        if (!peer_input_published_[addr] || peer_input_value_[addr] != inputValue) {
          publishRetainedString(base + "/input", inputValue ? "1" : "0");
          peer_input_published_[addr] = true;
          peer_input_value_[addr] = inputValue;
        }
        publishRetainedString(base + "/ack_state", peerAckStateText(node.ack_state));
        publishRetainedString(base + "/addr_hex", addrHex);
        publishRetainedString(base + "/addr_dec", addrDec);

        char numBuf[24];
        snprintf(numBuf, sizeof(numBuf), "%d", node.uplink_rssi);
        publishRetainedString(base + "/uplink_rssi_dbm", numBuf);
        if (node.downlink_rssi_valid) {
          snprintf(numBuf, sizeof(numBuf), "%d", node.downlink_rssi);
          publishRetainedString(base + "/downlink_rssi_dbm", numBuf);
        } else {
          publishRetainedString(base + "/downlink_rssi_dbm", "");
        }
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_seen_ms));
        publishRetainedString(base + "/last_seen_ms", numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_cmd_counter));
        publishRetainedString(base + "/last_cmd_counter", numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.poll_interval_ms / 1000U));
        publishRetainedString(base + "/poll_interval_s", numBuf);
        snprintf(numBuf, sizeof(numBuf), "%lu", static_cast<unsigned long>(node.last_poll_tx_ms));
        publishRetainedString(base + "/last_poll_tx_ms", numBuf);
        publishRetainedString(base + "/poll_state", node.poll_pending ? "pending" : "idle");
        if (node.temp_valid) {
          dtostrf(static_cast<float>(node.temp_c), 0, 1, numBuf);
          publishRetainedString(base + "/temp_c", numBuf);
        } else {
          publishRetainedString(base + "/temp_c", "");
        }
      };

      publishRemote(baseHex);
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
  doc["serial"] = cfg_.factory_serial;
  doc["chip_id"] = chip_id_hex_;
  doc["role"] = cfg_.role_tx ? "tx" : "rx";
  doc["addr"] = cfg_.local_address;
  doc["remote_addr"] = cfg_.remote_address;
  doc["mac"] = WiFi.macAddress();
  doc["sta_ip"] = WiFi.localIP().toString();
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["sta_ssid"] = cfg_.wifi_sta_ssid;
  doc["lan_mdns"] = cfg_.lan_hostname + ".local";
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
    if (mqtt_client_.publish(discovery_topic_.c_str(), payload, true)) {
      last_discovery_publish_ms_ = millis();
    } else {
      lrslog::event("mqtt_discovery_publish_failed", 0, 0, 0);
    }
  }
}
