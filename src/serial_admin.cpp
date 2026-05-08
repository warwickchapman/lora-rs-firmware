#include "serial_admin.h"

#include <cstring>

#include <ESP8266WiFi.h>

#include "build_info.h"
#include "logger.h"
#include "web_console_internal.h"
#include "web_console_settings_backup.h"

using namespace webconsole_internal;

namespace {
constexpr size_t kMaxLineBytes = 4096;
constexpr uint8_t kGatewayAddress = 254;
constexpr uint8_t kFirstRemoteAddress = 1;

const char *cmdName(const JsonDocument &doc) {
  return doc["cmd"] | doc["command"] | "";
}

const char *requestId(const JsonDocument &doc) { return doc["id"] | ""; }

uint8_t clampExpectedRemotes(int raw) {
  if (raw < 1)
    return 1;
  if (raw > static_cast<int>(Settings::kAddressListCap))
    return Settings::kAddressListCap;
  return static_cast<uint8_t>(raw);
}

void clearAddressList(uint8_t *list, uint8_t &count) {
  count = 0;
  memset(list, 0, Settings::kAddressListCap);
}

void writeAddressArray(JsonDocument &doc, const char *key, const uint8_t *values,
                       uint8_t count, uint8_t cap) {
  JsonArray arr = doc[key].to<JsonArray>();
  if (values == nullptr || cap == 0)
    return;
  if (count > cap)
    count = cap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] >= 1 && values[i] <= 254)
      arr.add(values[i]);
  }
}

