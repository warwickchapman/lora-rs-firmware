#include "config_store.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <SHA256.h>
#include <string.h>

#include "logger.h"
#include "runtime_utils.h"

namespace {
constexpr char kConfigPath[] = "/config.json";
constexpr char kConfigTmpPath[] = "/config.tmp";
constexpr char kPostOtaActionPath[] = "/post_ota_action.json";
constexpr char kPostOtaActionTmpPath[] = "/post_ota_action.tmp";
constexpr size_t kConfigMaxBytes = 8192;
constexpr size_t kPostOtaActionMaxBytes = 256;
constexpr uint16_t kConfigSchemaVersion = 3;
constexpr char kProductSecret[] = "LRS-v1-rotate-this-secret";
constexpr char kDefaultDeploymentKey[] = "lora-default-passphrase";
constexpr char kModeStandalone[] = "standalone";
constexpr char kModePaired[] = "paired";
constexpr char kRoleNone[] = "none";
constexpr char kRoleTransmitter[] = "transmitter";
constexpr char kRoleReceiver[] = "receiver";
#ifdef REGION_US
constexpr long kLockedLoraFrequencyHz = 915000000L;
constexpr float kDefaultWifiTxPowerDbm = 19.37f;
#else
constexpr long kLockedLoraFrequencyHz = 433000000L;
constexpr float kDefaultWifiTxPowerDbm = 20.5f;
#endif

constexpr const char *kAllowedFields[] = {
    "schema_version",
    "commissioned",
    "mode",
    "role",
    "local_address",
    "remote_address",
    "paired_target_addresses",
    "allowed_controller_addresses",
    "known_peer_addresses",
    "known_peer_chip_ids",
    "lora_frequency_hz",
    "lora_tx_power",
    "lora_spreading_factor",
    "lora_bandwidth_hz",
    "lora_coding_rate",
    "heartbeat_ms",
    "heartbeat_enabled",
    "ack_timeout_ms",
    "mqtt_remote_retry_timeout_ms",
    "tx_mqtt_remote_polling_enabled",
    "tx_mqtt_remote_default_poll_interval_ms",
    "maintenance_debug_telemetry_enabled",
    "rx_push_on_change_enabled",
    "rx_push_min_interval_ms",
    "input_control_paired_lora_enabled",
    "tx_command_retry_timeout_ms",
    "rx_failsafe_mode",
    "rx_failsafe_timeout_ms",
    "wifi_sta_ssid",
    "wifi_sta_password",
    "lan_hostname",
    "ap_always_on",
    "wifi_phy_mode",
    "wifi_tx_power_dbm",
    "wifi_sleep_enabled",
    "wifi_static_ip_enabled",
    "wifi_static_ip",
    "wifi_static_gateway",
    "wifi_static_subnet",
    "wifi_channel_override",
    "wifi_ap_fallback_policy",
    "wifi_admin_enabled",
    "power_save_listen_only",
    "power_save_boot_grace",
    "mqtt_client_enabled",
    "mqtt_control_enabled",
    "mqtt_controller_addresses",
    "mqtt_host",
    "mqtt_port",
    "mqtt_user",
    "mqtt_password",
    "mqtt_topic_root",
    "sensor_temp_enabled",
    "sensor_temp_pin",
    "sensor_temp_interval_s",
    "sensor_tank_enabled",
    "sensor_tank_range_mm",
    "sensor_tank_vref_mv",
    "sensor_tank_sense_ohms",
    "sensor_tank_interval_s",
    "fleet_passphrase",
    "fleet_setup_prompt_dismissed",
    "admin_password",
    "factory_serial",
};
constexpr size_t kAllowedFieldCount = sizeof(kAllowedFields) / sizeof(kAllowedFields[0]);
constexpr const char *kDeprecatedAcceptedFields[] = {
    "audit_last_saved_by",
    "audit_last_saved_ms",
    "audit_last_reboot_reason",
    "audit_last_reboot_ms",
    "audit_boot_count",
};
constexpr size_t kDeprecatedAcceptedFieldCount = sizeof(kDeprecatedAcceptedFields) / sizeof(kDeprecatedAcceptedFields[0]);

int monthFromShort(const String &m) {
  if (m == "Jan") return 1;
  if (m == "Feb") return 2;
  if (m == "Mar") return 3;
  if (m == "Apr") return 4;
  if (m == "May") return 5;
  if (m == "Jun") return 6;
  if (m == "Jul") return 7;
  if (m == "Aug") return 8;
  if (m == "Sep") return 9;
  if (m == "Oct") return 10;
  if (m == "Nov") return 11;
  return 12;
}

int dayOfYear(int y, int m, int d) {
  static const int kCumulative[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int doy = kCumulative[m - 1] + d;
  const bool leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
  if (leap && m > 2) {
    doy += 1;
  }
  return doy;
}

void compileWeekStamp(char out[5]) {
  const String date = __DATE__;
  const int mon = monthFromShort(date.substring(0, 3));
  const int day = date.substring(4, 6).toInt();
  const int year = date.substring(7, 11).toInt();

  const unsigned yy = static_cast<unsigned>(year % 100);
  const unsigned ww = static_cast<unsigned>(((dayOfYear(year, mon, day) - 1) / 7) + 1);

  snprintf(out, 5, "%02u%02u", yy, ww);
}

String deriveShortPassword(const String &chip) {
  uint8_t digest[32];
  SHA256 hash;
  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(kProductSecret), strlen(kProductSecret));
  hash.update(reinterpret_cast<const uint8_t *>(":"), 1);
  hash.update(reinterpret_cast<const uint8_t *>(chip.c_str()), chip.length());
  hash.finalize(digest, sizeof(digest));

  char out[9];
  for (size_t i = 0; i < 4; i++) {
    snprintf(out + (i * 2), 3, "%02x", digest[i]);
  }
  out[8] = '\0';
  return String(out);
}

bool isAllowedConfigKey(const char *key) {
  if (key == nullptr || key[0] == '\0') return false;
  for (size_t i = 0; i < kAllowedFieldCount; ++i) {
    if (strcmp(key, kAllowedFields[i]) == 0) return true;
  }
  for (size_t i = 0; i < kDeprecatedAcceptedFieldCount; ++i) {
    if (strcmp(key, kDeprecatedAcceptedFields[i]) == 0) return true;
  }
  return false;
}

uint8_t parseAddressList(JsonVariantConst src, uint8_t *out, uint8_t cap) {
  if (out == nullptr || cap == 0) return 0;
  for (uint8_t i = 0; i < cap; ++i) out[i] = 0;
  if (src.isNull()) return 0;
  if (!src.is<JsonArrayConst>()) return 0;
  JsonArrayConst arr = src.as<JsonArrayConst>();
  uint8_t count = 0;
  for (JsonVariantConst v : arr) {
    if (!v.is<uint8_t>() && !v.is<unsigned int>() && !v.is<int>()) continue;
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

void writeAddressList(JsonDocument &doc, const char *key, const uint8_t *values, uint8_t count, uint8_t cap) {
  JsonArray arr = doc[key].to<JsonArray>();
  if (values == nullptr || cap == 0) return;
  if (count > cap) count = cap;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t addr = values[i];
    if (addr < 1 || addr > 254) continue;
    arr.add(addr);
  }
}

char readFirstNonWhitespace(File &f) {
  while (f.available()) {
    const int c = f.read();
    if (c < 0) break;
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      return static_cast<char>(c);
    }
  }
  return '\0';
}

}  // namespace

