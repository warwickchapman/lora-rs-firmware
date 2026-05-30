#pragma once

#include <cstring>

#include "config_store.h"

struct SettingsBackup {
  FixedSettingString<16> mode;
  FixedSettingString<16> role;
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
  FixedSettingString<16> rx_failsafe_mode;
  uint32_t rx_failsafe_timeout_ms = 0;
  FixedSettingString<33> wifi_sta_ssid;
  FixedSettingString<65> wifi_sta_password;
  FixedSettingString<65> lan_hostname;
  FixedSettingString<16> wifi_phy_mode;
  float wifi_tx_power_dbm = 0.0f;
  bool wifi_sleep_enabled = false;
  bool wifi_static_ip_enabled = false;
  FixedSettingString<16> wifi_static_ip;
  FixedSettingString<16> wifi_static_gateway;
  FixedSettingString<16> wifi_static_subnet;
  uint8_t wifi_channel_override = 0;
  FixedSettingString<24> wifi_ap_fallback_policy;
  bool wifi_admin_enabled = true;
  FixedSettingString<65> fleet_passphrase;
  bool fleet_setup_prompt_dismissed = false;
  FixedSettingString<33> admin_password;
  bool ap_always_on = false;
  bool mqtt_client_enabled = false;
  bool mqtt_control_enabled = false;
  FixedSettingString<80> mqtt_controller_addresses;
  FixedSettingString<65> mqtt_host;
  uint16_t mqtt_port = 0;
  FixedSettingString<65> mqtt_user;
  FixedSettingString<65> mqtt_password;
  FixedSettingString<65> mqtt_topic_root;
  bool sensor_temp_enabled = false;
  uint8_t sensor_temp_pin = 0;
  uint16_t sensor_temp_interval_s = 10;
  bool sensor_tank_enabled = false;
  uint16_t sensor_tank_range_mm = 5000;
  uint16_t sensor_tank_vref_mv = 3553;
  uint16_t sensor_tank_sense_ohms = 120;
  uint16_t sensor_tank_interval_s = 5;
  bool commissioned = false;
};

inline void captureSettingsBackup(const Settings &src, SettingsBackup &dst) {
  dst.mode = src.mode.c_str();
  dst.role = src.role.c_str();
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
  dst.rx_failsafe_mode = src.rx_failsafe_mode.c_str();
  dst.rx_failsafe_timeout_ms = src.rx_failsafe_timeout_ms;
  dst.wifi_sta_ssid = src.wifi_sta_ssid.c_str();
  dst.wifi_sta_password = src.wifi_sta_password.c_str();
  dst.lan_hostname = src.lan_hostname.c_str();
  dst.wifi_phy_mode = src.wifi_phy_mode.c_str();
  dst.wifi_tx_power_dbm = src.wifi_tx_power_dbm;
  dst.wifi_sleep_enabled = src.wifi_sleep_enabled;
  dst.wifi_static_ip_enabled = src.wifi_static_ip_enabled;
  dst.wifi_static_ip = src.wifi_static_ip.c_str();
  dst.wifi_static_gateway = src.wifi_static_gateway.c_str();
  dst.wifi_static_subnet = src.wifi_static_subnet.c_str();
  dst.wifi_channel_override = src.wifi_channel_override;
  dst.wifi_ap_fallback_policy = src.wifi_ap_fallback_policy.c_str();
  dst.wifi_admin_enabled = src.wifi_admin_enabled;
  dst.fleet_passphrase = src.fleet_passphrase.c_str();
  dst.fleet_setup_prompt_dismissed = src.fleet_setup_prompt_dismissed;
  dst.admin_password = src.admin_password.c_str();
  dst.ap_always_on = src.ap_always_on;
  dst.mqtt_client_enabled = src.mqtt_client_enabled;
  dst.mqtt_control_enabled = src.mqtt_control_enabled;
  dst.mqtt_controller_addresses = src.mqtt_controller_addresses.c_str();
  dst.mqtt_host = src.mqtt_host.c_str();
  dst.mqtt_port = src.mqtt_port;
  dst.mqtt_user = src.mqtt_user.c_str();
  dst.mqtt_password = src.mqtt_password.c_str();
  dst.mqtt_topic_root = src.mqtt_topic_root.c_str();
  dst.sensor_temp_enabled = src.sensor_temp_enabled;
  dst.sensor_temp_pin = src.sensor_temp_pin;
  dst.sensor_temp_interval_s = src.sensor_temp_interval_s;
  dst.sensor_tank_enabled = src.sensor_tank_enabled;
  dst.sensor_tank_range_mm = src.sensor_tank_range_mm;
  dst.sensor_tank_vref_mv = src.sensor_tank_vref_mv;
  dst.sensor_tank_sense_ohms = src.sensor_tank_sense_ohms;
  dst.sensor_tank_interval_s = src.sensor_tank_interval_s;
  dst.commissioned = src.commissioned;
}

