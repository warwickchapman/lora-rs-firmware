#pragma once

#include <Arduino.h>
#include <cstring>

template <size_t N>
struct FixedSettingString {
  char value[N]{};

  FixedSettingString() = default;
  FixedSettingString(const char *s) { set(s); }
  FixedSettingString(const String &s) { set(s.c_str()); }

  FixedSettingString &operator=(const char *s) {
    set(s);
    return *this;
  }
  FixedSettingString &operator=(const String &s) {
    set(s.c_str());
    return *this;
  }
  FixedSettingString &operator=(const FixedSettingString &other) {
    if (this != &other) set(other.value);
    return *this;
  }

  const char *c_str() const { return value; }
  size_t length() const { return strlen(value); }
  bool isEmpty() const { return value[0] == '\0'; }
  operator const char *() const { return value; }

  bool equals(const char *s) const { return *this == s; }
  bool equals(const String &s) const { return s.equals(value); }
  bool operator==(const char *s) const {
    return strcmp(value, s ? s : "") == 0;
  }
  bool operator!=(const char *s) const { return !(*this == s); }
  bool operator==(const String &s) const { return s.equals(value); }
  bool operator!=(const String &s) const { return !s.equals(value); }

  void trim() {
    char *start = value;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') ++start;
    char *end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n')) {
      --end;
    }
    const size_t len = static_cast<size_t>(end - start);
    if (start != value) memmove(value, start, len);
    value[len] = '\0';
  }

  void toLowerCase() {
    for (size_t i = 0; value[i] != '\0'; ++i) {
      if (value[i] >= 'A' && value[i] <= 'Z') {
        value[i] = static_cast<char>(value[i] - 'A' + 'a');
      }
    }
  }

 private:
  void set(const char *s) {
    if (s == nullptr) s = "";
    strlcpy(value, s, N);
  }
};

struct Settings {
  static constexpr uint8_t kAddressListCap = 12;
  uint16_t schema_version;
  bool commissioned;
  FixedSettingString<16> mode;
  FixedSettingString<16> role;

  bool role_tx;
  uint8_t local_address;
  uint8_t remote_address;
  uint8_t paired_target_count;
  uint8_t paired_target_addresses[kAddressListCap];
  uint8_t allowed_controller_count;
  uint8_t allowed_controller_addresses[kAddressListCap];
  uint8_t known_peer_count;
  uint8_t known_peer_addresses[kAddressListCap];

  long lora_frequency_hz;
  uint8_t lora_tx_power;
  uint8_t lora_spreading_factor;
  long lora_bandwidth_hz;
  uint8_t lora_coding_rate;

  uint32_t heartbeat_ms;
  uint32_t ack_timeout_ms;
  uint32_t mqtt_remote_retry_timeout_ms;
  bool tx_mqtt_remote_polling_enabled;
  uint32_t tx_mqtt_remote_default_poll_interval_ms;
  bool maintenance_debug_telemetry_enabled;
  bool rx_push_on_change_enabled;
  uint32_t rx_push_min_interval_ms;
  bool input_control_paired_lora_enabled;
  uint32_t tx_command_retry_timeout_ms;
  FixedSettingString<16> rx_failsafe_mode;
  uint32_t rx_failsafe_timeout_ms;

  FixedSettingString<33> wifi_sta_ssid;
  FixedSettingString<65> wifi_sta_password;
  FixedSettingString<65> lan_hostname;
  bool ap_always_on;
  FixedSettingString<16> wifi_phy_mode;
  float wifi_tx_power_dbm;
  bool wifi_sleep_enabled;
  bool wifi_static_ip_enabled;
  FixedSettingString<16> wifi_static_ip;
  FixedSettingString<16> wifi_static_gateway;
  FixedSettingString<16> wifi_static_subnet;
  uint8_t wifi_channel_override;
  FixedSettingString<24> wifi_ap_fallback_policy;
  bool wifi_admin_enabled;

  bool mqtt_client_enabled;
  bool mqtt_control_enabled;
  FixedSettingString<80> mqtt_controller_addresses;
  FixedSettingString<65> mqtt_host;
  uint16_t mqtt_port;
  FixedSettingString<65> mqtt_user;
  FixedSettingString<65> mqtt_password;
  FixedSettingString<65> mqtt_topic_root;

  bool sensor_temp_enabled;
  uint8_t sensor_temp_pin;
  uint16_t sensor_temp_interval_s;

  FixedSettingString<65> fleet_passphrase;
  bool fleet_setup_prompt_dismissed;
  FixedSettingString<33> admin_password;

  FixedSettingString<33> factory_serial;
  FixedSettingString<33> audit_last_saved_by;
  uint32_t audit_last_saved_ms;
  FixedSettingString<33> audit_last_reboot_reason;
  uint32_t audit_last_reboot_ms;
  uint32_t audit_boot_count;
};

class ConfigStore {
 public:
  bool begin();
  Settings &settings();
  const Settings &settings() const;
  bool save();
  bool factoryReset(bool keepSharedFleetKey, bool keepWifiCredentials = false);
  bool schedulePostOtaFactoryReset(bool keepSharedFleetKey = false, bool keepWifiCredentials = false);
  bool consumePostOtaFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials);

  const String &chipIdHex() const;
  String defaultLanHostname() const;
  String apSsid() const;
  String apPassword() const;

 private:
  Settings cfg_{};
  mutable String chip_id_hex_cache_;

  void setDefaults();
  void ensureProvisionedDefaults();
};