bool ConfigStore::begin() {
  if (!LittleFS.begin()) {
    LRS_LOGE(FS, "event=fs_mount_failed path=%s", kConfigPath);
    return false;
  }

  setDefaults();
  (void)chipIdHex();

  if (!LittleFS.exists(kConfigPath)) {
    LRS_LOGW(FS, "event=config_missing path=%s action=write_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }

  File f = LittleFS.open(kConfigPath, "r");
  if (!f) {
    LRS_LOGE(FS, "event=config_open_failed path=%s mode=r action=write_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }

  const size_t fileSize = static_cast<size_t>(f.size());
  if (fileSize == 0 || fileSize > kConfigMaxBytes) {
    f.close();
    LRS_LOGW(FS,
             "event=config_invalid path=%s reason=size_invalid bytes=%lu action=reset_defaults",
             kConfigPath,
             static_cast<unsigned long>(fileSize));
    ensureProvisionedDefaults();
    return save();
  }
  const char firstChar = readFirstNonWhitespace(f);
  f.seek(0, SeekSet);
  if (firstChar != '{') {
    f.close();
    LRS_LOGW(FS, "event=config_invalid path=%s reason=not_json_object action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }

  size_t docCapacity = fileSize + 512;
  if (docCapacity < 2048) {
    docCapacity = 2048;
  }
  JsonDocument doc;
  auto err = deserializeJson(doc, f);
  f.close();
  if (err) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=parse_failed err=%s bytes=%lu action=reset_defaults", kConfigPath, err.c_str(),
             static_cast<unsigned long>(fileSize));
    ensureProvisionedDefaults();
    return save();
  }

  JsonObject root = doc.as<JsonObject>();
  if (root.isNull()) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=root_not_object action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }

  bool needs_save = false;

  // 1. Remove/Warn deprecated/unknown fields
  for (JsonPair kv : root) {
    if (!isAllowedConfigKey(kv.key().c_str())) {
      LRS_LOGI(FS, "event=config_migration info=removing_deprecated_field field=%s", kv.key().c_str());
      needs_save = true;
    }
  }

  // 2. Check for newly introduced/missing fields
  for (size_t i = 0; i < kAllowedFieldCount; ++i) {
    if (!root.containsKey(kAllowedFields[i])) {
      LRS_LOGI(FS, "event=config_migration info=adding_missing_field field=%s", kAllowedFields[i]);
      needs_save = true;
    }
  }

  cfg_.schema_version = root["schema_version"] | 0;
  if (cfg_.schema_version != kConfigSchemaVersion) {
    LRS_LOGI(FS, "event=config_migration info=schema_updated got=%u expected=%u",
             static_cast<unsigned>(cfg_.schema_version), static_cast<unsigned>(kConfigSchemaVersion));
    cfg_.schema_version = kConfigSchemaVersion;
    needs_save = true;
  }
  cfg_.commissioned = root["commissioned"] | false;
  cfg_.mode = root["mode"] | "";
  cfg_.role = root["role"] | "";
  if (!runtime_utils::parseRoleTxFromModeRole(String(cfg_.mode.c_str()),
                                              String(cfg_.role.c_str()),
                                              cfg_.role_tx)) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=mode_role_invalid mode=%s role=%s action=reset_defaults", kConfigPath,
             cfg_.mode.c_str(), cfg_.role.c_str());
    ensureProvisionedDefaults();
    return save();
  }

  cfg_.local_address = root["local_address"] | 1;
  cfg_.remote_address = root["remote_address"] | 2;
  cfg_.paired_target_count = parseAddressList(root["paired_target_addresses"], cfg_.paired_target_addresses, Settings::kAddressListCap);
  cfg_.allowed_controller_count =
      parseAddressList(root["allowed_controller_addresses"], cfg_.allowed_controller_addresses, Settings::kAddressListCap);
  cfg_.known_peer_count = parseAddressList(root["known_peer_addresses"], cfg_.known_peer_addresses, Settings::kAddressListCap);
  memset(cfg_.known_peer_chip_ids, 0, sizeof(cfg_.known_peer_chip_ids));
  if (root.containsKey("known_peer_chip_ids")) {
    JsonArrayConst arr = root["known_peer_chip_ids"].as<JsonArrayConst>();
    size_t idx = 0;
    for (JsonVariantConst v : arr) {
      if (idx >= Settings::kAddressListCap) break;
      cfg_.known_peer_chip_ids[idx++] = v.as<uint32_t>();
    }
  }

  cfg_.lora_frequency_hz = root["lora_frequency_hz"] | kLockedLoraFrequencyHz;
  cfg_.lora_frequency_hz = kLockedLoraFrequencyHz;
  cfg_.lora_tx_power = root["lora_tx_power"] | 17;
  cfg_.lora_spreading_factor = root["lora_spreading_factor"] | 7;
  cfg_.lora_bandwidth_hz = root["lora_bandwidth_hz"] | 125000L;
  cfg_.lora_coding_rate = root["lora_coding_rate"] | 5;

  cfg_.heartbeat_ms = root["heartbeat_ms"] | 60000;
  cfg_.heartbeat_enabled = root["heartbeat_enabled"] | true;
  cfg_.ack_timeout_ms = root["ack_timeout_ms"] | 5000;
  cfg_.mqtt_remote_retry_timeout_ms = root["mqtt_remote_retry_timeout_ms"] | 300000;
  cfg_.tx_mqtt_remote_polling_enabled = root["tx_mqtt_remote_polling_enabled"] | false;
  cfg_.tx_mqtt_remote_default_poll_interval_ms = root["tx_mqtt_remote_default_poll_interval_ms"] | 60000;
  cfg_.maintenance_debug_telemetry_enabled = root["maintenance_debug_telemetry_enabled"] | false;
  cfg_.rx_push_on_change_enabled = root["rx_push_on_change_enabled"] | false;
  cfg_.rx_push_min_interval_ms = root["rx_push_min_interval_ms"] | 60000;
  cfg_.input_control_paired_lora_enabled = root["input_control_paired_lora_enabled"] | false;
  cfg_.tx_command_retry_timeout_ms = root["tx_command_retry_timeout_ms"] | 180000;
  cfg_.rx_failsafe_mode = root["rx_failsafe_mode"] | "hold_last";
  cfg_.rx_failsafe_timeout_ms = root["rx_failsafe_timeout_ms"] | 180000;

  cfg_.wifi_sta_ssid = root["wifi_sta_ssid"] | "";
  cfg_.wifi_sta_password = root["wifi_sta_password"] | "";
  cfg_.lan_hostname = root["lan_hostname"] | "";
  cfg_.ap_always_on = root["ap_always_on"] | true;
  cfg_.wifi_phy_mode = root["wifi_phy_mode"] | "11b";
  cfg_.wifi_tx_power_dbm = root["wifi_tx_power_dbm"] | kDefaultWifiTxPowerDbm;
  cfg_.wifi_sleep_enabled = root["wifi_sleep_enabled"] | false;
  cfg_.wifi_static_ip_enabled = root["wifi_static_ip_enabled"] | false;
  cfg_.wifi_static_ip = root["wifi_static_ip"] | "";
  cfg_.wifi_static_gateway = root["wifi_static_gateway"] | "";
  cfg_.wifi_static_subnet = root["wifi_static_subnet"] | "255.255.255.0";
  cfg_.wifi_channel_override = static_cast<uint8_t>(root["wifi_channel_override"] | 0);
  cfg_.wifi_ap_fallback_policy = root["wifi_ap_fallback_policy"] | "fallback_on_disconnect";
  cfg_.wifi_admin_enabled = root["wifi_admin_enabled"] | true;
  cfg_.power_save_listen_only = root["power_save_listen_only"] | false;
  cfg_.power_save_boot_grace = root["power_save_boot_grace"] | true;
  cfg_.mqtt_client_enabled = root["mqtt_client_enabled"] | false;
  cfg_.mqtt_control_enabled = root["mqtt_control_enabled"] | false;
  cfg_.mqtt_controller_addresses = root["mqtt_controller_addresses"] | "";
  cfg_.mqtt_host = root["mqtt_host"] | "venus.local";
  cfg_.mqtt_port = root["mqtt_port"] | 1883;
  cfg_.mqtt_user = root["mqtt_user"] | "";
  cfg_.mqtt_password = root["mqtt_password"] | "";
  cfg_.mqtt_topic_root = root["mqtt_topic_root"] | "lora";
  cfg_.sensor_temp_enabled = root["sensor_temp_enabled"] | false;
  cfg_.sensor_temp_pin = root["sensor_temp_pin"] | 0;
  cfg_.sensor_temp_interval_s = root["sensor_temp_interval_s"] | 10;
  cfg_.sensor_tank_enabled = root["sensor_tank_enabled"] | false;
  cfg_.sensor_tank_range_mm = root["sensor_tank_range_mm"] | 5000;
  cfg_.sensor_tank_vref_mv = root["sensor_tank_vref_mv"] | 3553;
  cfg_.sensor_tank_sense_ohms = root["sensor_tank_sense_ohms"] | 120;
  cfg_.sensor_tank_interval_s = root["sensor_tank_interval_s"] | 5;

  cfg_.fleet_passphrase = root["fleet_passphrase"] | "lora-default-passphrase";
  cfg_.fleet_setup_prompt_dismissed = root["fleet_setup_prompt_dismissed"] | false;
  cfg_.admin_password = root["admin_password"] | "";
  cfg_.factory_serial = root["factory_serial"] | "";

  if (cfg_.local_address < 1 || cfg_.local_address > 254 || cfg_.remote_address < 1 || cfg_.remote_address > 254 ||
      cfg_.local_address == cfg_.remote_address) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=address_range action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }
  if (cfg_.paired_target_count == 0 && cfg_.remote_address >= 1 && cfg_.remote_address <= 254) {
    cfg_.paired_target_count = 1;
    cfg_.paired_target_addresses[0] = cfg_.remote_address;
  } else if (cfg_.paired_target_count > 0) {
    cfg_.paired_target_addresses[0] = cfg_.remote_address;
  }
  if (cfg_.allowed_controller_count == 0 && cfg_.remote_address >= 1 && cfg_.remote_address <= 254) {
    cfg_.allowed_controller_count = 1;
    cfg_.allowed_controller_addresses[0] = cfg_.remote_address;
  } else if (cfg_.allowed_controller_count > 0) {
    cfg_.allowed_controller_addresses[0] = cfg_.remote_address;
  }
  if (cfg_.mqtt_control_enabled && !cfg_.mqtt_client_enabled) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=mqtt_control_requires_client action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }
  if (cfg_.input_control_paired_lora_enabled && (cfg_.mode != kModePaired || !cfg_.role_tx)) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=paired_input_requires_paired_tx action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }
  if (cfg_.heartbeat_ms < 60000UL) cfg_.heartbeat_ms = 60000UL;
  if (cfg_.heartbeat_ms > 3600000UL) cfg_.heartbeat_ms = 3600000UL;
  if (cfg_.tx_command_retry_timeout_ms < 5000UL) cfg_.tx_command_retry_timeout_ms = 5000UL;
  if (cfg_.tx_command_retry_timeout_ms > 3600000UL) cfg_.tx_command_retry_timeout_ms = 3600000UL;
  cfg_.rx_failsafe_mode.trim();
  cfg_.rx_failsafe_mode.toLowerCase();
  if (cfg_.rx_failsafe_mode != "hold_last" && cfg_.rx_failsafe_mode != "force_off" && cfg_.rx_failsafe_mode != "force_on") {
    cfg_.rx_failsafe_mode = "hold_last";
  }
  if (cfg_.rx_failsafe_timeout_ms < 5000UL) cfg_.rx_failsafe_timeout_ms = 5000UL;
  if (cfg_.rx_failsafe_timeout_ms > 3600000UL) cfg_.rx_failsafe_timeout_ms = 3600000UL;
  if (cfg_.admin_password.length() < 8 || cfg_.factory_serial.length() < 4) {
    LRS_LOGW(FS, "event=config_invalid path=%s reason=identity_fields_invalid action=reset_defaults", kConfigPath);
    ensureProvisionedDefaults();
    return save();
  }
  cfg_.wifi_phy_mode.trim();
  cfg_.wifi_phy_mode.toLowerCase();
  if (cfg_.wifi_phy_mode != "11b" && cfg_.wifi_phy_mode != "11g" && cfg_.wifi_phy_mode != "11n") {
    cfg_.wifi_phy_mode = "11b";
  }
  if (cfg_.wifi_tx_power_dbm < 0.0f) cfg_.wifi_tx_power_dbm = 0.0f;
  if (cfg_.wifi_tx_power_dbm > kDefaultWifiTxPowerDbm) cfg_.wifi_tx_power_dbm = kDefaultWifiTxPowerDbm;
