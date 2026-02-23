#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "config_store.h"

class NodeStateMachine;
class LogBuffer;

class MqttBridge {
 public:
  bool begin(const Settings &cfg, const String &chipIdHex, NodeStateMachine *sm, LogBuffer *logs);
  void applyConfig(const Settings &cfg, const String &chipIdHex);
  void tick(bool wifiConnected);

 private:
  WiFiClient wifi_client_;
  PubSubClient mqtt_client_{wifi_client_};

  Settings cfg_{};
  String chip_id_hex_;
  String host_name_;
  String topic_base_;
  String relay_topic_;
  String input_topic_;
  String dry_contact_topic_;
  String temp_topic_;
  String remote_temp_topic_;
  String node_topic_;
  String addr_topic_;
  String control_topic_;
  String last_updated_topic_;
  String discovery_topic_;

  NodeStateMachine *sm_ = nullptr;
  LogBuffer *logs_ = nullptr;

  uint32_t last_reconnect_attempt_ms_ = 0;
  uint32_t last_publish_ms_ = 0;
  uint32_t last_discovery_publish_ms_ = 0;
  uint32_t peer_last_seen_published_[256]{};
  uint32_t peer_last_cmd_published_[256]{};
  bool peer_published_once_[256]{};
  bool peer_input_published_[256]{};
  uint8_t peer_input_value_[256]{};
  bool status_publish_in_progress_ = false;
  bool status_publish_locals_done_ = false;
  size_t status_publish_peer_index_ = 0;

  static MqttBridge *instance_;
  static void staticCallback(char *topic, uint8_t *payload, unsigned int length);

  void rebuildTopics();
  void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void clearPeerRetainedTopics(uint8_t addr);
  bool connectIfNeeded();
  void publishStatus();
  void publishDiscovery();
};
