#pragma once

#include <Arduino.h>

struct Settings {
  uint16_t version;
  bool provisioned;

  bool role_tx;
  uint8_t local_address;
  uint8_t remote_address;

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
  bool tx_input_lora_control_enabled;

  String wifi_sta_ssid;
  String wifi_sta_password;
  String lan_hostname;
  bool ap_always_on;

  bool mqtt_enabled;
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

  String chipIdHex() const;
  String defaultLanHostnameForRole(bool roleTx) const;
  String apSsid() const;
  String apPassword() const;

 private:
  Settings cfg_{};

  void setDefaults();
  void ensureProvisionedDefaults();
};