#ifdef REGION_US
  if (cfg_.wifi_channel_override > 11) cfg_.wifi_channel_override = 0;
#else
  if (cfg_.wifi_channel_override > 13) cfg_.wifi_channel_override = 0;
#endif
  cfg_.wifi_ap_fallback_policy.trim();
  cfg_.wifi_ap_fallback_policy.toLowerCase();
  if (cfg_.wifi_ap_fallback_policy != "fallback_on_disconnect" && cfg_.wifi_ap_fallback_policy != "secure_sta_only") {
    cfg_.wifi_ap_fallback_policy = "fallback_on_disconnect";
  }

  LRS_LOGI(FS,
           "event=config_loaded path=%s schema_version=%u commissioned=%u role=%s local=%u remote=%u wifi_ssid=%s fleet_key=%s",
           kConfigPath,
           static_cast<unsigned>(cfg_.schema_version),
           cfg_.commissioned ? 1U : 0U,
           cfg_.role_tx ? "tx" : "rx",
           static_cast<unsigned>(cfg_.local_address),
           static_cast<unsigned>(cfg_.remote_address),
           cfg_.wifi_sta_ssid.c_str(),
           lrslog::maskSecret(String(cfg_.fleet_passphrase.c_str())).c_str());
  if (needs_save) {
    LRS_LOGI(FS, "event=config_healed action=saving_clean_config");
    save();
  }
  return true;
}

