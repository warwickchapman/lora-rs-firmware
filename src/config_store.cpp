#include "config_store.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <SHA256.h>

#include "logger.h"

namespace {
constexpr char kConfigPath[] = "/config.json";
constexpr uint16_t kConfigVersion = 1;
constexpr char kProductSecret[] = "LRS-v1-rotate-this-secret";
constexpr char kDefaultDeploymentKey[] = "lora-default-passphrase";

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

String compileWeekStamp() {
  const String date = __DATE__;
  const int mon = monthFromShort(date.substring(0, 3));
  const int day = date.substring(4, 6).toInt();
  const int year = date.substring(7, 11).toInt();

  const unsigned yy = static_cast<unsigned>(year % 100);
  const unsigned ww = static_cast<unsigned>(((dayOfYear(year, mon, day) - 1) / 7) + 1);

  char out[6];
  snprintf(out, sizeof(out), "%02u%02u", yy, ww);
  return String(out);
}

String deriveShortPassword(const String &chip) {
  String material = String(kProductSecret) + ":" + chip;
  uint8_t digest[32];
  SHA256 hash;
  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(material.c_str()), material.length());
  hash.finalize(digest, sizeof(digest));

  char hexbuf[65];
  for (size_t i = 0; i < 32; i++) {
    snprintf(hexbuf + (i * 2), 3, "%02x", digest[i]);
  }
  hexbuf[64] = '\0';
  // ESP8266 SoftAP WPA2 passwords must be at least 8 chars.
  return String(hexbuf).substring(0, 8);
}

String extractJsonStringField(const String &json, const char *key) {
  if (key == nullptr || key[0] == '\0') return "";
  const String needle = String("\"") + key + "\":\"";
  const int start = json.indexOf(needle);
  if (start < 0) return "";
  const int valueStart = start + needle.length();
  int i = valueStart;
  bool escape = false;
  String out;
  while (i < json.length()) {
    const char c = json.charAt(i++);
    if (escape) {
      out += c;
      escape = false;
      continue;
    }
    if (c == '\\') {
      escape = true;
      continue;
    }
    if (c == '"') {
      return out;
    }
    out += c;
  }
  return "";
}

}  // namespace

