#include "config_serializer.h"
#include "runtime_utils.h"
#include "lora_config.h"

#ifdef UNIT_TEST
Settings g_test_settings;
String g_test_lan_hostname = "test-host";

Settings &ConfigStore::settings() {
  return g_test_settings;
}
const Settings &ConfigStore::settings() const {
  return g_test_settings;
}
String ConfigStore::defaultLanHostname() const {
  return g_test_lan_hostname;
}
#endif

void writeAddressArray(JsonDocument &doc, const char *key, const uint8_t *values,
                       uint8_t count, uint8_t cap) {
  JsonArray arr = doc[key].to<JsonArray>();
  if (values == nullptr || cap == 0)
    return;
  if (count > cap)
    count = cap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] >= runtime_utils::kMinAddress && values[i] <= runtime_utils::kMaxAddress)
      arr.add(values[i]);
  }
}

void writeSettingsJson(JsonDocument &doc, ConfigStore &config,
                       bool includeSecrets) {
  const auto &cfg = config.settings();
  doc["schema_version"] = cfg.schema_version;
  doc["commissioned"] = cfg.commissioned;
  doc["mode"] = cfg.mode;
  doc["role"] = cfg.role_tx ? "gateway" : "remote";
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  writeAddressArray(doc, "paired_target_addresses", cfg.paired_target_addresses,
                    cfg.paired_target_count, Settings::kAddressListCap);
  writeAddressArray(doc, "allowed_controller_addresses",
                    cfg.allowed_controller_addresses,
                    cfg.allowed_controller_count, Settings::kAddressListCap);
  writeAddressArray(doc, "known_peer_addresses", cfg.known_peer_addresses,
                    cfg.known_peer_count, Settings::kAddressListCap);
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["heartbeat_enabled"] = cfg.heartbeat_enabled;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] =
      cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["input_control_paired_lora_enabled"] =
      cfg.input_control_paired_lora_enabled;
  doc["tx_command_retry_timeout_ms"] = cfg.tx_command_retry_timeout_ms;
  doc["rx_failsafe_mode"] = cfg.rx_failsafe_mode;
  doc["rx_failsafe_timeout_ms"] = cfg.rx_failsafe_timeout_ms;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = includeSecrets ? cfg.wifi_sta_password : "";
  doc["wifi_sta_password_set"] = cfg.wifi_sta_password.length() > 0;
  doc["lan_hostname"] =
      cfg.lan_hostname.length() > 0 ? cfg.lan_hostname.c_str() : config.defaultLanHostname();
  doc["computed_lan_hostname"] = config.defaultLanHostname();
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["wifi_tx_power_dbm"] = cfg.wifi_tx_power_dbm;
  doc["wifi_sleep_enabled"] = cfg.wifi_sleep_enabled;
  doc["wifi_static_ip_enabled"] = cfg.wifi_static_ip_enabled;
  doc["wifi_static_ip"] = cfg.wifi_static_ip;
  doc["wifi_static_gateway"] = cfg.wifi_static_gateway;
  doc["wifi_static_subnet"] = cfg.wifi_static_subnet;
  doc["wifi_channel_override"] = cfg.wifi_channel_override;
  doc["wifi_ap_fallback_policy"] = cfg.wifi_ap_fallback_policy;
  doc["wifi_admin_enabled"] = cfg.wifi_admin_enabled;
  doc["power_save_listen_only"] = cfg.power_save_listen_only;
  doc["mqtt_client_enabled"] = cfg.mqtt_client_enabled;
  doc["mqtt_control_enabled"] = cfg.mqtt_control_enabled;
  doc["mqtt_controller_addresses"] = cfg.mqtt_controller_addresses;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = includeSecrets ? cfg.mqtt_password : "";
  doc["mqtt_password_set"] = cfg.mqtt_password.length() > 0;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg.sensor_temp_interval_s;
  doc["sensor_tank_enabled"] = cfg.sensor_tank_enabled;
  doc["sensor_tank_range_mm"] = cfg.sensor_tank_range_mm;
  doc["sensor_tank_vref_mv"] = cfg.sensor_tank_vref_mv;
  doc["sensor_tank_sense_ohms"] = cfg.sensor_tank_sense_ohms;
  doc["sensor_tank_interval_s"] = cfg.sensor_tank_interval_s;
  doc["fleet_passphrase"] = includeSecrets ? cfg.fleet_passphrase : "";
  doc["fleet_passphrase_set"] = cfg.fleet_passphrase.length() > 0;
  doc["fleet_passphrase_default"] = runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str());
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = includeSecrets ? cfg.admin_password : "";
  doc["admin_password_set"] = cfg.admin_password.length() > 0;
  doc["factory_serial"] = cfg.factory_serial;
}
