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
  bool ntp_started_ = false;
  bool ntp_time_valid_ = false;
  uint32_t ntp_last_check_ms_ = 0;
  uint32_t ntp_last_sync_ms_ = 0;
  uint32_t ntp_last_epoch_s_ = 0;
  String active_mdns_hostname_;
  bool mdns_suspended_for_provisioning_ = false;
  bool mdns_suspended_for_low_heap_ = false;
  uint32_t startup_trace_until_ms_ = 0;
  uint32_t startup_trace_next_breadcrumb_ms_ = 0;
  bool startup_defer_logged_ = false;
  uint32_t slow_phase_last_log_ms_ = 0;
  uint16_t slow_phase_suppressed_count_ = 0;
  DNSServer dns_;

  void startNetworking();
  void updateNetworking();
  void ensureApEnabled();
  void maybeDisableAp();
  void refreshCaptiveDns();
  void beginStaConnect();
  void startNtpClient();
  void tickTimeSync();
  void applyUpdatedConfig(bool restartNetwork, bool restartOtaAuth);
  void startOta();
  void refreshMdns();
  String normalizeHostname(const String &input) const;
};
