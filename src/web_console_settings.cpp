#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
#include "web_console_settings_backup.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

namespace {
uint8_t parseAddressArrayField(JsonVariantConst src, uint8_t *out, uint8_t cap) {
  if (out == nullptr || cap == 0) return 0;
  for (uint8_t i = 0; i < cap; ++i) out[i] = 0;
  if (src.isNull() || !src.is<JsonArrayConst>()) return 0;
  JsonArrayConst arr = src.as<JsonArrayConst>();
  uint8_t count = 0;
  for (JsonVariantConst v : arr) {
    const int addr = v.as<int>();
    if (addr < 1 || addr > 254) continue;
    bool dup = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (out[i] == static_cast<uint8_t>(addr)) {
        dup = true;
        break;
      }
    }
    if (dup) continue;
    out[count++] = static_cast<uint8_t>(addr);
    if (count >= cap) break;
  }
  return count;
}

void writeAddressArray(JsonDocument &doc, const char *key, const uint8_t *values, uint8_t count, uint8_t cap) {
  JsonArray arr = doc[key].to<JsonArray>();
  if (values == nullptr || cap == 0) return;
  if (count > cap) count = cap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] >= 1 && values[i] <= 254) arr.add(values[i]);
  }
}

}  // namespace

void WebConsole::handleGetSettings() {
  HeapProbeGuard heapProbe(this, "/api/settings:get");
  if (rejectApiIfLowHeap("/api/settings:get", kApiLowHeapRejectFreeBytes, kApiLowHeapRejectMaxBlockBytes)) {
    return;
  }
  JsonDocument doc;
  auto &cfg = config_->settings();
  doc["mode"] = cfg.mode;
  doc["role"] = cfg.role;
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  writeAddressArray(doc, "paired_target_addresses", cfg.paired_target_addresses, cfg.paired_target_count, Settings::kAddressListCap);
  writeAddressArray(doc, "allowed_controller_addresses", cfg.allowed_controller_addresses, cfg.allowed_controller_count,
                    Settings::kAddressListCap);
  writeAddressArray(doc, "known_peer_addresses", cfg.known_peer_addresses, cfg.known_peer_count, Settings::kAddressListCap);
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
  doc["lan_hostname"] = cfg.lan_hostname.length() > 0 ? cfg.lan_hostname : config_->defaultLanHostname();
  doc["computed_lan_hostname"] = config_->defaultLanHostname();
  doc["fleet_passphrase"] = "";
  doc["fleet_passphrase_set"] = (cfg.fleet_passphrase.length() > 0);
  doc["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = "";
  doc["admin_password_set"] = (cfg.admin_password.length() > 0);
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["wifi_phy_mode"] = cfg.wifi_phy_mode;
  doc["wifi_tx_power_dbm"] = cfg.wifi_tx_power_dbm;
  doc["wifi_sleep_enabled"] = cfg.wifi_sleep_enabled;
  doc["wifi_static_ip_enabled"] = cfg.wifi_static_ip_enabled;
  doc["wifi_static_ip"] = cfg.wifi_static_ip;
  doc["wifi_static_gateway"] = cfg.wifi_static_gateway;
  doc["wifi_static_subnet"] = cfg.wifi_static_subnet;
  doc["wifi_channel_override"] = cfg.wifi_channel_override;
  doc["wifi_ap_fallback_policy"] = cfg.wifi_ap_fallback_policy;
  doc["wifi_admin_enabled"] = cfg.wifi_admin_enabled;
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
  filter["paired_target_addresses"] = true;
  filter["allowed_controller_addresses"] = true;
  filter["known_peer_addresses"] = true;
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
  filter["wifi_phy_mode"] = true;
  filter["wifi_tx_power_dbm"] = true;
  filter["wifi_sleep_enabled"] = true;
  filter["wifi_static_ip_enabled"] = true;
  filter["wifi_static_ip"] = true;
  filter["wifi_static_gateway"] = true;
  filter["wifi_static_subnet"] = true;
  filter["wifi_channel_override"] = true;
  filter["wifi_ap_fallback_policy"] = true;
  filter["wifi_admin_enabled"] = true;
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
  SettingsBackup backup;
  captureSettingsBackup(cfg, backup);
  auto restoreOnFailure = [&]() { restoreSettingsBackup(backup, cfg); };
  const String prevStaSsid = cfg.wifi_sta_ssid;
  const String prevStaPassword = cfg.wifi_sta_password;
  const String prevLanHost = cfg.lan_hostname;
  const bool prevApAlwaysOn = cfg.ap_always_on;
  const String prevWifiPhyMode = cfg.wifi_phy_mode;
  const float prevWifiTxPowerDbm = cfg.wifi_tx_power_dbm;
  const bool prevWifiSleepEnabled = cfg.wifi_sleep_enabled;
  const bool prevWifiStaticIpEnabled = cfg.wifi_static_ip_enabled;
  const String prevWifiStaticIp = cfg.wifi_static_ip;
  const String prevWifiStaticGateway = cfg.wifi_static_gateway;
  const String prevWifiStaticSubnet = cfg.wifi_static_subnet;
  const uint8_t prevWifiChannelOverride = cfg.wifi_channel_override;
  const String prevWifiApFallbackPolicy = cfg.wifi_ap_fallback_policy;
  const bool prevWifiAdminEnabled = cfg.wifi_admin_enabled;
  const String prevAdminPassword = cfg.admin_password;
  const String oldDefaultHost = config_->defaultLanHostname();
  const String oldLegacyDefaultHost = String("lrs-") + config_->chipIdHex();
  const String oldLegacyRoleTxHost = oldLegacyDefaultHost + "-tx";
  const String oldLegacyRoleRxHost = oldLegacyDefaultHost + "-rx";
  
  cfg.mode = doc["mode"] | cfg.mode.c_str();
  cfg.role = doc["role"] | cfg.role.c_str();
  cfg.role_tx = parseBoolField(doc["role_tx"], cfg.role_tx);
  const bool hasRemoteAddressField = !doc["remote_address"].isNull();
  const bool hasPairedTargetsField = !doc["paired_target_addresses"].isNull();
  const bool hasAllowedControllersField = !doc["allowed_controller_addresses"].isNull();
  if (cfg.mode == "paired") {
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  } else if (cfg.mode.length() == 0) {
    cfg.mode = "paired";
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  }
  cfg.local_address = parseAddressField(doc["local_address"], cfg.local_address);
  cfg.remote_address = parseAddressField(doc["remote_address"], cfg.remote_address);
  if (hasPairedTargetsField) {
    cfg.paired_target_count =
        parseAddressArrayField(doc["paired_target_addresses"], cfg.paired_target_addresses, Settings::kAddressListCap);
  }
  if (hasAllowedControllersField) {
    cfg.allowed_controller_count = parseAddressArrayField(doc["allowed_controller_addresses"], cfg.allowed_controller_addresses,
                                                          Settings::kAddressListCap);
  }
  if (!doc["known_peer_addresses"].isNull()) {
    cfg.known_peer_count =
        parseAddressArrayField(doc["known_peer_addresses"], cfg.known_peer_addresses, Settings::kAddressListCap);
  }
  cfg.lora_frequency_hz = doc["lora_frequency_hz"] | cfg.lora_frequency_hz;
  cfg.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | cfg.lora_tx_power);
  cfg.lora_spreading_factor = static_cast<uint8_t>(doc["lora_spreading_factor"] | cfg.lora_spreading_factor);
  cfg.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | cfg.lora_bandwidth_hz;
  cfg.lora_coding_rate = static_cast<uint8_t>(doc["lora_coding_rate"] | cfg.lora_coding_rate);
  cfg.heartbeat_ms = doc["heartbeat_ms"] | cfg.heartbeat_ms;
  cfg.ack_timeout_ms = doc["ack_timeout_ms"] | cfg.ack_timeout_ms;
  cfg.mqtt_remote_retry_timeout_ms = doc["mqtt_remote_retry_timeout_ms"] | cfg.mqtt_remote_retry_timeout_ms;
  cfg.tx_mqtt_remote_polling_enabled = parseBoolField(doc["tx_mqtt_remote_polling_enabled"], cfg.tx_mqtt_remote_polling_enabled);
  cfg.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] | cfg.tx_mqtt_remote_default_poll_interval_ms;
  cfg.rx_push_on_change_enabled = parseBoolField(doc["rx_push_on_change_enabled"], cfg.rx_push_on_change_enabled);
  cfg.rx_push_min_interval_ms = doc["rx_push_min_interval_ms"] | cfg.rx_push_min_interval_ms;
  cfg.input_control_paired_lora_enabled = parseBoolField(doc["input_control_paired_lora_enabled"], cfg.input_control_paired_lora_enabled);
  cfg.tx_command_retry_timeout_ms = doc["tx_command_retry_timeout_ms"] | cfg.tx_command_retry_timeout_ms;
  cfg.rx_failsafe_mode = doc["rx_failsafe_mode"] | cfg.rx_failsafe_mode.c_str();
  cfg.rx_failsafe_timeout_ms = doc["rx_failsafe_timeout_ms"] | cfg.rx_failsafe_timeout_ms;
  cfg.wifi_sta_ssid = doc["wifi_sta_ssid"] | cfg.wifi_sta_ssid.c_str();
  cfg.wifi_sta_password = doc["wifi_sta_password"] | cfg.wifi_sta_password.c_str();
  
  const bool hasLanHostnameField = !doc["lan_hostname"].isNull();
  if (hasLanHostnameField) {
    const char *postedLanHost = doc["lan_hostname"] | cfg.lan_hostname.c_str();
    const bool wasDefaultHostname =
        (prevLanHost == oldDefaultHost) || (prevLanHost == oldLegacyDefaultHost) ||
        (prevLanHost == oldLegacyRoleTxHost) || (prevLanHost == oldLegacyRoleRxHost);
    if (wasDefaultHostname && (oldDefaultHost.equals(postedLanHost) || oldLegacyDefaultHost.equals(postedLanHost) ||
                               oldLegacyRoleTxHost.equals(postedLanHost) || oldLegacyRoleRxHost.equals(postedLanHost))) {
      cfg.lan_hostname = config_->defaultLanHostname();
    } else {
      cfg.lan_hostname = postedLanHost;
    }
  }
  cfg.wifi_phy_mode = doc["wifi_phy_mode"] | cfg.wifi_phy_mode.c_str();
  cfg.wifi_tx_power_dbm = doc["wifi_tx_power_dbm"] | cfg.wifi_tx_power_dbm;
  cfg.wifi_sleep_enabled = parseBoolField(doc["wifi_sleep_enabled"], cfg.wifi_sleep_enabled);
  cfg.wifi_static_ip_enabled = parseBoolField(doc["wifi_static_ip_enabled"], cfg.wifi_static_ip_enabled);
  cfg.wifi_static_ip = doc["wifi_static_ip"] | cfg.wifi_static_ip.c_str();
  cfg.wifi_static_gateway = doc["wifi_static_gateway"] | cfg.wifi_static_gateway.c_str();
  cfg.wifi_static_subnet = doc["wifi_static_subnet"] | cfg.wifi_static_subnet.c_str();
  cfg.wifi_channel_override = static_cast<uint8_t>(doc["wifi_channel_override"] | cfg.wifi_channel_override);
  cfg.wifi_ap_fallback_policy = doc["wifi_ap_fallback_policy"] | cfg.wifi_ap_fallback_policy.c_str();
  cfg.wifi_admin_enabled = parseBoolField(doc["wifi_admin_enabled"], cfg.wifi_admin_enabled);
  cfg.fleet_passphrase = doc["fleet_passphrase"] | cfg.fleet_passphrase.c_str();
  cfg.ap_always_on = parseBoolField(doc["ap_always_on"], cfg.ap_always_on);
  cfg.mqtt_client_enabled = parseBoolField(doc["mqtt_client_enabled"], cfg.mqtt_client_enabled);
  cfg.mqtt_control_enabled = parseBoolField(doc["mqtt_control_enabled"], cfg.mqtt_control_enabled);
  cfg.mqtt_controller_addresses = doc["mqtt_controller_addresses"] | cfg.mqtt_controller_addresses.c_str();
  cfg.mqtt_host = doc["mqtt_host"] | cfg.mqtt_host.c_str();
  cfg.mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | cfg.mqtt_port);
  cfg.mqtt_user = doc["mqtt_user"] | cfg.mqtt_user.c_str();
  cfg.mqtt_password = doc["mqtt_password"] | cfg.mqtt_password.c_str();
  cfg.mqtt_topic_root = doc["mqtt_topic_root"] | cfg.mqtt_topic_root.c_str();
  cfg.sensor_temp_enabled = parseBoolField(doc["sensor_temp_enabled"], cfg.sensor_temp_enabled);

  String newAdmin = doc["admin_password"] | cfg.admin_password.c_str();
  if (newAdmin.length() >= 8) {
    cfg.admin_password = newAdmin;
  }

  if (cfg.local_address < 1) cfg.local_address = 1;
  if (cfg.local_address > 254) cfg.local_address = 254;
  if (!hasRemoteAddressField) {
    if (cfg.role_tx && hasPairedTargetsField && cfg.paired_target_count > 0) {
      cfg.remote_address = cfg.paired_target_addresses[0];
    } else if (!cfg.role_tx && hasAllowedControllersField &&
               cfg.allowed_controller_count > 0) {
      cfg.remote_address = cfg.allowed_controller_addresses[0];
    }
  }
  if (cfg.remote_address < 1) cfg.remote_address = 1;
  if (cfg.remote_address > 254) cfg.remote_address = 254;
  if (cfg.paired_target_count == 0) {
    cfg.paired_target_count = 1;
  }
  cfg.paired_target_addresses[0] = cfg.remote_address;
  if (cfg.allowed_controller_count == 0) {
    cfg.allowed_controller_count = 1;
  }
  cfg.allowed_controller_addresses[0] = cfg.remote_address;
  cfg.fleet_passphrase.trim();
  const bool hasFleetPassphraseField = !doc["fleet_passphrase"].isNull();
  const bool allowDefaultDeploymentKey = parseBoolField(doc["allow_default_deployment_key"], false);
  if (hasFleetPassphraseField && cfg.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    restoreOnFailure();
    server_.send(400, "text/plain",
                 String("deployment key too short (min ") + String(static_cast<unsigned>(kMinDeploymentKeyLen)) +
                     " chars)");
    return;
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey && isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    restoreOnFailure();
    server_.send(400, "text/plain", "deployment key cannot be default; set unique key");
    return;
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    cfg.fleet_setup_prompt_dismissed = true;
  }
  // Frequency is region-locked by firmware build target.
  cfg.lora_frequency_hz = kDefaultFrequencyHz;
  if (cfg.mode == "paired") {
    if (cfg.heartbeat_ms < kMinHeartbeatMs) cfg.heartbeat_ms = kMinHeartbeatMs;
    if (cfg.heartbeat_ms > kMaxHeartbeatMs) cfg.heartbeat_ms = kMaxHeartbeatMs;
  } else {
    // Heartbeat setting is paired-mode only; keep a stable default elsewhere.
    cfg.heartbeat_ms = 60000UL;
  }
  if (cfg.ack_timeout_ms < kMinAckTimeoutMs) cfg.ack_timeout_ms = kMinAckTimeoutMs;
  if (cfg.ack_timeout_ms > kMaxAckTimeoutMs) cfg.ack_timeout_ms = kMaxAckTimeoutMs;
  if (cfg.mqtt_remote_retry_timeout_ms < kMinMqttRemoteRetryTimeoutMs) cfg.mqtt_remote_retry_timeout_ms = kMinMqttRemoteRetryTimeoutMs;
  if (cfg.mqtt_remote_retry_timeout_ms > kMaxMqttRemoteRetryTimeoutMs) cfg.mqtt_remote_retry_timeout_ms = kMaxMqttRemoteRetryTimeoutMs;
  if (cfg.tx_mqtt_remote_default_poll_interval_ms < kMinTxPollDefaultIntervalMs) {
    cfg.tx_mqtt_remote_default_poll_interval_ms = kMinTxPollDefaultIntervalMs;
  }
  if (cfg.tx_mqtt_remote_default_poll_interval_ms > kMaxTxPollDefaultIntervalMs) {
    cfg.tx_mqtt_remote_default_poll_interval_ms = kMaxTxPollDefaultIntervalMs;
  }
  if (cfg.rx_push_min_interval_ms < kMinRxPushIntervalMs) cfg.rx_push_min_interval_ms = kMinRxPushIntervalMs;
  if (cfg.rx_push_min_interval_ms > kMaxRxPushIntervalMs) cfg.rx_push_min_interval_ms = kMaxRxPushIntervalMs;
  if (cfg.tx_command_retry_timeout_ms < 5000UL) cfg.tx_command_retry_timeout_ms = 5000UL;
  if (cfg.tx_command_retry_timeout_ms > 3600000UL) cfg.tx_command_retry_timeout_ms = 3600000UL;
  cfg.rx_failsafe_mode.trim();
  cfg.rx_failsafe_mode.toLowerCase();
  if (cfg.rx_failsafe_mode != "hold_last" && cfg.rx_failsafe_mode != "force_off" && cfg.rx_failsafe_mode != "force_on") {
    cfg.rx_failsafe_mode = "hold_last";
  }
  if (cfg.rx_failsafe_timeout_ms < 5000UL) cfg.rx_failsafe_timeout_ms = 5000UL;
  if (cfg.rx_failsafe_timeout_ms > 3600000UL) cfg.rx_failsafe_timeout_ms = 3600000UL;
  if (cfg.mqtt_port == 0) cfg.mqtt_port = 1883;
  if (cfg.mqtt_topic_root.length() == 0) cfg.mqtt_topic_root = "lora";
  cfg.wifi_phy_mode.trim();
  cfg.wifi_phy_mode.toLowerCase();
  if (cfg.wifi_phy_mode != "11b" && cfg.wifi_phy_mode != "11g" && cfg.wifi_phy_mode != "11n") {
    restoreOnFailure();
    server_.send(400, "text/plain", "wifi_phy_mode must be 11b, 11g, or 11n");
    return;
  }
  const float maxWifiPower =
