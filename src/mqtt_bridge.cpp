#include "mqtt_bridge.h"

#include <ArduinoJson.h>

#include "build_info.h"
#include "log_buffer.h"
#include "state_machine.h"

namespace {
constexpr uint32_t kReconnectIntervalMs = 30000;
constexpr uint32_t kPublishIntervalMs = 10000;
constexpr uint32_t kDiscoveryPublishIntervalMs = 60000;
}

MqttBridge *MqttBridge::instance_ = nullptr;

bool MqttBridge::begin(const Settings &cfg, const String &chipIdHex, NodeStateMachine *sm, LogBuffer *logs) {
  cfg_ = cfg;
  chip_id_hex_ = chipIdHex;
  sm_ = sm;
  logs_ = logs;

  rebuildTopics();

  instance_ = this;
  // Keep connection attempts short so MQTT outages do not stall control loop timing.
  mqtt_client_.setSocketTimeout(1);
  mqtt_client_.setBufferSize(768);
  mqtt_client_.setCallback(MqttBridge::staticCallback);
  return true;
}

void MqttBridge::applyConfig(const Settings &cfg, const String &chipIdHex) {
  cfg_ = cfg;
  chip_id_hex_ = chipIdHex;
  rebuildTopics();

  if (mqtt_client_.connected()) {
    mqtt_client_.disconnect();
  }
}

void MqttBridge::tick(bool wifiConnected) {
  if (!cfg_.mqtt_enabled || cfg_.mqtt_host.length() == 0) {
    return;
  }

  if (!wifiConnected) {
    return;
  }

  if (!connectIfNeeded()) {
    return;
  }

  mqtt_client_.loop();

  const uint32_t now = millis();
  if ((now - last_publish_ms_) >= kPublishIntervalMs) {
    last_publish_ms_ = now;
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

    if (logs_) {
      logs_->add("mqtt_relay_topic", 0, 0, targetRelay ? 1 : 0);
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
      if (logs_) {
        logs_->add("mqtt_control_json_err", 0, 0, 0);
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

    sm_->mqttSendRemoteRelay(addr, relay ? 1 : 0);
    if (logs_) {
      logs_->add("mqtt_control_topic", 0, 0, relay ? 1 : 0);
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
    if (logs_) {
      logs_->add("mqtt_connect_failed", 0, 0, 0);
    }
    return false;
  }

  mqtt_client_.subscribe(relay_topic_.c_str());
  mqtt_client_.subscribe(control_topic_.c_str());

  if (logs_) {
    logs_->add("mqtt_connected", 0, 0, 0);
  }
  publishDiscovery();
  return true;
}

void MqttBridge::publishStatus() {
  if (!mqtt_client_.connected() || sm_ == nullptr) {
    return;
  }

  const bool localInput = sm_->localDryContactState() != 0;
  mqtt_client_.publish(input_topic_.c_str(), localInput ? "1" : "0", true);
  mqtt_client_.publish(dry_contact_topic_.c_str(), localInput ? "1" : "0", true);
  mqtt_client_.publish(relay_topic_.c_str(), sm_->relayState() ? "1" : "0", true);
  mqtt_client_.publish(node_topic_.c_str(), cfg_.role_tx ? "tx" : "rx", true);

  char addrHex[5];
  snprintf(addrHex, sizeof(addrHex), "%02X", cfg_.local_address);
  mqtt_client_.publish(addr_topic_.c_str(), addrHex, true);

  char tempBuf[16];
  if (sm_->localTemperatureValid()) {
    dtostrf(sm_->localTemperatureC(), 0, 1, tempBuf);
    mqtt_client_.publish(temp_topic_.c_str(), tempBuf, true);
  } else {
    mqtt_client_.publish(temp_topic_.c_str(), "n/a", true);
  }

  if (sm_->remoteTemperatureValid()) {
    dtostrf(sm_->remoteTemperatureC(), 0, 1, tempBuf);
    mqtt_client_.publish(remote_temp_topic_.c_str(), tempBuf, true);
  } else {
    mqtt_client_.publish(remote_temp_topic_.c_str(), "n/a", true);
  }

  char updatedMs[16];
  snprintf(updatedMs, sizeof(updatedMs), "%lu", static_cast<unsigned long>(millis()));
  mqtt_client_.publish(last_updated_topic_.c_str(), updatedMs, true);

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
    } else if (logs_) {
      logs_->add("mqtt_discovery_publish_failed", 0, 0, 0);
    }
  }
}