Settings &ConfigStore::settings() { return cfg_; }
const Settings &ConfigStore::settings() const { return cfg_; }

bool ConfigStore::save() {
  JsonDocument doc;
  doc["schema_version"] = cfg_.schema_version;
  doc["commissioned"] = cfg_.commissioned;
  doc["mode"] = cfg_.mode;
  doc["role"] = cfg_.role;
  doc["local_address"] = cfg_.local_address;
  doc["remote_address"] = cfg_.remote_address;
  writeAddressList(doc, "paired_target_addresses", cfg_.paired_target_addresses, cfg_.paired_target_count, Settings::kAddressListCap);
  writeAddressList(doc, "allowed_controller_addresses", cfg_.allowed_controller_addresses, cfg_.allowed_controller_count,
                   Settings::kAddressListCap);
  writeAddressList(doc, "known_peer_addresses", cfg_.known_peer_addresses, cfg_.known_peer_count, Settings::kAddressListCap);
  JsonArray chipIdsArr = doc["known_peer_chip_ids"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg_.known_peer_count; ++i) {
    chipIdsArr.add(cfg_.known_peer_chip_ids[i]);
  }

  doc["lora_frequency_hz"] = cfg_.lora_frequency_hz;
  doc["lora_tx_power"] = cfg_.lora_tx_power;
  doc["lora_spreading_factor"] = cfg_.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg_.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg_.lora_coding_rate;

  doc["heartbeat_ms"] = cfg_.heartbeat_ms;
  doc["heartbeat_enabled"] = cfg_.heartbeat_enabled;
  doc["ack_timeout_ms"] = cfg_.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg_.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg_.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg_.tx_mqtt_remote_default_poll_interval_ms;
  doc["maintenance_debug_telemetry_enabled"] = cfg_.maintenance_debug_telemetry_enabled;
  doc["rx_push_on_change_enabled"] = cfg_.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg_.rx_push_min_interval_ms;
  doc["input_control_paired_lora_enabled"] = cfg_.input_control_paired_lora_enabled;
  doc["tx_command_retry_timeout_ms"] = cfg_.tx_command_retry_timeout_ms;
  doc["rx_failsafe_mode"] = cfg_.rx_failsafe_mode;
  doc["rx_failsafe_timeout_ms"] = cfg_.rx_failsafe_timeout_ms;

  doc["wifi_sta_ssid"] = cfg_.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg_.wifi_sta_password;
  doc["lan_hostname"] = cfg_.lan_hostname;
  doc["ap_always_on"] = cfg_.ap_always_on;
  doc["wifi_phy_mode"] = cfg_.wifi_phy_mode;
  doc["wifi_tx_power_dbm"] = cfg_.wifi_tx_power_dbm;
  doc["wifi_sleep_enabled"] = cfg_.wifi_sleep_enabled;
  doc["wifi_static_ip_enabled"] = cfg_.wifi_static_ip_enabled;
  doc["wifi_static_ip"] = cfg_.wifi_static_ip;
  doc["wifi_static_gateway"] = cfg_.wifi_static_gateway;
  doc["wifi_static_subnet"] = cfg_.wifi_static_subnet;
  doc["wifi_channel_override"] = cfg_.wifi_channel_override;
  doc["wifi_ap_fallback_policy"] = cfg_.wifi_ap_fallback_policy;
  doc["wifi_admin_enabled"] = cfg_.wifi_admin_enabled;
  doc["power_save_listen_only"] = cfg_.power_save_listen_only;
  doc["power_save_boot_grace"] = cfg_.power_save_boot_grace;
  doc["mqtt_client_enabled"] = cfg_.mqtt_client_enabled;
  doc["mqtt_control_enabled"] = cfg_.mqtt_control_enabled;
  doc["mqtt_controller_addresses"] = cfg_.mqtt_controller_addresses;
  doc["mqtt_host"] = cfg_.mqtt_host;
  doc["mqtt_port"] = cfg_.mqtt_port;
  doc["mqtt_user"] = cfg_.mqtt_user;
  doc["mqtt_password"] = cfg_.mqtt_password;
  doc["mqtt_topic_root"] = cfg_.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg_.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg_.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg_.sensor_temp_interval_s;
  doc["sensor_tank_enabled"] = cfg_.sensor_tank_enabled;
  doc["sensor_tank_range_mm"] = cfg_.sensor_tank_range_mm;
  doc["sensor_tank_vref_mv"] = cfg_.sensor_tank_vref_mv;
  doc["sensor_tank_sense_ohms"] = cfg_.sensor_tank_sense_ohms;
  doc["sensor_tank_interval_s"] = cfg_.sensor_tank_interval_s;

  doc["fleet_passphrase"] = cfg_.fleet_passphrase;
  doc["fleet_setup_prompt_dismissed"] = cfg_.fleet_setup_prompt_dismissed;
  doc["admin_password"] = cfg_.admin_password;
  doc["factory_serial"] = cfg_.factory_serial;

  const size_t estimatedBytes = measureJson(doc);
  if (estimatedBytes == 0 || estimatedBytes > kConfigMaxBytes) {
    LRS_LOGE(FS,
             "event=config_save_failed path=%s reason=size_invalid estimated=%lu limit=%lu",
             kConfigPath,
             static_cast<unsigned long>(estimatedBytes),
             static_cast<unsigned long>(kConfigMaxBytes));
    return false;
  }

  LittleFS.remove(kConfigTmpPath);
  File f = LittleFS.open(kConfigTmpPath, "w");
  if (!f) {
    LRS_LOGE(FS, "event=config_save_failed path=%s reason=open_tmp_failed", kConfigTmpPath);
    return false;
  }
  const size_t bytes = serializeJson(doc, f);
  f.flush();
  f.close();
  const bool ok = (bytes == estimatedBytes);
  if (!ok) {
    LittleFS.remove(kConfigTmpPath);
    LRS_LOGE(FS,
             "event=config_save_failed path=%s reason=serialize_mismatch estimated=%lu actual=%lu",
             kConfigPath,
             static_cast<unsigned long>(estimatedBytes),
             static_cast<unsigned long>(bytes));
    return false;
  }

  LittleFS.remove(kConfigPath);
  if (!LittleFS.rename(kConfigTmpPath, kConfigPath)) {
    LittleFS.remove(kConfigTmpPath);
    LRS_LOGE(FS, "event=config_save_failed path=%s reason=atomic_rename_failed", kConfigPath);
    return false;
  }

  LRS_LOGI(FS,
           "event=config_saved path=%s bytes=%lu role=%s local=%u remote=%u wifi_ssid=%s fleet_key=%s",
           kConfigPath,
           static_cast<unsigned long>(bytes),
           cfg_.role_tx ? "tx" : "rx",
           static_cast<unsigned>(cfg_.local_address),
           static_cast<unsigned>(cfg_.remote_address),
           cfg_.wifi_sta_ssid.c_str(),
           lrslog::maskSecret(String(cfg_.fleet_passphrase.c_str())).c_str());
  return ok;
}