#ifdef REGION_US
      19.37f;
#else
      20.5f;
#endif
  if (cfg.wifi_tx_power_dbm < 0.0f || cfg.wifi_tx_power_dbm > maxWifiPower) {
    restoreOnFailure();
    server_.send(400, "text/plain", "wifi_tx_power_dbm out of range");
    return;
  }
#ifdef REGION_US
  if (cfg.wifi_channel_override > 11) {
#else
  if (cfg.wifi_channel_override > 13) {
#endif
    restoreOnFailure();
    server_.send(400, "text/plain", "wifi_channel_override out of range");
    return;
  }
  cfg.wifi_ap_fallback_policy.trim();
  cfg.wifi_ap_fallback_policy.toLowerCase();
  if (cfg.wifi_ap_fallback_policy != "fallback_on_disconnect" && cfg.wifi_ap_fallback_policy != "secure_sta_only") {
    restoreOnFailure();
    server_.send(400, "text/plain", "wifi_ap_fallback_policy invalid");
    return;
  }
  if (cfg.mqtt_control_enabled && !cfg.mqtt_client_enabled) {
    restoreOnFailure();
    server_.send(400, "text/plain", "mqtt_control_enabled requires mqtt_client_enabled");
    return;
  }
  cfg.audit_last_saved_by = "admin";
  cfg.audit_last_saved_ms = millis();

  const bool networkChanged = (cfg.wifi_sta_ssid != prevStaSsid) ||
                              (cfg.wifi_sta_password != prevStaPassword) ||
                              (cfg.lan_hostname != prevLanHost) ||
                              (cfg.ap_always_on != prevApAlwaysOn) ||
                              (cfg.wifi_phy_mode != prevWifiPhyMode) ||
                              (cfg.wifi_tx_power_dbm != prevWifiTxPowerDbm) ||
                              (cfg.wifi_sleep_enabled != prevWifiSleepEnabled) ||
                              (cfg.wifi_static_ip_enabled != prevWifiStaticIpEnabled) ||
                              (cfg.wifi_static_ip != prevWifiStaticIp) ||
                              (cfg.wifi_static_gateway != prevWifiStaticGateway) ||
                              (cfg.wifi_static_subnet != prevWifiStaticSubnet) ||
                              (cfg.wifi_channel_override != prevWifiChannelOverride) ||
                              (cfg.wifi_ap_fallback_policy != prevWifiApFallbackPolicy) ||
                              (cfg.wifi_admin_enabled != prevWifiAdminEnabled);
  const bool otaAuthChanged = (cfg.admin_password != prevAdminPassword);

  if (!config_->save()) {
    restoreOnFailure();
    server_.send(500, "text/plain", "save failed");
    return;
  }

  status_live_cache_.built_ms = 0;
  status_static_cache_.built_ms = 0;
  status_lite_cache_.built_ms = 0;
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
  writeAddressArray(doc, "paired_target_addresses", cfg.paired_target_addresses, cfg.paired_target_count, Settings::kAddressListCap);
  writeAddressArray(doc, "allowed_controller_addresses", cfg.allowed_controller_addresses, cfg.allowed_controller_count,
                    Settings::kAddressListCap);
  writeAddressArray(doc, "known_peer_addresses", cfg.known_peer_addresses, cfg.known_peer_count, Settings::kAddressListCap);
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
  doc["wifi_phy_mode"] = cfg.wifi_phy_mode;
  doc["wifi_tx_power_dbm"] = cfg.wifi_tx_power_dbm;
  doc["wifi_sleep_enabled"] = cfg.wifi_sleep_enabled;
  doc["wifi_static_ip_enabled"] = cfg.wifi_static_ip_enabled;
  doc["wifi_static_ip"] = cfg.wifi_static_ip;
  doc["wifi_static_gateway"] = cfg.wifi_static_gateway;
  doc["wifi_static_subnet"] = cfg.wifi_static_subnet;
  doc["wifi_channel_override"] = cfg.wifi_channel_override;
  doc["wifi_ap_fallback_policy"] = cfg.wifi_ap_fallback_policy;
  doc["wifi_admin_enabled"] = cfg.wifi_admin_enabled;
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
