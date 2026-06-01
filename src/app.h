#pragma once

#include "config_store.h"
#include "logger.h"
#include "mqtt_bridge.h"
#include "radio_protocol.h"
#include "sensor_manager.h"
#include "serial_admin.h"
#include "state_machine.h"

enum class PowerSaveRuntimeState {
  FullPower,
  ArmedAwake,
  Sleeping
};

enum class PowerSaveActivitySource {
  SerialAdminInput,
  UdpMirrorEnable,
  OtaTraffic,
  ConfigWrite,
  LoRaMaintCommand
};

class App {
 public:
  void begin();
  void tick();
  bool powerSaveActive() const { return power_save_state_ == PowerSaveRuntimeState::Sleeping; }
  PowerSaveRuntimeState powerSaveState() const { return power_save_state_; }
  void markPowerSaveActivity(PowerSaveActivitySource source);

 private:
  ConfigStore config_;
  MqttBridge mqtt_;
  RadioProtocol radio_;
  SensorManager sensors_;
  NodeStateMachine sm_;
  SerialAdmin serial_admin_;

  bool wifi_sta_connecting_ = false;
  bool wifi_sta_scanning_ = false;
  uint32_t wifi_sta_scan_started_ms_ = 0;
  uint32_t wifi_sta_connect_attempt_started_ms_ = 0;
  uint32_t wifi_sta_started_ms_ = 0;
  uint32_t wifi_sta_retry_ms_ = 0;
  bool sta_connected_ = false;
  uint32_t sta_connected_since_ms_ = 0;
  bool ap_enabled_ = false;
  bool ntp_started_ = false;
  bool ntp_time_valid_ = false;
  uint32_t ntp_last_check_ms_ = 0;
  uint32_t ntp_last_sync_ms_ = 0;
  uint32_t ntp_last_epoch_s_ = 0;
  bool ota_enabled_ = false;
  uint32_t startup_trace_until_ms_ = 0;
  uint32_t startup_trace_next_breadcrumb_ms_ = 0;
  bool startup_defer_logged_ = false;
  uint32_t slow_phase_last_log_ms_ = 0;
  uint16_t slow_phase_suppressed_count_ = 0;
  uint32_t sta_reconnect_heap_block_log_ms_ = 0;
  uint8_t sta_connect_consecutive_failures_ = 0;
  uint8_t sta_stack_reset_count_ = 0;
  bool wifi_stack_disabled_ = false;
  FixedSettingString<32> cached_sta_hostname_;
  uint8_t sta_target_bssid_[6]{};
  int32_t sta_target_channel_ = 0;
  int32_t sta_last_sdk_status_ = 0;
  uint32_t sta_last_connect_time_ms_ = 0;

  void startNetworking();
  void updateNetworking();
  void ensureApEnabled();
  void maybeDisableAp();
  void beginStaConnect();
  void startStaScan();
  void finishStaScan(int scanCount);
  void failStaConnectAttempt(const char *reason, wl_status_t status);
  void resetWifiStaAttempt();
  void applyWifiRuntimeSettings();
  void stopWifiForAdminDisable();
  bool shouldEnableSoftAp() const;
  WiFiPhyMode_t configuredWifiPhyMode() const;
  const char *configuredWifiPhyModeText() const;
  void startNtpClient();
  void tickTimeSync();
  void applyUpdatedConfig(bool restartNetwork, bool restartOtaAuth);
  void startOta();
  void refreshCachedStaHostname();
  void normalizeHostname(const char *input, char *out, size_t outSize) const;
  void advanceStaReconnectFibonacci();
  void resetStaReconnectFibonacci();

  uint32_t sta_reconnect_fib_prev_s_ = 0;
  uint32_t sta_reconnect_fib_curr_s_ = 1;
  
  PowerSaveRuntimeState power_save_state_ = PowerSaveRuntimeState::FullPower;
  uint32_t power_save_last_activity_ms_ = 0;

  void tickPowerSave();
  void enterPowerSave(const char *reason);
  void exitPowerSave(const char *reason);
  void stopWifiForPowerSave();
};
