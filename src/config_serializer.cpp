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

namespace {

void writeAddressArrayImpl(JsonObject obj, const char *key, const uint8_t *values,
                           uint8_t count, uint8_t cap) {
  JsonArray arr = obj[key].to<JsonArray>();
  if (values == nullptr || cap == 0)
    return;
  if (count > cap)
    count = cap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] >= runtime_utils::kMinAddress && values[i] <= runtime_utils::kMaxAddress)
      arr.add(values[i]);
  }
}

} // namespace

void writeAddressArray(JsonDocument &doc, const char *key, const uint8_t *values,
                       uint8_t count, uint8_t cap) {
  writeAddressArrayImpl(doc.to<JsonObject>(), key, values, count, cap);
}

void writeSettingsJsonObject(JsonObject config, ConfigStore &store,
                             bool includeSecrets) {
  const auto &cfg = store.settings();
  config["schema_version"] = cfg.schema_version;
  config["commissioned"] = cfg.commissioned;
  config["mode"] = cfg.mode;
  config["role"] = cfg.role_tx ? "gateway" : "remote";
  config["role_tx"] = cfg.role_tx;
  config["local_address"] = cfg.local_address;
  config["remote_address"] = cfg.remote_address;
  writeAddressArrayImpl(config, "paired_target_addresses", cfg.paired_target_addresses,
                        cfg.paired_target_count, Settings::kAddressListCap);
  writeAddressArrayImpl(config, "allowed_controller_addresses",
                        cfg.allowed_controller_addresses,
                        cfg.allowed_controller_count, Settings::kAddressListCap);
  writeAddressArrayImpl(config, "known_peer_addresses", cfg.known_peer_addresses,
                        cfg.known_peer_count, Settings::kAddressListCap);
  config["lora_frequency_hz"] = cfg.lora_frequency_hz;
  config["lora_tx_power"] = cfg.lora_tx_power;
  config["lora_spreading_factor"] = cfg.lora_spreading_factor;
  config["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  config["lora_coding_rate"] = cfg.lora_coding_rate;
  config["heartbeat_ms"] = cfg.heartbeat_ms;
  config["heartbeat_enabled"] = cfg.heartbeat_enabled;
  config["ack_timeout_ms"] = cfg.ack_timeout_ms;
  config["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  config["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  config["tx_mqtt_remote_default_poll_interval_ms"] =
      cfg.tx_mqtt_remote_default_poll_interval_ms;
  config["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  config["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  config["input_control_paired_lora_enabled"] =
      cfg.input_control_paired_lora_enabled;
  config["tx_command_retry_timeout_ms"] = cfg.tx_command_retry_timeout_ms;
  config["rx_failsafe_mode"] = cfg.rx_failsafe_mode;
  config["rx_failsafe_timeout_ms"] = cfg.rx_failsafe_timeout_ms;
  config["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  config["wifi_sta_password"] = includeSecrets ? cfg.wifi_sta_password : "";
  config["wifi_sta_password_set"] = cfg.wifi_sta_password.length() > 0;
  config["lan_hostname"] =
      cfg.lan_hostname.length() > 0 ? cfg.lan_hostname.c_str() : store.defaultLanHostname();
  config["computed_lan_hostname"] = store.defaultLanHostname();
  config["ap_always_on"] = cfg.ap_always_on;
  config["wifi_tx_power_dbm"] = cfg.wifi_tx_power_dbm;
  config["wifi_sleep_enabled"] = cfg.wifi_sleep_enabled;
  config["wifi_static_ip_enabled"] = cfg.wifi_static_ip_enabled;
  config["wifi_static_ip"] = cfg.wifi_static_ip;
  config["wifi_static_gateway"] = cfg.wifi_static_gateway;
  config["wifi_static_subnet"] = cfg.wifi_static_subnet;
  config["wifi_channel_override"] = cfg.wifi_channel_override;
  config["wifi_ap_fallback_policy"] = cfg.wifi_ap_fallback_policy;
  config["wifi_admin_enabled"] = cfg.wifi_admin_enabled;
  config["power_save_listen_only"] = cfg.power_save_listen_only;
  config["mqtt_client_enabled"] = cfg.mqtt_client_enabled;
  config["mqtt_control_enabled"] = cfg.mqtt_control_enabled;
  config["mqtt_controller_addresses"] = cfg.mqtt_controller_addresses;
  config["mqtt_host"] = cfg.mqtt_host;
  config["mqtt_port"] = cfg.mqtt_port;
  config["mqtt_user"] = cfg.mqtt_user;
  config["mqtt_password"] = includeSecrets ? cfg.mqtt_password : "";
  config["mqtt_password_set"] = cfg.mqtt_password.length() > 0;
  config["mqtt_topic_root"] = cfg.mqtt_topic_root;
  config["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  config["sensor_temp_pin"] = cfg.sensor_temp_pin;
  config["sensor_temp_interval_s"] = cfg.sensor_temp_interval_s;
  config["sensor_tank_enabled"] = cfg.sensor_tank_enabled;
  config["sensor_tank_range_mm"] = cfg.sensor_tank_range_mm;
  config["sensor_tank_vref_mv"] = cfg.sensor_tank_vref_mv;
  config["sensor_tank_sense_ohms"] = cfg.sensor_tank_sense_ohms;
  config["sensor_tank_interval_s"] = cfg.sensor_tank_interval_s;
  config["fleet_passphrase"] = includeSecrets ? cfg.fleet_passphrase : "";
  config["fleet_passphrase_set"] = cfg.fleet_passphrase.length() > 0;
  config["fleet_passphrase_default"] = runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str());
  config["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  config["admin_password"] = includeSecrets ? cfg.admin_password : "";
  config["admin_password_set"] = cfg.admin_password.length() > 0;
}

void writeSettingsJson(JsonDocument &doc, ConfigStore &config,
                       bool includeSecrets) {
  writeSettingsJsonObject(doc.to<JsonObject>(), config, includeSecrets);
}