bool ConfigStore::factoryReset(bool keepSharedFleetKey, bool keepWifiCredentials) {
  const String preservedFleetKey = cfg_.fleet_passphrase.c_str();
  const bool preservedFleetPromptDismissed = cfg_.fleet_setup_prompt_dismissed;
  const String preservedWifiSsid = cfg_.wifi_sta_ssid.c_str();
  const String preservedWifiPassword = cfg_.wifi_sta_password.c_str();

  setDefaults();
  ensureProvisionedDefaults();

  if (keepSharedFleetKey && preservedFleetKey.length() > 0) {
    cfg_.fleet_passphrase = preservedFleetKey;
    cfg_.fleet_setup_prompt_dismissed = preservedFleetPromptDismissed || (cfg_.fleet_passphrase != kDefaultDeploymentKey);
  }
  if (keepWifiCredentials) {
    cfg_.wifi_sta_ssid = preservedWifiSsid;
    cfg_.wifi_sta_password = preservedWifiPassword;
  }

  LRS_LOGW(SYS,
           "event=factory_reset_apply keep_fleet_key=%u keep_wifi=%u fleet_key=%s wifi_ssid=%s",
           keepSharedFleetKey ? 1U : 0U,
           keepWifiCredentials ? 1U : 0U,
           lrslog::maskSecret(String(cfg_.fleet_passphrase.c_str())).c_str(),
           cfg_.wifi_sta_ssid.c_str());
  return save();
}