inline void restoreSettingsBackup(const SettingsBackup &src, Settings &dst) {
  dst.mode = src.mode.c_str();
  dst.role = src.role.c_str();
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
  dst.rx_failsafe_mode = src.rx_failsafe_mode.c_str();
  dst.rx_failsafe_timeout_ms = src.rx_failsafe_timeout_ms;
  dst.wifi_sta_ssid = src.wifi_sta_ssid.c_str();
  dst.wifi_sta_password = src.wifi_sta_password.c_str();
  dst.lan_hostname = src.lan_hostname.c_str();
  dst.wifi_phy_mode = src.wifi_phy_mode.c_str();
  dst.wifi_tx_power_dbm = src.wifi_tx_power_dbm;
  dst.wifi_sleep_enabled = src.wifi_sleep_enabled;
  dst.wifi_static_ip_enabled = src.wifi_static_ip_enabled;
  dst.wifi_static_ip = src.wifi_static_ip.c_str();
  dst.wifi_static_gateway = src.wifi_static_gateway.c_str();
  dst.wifi_static_subnet = src.wifi_static_subnet.c_str();
  dst.wifi_channel_override = src.wifi_channel_override;
  dst.wifi_ap_fallback_policy = src.wifi_ap_fallback_policy.c_str();
  dst.wifi_admin_enabled = src.wifi_admin_enabled;
  dst.fleet_passphrase = src.fleet_passphrase.c_str();
  dst.fleet_setup_prompt_dismissed = src.fleet_setup_prompt_dismissed;
  dst.admin_password = src.admin_password.c_str();
  dst.ap_always_on = src.ap_always_on;
  dst.mqtt_client_enabled = src.mqtt_client_enabled;
  dst.mqtt_control_enabled = src.mqtt_control_enabled;
  dst.mqtt_controller_addresses = src.mqtt_controller_addresses.c_str();
  dst.mqtt_host = src.mqtt_host.c_str();
  dst.mqtt_port = src.mqtt_port;
  dst.mqtt_user = src.mqtt_user.c_str();
  dst.mqtt_password = src.mqtt_password.c_str();
  dst.mqtt_topic_root = src.mqtt_topic_root.c_str();
  dst.sensor_temp_enabled = src.sensor_temp_enabled;
  dst.sensor_temp_pin = src.sensor_temp_pin;
  dst.sensor_temp_interval_s = src.sensor_temp_interval_s;
  dst.sensor_tank_enabled = src.sensor_tank_enabled;
  dst.sensor_tank_range_mm = src.sensor_tank_range_mm;
  dst.sensor_tank_vref_mv = src.sensor_tank_vref_mv;
  dst.sensor_tank_sense_ohms = src.sensor_tank_sense_ohms;
  dst.sensor_tank_interval_s = src.sensor_tank_interval_s;
  dst.commissioned = src.commissioned;
}