uint8_t parseAddressArrayField(JsonVariantConst src, uint8_t *out,
                               uint8_t cap) {
  if (out == nullptr || cap == 0)
    return 0;
  memset(out, 0, cap);
  if (src.isNull() || !src.is<JsonArrayConst>())
    return 0;
  JsonArrayConst arr = src.as<JsonArrayConst>();
  uint8_t count = 0;
  for (JsonVariantConst v : arr) {
    const int addr = v.as<int>();
    if (addr < 1 || addr > 254)
      continue;
    bool dup = false;
    for (uint8_t i = 0; i < count; ++i) {
      if (out[i] == static_cast<uint8_t>(addr)) {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;
    out[count++] = static_cast<uint8_t>(addr);
    if (count >= cap)
      break;
  }
  return count;
}

void writeSettingsJson(JsonDocument &doc, ConfigStore &config,
                       bool includeSecrets) {
  const auto &cfg = config.settings();
  doc["schema_version"] = cfg.schema_version;
  doc["commissioned"] = cfg.commissioned;
  doc["mode"] = cfg.mode;
  doc["role"] = cfg.role;
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
      cfg.lan_hostname.length() > 0 ? cfg.lan_hostname : config.defaultLanHostname();
  doc["computed_lan_hostname"] = config.defaultLanHostname();
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
  doc["mqtt_password"] = includeSecrets ? cfg.mqtt_password : "";
  doc["mqtt_password_set"] = cfg.mqtt_password.length() > 0;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg.sensor_temp_interval_s;
  doc["fleet_passphrase"] = includeSecrets ? cfg.fleet_passphrase : "";
  doc["fleet_passphrase_set"] = cfg.fleet_passphrase.length() > 0;
  doc["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = includeSecrets ? cfg.admin_password : "";
  doc["admin_password_set"] = cfg.admin_password.length() > 0;
  doc["factory_serial"] = cfg.factory_serial;
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;
}

bool applySettingsPatch(JsonObjectConst doc, ConfigStore &config,
                        String &error, bool &networkChanged,
                        bool &otaAuthChanged) {
  auto &cfg = config.settings();
  SettingsBackup backup;
  captureSettingsBackup(cfg, backup);
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

  auto fail = [&](const String &msg) {
    restoreSettingsBackup(backup, cfg);
    error = msg;
    return false;
  };

  cfg.mode = doc["mode"] | cfg.mode.c_str();
  cfg.role = doc["role"] | cfg.role.c_str();
  cfg.role_tx = parseBoolField(doc["role_tx"], cfg.role_tx);
  const bool hasRemoteAddressField = !doc["remote_address"].isNull();
  const bool hasPairedTargetsField = !doc["paired_target_addresses"].isNull();
  const bool hasAllowedControllersField =
      !doc["allowed_controller_addresses"].isNull();
  if (cfg.mode == "paired") {
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  } else if (cfg.mode.length() == 0) {
    cfg.mode = "paired";
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  }
  cfg.local_address = parseAddressField(doc["local_address"], cfg.local_address);
  cfg.remote_address =
      parseAddressField(doc["remote_address"], cfg.remote_address);
  if (hasPairedTargetsField) {
    cfg.paired_target_count = parseAddressArrayField(
        doc["paired_target_addresses"], cfg.paired_target_addresses,
        Settings::kAddressListCap);
  }
  if (hasAllowedControllersField) {
    cfg.allowed_controller_count = parseAddressArrayField(
        doc["allowed_controller_addresses"], cfg.allowed_controller_addresses,
        Settings::kAddressListCap);
  }
  if (!doc["known_peer_addresses"].isNull()) {
    cfg.known_peer_count =
        parseAddressArrayField(doc["known_peer_addresses"],
                               cfg.known_peer_addresses,
                               Settings::kAddressListCap);
  }
  cfg.lora_frequency_hz = doc["lora_frequency_hz"] | cfg.lora_frequency_hz;
  cfg.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | cfg.lora_tx_power);
  cfg.lora_spreading_factor =
      static_cast<uint8_t>(doc["lora_spreading_factor"] | cfg.lora_spreading_factor);
  cfg.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | cfg.lora_bandwidth_hz;
  cfg.lora_coding_rate =
      static_cast<uint8_t>(doc["lora_coding_rate"] | cfg.lora_coding_rate);
  cfg.heartbeat_ms = doc["heartbeat_ms"] | cfg.heartbeat_ms;
  cfg.ack_timeout_ms = doc["ack_timeout_ms"] | cfg.ack_timeout_ms;
  cfg.mqtt_remote_retry_timeout_ms =
      doc["mqtt_remote_retry_timeout_ms"] | cfg.mqtt_remote_retry_timeout_ms;
  cfg.tx_mqtt_remote_polling_enabled = parseBoolField(
      doc["tx_mqtt_remote_polling_enabled"],
      cfg.tx_mqtt_remote_polling_enabled);
  cfg.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] |
      cfg.tx_mqtt_remote_default_poll_interval_ms;
  cfg.rx_push_on_change_enabled =
      parseBoolField(doc["rx_push_on_change_enabled"],
                     cfg.rx_push_on_change_enabled);
  cfg.rx_push_min_interval_ms =
      doc["rx_push_min_interval_ms"] | cfg.rx_push_min_interval_ms;
  cfg.input_control_paired_lora_enabled = parseBoolField(
      doc["input_control_paired_lora_enabled"],
      cfg.input_control_paired_lora_enabled);
  cfg.tx_command_retry_timeout_ms =
      doc["tx_command_retry_timeout_ms"] | cfg.tx_command_retry_timeout_ms;
  cfg.rx_failsafe_mode = doc["rx_failsafe_mode"] | cfg.rx_failsafe_mode.c_str();
  cfg.rx_failsafe_timeout_ms =
      doc["rx_failsafe_timeout_ms"] | cfg.rx_failsafe_timeout_ms;
  cfg.wifi_sta_ssid = doc["wifi_sta_ssid"] | cfg.wifi_sta_ssid.c_str();
  if (!doc["wifi_sta_password"].isNull()) {
    const char *posted = doc["wifi_sta_password"] | "";
    if (posted[0] != '\0')
      cfg.wifi_sta_password = posted;
  }
  cfg.lan_hostname = doc["lan_hostname"] | cfg.lan_hostname.c_str();
  cfg.ap_always_on = parseBoolField(doc["ap_always_on"], cfg.ap_always_on);
  cfg.wifi_phy_mode = doc["wifi_phy_mode"] | cfg.wifi_phy_mode.c_str();
  cfg.wifi_tx_power_dbm = doc["wifi_tx_power_dbm"] | cfg.wifi_tx_power_dbm;
  cfg.wifi_sleep_enabled =
      parseBoolField(doc["wifi_sleep_enabled"], cfg.wifi_sleep_enabled);
  cfg.wifi_static_ip_enabled = parseBoolField(
      doc["wifi_static_ip_enabled"], cfg.wifi_static_ip_enabled);
  cfg.wifi_static_ip = doc["wifi_static_ip"] | cfg.wifi_static_ip.c_str();
  cfg.wifi_static_gateway =
      doc["wifi_static_gateway"] | cfg.wifi_static_gateway.c_str();
  cfg.wifi_static_subnet =
      doc["wifi_static_subnet"] | cfg.wifi_static_subnet.c_str();
  cfg.wifi_channel_override =
      static_cast<uint8_t>(doc["wifi_channel_override"] | cfg.wifi_channel_override);
  cfg.wifi_ap_fallback_policy =
      doc["wifi_ap_fallback_policy"] | cfg.wifi_ap_fallback_policy.c_str();
  cfg.wifi_admin_enabled =
      parseBoolField(doc["wifi_admin_enabled"], cfg.wifi_admin_enabled);
  cfg.mqtt_client_enabled =
      parseBoolField(doc["mqtt_client_enabled"], cfg.mqtt_client_enabled);
  cfg.mqtt_control_enabled =
      parseBoolField(doc["mqtt_control_enabled"], cfg.mqtt_control_enabled);
  cfg.mqtt_controller_addresses =
      doc["mqtt_controller_addresses"] | cfg.mqtt_controller_addresses.c_str();
  cfg.mqtt_host = doc["mqtt_host"] | cfg.mqtt_host.c_str();
  cfg.mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | cfg.mqtt_port);
  cfg.mqtt_user = doc["mqtt_user"] | cfg.mqtt_user.c_str();
  if (!doc["mqtt_password"].isNull()) {
    const char *posted = doc["mqtt_password"] | "";
    if (posted[0] != '\0')
      cfg.mqtt_password = posted;
  }
  cfg.mqtt_topic_root = doc["mqtt_topic_root"] | cfg.mqtt_topic_root.c_str();
  cfg.sensor_temp_enabled =
      parseBoolField(doc["sensor_temp_enabled"], cfg.sensor_temp_enabled);
  cfg.sensor_temp_pin =
      static_cast<uint8_t>(doc["sensor_temp_pin"] | cfg.sensor_temp_pin);
  cfg.sensor_temp_interval_s =
      static_cast<uint16_t>(doc["sensor_temp_interval_s"] |
                            cfg.sensor_temp_interval_s);
  const bool hasFleetPassphraseField =
      !doc["fleet_passphrase"].isNull() &&
      String(static_cast<const char *>(doc["fleet_passphrase"] | "")).length() > 0;
  if (hasFleetPassphraseField) {
    cfg.fleet_passphrase = doc["fleet_passphrase"] | cfg.fleet_passphrase.c_str();
  }
  if (!doc["admin_password"].isNull()) {
    const String newAdmin = doc["admin_password"] | cfg.admin_password.c_str();
    if (newAdmin.length() == 0) {
      // Redacted get_config responses include an empty admin_password field;
      // keep the current password unless a new non-empty value is supplied.
    } else if (newAdmin.length() < 8) {
      return fail("admin_password_too_short");
    } else {
      cfg.admin_password = newAdmin;
    }
  }

  if (cfg.local_address < 1)
    cfg.local_address = 1;
  if (cfg.local_address > 254)
    cfg.local_address = 254;
  if (cfg.remote_address < 1)
    cfg.remote_address = 1;
  if (cfg.remote_address > 254)
    cfg.remote_address = 254;
  if (cfg.local_address == cfg.remote_address)
    return fail("local_remote_address_conflict");
  if (!hasRemoteAddressField) {
    if (cfg.role_tx && hasPairedTargetsField && cfg.paired_target_count > 0) {
      cfg.remote_address = cfg.paired_target_addresses[0];
    } else if (!cfg.role_tx && hasAllowedControllersField &&
               cfg.allowed_controller_count > 0) {
      cfg.remote_address = cfg.allowed_controller_addresses[0];
    }
  }
  if (cfg.paired_target_count == 0)
    cfg.paired_target_count = 1;
  cfg.paired_target_addresses[0] = cfg.remote_address;
  if (cfg.allowed_controller_count == 0)
    cfg.allowed_controller_count = 1;
  cfg.allowed_controller_addresses[0] = cfg.remote_address;

  const bool allowDefaultDeploymentKey =
      parseBoolField(doc["allow_default_deployment_key"], false);
  cfg.fleet_passphrase.trim();
  if (hasFleetPassphraseField &&
      cfg.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    return fail("fleet_passphrase_too_short");
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey &&
      isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    return fail("fleet_passphrase_default_blocked");
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    cfg.fleet_setup_prompt_dismissed = true;
  }

  // Frequency is intentionally region-locked by the firmware build.
  cfg.lora_frequency_hz = kDefaultFrequencyHz;
  if (cfg.mode == "paired") {
    if (cfg.heartbeat_ms < kMinHeartbeatMs)
      cfg.heartbeat_ms = kMinHeartbeatMs;
    if (cfg.heartbeat_ms > kMaxHeartbeatMs)
      cfg.heartbeat_ms = kMaxHeartbeatMs;
  } else {
    cfg.heartbeat_ms = 60000UL;
  }
  if (cfg.ack_timeout_ms < kMinAckTimeoutMs)
    cfg.ack_timeout_ms = kMinAckTimeoutMs;
  if (cfg.ack_timeout_ms > kMaxAckTimeoutMs)
    cfg.ack_timeout_ms = kMaxAckTimeoutMs;
  if (cfg.mqtt_remote_retry_timeout_ms < kMinMqttRemoteRetryTimeoutMs)
    cfg.mqtt_remote_retry_timeout_ms = kMinMqttRemoteRetryTimeoutMs;
  if (cfg.mqtt_remote_retry_timeout_ms > kMaxMqttRemoteRetryTimeoutMs)
    cfg.mqtt_remote_retry_timeout_ms = kMaxMqttRemoteRetryTimeoutMs;
  if (cfg.tx_mqtt_remote_default_poll_interval_ms <
      kMinTxPollDefaultIntervalMs) {
    cfg.tx_mqtt_remote_default_poll_interval_ms = kMinTxPollDefaultIntervalMs;
  }
  if (cfg.tx_mqtt_remote_default_poll_interval_ms >
      kMaxTxPollDefaultIntervalMs) {
    cfg.tx_mqtt_remote_default_poll_interval_ms = kMaxTxPollDefaultIntervalMs;
  }
  if (cfg.rx_push_min_interval_ms < kMinRxPushIntervalMs)
    cfg.rx_push_min_interval_ms = kMinRxPushIntervalMs;
  if (cfg.rx_push_min_interval_ms > kMaxRxPushIntervalMs)
    cfg.rx_push_min_interval_ms = kMaxRxPushIntervalMs;
  if (cfg.tx_command_retry_timeout_ms < 5000UL)
    cfg.tx_command_retry_timeout_ms = 5000UL;
  if (cfg.tx_command_retry_timeout_ms > 3600000UL)
    cfg.tx_command_retry_timeout_ms = 3600000UL;
  cfg.rx_failsafe_mode.trim();
  cfg.rx_failsafe_mode.toLowerCase();
  if (cfg.rx_failsafe_mode != "hold_last" &&
      cfg.rx_failsafe_mode != "force_off" &&
      cfg.rx_failsafe_mode != "force_on") {
    cfg.rx_failsafe_mode = "hold_last";
  }
  if (cfg.rx_failsafe_timeout_ms < 5000UL)
    cfg.rx_failsafe_timeout_ms = 5000UL;
  if (cfg.rx_failsafe_timeout_ms > 3600000UL)
    cfg.rx_failsafe_timeout_ms = 3600000UL;
  cfg.wifi_phy_mode.trim();
  cfg.wifi_phy_mode.toLowerCase();
  if (cfg.wifi_phy_mode != "11b" && cfg.wifi_phy_mode != "11g" &&
      cfg.wifi_phy_mode != "11n") {
    return fail("wifi_phy_mode_invalid");
  }
  const float maxWifiPower =
#ifdef REGION_US
      19.37f;
#else
      20.5f;
#endif
  if (cfg.wifi_tx_power_dbm < 0.0f || cfg.wifi_tx_power_dbm > maxWifiPower) {
    return fail("wifi_tx_power_dbm_out_of_range");
  }
#ifdef REGION_US
  if (cfg.wifi_channel_override > 11)
#else
  if (cfg.wifi_channel_override > 13)
#endif
  {
    return fail("wifi_channel_override_out_of_range");
  }
  cfg.wifi_ap_fallback_policy.trim();
  cfg.wifi_ap_fallback_policy.toLowerCase();
  if (cfg.wifi_ap_fallback_policy != "fallback_on_disconnect" &&
      cfg.wifi_ap_fallback_policy != "secure_sta_only") {
    return fail("wifi_ap_fallback_policy_invalid");
  }
  if (cfg.mqtt_control_enabled && !cfg.mqtt_client_enabled) {
    return fail("mqtt_control_requires_client");
  }
  if (cfg.mqtt_port == 0)
    cfg.mqtt_port = 1883;
  if (cfg.mqtt_topic_root.length() == 0)
    cfg.mqtt_topic_root = "lora";
  if (cfg.sensor_temp_interval_s < 5)
    cfg.sensor_temp_interval_s = 5;
  if (cfg.sensor_temp_interval_s > 3600)
    cfg.sensor_temp_interval_s = 3600;

  cfg.audit_last_saved_by = "serial_admin";
  cfg.audit_last_saved_ms = millis();
  networkChanged = (cfg.wifi_sta_ssid != prevStaSsid) ||
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
  otaAuthChanged = (cfg.admin_password != prevAdminPassword);
  if (!config.save()) {
    return fail("save_failed");
  }
  return true;
}
} // namespace

