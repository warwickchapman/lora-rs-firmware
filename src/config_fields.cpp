#include "config_fields.h"

const ConfigField kConfigFields[] = {
    {"schema_version", ConfigFieldClass::RetainedConfig},
    {"commissioned", ConfigFieldClass::RetainedConfig},
    {"mode", ConfigFieldClass::RetainedConfig},
    {"role_tx", ConfigFieldClass::RetainedConfig},
    {"local_address", ConfigFieldClass::RetainedConfig},
    {"controller_address", ConfigFieldClass::RetainedConfig},
    {"allowed_controller_addresses", ConfigFieldClass::RetainedConfig},
    {"known_peer_addresses", ConfigFieldClass::RetainedConfig},
    {"known_peer_chip_ids", ConfigFieldClass::InternalOnly},
    {"lora_frequency_hz", ConfigFieldClass::RetainedConfig},
    {"lora_tx_power", ConfigFieldClass::RetainedConfig},
    {"lora_spreading_factor", ConfigFieldClass::RetainedConfig},
    {"lora_bandwidth_hz", ConfigFieldClass::RetainedConfig},
    {"lora_coding_rate", ConfigFieldClass::RetainedConfig},
    {"heartbeat_ms", ConfigFieldClass::RetainedConfig},
    {"heartbeat_enabled", ConfigFieldClass::RetainedConfig},
    {"ack_timeout_ms", ConfigFieldClass::RetainedConfig},
    {"mqtt_remote_retry_timeout_ms", ConfigFieldClass::RetainedConfig},
    {"tx_mqtt_remote_polling_enabled", ConfigFieldClass::RetainedConfig},
    {"tx_mqtt_remote_default_poll_interval_ms", ConfigFieldClass::RetainedConfig},
    {"input_control_paired_lora_enabled", ConfigFieldClass::RetainedConfig},
    {"tx_command_retry_timeout_ms", ConfigFieldClass::RetainedConfig},
    {"rx_failsafe_mode", ConfigFieldClass::RetainedConfig},
    {"rx_failsafe_timeout_ms", ConfigFieldClass::RetainedConfig},
    {"wifi_sta_ssid", ConfigFieldClass::RetainedConfig},
    {"wifi_sta_password", ConfigFieldClass::SecretMetadata},
    {"lan_hostname", ConfigFieldClass::RetainedConfig},
    {"ap_always_on", ConfigFieldClass::RetainedConfig},
    {"wifi_tx_power_dbm", ConfigFieldClass::RetainedConfig},
    {"wifi_sleep_enabled", ConfigFieldClass::RetainedConfig},
    {"wifi_static_ip_enabled", ConfigFieldClass::RetainedConfig},
    {"wifi_static_ip", ConfigFieldClass::RetainedConfig},
    {"wifi_static_gateway", ConfigFieldClass::RetainedConfig},
    {"wifi_static_subnet", ConfigFieldClass::RetainedConfig},
    {"wifi_channel_override", ConfigFieldClass::RetainedConfig},
    {"wifi_ap_fallback_policy", ConfigFieldClass::RetainedConfig},
    {"wifi_admin_enabled", ConfigFieldClass::RetainedConfig},
    {"power_save_listen_only", ConfigFieldClass::RetainedConfig},
    {"mqtt_client_enabled", ConfigFieldClass::RetainedConfig},
    {"mqtt_control_enabled", ConfigFieldClass::RetainedConfig},
    {"mqtt_controller_addresses", ConfigFieldClass::RetainedConfig},
    {"mqtt_host", ConfigFieldClass::RetainedConfig},
    {"mqtt_port", ConfigFieldClass::RetainedConfig},
    {"mqtt_user", ConfigFieldClass::RetainedConfig},
    {"mqtt_password", ConfigFieldClass::SecretMetadata},
    {"mqtt_topic_root", ConfigFieldClass::RetainedConfig},
    {"sensor_temp_enabled", ConfigFieldClass::RetainedConfig},
    {"sensor_temp_pin", ConfigFieldClass::RetainedConfig},
    {"sensor_temp_interval_s", ConfigFieldClass::RetainedConfig},
    {"sensor_tank_enabled", ConfigFieldClass::RetainedConfig},
    {"sensor_tank_range_mm", ConfigFieldClass::RetainedConfig},
    {"sensor_tank_vref_mv", ConfigFieldClass::RetainedConfig},
    {"sensor_tank_sense_ohms", ConfigFieldClass::RetainedConfig},
    {"sensor_tank_interval_s", ConfigFieldClass::RetainedConfig},
    {"fleet_passphrase", ConfigFieldClass::SecretMetadata},
    {"fleet_setup_prompt_dismissed", ConfigFieldClass::RetainedConfig},
    {"admin_password", ConfigFieldClass::SecretMetadata}
};

const size_t kConfigFieldCount = sizeof(kConfigFields) / sizeof(kConfigFields[0]);

const ConfigField *findConfigField(const char *name) {
  if (name == nullptr) return nullptr;
  for (size_t i = 0; i < kConfigFieldCount; ++i) {
    if (strcmp(kConfigFields[i].name, name) == 0) {
      return &kConfigFields[i];
    }
  }
  return nullptr;
}

bool isMqttWritableConfigField(const char *name) {
  const ConfigField *field = findConfigField(name);
  return field != nullptr && field->classification != ConfigFieldClass::InternalOnly;
}
