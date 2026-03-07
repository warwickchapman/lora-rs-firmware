#pragma once

#include <Arduino.h>

struct Settings {
  static constexpr uint8_t kAddressListCap = 12;
  uint16_t schema_version;
  bool commissioned;
  String mode;
  String role;

  bool role_tx;
  uint8_t local_address;
  uint8_t remote_address;
  uint8_t paired_target_count;
  uint8_t paired_target_addresses[kAddressListCap];
  uint8_t allowed_controller_count;
  uint8_t allowed_controller_addresses[kAddressListCap];
  uint8_t known_peer_count;
  uint8_t known_peer_addresses[kAddressListCap];

  long lora_frequency_hz;
  uint8_t lora_tx_power;
  uint8_t lora_spreading_factor;
  long lora_bandwidth_hz;
  uint8_t lora_coding_rate;

  uint32_t heartbeat_ms;
  uint32_t ack_timeout_ms;
  uint32_t mqtt_remote_retry_timeout_ms;
  bool tx_mqtt_remote_polling_enabled;
  uint32_t tx_mqtt_remote_default_poll_interval_ms;
  bool rx_push_on_change_enabled;
  uint32_t rx_push_min_interval_ms;
  bool input_control_paired_lora_enabled;
  uint32_t tx_command_retry_timeout_ms;
  String rx_failsafe_mode;
  uint32_t rx_failsafe_timeout_ms;

  String wifi_sta_ssid;
  String wifi_sta_password;
  String lan_hostname;
  bool ap_always_on;

  bool mqtt_client_enabled;
  bool mqtt_control_enabled;
  String mqtt_controller_addresses;
  String mqtt_host;
  uint16_t mqtt_port;
  String mqtt_user;
  String mqtt_password;
  String mqtt_topic_root;

  bool sensor_temp_enabled;
  uint8_t sensor_temp_pin;
  uint16_t sensor_temp_interval_s;

  String fleet_passphrase;
  bool fleet_setup_prompt_dismissed;
  String admin_password;

  String factory_serial;
  String audit_last_saved_by;
  uint32_t audit_last_saved_ms;
  String audit_last_reboot_reason;
  uint32_t audit_last_reboot_ms;
  uint32_t audit_boot_count;
};

class ConfigStore {
 public:
  bool begin();
  Settings &settings();
  bool save();
  bool factoryReset(bool keepSharedFleetKey, bool keepWifiCredentials = false);
  bool schedulePostOtaFactoryReset(bool keepSharedFleetKey = false, bool keepWifiCredentials = false);
  bool consumePostOtaFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials);

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