bool SerialAdmin::begin(ConfigStore *config, NodeStateMachine *sm,
                        std::function<void(bool, bool)> onApply,
                        std::function<void(uint32_t)> onEnableWeb) {
  config_ = config;
  sm_ = sm;
  on_apply_ = onApply;
  on_enable_web_ = onEnableWeb;
  input_.reserve(256);
  return true;
}

void SerialAdmin::tick() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r')
      continue;
    if (c == '\n') {
      if (input_.length() > 0) {
        handleLine(input_);
        input_ = "";
      }
      continue;
    }
    if (input_.length() >= kMaxLineBytes) {
      input_ = "";
      sendError("unknown", "line_too_long");
      continue;
    }
    input_ += c;
  }
}

void SerialAdmin::handleLine(const String &line) {
  if (!line.startsWith("LRS:"))
    return;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, line.substring(4));
  if (err) {
    sendError("unknown", "invalid_json");
    return;
  }
  handleCommand(doc);
}

bool SerialAdmin::requireAdmin(const JsonDocument &doc) {
  if (config_ == nullptr)
    return false;
  const char *password = doc["admin_password"] | doc["password"] | "";
  return config_->settings().admin_password.equals(password);
}

void SerialAdmin::sendError(const char *cmd, const char *error,
                            const char *id) {
  JsonDocument out;
  out["ok"] = false;
  out["cmd"] = cmd ? cmd : "";
  if (id != nullptr && id[0] != '\0')
    out["id"] = id;
  out["error"] = error ? error : "unknown";
  Serial.print(F("LRS:"));
  serializeJson(out, Serial);
  Serial.println();
}

