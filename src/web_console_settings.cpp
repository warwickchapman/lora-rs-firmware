#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
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

void captureSettingsBackup(const Settings &src, SettingsBackup &dst) {
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

void restoreSettingsBackup(const SettingsBackup &src, Settings &dst) {
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
}  // namespace

void WebConsole::handleGetSettings() {
  HeapProbeGuard heapProbe(this, "/api/settings:get");
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
  const String prevAdminPassword = cfg.admin_password;
  const String oldDefaultHost = config_->defaultLanHostname();
  const String oldLegacyDefaultHost = String("lrs-") + config_->chipIdHex();
  const String oldLegacyRoleTxHost = oldLegacyDefaultHost + "-tx";
  const String oldLegacyRoleRxHost = oldLegacyDefaultHost + "-rx";
  
  cfg.mode = doc["mode"] | cfg.mode.c_str();
  cfg.role = doc["role"] | cfg.role.c_str();
  cfg.role_tx = parseBoolField(doc["role_tx"], cfg.role_tx);
  if (cfg.mode == "paired") {
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  } else if (cfg.mode.length() == 0) {
    cfg.mode = "paired";
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  }
  cfg.local_address = parseAddressField(doc["local_address"], cfg.local_address);
  cfg.remote_address = parseAddressField(doc["remote_address"], cfg.remote_address);
  if (!doc["paired_target_addresses"].isNull()) {
    cfg.paired_target_count =
        parseAddressArrayField(doc["paired_target_addresses"], cfg.paired_target_addresses, Settings::kAddressListCap);
  }
  if (!doc["allowed_controller_addresses"].isNull()) {
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
  if (cfg.remote_address < 1) cfg.remote_address = 1;
  if (cfg.remote_address > 254) cfg.remote_address = 254;
  if (cfg.role_tx) {
    if (cfg.paired_target_count == 0) {
      cfg.paired_target_count = 1;
      cfg.paired_target_addresses[0] = cfg.remote_address;
    }
    cfg.remote_address = cfg.paired_target_addresses[0];
  } else {
    if (cfg.allowed_controller_count == 0) {
      cfg.allowed_controller_count = 1;
      cfg.allowed_controller_addresses[0] = cfg.remote_address;
    }
    cfg.remote_address = cfg.allowed_controller_addresses[0];
  }
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
  if (cfg.lora_frequency_hz < kMinFrequencyHz || cfg.lora_frequency_hz > kMaxFrequencyHz) {
    cfg.lora_frequency_hz = kDefaultFrequencyHz;
  }
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
                              (cfg.ap_always_on != prevApAlwaysOn);
  const bool otaAuthChanged = (cfg.admin_password != prevAdminPassword);

  if (!config_->save()) {
    restoreOnFailure();
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