bool ConfigStore::begin() {
  if (!LittleFS.begin()) {
    LRS_LOGE(FS, "event=fs_mount_failed path=%s", kConfigPath);
    return false;
  }

  setDefaults();

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
  String raw;
  raw.reserve(fileSize + 1);
  while (f.available()) {
    raw += static_cast<char>(f.read());
  }
  f.close();

  size_t docCapacity = fileSize + 512;
  if (docCapacity < 4096) {
    docCapacity = 4096;
  }
  DynamicJsonDocument doc(docCapacity);
  auto err = deserializeJson(doc, raw);
  if (err) {
    // Keep config file intact on parse failure to avoid destructive resets.
    LRS_LOGW(FS, "event=config_parse_failed path=%s err=%s bytes=%lu", kConfigPath, err.c_str(), static_cast<unsigned long>(fileSize));
    ensureProvisionedDefaults();
    const String recoveredAdmin = extractJsonStringField(raw, "admin_password");
    if (recoveredAdmin.length() >= 8) {
      cfg_.admin_password = recoveredAdmin;
    }
    return false;
  }

  cfg_.version = doc["version"] | kConfigVersion;
  cfg_.provisioned = doc["provisioned"] | false;

  cfg_.role_tx = doc["role_tx"] | true;
  cfg_.local_address = doc["local_address"] | 1;
  cfg_.remote_address = doc["remote_address"] | 2;

  cfg_.lora_frequency_hz = doc["lora_frequency_hz"] | 433000000L;
  cfg_.lora_tx_power = doc["lora_tx_power"] | 17;
  cfg_.lora_spreading_factor = doc["lora_spreading_factor"] | 7;
  cfg_.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | 125000L;
  cfg_.lora_coding_rate = doc["lora_coding_rate"] | 5;

  cfg_.heartbeat_ms = doc["heartbeat_ms"] | 60000;
  cfg_.ack_timeout_ms = doc["ack_timeout_ms"] | 5000;
  cfg_.mqtt_remote_retry_timeout_ms = doc["mqtt_remote_retry_timeout_ms"] | 300000;
  cfg_.tx_mqtt_remote_polling_enabled = doc["tx_mqtt_remote_polling_enabled"] | false;
  cfg_.tx_mqtt_remote_default_poll_interval_ms = doc["tx_mqtt_remote_default_poll_interval_ms"] | 60000;
  cfg_.rx_push_on_change_enabled = doc["rx_push_on_change_enabled"] | false;
  cfg_.rx_push_min_interval_ms = doc["rx_push_min_interval_ms"] | 60000;
  cfg_.tx_input_lora_control_enabled = doc["tx_input_lora_control_enabled"] | true;

  cfg_.wifi_sta_ssid = String(static_cast<const char *>(doc["wifi_sta_ssid"] | ""));
  cfg_.wifi_sta_password = String(static_cast<const char *>(doc["wifi_sta_password"] | ""));
  cfg_.lan_hostname = String(static_cast<const char *>(doc["lan_hostname"] | ""));
  cfg_.ap_always_on = doc["ap_always_on"] | true;
  cfg_.mqtt_enabled = doc["mqtt_enabled"] | false;
  cfg_.mqtt_host = String(static_cast<const char *>(doc["mqtt_host"] | "venus.local"));
  cfg_.mqtt_port = doc["mqtt_port"] | 1883;
  cfg_.mqtt_user = String(static_cast<const char *>(doc["mqtt_user"] | ""));
  cfg_.mqtt_password = String(static_cast<const char *>(doc["mqtt_password"] | ""));
  cfg_.mqtt_topic_root = String(static_cast<const char *>(doc["mqtt_topic_root"] | "lora"));
  cfg_.sensor_temp_enabled = doc["sensor_temp_enabled"] | true;
  cfg_.sensor_temp_pin = doc["sensor_temp_pin"] | 0;
  cfg_.sensor_temp_interval_s = doc["sensor_temp_interval_s"] | 10;

  cfg_.fleet_passphrase = String(static_cast<const char *>(doc["fleet_passphrase"] | "lora-default-passphrase"));
  cfg_.fleet_setup_prompt_dismissed = doc["fleet_setup_prompt_dismissed"] | false;
  cfg_.admin_password = String(static_cast<const char *>(doc["admin_password"] | ""));
  cfg_.factory_serial = String(static_cast<const char *>(doc["factory_serial"] | ""));
  cfg_.audit_last_saved_by = String(static_cast<const char *>(doc["audit_last_saved_by"] | "factory"));
  cfg_.audit_last_saved_ms = doc["audit_last_saved_ms"] | 0;
  cfg_.audit_last_reboot_reason = String(static_cast<const char *>(doc["audit_last_reboot_reason"] | "power_on"));
  cfg_.audit_last_reboot_ms = doc["audit_last_reboot_ms"] | 0;
  cfg_.audit_boot_count = doc["audit_boot_count"] | 0;

  bool changed = false;
  const String chipHex = chipIdHex();

  // Legacy config migration: never reprovision/reset role/addresses if config file exists.
  if (!cfg_.provisioned) {
    cfg_.provisioned = true;
    changed = true;
  }

  if (cfg_.admin_password.length() < 8) {
    cfg_.admin_password = deriveShortPassword(chipHex);
    changed = true;
  }
  if (cfg_.factory_serial.length() < 4) {
    cfg_.factory_serial = String("lrs") + compileWeekStamp() + "-" + chipHex;
    changed = true;
  }
  if (cfg_.local_address < 1 || cfg_.local_address > 254) {
    cfg_.local_address = static_cast<uint8_t>((ESP.getChipId() & 0xFF) % 254) + 1;
    changed = true;
  }
  if (cfg_.remote_address < 1 || cfg_.remote_address > 254 || cfg_.remote_address == cfg_.local_address) {
    cfg_.remote_address = static_cast<uint8_t>(((ESP.getChipId() >> 8) & 0xFF) % 254) + 1;
    if (cfg_.remote_address == cfg_.local_address) {
      cfg_.remote_address = (cfg_.local_address % 254) + 1;
    }
    changed = true;
  }

  if (cfg_.lan_hostname.length() == 0) {
    cfg_.lan_hostname = defaultLanHostnameForRole(cfg_.role_tx);
    changed = true;
  }

  const String defaultHost = String("lrs-") + chipHex;
  const String legacyRoleHostTx = defaultHost + "-tx";
  const String legacyRoleHostRx = defaultHost + "-rx";
  if (cfg_.lan_hostname == legacyRoleHostTx || cfg_.lan_hostname == legacyRoleHostRx) {
    cfg_.lan_hostname = defaultHost;
    changed = true;
  }

  if (cfg_.mqtt_host.length() == 0) {
    cfg_.mqtt_host = "venus.local";
    changed = true;
  }
  if (cfg_.fleet_passphrase.length() == 0) {
    cfg_.fleet_passphrase = "lora-default-passphrase";
    changed = true;
  }
  if (cfg_.tx_mqtt_remote_default_poll_interval_ms < 60000) {
    cfg_.tx_mqtt_remote_default_poll_interval_ms = 60000;
    changed = true;
  }
  if (cfg_.tx_mqtt_remote_default_poll_interval_ms > 3600000) {
    cfg_.tx_mqtt_remote_default_poll_interval_ms = 3600000;
    changed = true;
  }
  if (cfg_.rx_push_min_interval_ms < 60000) {
    cfg_.rx_push_min_interval_ms = 60000;
    changed = true;
  }
  if (cfg_.rx_push_min_interval_ms > 3600000) {
    cfg_.rx_push_min_interval_ms = 3600000;
    changed = true;
  }

  cfg_.audit_boot_count += 1;
  if (changed) {
    LRS_LOGI(FS, "event=config_migrated path=%s write_back=1", kConfigPath);
    return save();
  }
  LRS_LOGI(FS,
           "event=config_loaded path=%s version=%u provisioned=%u role=%s local=%u remote=%u wifi_ssid=%s fleet_key=%s",
           kConfigPath,
           static_cast<unsigned>(cfg_.version),
           cfg_.provisioned ? 1U : 0U,
           cfg_.role_tx ? "tx" : "rx",
           static_cast<unsigned>(cfg_.local_address),
           static_cast<unsigned>(cfg_.remote_address),
           cfg_.wifi_sta_ssid.c_str(),
           lrslog::maskSecret(cfg_.fleet_passphrase).c_str());
  return true;
}