void SerialAdmin::sendOk(JsonDocument &doc) {
  doc["ok"] = true;
  Serial.print(F("LRS:"));
  serializeJson(doc, Serial);
  Serial.println();
}

void SerialAdmin::buildProvisioningStatus(JsonDocument &doc) {
  if (sm_ == nullptr) {
    doc["ok"] = false;
    doc["error"] = "state_machine_unavailable";
    return;
  }

  ProvisioningSessionSnapshot sess{};
  sm_->provisioningSession(sess);
  JsonObject s = doc["session"].to<JsonObject>();
  s["active"] = sess.active;
  s["state"] = provisioningSessionStateText(sess.state);
  s["session_nonce"] = sess.session_nonce;
  s["estimated_count"] = sess.estimated_count;
  s["started_ms"] = sess.started_ms;
  s["phase_deadline_ms"] = sess.phase_deadline_ms;
  s["paused_normal_tx"] = sess.paused_normal_tx;
  s["discovered_count"] = sess.discovered_count;
  s["selected_count"] = sess.selected_count;
  s["conflict_count"] = sess.conflict_count;
  s["verified_count"] = sess.verified_count;
  s["failed_count"] = sess.failed_count;
  s["now_ms"] = millis();

  JsonArray devices = doc["devices"].to<JsonArray>();
  const size_t count = sm_->provisioningDeviceCount();
  for (size_t i = 0; i < count; ++i) {
    ProvisioningDeviceSnapshot d{};
    if (!sm_->provisioningDeviceByIndex(i, d))
      continue;
    JsonObject o = devices.add<JsonObject>();
    char chipHex[11];
    snprintf(chipHex, sizeof(chipHex), "0x%08lx",
             static_cast<unsigned long>(d.chip_id));
    o["chip_id_hex"] = chipHex;
    o["current_address"] = d.current_address;
    o["assigned_address"] = d.assigned_address;
    o["role_tx"] = d.role_tx;
    o["fw_major"] = d.fw_major;
    o["fw_minor"] = d.fw_minor;
    o["fw_patch"] = d.fw_patch;
    o["rssi"] = d.rssi;
    o["selected"] = d.selected;
    o["address_conflict"] = d.address_conflict;
    o["state"] = provisioningDeviceStateText(d.state);
  }
}