bool ConfigStore::schedulePostOtaFactoryReset(bool keepSharedFleetKey, bool keepWifiCredentials) {
  JsonDocument doc;
  doc["factory_reset"] = true;
  doc["keep_shared_fleet_key"] = keepSharedFleetKey;
  doc["keep_wifi_credentials"] = keepWifiCredentials;

  const size_t estimatedBytes = measureJson(doc);
  if (estimatedBytes == 0 || estimatedBytes > kPostOtaActionMaxBytes) {
    LRS_LOGE(FS,
             "event=post_ota_action_set_failed reason=size_invalid estimated=%lu limit=%lu",
             static_cast<unsigned long>(estimatedBytes),
             static_cast<unsigned long>(kPostOtaActionMaxBytes));
    return false;
  }

  LittleFS.remove(kPostOtaActionTmpPath);
  File f = LittleFS.open(kPostOtaActionTmpPath, "w");
  if (!f) {
    LRS_LOGE(FS, "event=post_ota_action_set_failed reason=open_tmp_failed path=%s", kPostOtaActionTmpPath);
    return false;
  }

  const size_t bytes = serializeJson(doc, f);
  f.flush();
  f.close();
  if (bytes != estimatedBytes) {
    LittleFS.remove(kPostOtaActionTmpPath);
    LRS_LOGE(FS,
             "event=post_ota_action_set_failed reason=serialize_mismatch estimated=%lu actual=%lu",
             static_cast<unsigned long>(estimatedBytes),
             static_cast<unsigned long>(bytes));
    return false;
  }

  LittleFS.remove(kPostOtaActionPath);
  if (!LittleFS.rename(kPostOtaActionTmpPath, kPostOtaActionPath)) {
    LittleFS.remove(kPostOtaActionTmpPath);
    LRS_LOGE(FS, "event=post_ota_action_set_failed reason=atomic_rename_failed path=%s", kPostOtaActionPath);
    return false;
  }

  LRS_LOGW(SYS,
           "event=post_ota_action_set action=factory_reset keep_fleet_key=%u keep_wifi=%u bytes=%lu",
           keepSharedFleetKey ? 1U : 0U,
           keepWifiCredentials ? 1U : 0U,
           static_cast<unsigned long>(bytes));
  return true;
}

