#include "config_store.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <SHA256.h>

namespace {
constexpr char kConfigPath[] = "/config.json";
constexpr uint16_t kConfigVersion = 1;
constexpr char kProductSecret[] = "LRS-v1-rotate-this-secret";

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

}  // namespace

bool ConfigStore::begin() {
  if (!LittleFS.begin()) {
    return false;
  }

  setDefaults();

  if (!LittleFS.exists(kConfigPath)) {
    ensureProvisionedDefaults();
    return save();
  }

  File f = LittleFS.open(kConfigPath, "r");
  if (!f) {
    ensureProvisionedDefaults();
    return save();
  }

  DynamicJsonDocument doc(2048);
  auto err = deserializeJson(doc, f);
  f.close();
  if (err) {
    ensureProvisionedDefaults();
    return save();
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

  cfg_.audit_boot_count += 1;
  changed = true;
  if (changed) {
    return save();
  }
  return true;
}

Settings &ConfigStore::settings() { return cfg_; }

bool ConfigStore::save() {
  DynamicJsonDocument doc(2048);
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
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
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