Settings &ConfigStore::settings() { return cfg_; }

bool ConfigStore::save() {
  DynamicJsonDocument doc(4096);
  doc["version"] = cfg_.version;
  doc["provisioned"] = cfg_.provisioned;

  doc["role_tx"] = cfg_.role_tx;
  doc["local_address"] = cfg_.local_address;
  doc["remote_address"] = cfg_.remote_address;

  doc["lora_frequency_hz"] = cfg_.lora_frequency_hz;
  doc["lora_tx_power"] = cfg_.lora_tx_power;
  doc["lora_spreading_factor"] = cfg_.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg_.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg_.lora_coding_rate;

  doc["heartbeat_ms"] = cfg_.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg_.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg_.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg_.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg_.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg_.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg_.rx_push_min_interval_ms;
  doc["tx_input_lora_control_enabled"] = cfg_.tx_input_lora_control_enabled;

  doc["wifi_sta_ssid"] = cfg_.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg_.wifi_sta_password;
  doc["lan_hostname"] = cfg_.lan_hostname;
  doc["ap_always_on"] = cfg_.ap_always_on;
  doc["mqtt_enabled"] = cfg_.mqtt_enabled;
  doc["mqtt_host"] = cfg_.mqtt_host;
  doc["mqtt_port"] = cfg_.mqtt_port;
  doc["mqtt_user"] = cfg_.mqtt_user;
  doc["mqtt_password"] = cfg_.mqtt_password;
  doc["mqtt_topic_root"] = cfg_.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg_.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg_.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg_.sensor_temp_interval_s;

  doc["fleet_passphrase"] = cfg_.fleet_passphrase;
  doc["fleet_setup_prompt_dismissed"] = cfg_.fleet_setup_prompt_dismissed;
  doc["admin_password"] = cfg_.admin_password;
  doc["factory_serial"] = cfg_.factory_serial;
  doc["audit_last_saved_by"] = cfg_.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg_.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg_.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg_.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg_.audit_boot_count;

  File f = LittleFS.open(kConfigPath, "w");
  if (!f) {
    return false;
  }
  const size_t bytes = serializeJson(doc, f);
  bool ok = bytes > 0;
  f.close();
  if (!ok) {
    LRS_LOGE(FS, "event=config_save_failed path=%s", kConfigPath);
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
           lrslog::maskSecret(cfg_.fleet_passphrase).c_str());
  return ok;
}

bool ConfigStore::factoryReset(bool keepSharedFleetKey, bool keepWifiCredentials) {
  const String preservedFleetKey = cfg_.fleet_passphrase;
  const bool preservedFleetPromptDismissed = cfg_.fleet_setup_prompt_dismissed;
  const String preservedWifiSsid = cfg_.wifi_sta_ssid;
  const String preservedWifiPassword = cfg_.wifi_sta_password;

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

  if (keepSharedFleetKey && keepWifiCredentials) {
    cfg_.audit_last_saved_by = "factory_reset_keep_fleet_wifi";
    cfg_.audit_last_reboot_reason = "factory_reset_keep_fleet_wifi";
  } else if (keepSharedFleetKey) {
    cfg_.audit_last_saved_by = "factory_reset_keep_fleet";
    cfg_.audit_last_reboot_reason = "factory_reset_keep_fleet";
  } else if (keepWifiCredentials) {
    cfg_.audit_last_saved_by = "factory_reset_keep_wifi";
    cfg_.audit_last_reboot_reason = "factory_reset_keep_wifi";
  } else {
    cfg_.audit_last_saved_by = "factory_reset_full";
    cfg_.audit_last_reboot_reason = "factory_reset_full";
  }
  cfg_.audit_last_saved_ms = 0;
  cfg_.audit_last_reboot_ms = 0;
  LRS_LOGW(SYS,
           "event=factory_reset_apply keep_fleet_key=%u keep_wifi=%u fleet_key=%s wifi_ssid=%s",
           keepSharedFleetKey ? 1U : 0U,
           keepWifiCredentials ? 1U : 0U,
           lrslog::maskSecret(cfg_.fleet_passphrase).c_str(),
           cfg_.wifi_sta_ssid.c_str());
  return save();
}