void SerialAdmin::handleStatus(JsonDocument &doc) {
  if (config_ == nullptr) {
    sendError("status", "config_unavailable", requestId(doc));
    return;
  }
  const auto &cfg = config_->settings();
  JsonDocument out;
  out["cmd"] = "status";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["fw_version"] = LRS_FW_VERSION;
  out["chip_id"] = config_->chipIdHex();
  out["serial"] = cfg.factory_serial;
  out["uptime_ms"] = millis();
  out["heap_free"] = lrslog::heapFree();
  out["heap_frag_pct"] = lrslog::heapFragPercent();
  out["heap_max_block"] = lrslog::heapMaxFreeBlock();
  out["mode"] = cfg.mode;
  out["role"] = cfg.role;
  out["role_tx"] = cfg.role_tx;
  out["local_address"] = cfg.local_address;
  out["remote_address"] = cfg.remote_address;
  out["commissioned"] = cfg.commissioned;

  JsonObject wifi = out["wifi"].to<JsonObject>();
  wifi["admin_enabled"] = cfg.wifi_admin_enabled;
  wifi["sta_ssid"] = cfg.wifi_sta_ssid;
  wifi["sta_connected"] = WiFi.isConnected();
  wifi["status"] = wifiStatusText(WiFi.status());
  wifi["ip"] = WiFi.localIP().toString();
  wifi["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  wifi["ap_active"] = softApActiveNow();

  JsonObject mqtt = out["mqtt"].to<JsonObject>();
  mqtt["client_enabled"] = cfg.mqtt_client_enabled;
  mqtt["control_enabled"] = cfg.mqtt_control_enabled;
  mqtt["host"] = cfg.mqtt_host;
  mqtt["topic_root"] = cfg.mqtt_topic_root;

  if (sm_ != nullptr) {
    out["link_state"] = linkStateText(sm_->linkState());
    out["relay_state"] = sm_->relayState();
    out["input_state"] = sm_->inputState();
    out["last_packet_rssi"] = sm_->lastPacketRssi();
    out["last_packet_ms"] = sm_->lastPacketMs();
    out["last_tx_ms"] = sm_->lastTxMs();
    out["peer_count"] = sm_->peerCount();
    out["local_temp_valid"] = sm_->localTemperatureValid();
    if (sm_->localTemperatureValid())
      out["local_temp_c"] = sm_->localTemperatureC();
    out["remote_temp_valid"] = sm_->remoteTemperatureValid();
    if (sm_->remoteTemperatureValid())
      out["remote_temp_c"] = sm_->remoteTemperatureC();
  }
  sendOk(out);
}

void SerialAdmin::handleGetConfig(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("get_config", "auth_failed", id);
    return;
  }
  if (config_ == nullptr) {
    sendError("get_config", "config_unavailable", id);
    return;
  }
  const bool includeSecrets = parseBoolField(doc["include_secrets"], false);
  JsonDocument out;
  out["cmd"] = "get_config";
  if (id[0] != '\0')
    out["id"] = id;
  JsonObject config = out["config"].to<JsonObject>();
  JsonDocument cfgDoc;
  writeSettingsJson(cfgDoc, *config_, includeSecrets);
  for (JsonPair kv : cfgDoc.as<JsonObject>()) {
    config[kv.key()] = kv.value();
  }
  sendOk(out);
}

