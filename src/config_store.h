#pragma once

#include <Arduino.h>

#include "fixed_setting_string.h"

struct Settings {
  static constexpr uint8_t kAddressListCap = 12;
  uint16_t schema_version;
  bool commissioned;
  FixedSettingString<16> mode;
  FixedSettingString<16> role;

  bool role_tx;
  uint8_t local_address;
  // Remote-only: the gateway/controller that receives pushes and controls it.
  uint8_t controller_address;
  uint8_t allowed_controller_count;
  uint8_t allowed_controller_addresses[kAddressListCap];
  uint8_t known_peer_count;
  uint8_t known_peer_addresses[kAddressListCap];
  uint32_t known_peer_chip_ids[kAddressListCap];

  long lora_frequency_hz;
  uint8_t lora_tx_power;
  uint8_t lora_spreading_factor;
  long lora_bandwidth_hz;
  uint8_t lora_coding_rate;

  uint32_t heartbeat_ms;
  bool heartbeat_enabled;
  uint32_t ack_timeout_ms;
  bool remote_refresh_enabled;
  uint32_t remote_refresh_cycle_ms;
  bool input_control_paired_lora_enabled;
  uint32_t tx_command_retry_timeout_ms;
  FixedSettingString<16> rx_failsafe_mode;
  uint32_t rx_failsafe_timeout_ms;

  FixedSettingString<33> wifi_sta_ssid;
  FixedSettingString<65> wifi_sta_password;
  FixedSettingString<65> lan_hostname;
  bool ap_always_on;
  float wifi_tx_power_dbm;
  bool wifi_sleep_enabled;
  bool wifi_static_ip_enabled;
  FixedSettingString<16> wifi_static_ip;
  FixedSettingString<16> wifi_static_gateway;
  FixedSettingString<16> wifi_static_subnet;
  uint8_t wifi_channel_override;
  FixedSettingString<24> wifi_ap_fallback_policy;
  bool wifi_admin_enabled;
  bool power_save_listen_only;

  bool mqtt_client_enabled;
  bool mqtt_control_enabled;
  FixedSettingString<80> mqtt_controller_addresses;
  FixedSettingString<65> mqtt_host;
  uint16_t mqtt_port;
  FixedSettingString<65> mqtt_user;
  FixedSettingString<65> mqtt_password;
  FixedSettingString<65> mqtt_topic_root;

  bool sensor_temp_enabled;
  uint8_t sensor_temp_pin;
  uint16_t sensor_temp_interval_s;
  bool sensor_tank_enabled;
  uint16_t sensor_tank_range_mm;
  uint16_t sensor_tank_vref_mv;
  uint16_t sensor_tank_sense_ohms;
  uint16_t sensor_tank_interval_s;

  FixedSettingString<65> fleet_passphrase;
  bool fleet_setup_prompt_dismissed;
  FixedSettingString<33> admin_password;
};

class ConfigStore {
 public:
  bool begin();
  Settings &settings();
  const Settings &settings() const;
  bool save();
  bool removeKnownPeer(uint8_t address, uint32_t &chipId, bool &removed);
  bool factoryReset(bool keepSharedFleetKey, bool keepWifiCredentials = false);
  bool schedulePostOtaFactoryReset(bool keepSharedFleetKey = false, bool keepWifiCredentials = false);
  bool consumePostOtaFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials);
  bool writePostOtaWifiFastMarker();
  bool consumePostOtaWifiFastMarker();

  const String &chipIdHex() const;
  String defaultLanHostname() const;
  String apSsid() const;
  String apPassword() const;

 private:
  Settings cfg_{};
  mutable String chip_id_hex_cache_;

  void setDefaults();
  void ensureProvisionedDefaults();
};