bool ConfigStore::consumePostOtaFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials) {
  keepSharedFleetKey = false;
  keepWifiCredentials = false;
  if (!LittleFS.exists(kPostOtaActionPath)) {
    return false;
  }

  File f = LittleFS.open(kPostOtaActionPath, "r");
  if (!f) {
    LRS_LOGE(FS, "event=post_ota_action_consume_failed reason=open_failed path=%s", kPostOtaActionPath);
    return false;
  }

  const size_t fileSize = static_cast<size_t>(f.size());
  if (fileSize == 0 || fileSize > kPostOtaActionMaxBytes) {
    f.close();
    LittleFS.remove(kPostOtaActionPath);
    LRS_LOGW(FS,
             "event=post_ota_action_consume_dropped reason=size_invalid bytes=%lu limit=%lu",
             static_cast<unsigned long>(fileSize),
             static_cast<unsigned long>(kPostOtaActionMaxBytes));
    return false;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, f);
  f.close();
  if (err) {
    LittleFS.remove(kPostOtaActionPath);
    LRS_LOGW(FS, "event=post_ota_action_consume_dropped reason=parse_failed err=%s", err.c_str());
    return false;
  }

  const bool doFactoryReset = doc["factory_reset"] | false;
  keepSharedFleetKey = doc["keep_shared_fleet_key"] | false;
  keepWifiCredentials = doc["keep_wifi_credentials"] | false;

  if (!LittleFS.remove(kPostOtaActionPath)) {
    LRS_LOGE(FS, "event=post_ota_action_consume_failed reason=remove_failed path=%s", kPostOtaActionPath);
    keepSharedFleetKey = false;
    keepWifiCredentials = false;
    return false;
  }

  if (!doFactoryReset) {
    keepSharedFleetKey = false;
    keepWifiCredentials = false;
    return false;
  }

  LRS_LOGW(SYS,
           "event=post_ota_action_consumed action=factory_reset keep_fleet_key=%u keep_wifi=%u",
           keepSharedFleetKey ? 1U : 0U,
           keepWifiCredentials ? 1U : 0U);
  return true;
}

const String &ConfigStore::chipIdHex() const {
  if (chip_id_hex_cache_.length() == 0) {
    char chip[9];
    snprintf(chip, sizeof(chip), "%08x", ESP.getChipId());
    chip_id_hex_cache_ = chip;
  }
  return chip_id_hex_cache_;
}

String ConfigStore::defaultLanHostname() const {
  return String("lrs-") + chipIdHex();
}

String ConfigStore::apSsid() const {
  return String("lrs-") + chipIdHex();
}

String ConfigStore::apPassword() const {
  return deriveShortPassword(chipIdHex());
}

