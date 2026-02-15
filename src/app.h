#pragma once

#include <DNSServer.h>

#include "config_store.h"
#include "log_buffer.h"
#include "mqtt_bridge.h"
#include "radio_protocol.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "web_console.h"

class App {
 public:
  void begin();
  void tick();

 private:
  ConfigStore config_;
  LogBuffer logs_;
  MqttBridge mqtt_;
  RadioProtocol radio_;
  SensorManager sensors_;
  NodeStateMachine sm_;
  WebConsole web_;

  bool wifi_sta_connecting_ = false;
  uint32_t wifi_sta_started_ms_ = 0;
  uint32_t wifi_sta_retry_ms_ = 0;
  bool sta_connected_ = false;
  uint32_t sta_connected_since_ms_ = 0;
  bool ap_enabled_ = false;
  bool dns_running_ = false;
  String active_mdns_hostname_;
  DNSServer dns_;

  void startNetworking();
  void updateNetworking();
  void ensureApEnabled();
  void maybeDisableAp();
  void refreshCaptiveDns();
  void beginStaConnect();
  void applyUpdatedConfig(bool restartNetwork, bool restartOtaAuth);
  void startOta();
  void refreshMdns();
  String normalizeHostname(const String &input) const;
};