void SerialAdmin::handleSetConfig(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("set_config", "auth_failed", id);
    return;
  }
  if (config_ == nullptr) {
    sendError("set_config", "config_unavailable", id);
    return;
  }
  JsonObjectConst patch = doc["config"].as<JsonObjectConst>();
  if (patch.isNull()) {
    patch = doc.as<JsonObjectConst>();
  }
  if (patch.isNull()) {
    sendError("set_config", "invalid_config", id);
    return;
  }
  String error;
  bool networkChanged = false;
  bool otaAuthChanged = false;
  if (!applySettingsPatch(patch, *config_, error, networkChanged,
                          otaAuthChanged)) {
    sendError("set_config", error.c_str(), id);
    return;
  }
  if (on_apply_)
    on_apply_(networkChanged, otaAuthChanged);

  JsonDocument out;
  out["cmd"] = "set_config";
  if (id[0] != '\0')
    out["id"] = id;
  out["network_restarted"] = networkChanged;
  out["rebooting"] = otaAuthChanged;
  out["saved_by"] = config_->settings().audit_last_saved_by;
  sendOk(out);
}

void SerialAdmin::handleFactoryReset(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("factory_reset", "auth_failed", id);
    return;
  }
  if (config_ == nullptr) {
    sendError("factory_reset", "config_unavailable", id);
    return;
  }
  const bool keepFleetKey =
      parseBoolField(doc["keep_shared_fleet_key"], false);
  const bool keepWifi = parseBoolField(doc["keep_wifi_credentials"], false);
  if (!config_->factoryReset(keepFleetKey, keepWifi)) {
    sendError("factory_reset", "save_failed", id);
    return;
  }
  JsonDocument out;
  out["cmd"] = "factory_reset";
  if (id[0] != '\0')
    out["id"] = id;
  out["keep_shared_fleet_key"] = keepFleetKey;
  out["keep_wifi_credentials"] = keepWifi;
  sendOk(out);
  delay(100);
  ESP.restart();
}

void SerialAdmin::handleConfigureGateway(JsonDocument &doc) {
  if (!requireAdmin(doc)) {
    sendError("configure_gateway", "auth_failed", requestId(doc));
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError("configure_gateway", "runtime_unavailable", requestId(doc));
    return;
  }

  const String fleetKey = String(static_cast<const char *>(
      doc["fleet_passphrase"] | doc["fleet_key"] | ""));
  if (fleetKey.length() < kMinDeploymentKeyLen ||
      isDefaultDeploymentKey(fleetKey)) {
    sendError("configure_gateway", "invalid_fleet_key", requestId(doc));
    return;
  }

  const uint8_t expected =
      clampExpectedRemotes(doc["expected_remotes"] | doc["expected_count"] | 1);
  auto &cfg = config_->settings();
  cfg.commissioned = true;
  cfg.mode = "paired";
  cfg.role = "transmitter";
  cfg.role_tx = true;
  const int requestedLocal = doc["local_address"] | kGatewayAddress;
  cfg.local_address = (requestedLocal >= 1 && requestedLocal <= 254)
                          ? static_cast<uint8_t>(requestedLocal)
                          : kGatewayAddress;
  if (cfg.local_address == 0 || cfg.local_address == 255)
    cfg.local_address = kGatewayAddress;
  cfg.remote_address = kFirstRemoteAddress;
  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  clearAddressList(cfg.paired_target_addresses, cfg.paired_target_count);
  clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
  clearAddressList(cfg.allowed_controller_addresses,
                   cfg.allowed_controller_count);
  cfg.input_control_paired_lora_enabled =
      doc["input_control_paired_lora_enabled"] | true;
  cfg.audit_last_saved_by = "serial_easy_pair_gateway";
  cfg.audit_last_saved_ms = millis();

  if (!config_->save()) {
    sendError("configure_gateway", "save_failed", requestId(doc));
    return;
  }
  if (on_apply_)
    on_apply_(false, false);

  JsonDocument out;
  out["cmd"] = "configure_gateway";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["local_address"] = cfg.local_address;
  out["expected_remotes"] = expected;
  out["paired_target_count"] = cfg.paired_target_count;
  sendOk(out);
}

void SerialAdmin::handleWifiScan(JsonDocument &doc) {
  if (!requireAdmin(doc)) {
    sendError("wifi_scan", "auth_failed", requestId(doc));
    return;
  }
  JsonDocument out;
  out["cmd"] = "wifi_scan";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;

  const int count = WiFi.scanNetworks(false, true);
  JsonArray arr = out["networks"].to<JsonArray>();
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0)
      continue;
    if (isOwnLrsSoftApLike(ssid))
      continue;
    JsonObject n = arr.add<JsonObject>();
    n["ssid"] = ssid;
    n["rssi"] = WiFi.RSSI(i);
    n["channel"] = WiFi.channel(i);
    n["bssid"] = WiFi.BSSIDstr(i);
    n["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  sendOk(out);
}