void ConfigStore::setDefaults() {
  constexpr uint8_t kGatewayAddress = 254;
  constexpr uint8_t kFirstRemoteAddress = 1;
  cfg_.schema_version = kConfigSchemaVersion;
  cfg_.commissioned = false;
  cfg_.mode = kModePaired;
  cfg_.role = kRoleTransmitter;

  cfg_.role_tx = true;
  cfg_.local_address = kGatewayAddress;
  cfg_.remote_address = kFirstRemoteAddress;
  cfg_.paired_target_count = 1;
  memset(cfg_.paired_target_addresses, 0, sizeof(cfg_.paired_target_addresses));
  cfg_.paired_target_addresses[0] = kFirstRemoteAddress;
  cfg_.allowed_controller_count = 1;
  memset(cfg_.allowed_controller_addresses, 0, sizeof(cfg_.allowed_controller_addresses));
  cfg_.allowed_controller_addresses[0] = kGatewayAddress;
  cfg_.known_peer_count = 0;
  memset(cfg_.known_peer_addresses, 0, sizeof(cfg_.known_peer_addresses));
  memset(cfg_.known_peer_chip_ids, 0, sizeof(cfg_.known_peer_chip_ids));

  cfg_.lora_frequency_hz = kLockedLoraFrequencyHz;
  cfg_.lora_tx_power = 17;
  cfg_.lora_spreading_factor = 7;
  cfg_.lora_bandwidth_hz = 125000L;
  cfg_.lora_coding_rate = 5;

  cfg_.heartbeat_ms = 60000;
  cfg_.heartbeat_enabled = true;
  cfg_.ack_timeout_ms = 5000;
  cfg_.mqtt_remote_retry_timeout_ms = 300000;
  cfg_.tx_mqtt_remote_polling_enabled = false;
  cfg_.tx_mqtt_remote_default_poll_interval_ms = 60000;
  cfg_.maintenance_debug_telemetry_enabled = false;
  cfg_.rx_push_on_change_enabled = false;
  cfg_.rx_push_min_interval_ms = 60000;
  cfg_.input_control_paired_lora_enabled = true;
  cfg_.tx_command_retry_timeout_ms = 180000;
  cfg_.rx_failsafe_mode = "hold_last";
  cfg_.rx_failsafe_timeout_ms = 180000;

  cfg_.wifi_sta_ssid = "";
  cfg_.wifi_sta_password = "";
  cfg_.lan_hostname = defaultLanHostname();
  cfg_.ap_always_on = true;
  cfg_.wifi_phy_mode = "11b";
  cfg_.wifi_tx_power_dbm = kDefaultWifiTxPowerDbm;
  cfg_.wifi_sleep_enabled = false;
  cfg_.wifi_static_ip_enabled = false;
  cfg_.wifi_static_ip = "";
  cfg_.wifi_static_gateway = "";
  cfg_.wifi_static_subnet = "255.255.255.0";
  cfg_.wifi_channel_override = 0;
  cfg_.wifi_ap_fallback_policy = "fallback_on_disconnect";
  cfg_.wifi_admin_enabled = true;
  cfg_.power_save_listen_only = false;
  cfg_.power_save_boot_grace = true;
  cfg_.mqtt_client_enabled = false;
  cfg_.mqtt_control_enabled = false;
  cfg_.mqtt_controller_addresses = "";
  cfg_.mqtt_host = "venus.local";
  cfg_.mqtt_port = 1883;
  cfg_.mqtt_user = "";
  cfg_.mqtt_password = "";
  cfg_.mqtt_topic_root = "lora";
  cfg_.sensor_temp_enabled = false;
  cfg_.sensor_temp_pin = 0;
  cfg_.sensor_temp_interval_s = 10;
  cfg_.sensor_tank_enabled = false;
  cfg_.sensor_tank_range_mm = 5000;
  cfg_.sensor_tank_vref_mv = 3553;
  cfg_.sensor_tank_sense_ohms = 120;
  cfg_.sensor_tank_interval_s = 5;

  cfg_.fleet_passphrase = "lora-default-passphrase";
  cfg_.fleet_setup_prompt_dismissed = false;
  cfg_.admin_password = "";
  cfg_.factory_serial = "";
}

void ConfigStore::ensureProvisionedDefaults() {
  constexpr uint8_t kGatewayAddress = 254;
  constexpr uint8_t kFirstRemoteAddress = 1;
  if (cfg_.role_tx) {
    cfg_.local_address = kGatewayAddress;
    cfg_.remote_address = kFirstRemoteAddress;
  } else {
    cfg_.local_address = kFirstRemoteAddress;
    cfg_.remote_address = kGatewayAddress;
  }
  cfg_.paired_target_count = 1;
  memset(cfg_.paired_target_addresses, 0, sizeof(cfg_.paired_target_addresses));
  cfg_.paired_target_addresses[0] = kFirstRemoteAddress;
  cfg_.allowed_controller_count = 1;
  memset(cfg_.allowed_controller_addresses, 0, sizeof(cfg_.allowed_controller_addresses));
  cfg_.allowed_controller_addresses[0] = kGatewayAddress;
  cfg_.known_peer_count = 0;
  memset(cfg_.known_peer_addresses, 0, sizeof(cfg_.known_peer_addresses));
  memset(cfg_.known_peer_chip_ids, 0, sizeof(cfg_.known_peer_chip_ids));

  cfg_.lora_frequency_hz = kLockedLoraFrequencyHz;

  const String chipHex = chipIdHex();
  cfg_.lan_hostname = defaultLanHostname();
  cfg_.admin_password = deriveShortPassword(chipHex);
  char weekStamp[5];
  compileWeekStamp(weekStamp);
  char serialBuf[24];
  snprintf(serialBuf, sizeof(serialBuf), "lrs%s-%s", weekStamp, chipHex.c_str());
  cfg_.factory_serial = serialBuf;
  cfg_.commissioned = true;
  cfg_.mode = kModePaired;
  cfg_.role = cfg_.role_tx ? kRoleTransmitter : kRoleReceiver;
}
