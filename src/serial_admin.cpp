#include "serial_admin.h"

#include <cstring>

#include <ESP8266WiFi.h>

#include "build_info.h"
#include "logger.h"
#include "ota_pull.h"
#include "admin_config_utils.h"
#include "sensor_status.h"
#include "settings_backup.h"

using namespace admin_config_utils;

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

bool isSha256Hex(const char *hex) {
  if (hex == nullptr || strlen(hex) != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char c = hex[i];
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                    (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
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
  doc["maintenance_debug_telemetry_enabled"] =
      cfg.maintenance_debug_telemetry_enabled;
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
  doc["sensor_tank_enabled"] = cfg.sensor_tank_enabled;
  doc["sensor_tank_range_mm"] = cfg.sensor_tank_range_mm;
  doc["sensor_tank_vref_mv"] = cfg.sensor_tank_vref_mv;
  doc["sensor_tank_sense_ohms"] = cfg.sensor_tank_sense_ohms;
  doc["sensor_tank_interval_s"] = cfg.sensor_tank_interval_s;
  doc["fleet_passphrase"] = includeSecrets ? cfg.fleet_passphrase : "";
  doc["fleet_passphrase_set"] = cfg.fleet_passphrase.length() > 0;
  doc["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase.c_str());
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = includeSecrets ? cfg.admin_password : "";
  doc["admin_password_set"] = cfg.admin_password.length() > 0;
  doc["factory_serial"] = cfg.factory_serial;
}

bool applySettingsPatch(JsonObjectConst doc, ConfigStore &config,
                        String &error, bool &networkChanged,
                        bool &otaAuthChanged) {
  auto &cfg = config.settings();
  SettingsBackup backup;
  captureSettingsBackup(cfg, backup);
  FixedSettingString<33> prevStaSsid = cfg.wifi_sta_ssid;
  FixedSettingString<65> prevStaPassword = cfg.wifi_sta_password;
  FixedSettingString<65> prevLanHost = cfg.lan_hostname;
  const bool prevApAlwaysOn = cfg.ap_always_on;
  FixedSettingString<16> prevWifiPhyMode = cfg.wifi_phy_mode;
  const float prevWifiTxPowerDbm = cfg.wifi_tx_power_dbm;
  const bool prevWifiSleepEnabled = cfg.wifi_sleep_enabled;
  const bool prevWifiStaticIpEnabled = cfg.wifi_static_ip_enabled;
  FixedSettingString<16> prevWifiStaticIp = cfg.wifi_static_ip;
  FixedSettingString<16> prevWifiStaticGateway = cfg.wifi_static_gateway;
  FixedSettingString<16> prevWifiStaticSubnet = cfg.wifi_static_subnet;
  const uint8_t prevWifiChannelOverride = cfg.wifi_channel_override;
  FixedSettingString<24> prevWifiApFallbackPolicy = cfg.wifi_ap_fallback_policy;
  const bool prevWifiAdminEnabled = cfg.wifi_admin_enabled;
  FixedSettingString<33> prevAdminPassword = cfg.admin_password;

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
  } else if (cfg.mode == "standalone") {
    cfg.role = "none";
    cfg.role_tx = true;
  } else if (cfg.mode.length() == 0) {
    cfg.mode = "paired";
    cfg.role = cfg.role_tx ? "transmitter" : "receiver";
  } else {
    return fail("mode_invalid");
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
  cfg.maintenance_debug_telemetry_enabled = parseBoolField(
      doc["maintenance_debug_telemetry_enabled"],
      cfg.maintenance_debug_telemetry_enabled);
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
  cfg.sensor_tank_enabled =
      parseBoolField(doc["sensor_tank_enabled"], cfg.sensor_tank_enabled);
  cfg.sensor_tank_range_mm =
      static_cast<uint16_t>(doc["sensor_tank_range_mm"] |
                            cfg.sensor_tank_range_mm);
  cfg.sensor_tank_vref_mv =
      static_cast<uint16_t>(doc["sensor_tank_vref_mv"] |
                            cfg.sensor_tank_vref_mv);
  cfg.sensor_tank_sense_ohms =
      static_cast<uint16_t>(doc["sensor_tank_sense_ohms"] |
                            cfg.sensor_tank_sense_ohms);
  cfg.sensor_tank_interval_s =
      static_cast<uint16_t>(doc["sensor_tank_interval_s"] |
                            cfg.sensor_tank_interval_s);
  const char *postedFleetPassphrase = doc["fleet_passphrase"] | "";
  const bool hasFleetPassphraseField =
      !doc["fleet_passphrase"].isNull() && postedFleetPassphrase[0] != '\0';
  if (hasFleetPassphraseField) {
    cfg.fleet_passphrase = postedFleetPassphrase;
  }
  if (!doc["admin_password"].isNull()) {
    const char *newAdmin = doc["admin_password"] | "";
    const size_t newAdminLen = strlen(newAdmin);
    if (newAdminLen == 0) {
      // Redacted get_config responses include an empty admin_password field;
      // keep the current password unless a new non-empty value is supplied.
    } else if (newAdminLen < 8) {
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
      isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    return fail("fleet_passphrase_default_blocked");
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
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
  if (cfg.sensor_tank_range_mm == 0)
    cfg.sensor_tank_range_mm = 5000;
  if (cfg.sensor_tank_vref_mv == 0)
    cfg.sensor_tank_vref_mv = 3553;
  if (cfg.sensor_tank_sense_ohms == 0)
    cfg.sensor_tank_sense_ohms = 120;
  if (cfg.sensor_tank_interval_s < 5)
    cfg.sensor_tank_interval_s = 5;
  if (cfg.sensor_tank_interval_s > 3600)
    cfg.sensor_tank_interval_s = 3600;

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
                        std::function<void(bool, bool)> onApply) {
  config_ = config;
  sm_ = sm;
  on_apply_ = onApply;
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
    if (d.fw_build > 0)
      o["fw_build"] = d.fw_build;
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
  out["fleet_passphrase_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase.c_str());

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
    out["relay_feedback"] = sm_->relayFeedbackState();
    out["input_state"] = sm_->inputState();
    out["last_packet_rssi"] = sm_->lastPacketRssi();
    out["last_packet_ms"] = sm_->lastPacketMs();
    out["last_tx_ms"] = sm_->lastTxMs();
    out["peer_count"] = sm_->peerCount();
    out["local_temp_valid"] = sm_->localTemperatureValid();
    if (sm_->localTemperatureValid())
      out["local_temp_c"] = sm_->localTemperatureC();
    out["local_tank_enabled"] = sm_->localTankEnabled();
    out["local_tank_valid"] = sm_->localTankValid();
    out["local_tank_status"] = tankSensorStateText(sm_->localTankState());
    out["local_tank_depth_mm"] = sm_->localTankDepthMm();
    out["local_tank_current_centi_ma"] = sm_->localTankCurrentCentiMa();
    out["local_tank_current_ma"] =
        static_cast<float>(sm_->localTankCurrentCentiMa()) / 100.0f;
    out["local_tank_voltage_mv"] = sm_->localTankVoltageMv();
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

  const char *fleetKey = doc["fleet_passphrase"] | doc["fleet_key"] | "";
  if (strlen(fleetKey) < kMinDeploymentKeyLen ||
      isDefaultDeploymentKey(fleetKey)) {
    sendError("configure_gateway", "invalid_fleet_key", requestId(doc));
    return;
  }

  const uint8_t expected =
      clampExpectedRemotes(doc["expected_remotes"] | doc["expected_count"] | 1);
  auto &cfg = config_->settings();
  const bool preserveTargets = cfg.commissioned && cfg.role_tx &&
                               cfg.fleet_passphrase.equals(fleetKey) &&
                               cfg.paired_target_count > 0;
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
  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  if (preserveTargets) {
    cfg.remote_address = cfg.paired_target_addresses[0];
  } else {
    cfg.remote_address = kFirstRemoteAddress;
    clearAddressList(cfg.paired_target_addresses, cfg.paired_target_count);
    clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
  }
  clearAddressList(cfg.allowed_controller_addresses,
                   cfg.allowed_controller_count);
  cfg.input_control_paired_lora_enabled =
      doc["input_control_paired_lora_enabled"] | true;

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
  out["preserved_targets"] = preserveTargets;
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
  if (isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
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

void SerialAdmin::handleStartLoraInventory(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("start_lora_inventory", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("start_lora_inventory", "runtime_unavailable", id);
    return;
  }
  if (config_ == nullptr) {
    sendError("start_lora_inventory", "config_unavailable", id);
    return;
  }
  const auto &cfg = config_->settings();
  if (!cfg.commissioned) {
    sendError("start_lora_inventory", "not_commissioned", id);
    return;
  }
  if (isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    sendError("start_lora_inventory", "factory_fleet_key", id);
    return;
  }
  int start = doc["start_address"] | 1;
  int end = doc["end_address"] | Settings::kAddressListCap;
  uint16_t intervalMs = static_cast<uint16_t>(doc["interval_ms"] | 250);
  if (start < 1)
    start = 1;
  if (end > 254)
    end = 254;
  if (end < start || !sm_->fleetScanStart(static_cast<uint8_t>(start),
                                          static_cast<uint8_t>(end),
                                          intervalMs)) {
    sendError("start_lora_inventory", "start_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "start_lora_inventory";
  if (id[0] != '\0')
    out["id"] = id;
  out["start_address"] = start;
  out["end_address"] = end;
  out["interval_ms"] = intervalMs;
  sendOk(out);
}

void SerialAdmin::handleLoraInventoryStatus(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (sm_ == nullptr) {
    sendError("lora_inventory_status", "runtime_unavailable", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "lora_inventory_status";
  if (id[0] != '\0')
    out["id"] = id;
  const uint32_t now = millis();

  FleetScanSnapshot scan{};
  sm_->fleetScanSnapshot(scan);
  JsonObject s = out["scan"].to<JsonObject>();
  s["active"] = scan.active;
  s["start_address"] = scan.start_address;
  s["end_address"] = scan.end_address;
  s["next_address"] = scan.next_address;
  s["interval_ms"] = scan.interval_ms;
  s["started_ms"] = scan.started_ms;
  s["last_tx_ms"] = scan.last_tx_ms;
  s["sent"] = scan.sent;
  s["now_ms"] = now;

  JsonArray devices = out["devices"].to<JsonArray>();
  const size_t count = sm_->peerCount();
  for (size_t i = 0; i < count; ++i) {
    PeerStatusSnapshot p{};
    if (!sm_->peerByIndex(i, p))
      continue;
    JsonObject row = devices.add<JsonObject>();
    row["address"] = p.address;
    row["role"] = "remote";
    row["mode"] = "paired";
    row["wifi_enabled_known"] = p.wifi_state_known;
    row["wifi_enabled"] = p.wifi_enabled;
    row["wifi_connected_known"] = p.wifi_connected_known;
    row["wifi_connected"] = p.wifi_connected;
    if (p.wifi_connected && p.ip[0] != 0) {
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);
      row["ip"] = ipBuf;
    } else {
      row["ip"] = "";
    }
    row["mqtt_known"] = p.mqtt_state_known;
    row["mqtt_enabled"] = p.mqtt_enabled;
    row["mqtt_connected"] = p.mqtt_connected;
    if (p.chip_id != 0) {
      char chipBuf[9];
      snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(p.chip_id & 0xFFFFFFUL));
      row["chip_id"] = chipBuf;
      if (p.fw_major != 0 || p.fw_minor != 0 || p.fw_patch != 0) {
        char fwBuf[24];
        if (p.fw_build > 0) {
          snprintf(fwBuf, sizeof(fwBuf), "%u.%u.%u~%u", p.fw_major, p.fw_minor, p.fw_patch, p.fw_build);
          row["fw_build"] = p.fw_build;
        } else {
          snprintf(fwBuf, sizeof(fwBuf), "%u.%u.%u", p.fw_major, p.fw_minor, p.fw_patch);
        }
        row["fw_version"] = fwBuf;
      } else {
        row["fw_version"] = "";
      }
    } else {
      row["chip_id"] = "";
      row["fw_version"] = "";
    }
    if (p.uptime_ms > 0)
      row["uptime_ms"] = p.uptime_ms;
    if (p.last_seen_ms != 0) {
      row["relay_state"] = p.relay_state;
      row["input_state"] = p.input_state;
      if (p.temp_valid) {
        row["temp_valid"] = true;
        row["temp_c"] = p.temp_c;
      }
      if (p.tank_enabled) {
        row["tank_enabled"] = true;
        row["tank_valid"] = p.tank_valid;
        row["tank_status"] = tankSensorStateText(p.tank_state);
        if (p.tank_valid) {
          row["tank_depth_mm"] = p.tank_depth_mm;
          row["tank_current_centi_ma"] = p.tank_current_centi_ma;
          row["tank_current_ma"] = static_cast<float>(p.tank_current_centi_ma) / 100.0f;
          row["tank_voltage_mv"] = p.tank_voltage_mv;
        }
      }
    }
    if (p.maintenance_debug_known) {
      row["maintenance_debug_known"] = true;
      row["heap_free"] = p.heap_free;
      row["heap_max_block"] = p.heap_max_block;
      row["heap_frag_pct"] = p.heap_frag_pct;
      row["relay_feedback"] = p.relay_feedback;
      row["input_feedback"] = p.input_feedback;
      row["debug_uptime_ms"] = p.debug_uptime_ms;
    }
    row["rssi"] = p.uplink_rssi;
    if (p.downlink_rssi_valid) {
      row["downlink_rssi_known"] = true;
      row["downlink_rssi"] = p.downlink_rssi;
    }
    if (p.last_seen_ms != 0) {
      row["last_seen_ms"] = p.last_seen_ms;
      row["age_ms"] = now - p.last_seen_ms;
    }
    if (p.poll_pending)
      row["poll_pending"] = true;
    const bool otaEligible = p.wifi_connected_known && p.wifi_connected &&
                             p.ip[0] != 0;
    row["ota_eligible"] = otaEligible;
    if (otaEligible) {
      row["ota_reason"] = "ready";
    } else if (p.wifi_connected_known) {
      row["ota_reason"] = p.wifi_connected ? "ip_missing" : "wifi_offline";
    } else {
      row["ota_reason"] = "wifi_status_unknown";
    }
  }
  sendOk(out);
}

void SerialAdmin::handleCancelLoraInventory(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("cancel_lora_inventory", "auth_failed", id);
    return;
  }
  if (sm_ != nullptr)
    sm_->fleetScanCancel();
  JsonDocument out;
  out["cmd"] = "cancel_lora_inventory";
  if (id[0] != '\0')
    out["id"] = id;
  sendOk(out);
}

void SerialAdmin::handleUdpLogControl(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("udp_log_control", "auth_failed", id);
    return;
  }
  const bool enabled = parseBoolField(doc["enabled"], true);
  uint32_t ttlS = doc["ttl_s"] | 300UL;
  if (ttlS > 3600UL)
    ttlS = 3600UL;

  IPAddress host;
  const uint16_t port = static_cast<uint16_t>(doc["port"] | 5514);
  const char *hostStr = doc["host"] | "";
  if (enabled && (port == 0 || !host.fromString(hostStr))) {
    sendError("udp_log_control", "invalid_target", id);
    return;
  }

  if (enabled) {
    lrslog::setUdpMirror(host, port, ttlS * 1000UL);
  } else {
    lrslog::disableUdpMirror();
  }

  JsonDocument out;
  out["cmd"] = "udp_log_control";
  if (id[0] != '\0')
    out["id"] = id;
  out["enabled"] = enabled;
  out["host"] = enabled ? host.toString() : "";
  out["port"] = enabled ? port : 0;
  out["ttl_s"] = enabled ? ttlS : 0;
  sendOk(out);
}

void SerialAdmin::handleRemoteUdpLogControl(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_udp_log_control", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_udp_log_control", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_udp_log_control", "invalid_address", id);
    return;
  }
  const bool enabled = parseBoolField(doc["enabled"], true);
  uint32_t ttlS = doc["ttl_s"] | 300UL;
  if (ttlS > 3600UL)
    ttlS = 3600UL;

  IPAddress host;
  const uint16_t port = static_cast<uint16_t>(doc["port"] | 5514);
  const char *hostStr = doc["host"] | "";
  if (enabled && (port == 0 || !host.fromString(hostStr))) {
    sendError("remote_udp_log_control", "invalid_target", id);
    return;
  }

  if (!sm_->mqttSetPeerUdpLogControl(static_cast<uint8_t>(rawAddr), enabled, host, port, ttlS)) {
    sendError("remote_udp_log_control", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_udp_log_control";
  if (id[0] != '\0')
    out["id"] = id;
  out["addr"] = rawAddr;
  out["enabled"] = enabled;
  out["host"] = enabled ? host.toString() : "";
  out["port"] = enabled ? port : 0;
  out["ttl_s"] = enabled ? ttlS : 0;
  out["requires_remote_wifi"] = enabled;
  sendOk(out);
}

void SerialAdmin::handleRemoteOtaPull(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_ota_pull", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_ota_pull", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_ota_pull", "invalid_address", id);
    return;
  }

  IPAddress host;
  const uint16_t port = static_cast<uint16_t>(doc["port"] | 0);
  const char *hostStr = doc["host"] | "";
  if (port == 0 || !host.fromString(hostStr)) {
    sendError("remote_ota_pull", "invalid_target", id);
    return;
  }

  const char *sha256 = doc["sha256"] | "";
  if (sha256[0] == '\0') {
    sendError("remote_ota_pull", "sha256_required", id);
    return;
  }
  if (!isSha256Hex(sha256)) {
    sendError("remote_ota_pull", "invalid_sha256", id);
    return;
  }

  if (sm_->isOtaPullTxActive()) {
    sendError("remote_ota_pull", "gateway_busy", id);
    return;
  }

  if (!sm_->sendPeerOtaPullControl(static_cast<uint8_t>(rawAddr), host, port, sha256)) {
    sendError("remote_ota_pull", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_ota_pull";
  if (id[0] != '\0')
    out["id"] = id;
  out["addr"] = rawAddr;
  out["host"] = host.toString();
  out["port"] = port;
  out["path"] = "/firmware.bin";
  out["sha256"] = sha256;
  sendOk(out);
}

void SerialAdmin::handleOtaPull(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("ota_pull", "auth_failed", id);
    return;
  }
  const char *url = doc["url"] | "";
  const char *sha256 = doc["sha256"] | "";
  String error;
  if (!otaPullFromUrl(url, sha256, error)) {
    sendError("ota_pull", error.c_str(), id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "ota_pull";
  if (id[0] != '\0')
    out["id"] = id;
  out["rebooting"] = true;
  sendOk(out);
  delay(150);
  ESP.restart();
}

void SerialAdmin::handleRemoteReboot(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_reboot", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_reboot", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_reboot", "invalid_address", id);
    return;
  }

  if (!sm_->sendPeerReboot(static_cast<uint8_t>(rawAddr))) {
    sendError("remote_reboot", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_reboot";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  sendOk(out);
}

void SerialAdmin::handleRemoteSensorConfig(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_sensor_config", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_sensor_config", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_sensor_config", "invalid_address", id);
    return;
  }

  if (doc["sensor_temp_enabled"].isNull() || doc["sensor_tank_enabled"].isNull()) {
    sendError("remote_sensor_config", "missing_parameters", id);
    return;
  }

  const bool tempEnabled = doc["sensor_temp_enabled"] | false;
  const bool tankEnabled = doc["sensor_tank_enabled"] | false;

  if (!sm_->sendPeerSensorConfig(static_cast<uint8_t>(rawAddr), tempEnabled, tankEnabled)) {
    sendError("remote_sensor_config", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_sensor_config";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  out["sensor_temp_enabled"] = tempEnabled;
  out["sensor_tank_enabled"] = tankEnabled;
  sendOk(out);
}

void SerialAdmin::handleRemoteFleetKeyChange(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_fleet_key_change", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_fleet_key_change", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_fleet_key_change", "invalid_address", id);
    return;
  }

  const char *newKey = doc["fleet_passphrase"] | doc["new_fleet_passphrase"] | doc["fleet_key"] | "";
  const size_t keyLen = strlen(newKey);
  if (keyLen < kMinDeploymentKeyLen) {
    sendError("remote_fleet_key_change", "fleet_passphrase_too_short", id);
    return;
  }
  if (keyLen > 64) {
    sendError("remote_fleet_key_change", "fleet_passphrase_too_long", id);
    return;
  }
  if (isDefaultDeploymentKey(newKey)) {
    sendError("remote_fleet_key_change", "fleet_passphrase_default_blocked", id);
    return;
  }

  if (!sm_->sendPeerFleetKeyChange(static_cast<uint8_t>(rawAddr), String(newKey))) {
    sendError("remote_fleet_key_change", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_fleet_key_change";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  sendOk(out);
}

void SerialAdmin::handleRemoteFactoryReset(JsonDocument &doc) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_factory_reset", "auth_failed", id);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_factory_reset", "runtime_unavailable", id);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < 1 || rawAddr > 254) {
    sendError("remote_factory_reset", "invalid_address", id);
    return;
  }

  const bool keepFleetKey = doc["keep_shared_fleet_key"] | doc["keep_fleet_key"] | false;
  const bool keepWifi = doc["keep_wifi_credentials"] | doc["keep_wifi"] | false;

  if (!sm_->sendPeerFactoryReset(static_cast<uint8_t>(rawAddr), keepFleetKey, keepWifi)) {
    sendError("remote_factory_reset", "send_failed", id);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_factory_reset";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  out["keep_shared_fleet_key"] = keepFleetKey;
  out["keep_wifi_credentials"] = keepWifi;
  sendOk(out);
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

  if (strcmp(cmd, "start_lora_inventory") == 0) {
    handleStartLoraInventory(doc);
    return;
  }

  if (strcmp(cmd, "lora_inventory_status") == 0) {
    handleLoraInventoryStatus(doc);
    return;
  }

  if (strcmp(cmd, "cancel_lora_inventory") == 0) {
    handleCancelLoraInventory(doc);
    return;
  }

  if (strcmp(cmd, "udp_log_control") == 0) {
    handleUdpLogControl(doc);
    return;
  }

  if (strcmp(cmd, "remote_udp_log_control") == 0) {
    handleRemoteUdpLogControl(doc);
    return;
  }

  if (strcmp(cmd, "remote_ota_pull") == 0) {
    handleRemoteOtaPull(doc);
    return;
  }

  if (strcmp(cmd, "remote_reboot") == 0) {
    handleRemoteReboot(doc);
    return;
  }

  if (strcmp(cmd, "remote_sensor_config") == 0) {
    handleRemoteSensorConfig(doc);
    return;
  }

  if (strcmp(cmd, "remote_fleet_key_change") == 0) {
    handleRemoteFleetKeyChange(doc);
    return;
  }

  if (strcmp(cmd, "remote_factory_reset") == 0) {
    handleRemoteFactoryReset(doc);
    return;
  }

  if (strcmp(cmd, "ota_pull") == 0) {
    handleOtaPull(doc);
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
    bool used[256]{};
    used[0] = true;
    used[255] = true;
    used[cfg.local_address] = true;
    uint8_t targetAddresses[Settings::kAddressListCap]{};
    uint8_t targetCount = 0;
    for (JsonVariantConst v : arr) {
      const int raw = v.as<int>();
      if (raw < 1 || raw > 254 || used[raw]) {
        sendError(cmd, "invalid_addresses", id);
        return;
      }
      if (targetCount >= Settings::kAddressListCap) {
        sendError(cmd, "too_many_targets", id);
        return;
      }
      used[raw] = true;
      targetAddresses[targetCount++] = static_cast<uint8_t>(raw);
    }
    if (targetCount == 0) {
      sendError(cmd, "invalid_addresses", id);
      return;
    }
    clearAddressList(cfg.paired_target_addresses, cfg.paired_target_count);
    clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
    for (uint8_t i = 0; i < targetCount; ++i) {
      const uint8_t addr = targetAddresses[i];
      cfg.paired_target_addresses[cfg.paired_target_count++] = addr;
      cfg.known_peer_addresses[cfg.known_peer_count++] = addr;
    }
    cfg.remote_address = cfg.paired_target_addresses[0];
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
