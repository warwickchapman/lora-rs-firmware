#pragma once

#include <cstring>

#include "config_store.h"

struct SettingsBackup {
  String mode;
  String role;
  bool role_tx = false;
  uint8_t local_address = 0;
  uint8_t remote_address = 0;
  uint8_t paired_target_count = 0;
  uint8_t paired_target_addresses[Settings::kAddressListCap]{};
  uint8_t allowed_controller_count = 0;
  uint8_t allowed_controller_addresses[Settings::kAddressListCap]{};
  uint8_t known_peer_count = 0;
  uint8_t known_peer_addresses[Settings::kAddressListCap]{};
  long lora_frequency_hz = 0;
  uint8_t lora_tx_power = 0;
  uint8_t lora_spreading_factor = 0;
  long lora_bandwidth_hz = 0;
  uint8_t lora_coding_rate = 0;
  uint32_t heartbeat_ms = 0;
  uint32_t ack_timeout_ms = 0;
  uint32_t mqtt_remote_retry_timeout_ms = 0;
  bool tx_mqtt_remote_polling_enabled = false;
  uint32_t tx_mqtt_remote_default_poll_interval_ms = 0;
  bool rx_push_on_change_enabled = false;
  uint32_t rx_push_min_interval_ms = 0;
  bool input_control_paired_lora_enabled = false;
  uint32_t tx_command_retry_timeout_ms = 0;
  String rx_failsafe_mode;
  uint32_t rx_failsafe_timeout_ms = 0;
  String wifi_sta_ssid;
  String wifi_sta_password;
  String lan_hostname;
  String fleet_passphrase;
  bool fleet_setup_prompt_dismissed = false;
  String admin_password;
  bool ap_always_on = false;
  bool mqtt_client_enabled = false;
  bool mqtt_control_enabled = false;
  String mqtt_controller_addresses;
  String mqtt_host;
  uint16_t mqtt_port = 0;
  String mqtt_user;
  String mqtt_password;
  String mqtt_topic_root;
  bool sensor_temp_enabled = false;
  bool commissioned = false;
  String audit_last_saved_by;
  uint32_t audit_last_saved_ms = 0;
};

inline void captureSettingsBackup(const Settings &src, SettingsBackup &dst) {
  dst.mode = src.mode;
  dst.role = src.role;
  dst.role_tx = src.role_tx;
  dst.local_address = src.local_address;
  dst.remote_address = src.remote_address;
  dst.paired_target_count = src.paired_target_count;
  memcpy(dst.paired_target_addresses, src.paired_target_addresses,
         sizeof(dst.paired_target_addresses));
  dst.allowed_controller_count = src.allowed_controller_count;
  memcpy(dst.allowed_controller_addresses, src.allowed_controller_addresses,
         sizeof(dst.allowed_controller_addresses));
  dst.known_peer_count = src.known_peer_count;
  memcpy(dst.known_peer_addresses, src.known_peer_addresses,
         sizeof(dst.known_peer_addresses));
  dst.lora_frequency_hz = src.lora_frequency_hz;
  dst.lora_tx_power = src.lora_tx_power;
  dst.lora_spreading_factor = src.lora_spreading_factor;
  dst.lora_bandwidth_hz = src.lora_bandwidth_hz;
  dst.lora_coding_rate = src.lora_coding_rate;
  dst.heartbeat_ms = src.heartbeat_ms;
  dst.ack_timeout_ms = src.ack_timeout_ms;
  dst.mqtt_remote_retry_timeout_ms = src.mqtt_remote_retry_timeout_ms;
  dst.tx_mqtt_remote_polling_enabled = src.tx_mqtt_remote_polling_enabled;
  dst.tx_mqtt_remote_default_poll_interval_ms =
      src.tx_mqtt_remote_default_poll_interval_ms;
  dst.rx_push_on_change_enabled = src.rx_push_on_change_enabled;
  dst.rx_push_min_interval_ms = src.rx_push_min_interval_ms;
  dst.input_control_paired_lora_enabled = src.input_control_paired_lora_enabled;
  dst.tx_command_retry_timeout_ms = src.tx_command_retry_timeout_ms;
  dst.rx_failsafe_mode = src.rx_failsafe_mode;
  dst.rx_failsafe_timeout_ms = src.rx_failsafe_timeout_ms;
  dst.wifi_sta_ssid = src.wifi_sta_ssid;
  dst.wifi_sta_password = src.wifi_sta_password;
  dst.lan_hostname = src.lan_hostname;
  dst.fleet_passphrase = src.fleet_passphrase;
  dst.fleet_setup_prompt_dismissed = src.fleet_setup_prompt_dismissed;
  dst.admin_password = src.admin_password;
  dst.ap_always_on = src.ap_always_on;
  dst.mqtt_client_enabled = src.mqtt_client_enabled;
  dst.mqtt_control_enabled = src.mqtt_control_enabled;
  dst.mqtt_controller_addresses = src.mqtt_controller_addresses;
  dst.mqtt_host = src.mqtt_host;
  dst.mqtt_port = src.mqtt_port;
  dst.mqtt_user = src.mqtt_user;
  dst.mqtt_password = src.mqtt_password;
  dst.mqtt_topic_root = src.mqtt_topic_root;
  dst.sensor_temp_enabled = src.sensor_temp_enabled;
  dst.commissioned = src.commissioned;
  dst.audit_last_saved_by = src.audit_last_saved_by;
  dst.audit_last_saved_ms = src.audit_last_saved_ms;
}

