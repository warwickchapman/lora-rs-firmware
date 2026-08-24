#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "config_store.h"
#include "state_machine.h"

class AdminExecutor;

class MqttBridge {
 public:
  bool begin(ConfigStore *config, const String &chipIdHex, NodeStateMachine *sm, AdminExecutor *executor);
  void applyConfig(const Settings &cfg, const String &chipIdHex);
  void tick(bool wifiConnected);
  bool connected();
  void disconnect();
  static bool clearPeerRetained(uint8_t addr, uint32_t chipId = 0);
  static void clearAllConfiguredPeerRetainedTopics(const Settings &cfg);
  static bool publishCmdResult(uint8_t addr, uint32_t commandId, const char *outcome);

 private:
  WiFiClient wifi_client_;
  PubSubClient mqtt_client_{wifi_client_};

  struct RuntimeCfg {
    bool mqtt_client_enabled = false;
    bool mqtt_control_enabled = false;
    bool role_tx = false;
    uint8_t local_address = 0;
    uint8_t controller_address = 0;
    uint16_t mqtt_port = 1883;
    bool remote_refresh_enabled = false;
  };

  ConfigStore *config_ = nullptr;
  const Settings *settings_ = nullptr;
  RuntimeCfg runtime_{};
  String chip_id_hex_;
  char host_name_[24]{};
  char topic_base_[128]{};

  // Cached MQTT settings to detect changes since we point to a mutable ConfigStore settings reference
  FixedSettingString<65> cached_mqtt_host_;
  FixedSettingString<65> cached_mqtt_user_;
  FixedSettingString<65> cached_mqtt_password_;
  FixedSettingString<65> cached_mqtt_topic_root_;

  NodeStateMachine *sm_ = nullptr;
  AdminExecutor *executor_ = nullptr;

  static constexpr size_t kPeerPublishCacheSize = LRS_MAX_PEERS;
  struct PeerPublishCacheEntry {
    bool in_use = false;
    uint8_t addr = 0;
    uint32_t chip_id = 0;
    uint32_t last_seen_ms = 0;
    uint32_t last_cmd_counter = 0;
    bool published_once = false;
    bool input_published = false;
    uint8_t input_value = 0;
  };

  uint32_t last_reconnect_attempt_ms_ = 0;
  uint32_t fib_prev_s_ = 0;
  uint32_t fib_curr_s_ = 1;
  static constexpr uint32_t kFibMaxDelayS = 300;

  uint32_t last_publish_ms_ = 0;
  uint32_t last_discovery_publish_ms_ = 0;
  PeerPublishCacheEntry peer_publish_cache_[kPeerPublishCacheSize]{};
  bool status_publish_in_progress_ = false;
  bool status_publish_locals_done_ = false;
  size_t status_publish_peer_index_ = 0;

  static MqttBridge *instance_;
  static void staticCallback(char *topic, uint8_t *payload, unsigned int length);

  void refreshRuntimeCfg(const Settings &cfg);
  void rebuildTopics();
  bool buildLocalTopic(char *out, size_t outLen, const char *leaf) const;
  bool buildPeerTopic(char *out, size_t outLen, const char *addrSegment, const char *leaf) const;
  void mqttCallback(char *topic, uint8_t *payload, unsigned int length);
  void advanceFibonacci();
  void resetFibonacci();
  void resetPeerPublishCache();
  PeerPublishCacheEntry *findPeerPublishCache(uint8_t addr);
  PeerPublishCacheEntry *upsertPeerPublishCache(uint8_t addr);
  void clearPeerPublishCache(uint8_t addr);
  bool clearPeerRetainedTopics(uint8_t addr, uint32_t passedChipId = 0);
  bool connectIfNeeded();
  void publishLocalStatus();
  bool publishPeerStatus(size_t peerIndex, uint8_t &publishOpsSinceYield);
  void publishStatus();
  void publishDiscovery();
  void publishOtaStatus(const char *status);
  void publishLocalConfig();

  bool config_dirty_ = false;
};