void SerialAdmin::handleConfigureWifi(JsonDocument &doc) {
  if (!requireAdmin(doc)) {
    sendError("configure_wifi", "auth_failed", requestId(doc));
    return;
  }
  if (config_ == nullptr) {
    sendError("configure_wifi", "runtime_unavailable", requestId(doc));
    return;
  }
  const char *ssid = doc["wifi_sta_ssid"] | doc["ssid"] | "";
  const char *pass = doc["wifi_sta_password"] | doc["password_value"] | "";
  const size_t ssidLen = strlen(ssid);
  const size_t passLen = strlen(pass);
  if (ssidLen == 0U) {
    sendError("configure_wifi", "ssid_required", requestId(doc));
    return;
  }
  if (ssidLen > 32U || passLen > 64U) {
    sendError("configure_wifi", "credentials_too_long", requestId(doc));
    return;
  }

  auto &cfg = config_->settings();
  cfg.wifi_sta_ssid = ssid;
  cfg.wifi_sta_password = pass;
  cfg.audit_last_saved_by = "serial_easy_pair_wifi";
  cfg.audit_last_saved_ms = millis();
  if (!config_->save()) {
    sendError("configure_wifi", "save_failed", requestId(doc));
    return;
  }
  if (on_apply_)
    on_apply_(true, false);

  JsonDocument out;
  out["cmd"] = "configure_wifi";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  sendOk(out);
}

void SerialAdmin::handleProvisionFleetWifi(JsonDocument &doc) {
  if (!requireAdmin(doc)) {
    sendError("provision_fleet_wifi", "auth_failed", requestId(doc));
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError("provision_fleet_wifi", "runtime_unavailable", requestId(doc));
    return;
  }

  const auto &cfg = config_->settings();
  const char *requestedSsid = doc["wifi_sta_ssid"] | doc["ssid"] | nullptr;
  const char *requestedPass =
      doc["wifi_sta_password"] | doc["password_value"] | nullptr;
  const char *ssid =
      (requestedSsid != nullptr && requestedSsid[0] != '\0')
          ? requestedSsid
          : cfg.wifi_sta_ssid.c_str();
  const char *pass =
      (requestedPass != nullptr && requestedPass[0] != '\0')
          ? requestedPass
          : cfg.wifi_sta_password.c_str();
  const size_t ssidLen = (ssid != nullptr) ? strlen(ssid) : 0U;
  const size_t passLen = (pass != nullptr) ? strlen(pass) : 0U;
  if (ssidLen == 0U) {
    sendError("provision_fleet_wifi", "ssid_required", requestId(doc));
    return;
  }
  if (ssidLen > 32U || passLen > 64U) {
    sendError("provision_fleet_wifi", "credentials_too_long", requestId(doc));
    return;
  }
  if (isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    sendError("provision_fleet_wifi", "fleet_key_default", requestId(doc));
    return;
  }

  const uint32_t cooldownRemainingMs =
      sm_->fleetWifiProvisionCooldownRemainingMs();
  if (cooldownRemainingMs > 0) {
    JsonDocument out;
    out["ok"] = false;
    out["cmd"] = "provision_fleet_wifi";
    const char *id = requestId(doc);
    if (id[0] != '\0')
      out["id"] = id;
    out["error"] = "cooldown_active";
    out["retry_after_ms"] = cooldownRemainingMs;
    out["retry_after_s"] = (cooldownRemainingMs + 999U) / 1000U;
    Serial.print(F("LRS:"));
    serializeJson(out, Serial);
    Serial.println();
    return;
  }

  const uint8_t targetAddress = doc["target_address"] | 255;
  if (targetAddress == 0) {
    sendError("provision_fleet_wifi", "invalid_target_address", requestId(doc));
    return;
  }
  if (!sm_->sendFleetWifiProvision(String(ssid), String(pass), targetAddress)) {
    sendError("provision_fleet_wifi", "send_failed", requestId(doc));
    return;
  }

  const size_t totalLen = ssidLen + passLen;
  const size_t chunks = (totalLen + 6U) / 7U;
  JsonDocument out;
  out["cmd"] = "provision_fleet_wifi";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["target_address"] = targetAddress;
  out["packets"] = static_cast<uint32_t>(chunks + 2U);
  sendOk(out);
  LRS_LOGI(API,
           "event=serial_fleet_wifi_provision_tx target=%u ssid=%s "
           "password=%s packets=%lu",
           static_cast<unsigned>(targetAddress), ssid,
           lrslog::maskSecret(String(pass)).c_str(),
           static_cast<unsigned long>(chunks + 2U));
}

void SerialAdmin::handleIdentify(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("identify", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("identify", "runtime_unavailable", id);
    return;
  }
  uint32_t durationMs = doc["duration_ms"] | NodeStateMachine::kIdentifyLedDurationMs;
  if (durationMs < 1000UL) durationMs = 1000UL;
  if (durationMs > 30000UL) durationMs = 30000UL;
  sm_->triggerIdentify(durationMs);

  JsonDocument out;
  out["cmd"] = "identify";
  if (id[0] != '\0')
    out["id"] = id;
  out["duration_ms"] = durationMs;
  out["pattern"] = "triple_flash_pause_triple_flash";
  sendOk(out);
  LRS_LOGI(API, "event=serial_identify_led duration_ms=%lu",
           static_cast<unsigned long>(durationMs));
}