inline void restoreSettingsBackup(const SettingsBackup &src, Settings &dst) {
  dst.mode = src.mode;
  dst.role = src.role;
  dst.role_tx = src.role_tx;
  dst.local_address = src.local_address;
  dst.remote_address = src.remote_address;
  dst.paired_target_count = src.paired_target_count;
  memcpy(dst.paired_target_addresses, src.paired_target_addresses,
         sizeof(dst.paired_target_addresses));
  dst.allowed_controller_count = src.allowed_controller_count;
  memcpy(dst.allowed_controller_addresses, src.allowed_controller_addresses,
         sizeof(dst.allowed_controller_addresses));
  dst.known_peer_count = src.known_peer_count;
  memcpy(dst.known_peer_addresses, src.known_peer_addresses,
         sizeof(dst.known_peer_addresses));
  dst.lora_frequency_hz = src.lora_frequency_hz;
  dst.lora_tx_power = src.lora_tx_power;
  dst.lora_spreading_factor = src.lora_spreading_factor;
  dst.lora_bandwidth_hz = src.lora_bandwidth_hz;
  dst.lora_coding_rate = src.lora_coding_rate;
  dst.heartbeat_ms = src.heartbeat_ms;
  dst.ack_timeout_ms = src.ack_timeout_ms;
  dst.mqtt_remote_retry_timeout_ms = src.mqtt_remote_retry_timeout_ms;
  dst.tx_mqtt_remote_polling_enabled = src.tx_mqtt_remote_polling_enabled;
  dst.tx_mqtt_remote_default_poll_interval_ms =
      src.tx_mqtt_remote_default_poll_interval_ms;
  dst.rx_push_on_change_enabled = src.rx_push_on_change_enabled;
  dst.rx_push_min_interval_ms = src.rx_push_min_interval_ms;
  dst.input_control_paired_lora_enabled = src.input_control_paired_lora_enabled;
  dst.tx_command_retry_timeout_ms = src.tx_command_retry_timeout_ms;
  dst.rx_failsafe_mode = src.rx_failsafe_mode;
  dst.rx_failsafe_timeout_ms = src.rx_failsafe_timeout_ms;
  dst.wifi_sta_ssid = src.wifi_sta_ssid;
  dst.wifi_sta_password = src.wifi_sta_password;
  dst.lan_hostname = src.lan_hostname;
  dst.fleet_passphrase = src.fleet_passphrase;
  dst.fleet_setup_prompt_dismissed = src.fleet_setup_prompt_dismissed;
  dst.admin_password = src.admin_password;
  dst.ap_always_on = src.ap_always_on;
  dst.mqtt_client_enabled = src.mqtt_client_enabled;
  dst.mqtt_control_enabled = src.mqtt_control_enabled;
  dst.mqtt_controller_addresses = src.mqtt_controller_addresses;
  dst.mqtt_host = src.mqtt_host;
  dst.mqtt_port = src.mqtt_port;
  dst.mqtt_user = src.mqtt_user;
  dst.mqtt_password = src.mqtt_password;
  dst.mqtt_topic_root = src.mqtt_topic_root;
  dst.sensor_temp_enabled = src.sensor_temp_enabled;
  dst.commissioned = src.commissioned;
  dst.audit_last_saved_by = src.audit_last_saved_by;
  dst.audit_last_saved_ms = src.audit_last_saved_ms;
}
