#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleGetSettings() {
  HeapProbeGuard heapProbe(this, "/api/settings:get");
  JsonDocument doc;
  auto &cfg = config_->settings();
  doc["mode"] = cfg.mode;
  doc["role"] = cfg.role;
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["input_control_paired_lora_enabled"] = cfg.input_control_paired_lora_enabled;
  doc["tx_command_retry_timeout_ms"] = cfg.tx_command_retry_timeout_ms;
  doc["rx_failsafe_mode"] = cfg.rx_failsafe_mode;
  doc["rx_failsafe_timeout_ms"] = cfg.rx_failsafe_timeout_ms;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = "";
  doc["wifi_sta_password_set"] = (cfg.wifi_sta_password.length() > 0);
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = "";
  doc["fleet_passphrase_set"] = (cfg.fleet_passphrase.length() > 0);
  doc["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = "";
  doc["admin_password_set"] = (cfg.admin_password.length() > 0);
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_client_enabled"] = cfg.mqtt_client_enabled;
  doc["mqtt_control_enabled"] = cfg.mqtt_control_enabled;
  doc["mqtt_controller_addresses"] = cfg.mqtt_controller_addresses;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = "";
  doc["mqtt_password_set"] = (cfg.mqtt_password.length() > 0);
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;

  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handlePostSettings() {
  HeapProbeGuard heapProbe(this, "/api/settings:post");
  if (!requireAuth(true)) return;

  JsonDocument doc;
  JsonDocument filter;
  filter["mode"] = true;
  filter["role"] = true;
  filter["role_tx"] = true;
  filter["local_address"] = true;
  filter["remote_address"] = true;
  filter["lora_frequency_hz"] = true;
  filter["lora_tx_power"] = true;
  filter["lora_spreading_factor"] = true;
  filter["lora_bandwidth_hz"] = true;
  filter["lora_coding_rate"] = true;
  filter["heartbeat_ms"] = true;
  filter["ack_timeout_ms"] = true;
  filter["mqtt_remote_retry_timeout_ms"] = true;
  filter["tx_mqtt_remote_polling_enabled"] = true;
  filter["tx_mqtt_remote_default_poll_interval_ms"] = true;
  filter["rx_push_on_change_enabled"] = true;
  filter["rx_push_min_interval_ms"] = true;
  filter["input_control_paired_lora_enabled"] = true;
  filter["tx_command_retry_timeout_ms"] = true;
  filter["rx_failsafe_mode"] = true;
  filter["rx_failsafe_timeout_ms"] = true;
  filter["wifi_sta_ssid"] = true;
  filter["wifi_sta_password"] = true;
  filter["lan_hostname"] = true;
  filter["fleet_passphrase"] = true;
  filter["allow_default_deployment_key"] = true;
  filter["admin_password"] = true;
  filter["ap_always_on"] = true;
  filter["mqtt_client_enabled"] = true;
  filter["mqtt_control_enabled"] = true;
  filter["mqtt_controller_addresses"] = true;
  filter["mqtt_host"] = true;
  filter["mqtt_port"] = true;
  filter["mqtt_user"] = true;
  filter["mqtt_password"] = true;
  filter["mqtt_topic_root"] = true;
  filter["sensor_temp_enabled"] = true;

  auto err = deserializeJson(doc, server_.arg("plain"), DeserializationOption::Filter(filter));
  if (err) {
    server_.send(400, "text/plain", "invalid json");
    return;
  }

  auto &cfg = config_->settings();
  const Settings prev = cfg;
  Settings next = cfg;
  const String prevStaSsid = prev.wifi_sta_ssid;
  const String prevStaPassword = prev.wifi_sta_password;
  const String prevLanHost = prev.lan_hostname;
  const bool prevApAlwaysOn = prev.ap_always_on;
  const String prevAdminPassword = prev.admin_password;
  const bool oldRoleTx = prev.role_tx;
  const String oldDefaultHost = config_->defaultLanHostnameForRole(oldRoleTx);
  const String oldLegacyDefaultHost = String("lrs-") + config_->chipIdHex();
  const String oldLegacyRoleTxHost = oldLegacyDefaultHost + "-tx";
  const String oldLegacyRoleRxHost = oldLegacyDefaultHost + "-rx";
  next.mode = doc["mode"] | next.mode.c_str();
  next.role = doc["role"] | next.role.c_str();
  next.role_tx = parseBoolField(doc["role_tx"], next.role_tx);
  if (next.mode == "paired") {
    next.role = next.role_tx ? "transmitter" : "receiver";
  } else if (next.mode.length() == 0) {
    next.mode = "paired";
    next.role = next.role_tx ? "transmitter" : "receiver";
  }
  next.local_address = parseAddressField(doc["local_address"], next.local_address);
  next.remote_address = parseAddressField(doc["remote_address"], next.remote_address);
  next.lora_frequency_hz = doc["lora_frequency_hz"] | next.lora_frequency_hz;
  next.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | next.lora_tx_power);
  next.lora_spreading_factor = static_cast<uint8_t>(doc["lora_spreading_factor"] | next.lora_spreading_factor);
  next.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | next.lora_bandwidth_hz;
  next.lora_coding_rate = static_cast<uint8_t>(doc["lora_coding_rate"] | next.lora_coding_rate);
  next.heartbeat_ms = doc["heartbeat_ms"] | next.heartbeat_ms;
  next.ack_timeout_ms = doc["ack_timeout_ms"] | next.ack_timeout_ms;
  next.mqtt_remote_retry_timeout_ms = doc["mqtt_remote_retry_timeout_ms"] | next.mqtt_remote_retry_timeout_ms;
  next.tx_mqtt_remote_polling_enabled = parseBoolField(doc["tx_mqtt_remote_polling_enabled"], next.tx_mqtt_remote_polling_enabled);
  next.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] | next.tx_mqtt_remote_default_poll_interval_ms;
  next.rx_push_on_change_enabled = parseBoolField(doc["rx_push_on_change_enabled"], next.rx_push_on_change_enabled);
  next.rx_push_min_interval_ms = doc["rx_push_min_interval_ms"] | next.rx_push_min_interval_ms;
  next.input_control_paired_lora_enabled = parseBoolField(doc["input_control_paired_lora_enabled"], next.input_control_paired_lora_enabled);
  next.tx_command_retry_timeout_ms = doc["tx_command_retry_timeout_ms"] | next.tx_command_retry_timeout_ms;
  next.rx_failsafe_mode = doc["rx_failsafe_mode"] | next.rx_failsafe_mode.c_str();
  next.rx_failsafe_timeout_ms = doc["rx_failsafe_timeout_ms"] | next.rx_failsafe_timeout_ms;
  next.wifi_sta_ssid = doc["wifi_sta_ssid"] | next.wifi_sta_ssid.c_str();
  next.wifi_sta_password = doc["wifi_sta_password"] | next.wifi_sta_password.c_str();
  const bool hasLanHostnameField = !doc["lan_hostname"].isNull();
  if (hasLanHostnameField) {
    const char *postedLanHost = doc["lan_hostname"] | next.lan_hostname.c_str();
    const bool wasDefaultHostname =
        (prev.lan_hostname == oldDefaultHost) || (prev.lan_hostname == oldLegacyDefaultHost) ||
        (prev.lan_hostname == oldLegacyRoleTxHost) || (prev.lan_hostname == oldLegacyRoleRxHost);
    if (wasDefaultHostname && (oldDefaultHost.equals(postedLanHost) || oldLegacyDefaultHost.equals(postedLanHost) ||
                               oldLegacyRoleTxHost.equals(postedLanHost) || oldLegacyRoleRxHost.equals(postedLanHost))) {
      next.lan_hostname = config_->defaultLanHostnameForRole(next.role_tx);
    } else {
      next.lan_hostname = postedLanHost;
    }
  }
  next.fleet_passphrase = doc["fleet_passphrase"] | next.fleet_passphrase.c_str();
  next.ap_always_on = parseBoolField(doc["ap_always_on"], next.ap_always_on);
  next.mqtt_client_enabled = parseBoolField(doc["mqtt_client_enabled"], next.mqtt_client_enabled);
  next.mqtt_control_enabled = parseBoolField(doc["mqtt_control_enabled"], next.mqtt_control_enabled);
  next.mqtt_controller_addresses = doc["mqtt_controller_addresses"] | next.mqtt_controller_addresses.c_str();
  next.mqtt_host = doc["mqtt_host"] | next.mqtt_host.c_str();
  next.mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | next.mqtt_port);
  next.mqtt_user = doc["mqtt_user"] | next.mqtt_user.c_str();
  next.mqtt_password = doc["mqtt_password"] | next.mqtt_password.c_str();
  next.mqtt_topic_root = doc["mqtt_topic_root"] | next.mqtt_topic_root.c_str();
  next.sensor_temp_enabled = parseBoolField(doc["sensor_temp_enabled"], next.sensor_temp_enabled);

  String newAdmin = doc["admin_password"] | next.admin_password.c_str();
  if (newAdmin.length() >= 8) {
    next.admin_password = newAdmin;
  }

  if (next.local_address < 1) next.local_address = 1;
  if (next.local_address > 254) next.local_address = 254;
  if (next.remote_address < 1) next.remote_address = 1;
  if (next.remote_address > 254) next.remote_address = 254;
  next.fleet_passphrase.trim();
  const bool hasFleetPassphraseField = !doc["fleet_passphrase"].isNull();
  const bool allowDefaultDeploymentKey = parseBoolField(doc["allow_default_deployment_key"], false);
  if (hasFleetPassphraseField && next.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    server_.send(400, "text/plain",
                 String("deployment key too short (min ") + String(static_cast<unsigned>(kMinDeploymentKeyLen)) +
                     " chars)");
    return;
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey && isDefaultDeploymentKey(next.fleet_passphrase)) {
    server_.send(400, "text/plain", "deployment key cannot be default; set unique key");
    return;
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(next.fleet_passphrase)) {
    next.fleet_setup_prompt_dismissed = true;
  }
  if (next.lora_frequency_hz < kMinFrequencyHz || next.lora_frequency_hz > kMaxFrequencyHz) {
    next.lora_frequency_hz = kDefaultFrequencyHz;
  }
  if (next.mode == "paired") {
    if (next.heartbeat_ms < kMinHeartbeatMs) next.heartbeat_ms = kMinHeartbeatMs;
    if (next.heartbeat_ms > kMaxHeartbeatMs) next.heartbeat_ms = kMaxHeartbeatMs;
  } else {
    // Heartbeat setting is paired-mode only; keep a stable default elsewhere.
    next.heartbeat_ms = 60000UL;
  }
  if (next.ack_timeout_ms < kMinAckTimeoutMs) next.ack_timeout_ms = kMinAckTimeoutMs;
  if (next.ack_timeout_ms > kMaxAckTimeoutMs) next.ack_timeout_ms = kMaxAckTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms < kMinMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMinMqttRemoteRetryTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms > kMaxMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMaxMqttRemoteRetryTimeoutMs;
  if (next.tx_mqtt_remote_default_poll_interval_ms < kMinTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMinTxPollDefaultIntervalMs;
  }
  if (next.tx_mqtt_remote_default_poll_interval_ms > kMaxTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMaxTxPollDefaultIntervalMs;
  }
  if (next.rx_push_min_interval_ms < kMinRxPushIntervalMs) next.rx_push_min_interval_ms = kMinRxPushIntervalMs;
  if (next.rx_push_min_interval_ms > kMaxRxPushIntervalMs) next.rx_push_min_interval_ms = kMaxRxPushIntervalMs;
  if (next.tx_command_retry_timeout_ms < 5000UL) next.tx_command_retry_timeout_ms = 5000UL;
  if (next.tx_command_retry_timeout_ms > 3600000UL) next.tx_command_retry_timeout_ms = 3600000UL;
  next.rx_failsafe_mode.trim();
  next.rx_failsafe_mode.toLowerCase();
  if (next.rx_failsafe_mode != "hold_last" && next.rx_failsafe_mode != "force_off" && next.rx_failsafe_mode != "force_on") {
    next.rx_failsafe_mode = "hold_last";
  }
  if (next.rx_failsafe_timeout_ms < 5000UL) next.rx_failsafe_timeout_ms = 5000UL;
  if (next.rx_failsafe_timeout_ms > 3600000UL) next.rx_failsafe_timeout_ms = 3600000UL;
  if (next.mqtt_port == 0) next.mqtt_port = 1883;
  if (next.mqtt_topic_root.length() == 0) next.mqtt_topic_root = "lora";
  if (next.mqtt_control_enabled && !next.mqtt_client_enabled) {
    server_.send(400, "text/plain", "mqtt_control_enabled requires mqtt_client_enabled");
    return;
  }
  next.audit_last_saved_by = "admin";
  next.audit_last_saved_ms = millis();

  const bool networkChanged = (next.wifi_sta_ssid != prevStaSsid) ||
                              (next.wifi_sta_password != prevStaPassword) ||
                              (next.lan_hostname != prevLanHost) ||
                              (next.ap_always_on != prevApAlwaysOn);
  const bool otaAuthChanged = (next.admin_password != prevAdminPassword);

  cfg = next;
  if (!config_->save()) {
    cfg = prev;
    server_.send(500, "text/plain", "save failed");
    return;
  }

  server_.send(200, "text/plain", "saved");
  if (on_apply_) on_apply_(networkChanged, otaAuthChanged);
}

void WebConsole::handleExportSettings() {
  if (!requireAuth(true)) return;
  server_.sendHeader("Content-Disposition", "attachment; filename=lrs-config.json");
  JsonDocument doc;
  auto &cfg = config_->settings();
  doc["mode"] = cfg.mode;
  doc["role"] = cfg.role;
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["input_control_paired_lora_enabled"] = cfg.input_control_paired_lora_enabled;
  doc["tx_command_retry_timeout_ms"] = cfg.tx_command_retry_timeout_ms;
  doc["rx_failsafe_mode"] = cfg.rx_failsafe_mode;
  doc["rx_failsafe_timeout_ms"] = cfg.rx_failsafe_timeout_ms;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg.wifi_sta_password;
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = cfg.fleet_passphrase;
  doc["admin_password"] = cfg.admin_password;
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_client_enabled"] = cfg.mqtt_client_enabled;
  doc["mqtt_control_enabled"] = cfg.mqtt_control_enabled;
  doc["mqtt_controller_addresses"] = cfg.mqtt_controller_addresses;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = cfg.mqtt_password;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  const size_t len = measureJsonPretty(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJsonPretty(doc, server_.client());
}

void WebConsole::handleImportSettings() { handlePostSettings(); }