void SerialAdmin::handleCommand(JsonDocument &doc) {
  const char *cmd = cmdName(doc);
  const char *id = requestId(doc);
  if (cmd == nullptr || cmd[0] == '\0') {
    sendError("unknown", "missing_cmd", id);
    return;
  }

  if (strcmp(cmd, "hello") == 0) {
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["protocol"] = 1;
    out["fw_version"] = LRS_FW_VERSION;
    out["max_remotes"] = Settings::kAddressListCap;
    out["requires_prefix"] = "LRS:";
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "identity") == 0) {
    if (config_ == nullptr) {
      sendError(cmd, "config_unavailable", id);
      return;
    }
    const auto &cfg = config_->settings();
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["chip_id"] = config_->chipIdHex();
    out["serial"] = cfg.factory_serial;
    out["ap_ssid"] = config_->apSsid();
    out["mode"] = cfg.mode;
    out["role"] = cfg.role;
    out["role_tx"] = cfg.role_tx;
    out["local_address"] = cfg.local_address;
    out["remote_address"] = cfg.remote_address;
    out["fw_version"] = LRS_FW_VERSION;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "status") == 0) {
    handleStatus(doc);
    return;
  }

  if (strcmp(cmd, "get_config") == 0) {
    handleGetConfig(doc);
    return;
  }

  if (strcmp(cmd, "set_config") == 0) {
    handleSetConfig(doc);
    return;
  }

  if (strcmp(cmd, "factory_reset") == 0) {
    handleFactoryReset(doc);
    return;
  }

  if (strcmp(cmd, "configure_gateway") == 0) {
    handleConfigureGateway(doc);
    return;
  }

  if (strcmp(cmd, "wifi_scan") == 0) {
    handleWifiScan(doc);
    return;
  }

  if (strcmp(cmd, "configure_wifi") == 0) {
    handleConfigureWifi(doc);
    return;
  }

  if (strcmp(cmd, "provision_fleet_wifi") == 0) {
    handleProvisionFleetWifi(doc);
    return;
  }

  if (strcmp(cmd, "identify") == 0) {
    handleIdentify(doc);
    return;
  }

  if (strcmp(cmd, "set_gateway_targets") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    if (config_ == nullptr || sm_ == nullptr) {
      sendError(cmd, "runtime_unavailable", id);
      return;
    }
    JsonArrayConst arr = doc["addresses"].as<JsonArrayConst>();
    if (arr.isNull() || arr.size() == 0 ||
        arr.size() > Settings::kAddressListCap) {
      sendError(cmd, "invalid_addresses", id);
      return;
    }
    auto &cfg = config_->settings();
    clearAddressList(cfg.paired_target_addresses, cfg.paired_target_count);
    clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
    bool used[256]{};
    used[0] = true;
    used[255] = true;
    used[cfg.local_address] = true;
    for (JsonVariantConst v : arr) {
      const int raw = v.as<int>();
      if (raw < 1 || raw > 254 || used[raw]) {
        sendError(cmd, "invalid_addresses", id);
        return;
      }
      used[raw] = true;
      const uint8_t addr = static_cast<uint8_t>(raw);
      cfg.paired_target_addresses[cfg.paired_target_count++] = addr;
      cfg.known_peer_addresses[cfg.known_peer_count++] = addr;
    }
    cfg.remote_address = cfg.paired_target_addresses[0];
    cfg.audit_last_saved_by = "serial_easy_pair_targets";
    cfg.audit_last_saved_ms = millis();
    if (!config_->save()) {
      sendError(cmd, "save_failed", id);
      return;
    }
    if (on_apply_)
      on_apply_(false, false);
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["target_count"] = cfg.paired_target_count;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "start_discovery") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    const uint8_t expected =
        clampExpectedRemotes(doc["expected_remotes"] | doc["expected_count"] |
                             Settings::kAddressListCap);
    if (sm_ == nullptr || !sm_->provisioningStartDiscovery(expected)) {
      sendError(cmd, "start_failed", id);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["expected_remotes"] = expected;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "provision_all") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    if (sm_ == nullptr || !sm_->provisioningStartProvisionAll()) {
      sendError(cmd, "invalid_state", id);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "provisioning_status") == 0) {
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    buildProvisioningStatus(out);
    if (out["ok"].isNull())
      sendOk(out);
    else {
      Serial.print(F("LRS:"));
      serializeJson(out, Serial);
      Serial.println();
    }
    return;
  }

  if (strcmp(cmd, "cancel_provisioning") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    if (sm_ != nullptr)
      sm_->provisioningCancel();
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "enable_web") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    const uint32_t seconds = doc["seconds"] | 300UL;
    if (on_enable_web_)
      on_enable_web_(seconds * 1000UL);
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["seconds"] = seconds;
    sendOk(out);
    return;
  }

  if (strcmp(cmd, "reboot") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out);
    delay(100);
    ESP.restart();
    return;
  }

  sendError(cmd, "unknown_cmd", id);
}
