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

  static MqttBridge *instance_;
  static void staticCallback(char *topic, uint8_t *payload, unsigned int length);

  void rebuildTopics();
  void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  bool connectIfNeeded();
  void publishStatus();
  void publishDiscovery();
};