String ConfigStore::chipIdHex() const {
  char chip[9];
  snprintf(chip, sizeof(chip), "%08x", ESP.getChipId());
  return String(chip);
}

String ConfigStore::defaultLanHostnameForRole(bool roleTx) const {
  (void)roleTx;
  return String("lrs-") + chipIdHex();
}

String ConfigStore::apSsid() const {
  return String("lrs-") + chipIdHex();
}

String ConfigStore::apPassword() const {
  return deriveShortPassword(chipIdHex());
}

void ConfigStore::setDefaults() {
  cfg_.version = kConfigVersion;
  cfg_.provisioned = false;

  cfg_.role_tx = true;
  cfg_.local_address = 1;
  cfg_.remote_address = 2;

  cfg_.lora_frequency_hz = 433000000L;
  cfg_.lora_tx_power = 17;
  cfg_.lora_spreading_factor = 7;
  cfg_.lora_bandwidth_hz = 125000L;
  cfg_.lora_coding_rate = 5;

  cfg_.heartbeat_ms = 60000;
  cfg_.ack_timeout_ms = 5000;
  cfg_.mqtt_remote_retry_timeout_ms = 300000;
  cfg_.tx_mqtt_remote_polling_enabled = false;
  cfg_.tx_mqtt_remote_default_poll_interval_ms = 60000;
  cfg_.rx_push_on_change_enabled = false;
  cfg_.rx_push_min_interval_ms = 60000;
  cfg_.tx_input_lora_control_enabled = true;

  cfg_.wifi_sta_ssid = "";
  cfg_.wifi_sta_password = "";
  cfg_.lan_hostname = "";
  cfg_.ap_always_on = true;
  cfg_.mqtt_enabled = false;
  cfg_.mqtt_host = "venus.local";
  cfg_.mqtt_port = 1883;
  cfg_.mqtt_user = "";
  cfg_.mqtt_password = "";
  cfg_.mqtt_topic_root = "lora";
  cfg_.sensor_temp_enabled = true;
  cfg_.sensor_temp_pin = 0;
  cfg_.sensor_temp_interval_s = 10;

  cfg_.fleet_passphrase = "lora-default-passphrase";
  cfg_.fleet_setup_prompt_dismissed = false;
  cfg_.admin_password = "";
  cfg_.factory_serial = "";
  cfg_.audit_last_saved_by = "factory";
  cfg_.audit_last_saved_ms = 0;
  cfg_.audit_last_reboot_reason = "power_on";
  cfg_.audit_last_reboot_ms = 0;
  cfg_.audit_boot_count = 0;
}

void ConfigStore::ensureProvisionedDefaults() {
  const uint32_t chipId = ESP.getChipId();
  cfg_.local_address = static_cast<uint8_t>((chipId & 0xFF) % 254) + 1;
  cfg_.remote_address = static_cast<uint8_t>(((chipId >> 8) & 0xFF) % 254) + 1;
  if (cfg_.remote_address == cfg_.local_address) {
    cfg_.remote_address = (cfg_.local_address % 254) + 1;
  }

#ifdef REGION_US
  cfg_.lora_frequency_hz = 915000000L;
#else
  cfg_.lora_frequency_hz = 433000000L;
#endif

  const String chipHex = chipIdHex();
  cfg_.lan_hostname = defaultLanHostnameForRole(cfg_.role_tx);
  cfg_.admin_password = deriveShortPassword(chipHex);
  cfg_.factory_serial = String("lrs") + compileWeekStamp() + "-" + chipHex;
  cfg_.audit_last_saved_by = "factory";
  cfg_.audit_last_saved_ms = 0;
  cfg_.audit_last_reboot_reason = "power_on";
  cfg_.audit_last_reboot_ms = 0;
  cfg_.audit_boot_count = 1;
  cfg_.provisioned = true;
}
