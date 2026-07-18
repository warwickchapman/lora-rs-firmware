#include "admin_executor.h"

#include <cstring>
#include <ESP8266WiFi.h>

#include "config_serializer.h"
#include "admin_session.h"

#include "build_info.h"
#include "logger.h"
#include "ota_pull.h"
#include "admin_config_utils.h"
#include "sensor_status.h"
#include "mqtt_bridge.h"
#include "runtime_utils.h"
#include "config_fields.h"

using namespace admin_config_utils;

namespace {

const char *cmdName(const JsonDocument &doc) {
  return doc["cmd"] | doc["command"] | "";
}

const char *requestId(const JsonDocument &doc) { return doc["id"] | ""; }

uint8_t clampMaxRemotes(int raw) {
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

const char* candidateReasonToString(CandidateReason r) {
  switch (r) {
    case CandidateReason::Ok: return "ok";
    case CandidateReason::KnownChipMoved: return "known_chip_moved";
    case CandidateReason::Conflict: return "conflict";
    case CandidateReason::OutOfRange: return "out_of_range";
    case CandidateReason::Full: return "full";
    default: return "unknown";
  }
}

const char* candidateStateToString(CandidateState s) {
  switch (s) {
    case CandidateState::SeenAddressOnly: return "seen_address_only";
    case CandidateState::Identified: return "identified";
    case CandidateState::Readdressing: return "readdressing";
    case CandidateState::Adopted: return "adopted";
    case CandidateState::Failed: return "failed";
    case CandidateState::ResetRequested: return "reset_requested";
    default: return "unknown";
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
    if (addr < runtime_utils::kMinAddress || addr > runtime_utils::kMaxAddress)
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



bool applySettingsPatch(JsonObjectConst doc, ConfigStore &config,
                        String &error, bool &networkChanged,
                        bool &otaAuthChanged) {
  auto &cfg = config.settings();
  Settings backup = cfg;
  FixedSettingString<33> prevStaSsid = cfg.wifi_sta_ssid;
  FixedSettingString<65> prevStaPassword = cfg.wifi_sta_password;
  FixedSettingString<65> prevLanHost = cfg.lan_hostname;
  const bool prevApAlwaysOn = cfg.ap_always_on;
  const float prevWifiTxPowerDbm = cfg.wifi_tx_power_dbm;
  const bool prevWifiSleepEnabled = cfg.wifi_sleep_enabled;
  const bool prevWifiStaticIpEnabled = cfg.wifi_static_ip_enabled;
  FixedSettingString<16> prevWifiStaticIp = cfg.wifi_static_ip;
  FixedSettingString<16> prevWifiStaticGateway = cfg.wifi_static_gateway;
  FixedSettingString<16> prevWifiStaticSubnet = cfg.wifi_static_subnet;
  const uint8_t prevWifiChannelOverride = cfg.wifi_channel_override;
  FixedSettingString<24> prevWifiApFallbackPolicy = cfg.wifi_ap_fallback_policy;
  const bool prevWifiAdminEnabled = cfg.wifi_admin_enabled;
  const bool prevPowerSaveListenOnly = cfg.power_save_listen_only;
  FixedSettingString<33> prevAdminPassword = cfg.admin_password;

  auto fail = [&](const String &msg) {
    cfg = backup;
    error = msg;
    return false;
  };

  cfg.mode = doc["mode"] | cfg.mode.c_str();
  if (!doc["role_tx"].isNull()) {
    cfg.role_tx = parseBoolField(doc["role_tx"], cfg.role_tx);
  } else if (!doc["role"].isNull()) {
    String role_input = doc["role"] | "";
    bool role_parsed = false;
    if (runtime_utils::parseRoleTxFromModeRole(String(cfg.mode.c_str()), role_input, role_parsed)) {
      cfg.role_tx = role_parsed;
    }
  }
  const bool hasAllowedControllersField =
      !doc["allowed_controller_addresses"].isNull();
  if (cfg.mode == "paired") {
    cfg.role = cfg.role_tx ? "gateway" : "remote";
  } else if (cfg.mode == "standalone") {
    cfg.role = "none";
    cfg.role_tx = true;
  } else if (cfg.mode.length() == 0) {
    cfg.mode = "paired";
    cfg.role = cfg.role_tx ? "gateway" : "remote";
  } else {
    return fail("mode_invalid");
  }
  cfg.local_address = parseAddressField(doc["local_address"], cfg.local_address);
  if (!cfg.role_tx && !doc["controller_address"].isNull()) {
    cfg.controller_address = parseAddressField(doc["controller_address"], cfg.controller_address);
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
  if (!doc["lora_frequency_hz"].isNull()) {
    const long requestedFrequency = doc["lora_frequency_hz"] | kDefaultFrequencyHz;
    if (requestedFrequency != kDefaultFrequencyHz) {
      return fail("lora_frequency_locked");
    }
  }
  cfg.lora_frequency_hz = kDefaultFrequencyHz;
  cfg.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | cfg.lora_tx_power);
  cfg.lora_spreading_factor =
      static_cast<uint8_t>(doc["lora_spreading_factor"] | cfg.lora_spreading_factor);
  cfg.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | cfg.lora_bandwidth_hz;
  cfg.lora_coding_rate =
      static_cast<uint8_t>(doc["lora_coding_rate"] | cfg.lora_coding_rate);
  cfg.heartbeat_ms = doc["heartbeat_ms"] | cfg.heartbeat_ms;
  cfg.heartbeat_enabled = parseBoolField(
      doc["heartbeat_enabled"],
      cfg.heartbeat_enabled);
  cfg.ack_timeout_ms = doc["ack_timeout_ms"] | cfg.ack_timeout_ms;
  cfg.mqtt_remote_retry_timeout_ms =
      doc["mqtt_remote_retry_timeout_ms"] | cfg.mqtt_remote_retry_timeout_ms;
  cfg.tx_mqtt_remote_polling_enabled = parseBoolField(
      doc["tx_mqtt_remote_polling_enabled"],
      cfg.tx_mqtt_remote_polling_enabled);
  cfg.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] |
      cfg.tx_mqtt_remote_default_poll_interval_ms;
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
  cfg.power_save_listen_only =
      parseBoolField(doc["power_save_listen_only"], cfg.power_save_listen_only);
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
    } else if (newAdminLen < 8) {
      return fail("admin_password_too_short");
    } else {
      cfg.admin_password = newAdmin;
    }
  }

  if (cfg.local_address < runtime_utils::kMinAddress)
    cfg.local_address = runtime_utils::kMinAddress;
  if (cfg.local_address > runtime_utils::kMaxAddress)
    cfg.local_address = runtime_utils::kMaxAddress;
  if (!cfg.role_tx && (cfg.controller_address < runtime_utils::kMinAddress ||
                       cfg.controller_address > runtime_utils::kMaxAddress ||
                       cfg.local_address == cfg.controller_address))
    return fail("local_controller_address_conflict");
  if (!cfg.role_tx && cfg.allowed_controller_count == 0) {
    cfg.allowed_controller_count = 1;
    cfg.allowed_controller_addresses[0] = cfg.controller_address;
  }

  const bool allowDefaultDeploymentKey =
      parseBoolField(doc["allow_default_deployment_key"], false);
  cfg.fleet_passphrase.trim();
  if (hasFleetPassphraseField &&
      cfg.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    return fail("fleet_passphrase_too_short");
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey &&
      runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    return fail("fleet_passphrase_default_blocked");
  }
  if (hasFleetPassphraseField && !runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    cfg.fleet_setup_prompt_dismissed = true;
  }

  if (cfg.heartbeat_ms < kMinHeartbeatMs)
    cfg.heartbeat_ms = kMinHeartbeatMs;
  if (cfg.heartbeat_ms > kMaxHeartbeatMs)
    cfg.heartbeat_ms = kMaxHeartbeatMs;
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
  if (cfg.role_tx && cfg.mqtt_control_enabled && !cfg.mqtt_client_enabled) {
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
                   (cfg.wifi_tx_power_dbm != prevWifiTxPowerDbm) ||
                   (cfg.wifi_sleep_enabled != prevWifiSleepEnabled) ||
                   (cfg.wifi_static_ip_enabled != prevWifiStaticIpEnabled) ||
                   (cfg.wifi_static_ip != prevWifiStaticIp) ||
                   (cfg.wifi_static_gateway != prevWifiStaticGateway) ||
                   (cfg.wifi_static_subnet != prevWifiStaticSubnet) ||
                   (cfg.wifi_channel_override != prevWifiChannelOverride) ||
                   (cfg.wifi_ap_fallback_policy != prevWifiApFallbackPolicy) ||
                   (cfg.wifi_admin_enabled != prevWifiAdminEnabled) ||
                   (cfg.power_save_listen_only != prevPowerSaveListenOnly);
  otaAuthChanged = (cfg.admin_password != prevAdminPassword);
  if (!config.save()) {
    return fail("save_failed");
  }
  return true;
}
} // namespace



bool AdminExecutor::begin(ConfigStore *config, NodeStateMachine *sm,
                          ConfigApplyCallback onApply, void *context) {
  config_ = config;
  sm_ = sm;
  on_apply_ = onApply;
  on_apply_ctx_ = context;
  mqtt_session_ = AdminSession{};
  return true;
}

void AdminExecutor::handleAdminChallenge(JsonDocument &doc, ResponseWriter writer) {
  const char *id = doc["id"] | "";
  uint32_t session_id = random(1, 0x7FFFFFFF);
  mqtt_session_.create(millis(), session_id);

  JsonDocument out;
  out["cmd"] = "admin_challenge";
  if (id[0] != '\0')
    out["id"] = id;
  out["session_id"] = mqtt_session_.state().session_id;
  out["expires_in_ms"] = 300000;
  sendOk(out, writer);
}

void AdminExecutor::execute(const char *jsonCommand, size_t length, ResponseWriter writer, bool isMqtt) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, jsonCommand, length);
  if (err) {
    sendError("unknown", "invalid_json", nullptr, writer);
    return;
  }

  const char *cmd = cmdName(doc);
  const char *id = requestId(doc);

  if (isMqtt) {
    if (id == nullptr || id[0] == '\0' || cmd == nullptr || cmd[0] == '\0') {
      sendError(cmd ? cmd : "unknown", "invalid_envelope", id ? id : nullptr, writer);
      return;
    }

    if (strcmp(cmd, "admin_challenge") != 0) {
      AdminSession::ValidationResult res = mqtt_session_.validate(doc, millis());
      switch (res) {
        case AdminSession::ValidationResult::SessionRequired:
          sendError(cmd, "admin_session_required", id, writer);
          return;
        case AdminSession::ValidationResult::SessionInvalid:
          sendError(cmd, "admin_session_invalid", id, writer);
          return;
        case AdminSession::ValidationResult::SessionExpired:
          sendError(cmd, "admin_session_expired", id, writer);
          return;
        case AdminSession::ValidationResult::SequenceReplay:
          sendError(cmd, "admin_sequence_replay", id, writer);
          return;
        case AdminSession::ValidationResult::Ok:
          break;
      }
    }
  }

  handleCommand(doc, writer, isMqtt);
}

bool AdminExecutor::requireAdmin(const JsonDocument &doc) {
  if (config_ == nullptr)
    return false;
  const char *password = doc["admin_password"] | doc["password"] | "";
  return config_->settings().admin_password.equals(password);
}

void AdminExecutor::sendError(const char *cmd, const char *error,
                              const char *id, ResponseWriter writer) {
  JsonDocument out;
  out["ok"] = false;
  out["cmd"] = cmd ? cmd : "";
  if (id != nullptr && id[0] != '\0')
    out["id"] = id;
  out["error"] = error ? error : "unknown";
  String output;
  serializeJson(out, output);
  writer(output);
}

void AdminExecutor::sendOk(JsonDocument &doc, ResponseWriter writer) {
  doc["ok"] = true;
  String output;
  serializeJson(doc, output);
  writer(output);
}

void AdminExecutor::buildProvisioningStatus(JsonDocument &doc, bool isMqtt) {
  if (sm_ == nullptr) {
    doc["ok"] = false;
    doc["error"] = "state_machine_unavailable";
    return;
  }

  ProvisioningSessionSnapshot sess{};
  sm_->provisioningSession(sess);
  JsonObject s = doc["session"].to<JsonObject>();

  if (isMqtt) {
    s["active"] = sess.active;
    s["state"] = provisioningSessionStateText(sess.state);
    s["max"] = sess.max_remotes;
    s["found"] = sess.discovered_count;
    s["verified"] = sess.verified_count;
    s["failed"] = sess.failed_count;
    s["deadline"] = sess.phase_deadline_ms;
    s["now"] = millis();
    doc["log_truncated"] = true;
  } else {
    s["active"] = sess.active;
    s["state"] = provisioningSessionStateText(sess.state);
    s["session_nonce"] = sess.session_nonce;
    s["max_remotes"] = sess.max_remotes;
    s["started_ms"] = sess.started_ms;
    s["phase_deadline_ms"] = sess.phase_deadline_ms;
    s["paused_normal_tx"] = sess.paused_normal_tx;
    s["discovered_count"] = sess.discovered_count;
    s["selected_count"] = sess.selected_count;
    s["conflict_count"] = sess.conflict_count;
    s["verified_count"] = sess.verified_count;
    s["failed_count"] = sess.failed_count;
    s["now_ms"] = millis();

    JsonArray debugEvents = s["debug_events"].to<JsonArray>();
    const size_t logCount = sm_->provisioningLogCount();
    for (size_t i = 0; i < logCount; ++i) {
      uint32_t ts = 0;
      char logMsg[56]{};
      if (sm_->provisioningLogByIndex(i, ts, logMsg)) {
        char buf[72];
        snprintf(buf, sizeof(buf), "[%lu] %s", static_cast<unsigned long>(ts), logMsg);
        debugEvents.add(buf);
      }
    }
  }

  JsonArray devices = doc["devices"].to<JsonArray>();
  const size_t count = sm_->provisioningDeviceCount();
  for (size_t i = 0; i < count; ++i) {
    ProvisioningDeviceSnapshot d{};
    if (!sm_->provisioningDeviceByIndex(i, d))
      continue;

    if (isMqtt) {
      JsonArray o = devices.add<JsonArray>();
      char chipHex[9];
      snprintf(chipHex, sizeof(chipHex), "%08lx", static_cast<unsigned long>(d.chip_id));
      o.add(chipHex);
      o.add(d.current_address);
      o.add(d.assigned_address);
      o.add(d.rssi);
      o.add(provisioningDeviceStateText(d.state));
      o.add(d.fw_major);
      o.add(d.fw_minor);
      o.add(d.fw_patch);
      o.add(d.fw_build);
    } else {
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
}

void AdminExecutor::handleStatus(JsonDocument &doc, ResponseWriter writer) {
  if (config_ == nullptr) {
    sendError("status", "config_unavailable", requestId(doc), writer);
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
  out["uptime_ms"] = millis();
  out["heap_free"] = lrslog::heapFree();
  out["heap_frag_pct"] = lrslog::heapFragPercent();
  out["heap_max_block"] = lrslog::heapMaxFreeBlock();
  out["mode"] = cfg.mode;
  out["role"] = cfg.role_tx ? "gateway" : "remote";
  out["role_tx"] = cfg.role_tx;
  out["local_address"] = cfg.local_address;
  if (!cfg.role_tx) out["controller_address"] = cfg.controller_address;
  out["commissioned"] = cfg.commissioned;
  out["fleet_passphrase_default"] = runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str());

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
    JsonArray sensorsArr = out["sensors"].to<JsonArray>();
    const SensorRegistry &localReg = sm_->localSensors();
    for (uint8_t i = 0; i < localReg.count(); ++i) {
      SensorReading r{};
      if (localReg.byIndex(i, r)) {
        JsonObject sObj = sensorsArr.add<JsonObject>();
        sObj["kind"] = sensorKindToString(r.kind);
        sObj["state"] = sensorStateToString(r.state);
        sObj["instance"] = r.instance;
        if (r.state == SensorState::Ok || r.state == SensorState::Overrange) {
          if (r.scale == 0) {
            sObj["value"] = r.value;
          } else {
            float divisor = 1.0f;
            for (uint8_t s = 0; s < r.scale; ++s) divisor *= 10.0f;
            sObj["value"] = static_cast<float>(r.value) / divisor;
          }
        }
        sObj["unit"] = sensorKindToUnit(r.kind);
      }
    }
  }
  sendOk(out, writer);
}

void AdminExecutor::handleGetConfig(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("get_config", "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr) {
    sendError("get_config", "config_unavailable", id, writer);
    return;
  }
  bool includeSecrets = parseBoolField(doc["include_secrets"], false);
  if (isMqtt) {
    includeSecrets = false;
  }
  JsonDocument out;
  out["cmd"] = "get_config";
  if (id[0] != '\0')
    out["id"] = id;
  writeSettingsJsonObject(out["config"].to<JsonObject>(), *config_, includeSecrets);
  sendOk(out, writer);
}

void AdminExecutor::handleSetConfig(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("set_config", "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr) {
    sendError("set_config", "config_unavailable", id, writer);
    return;
  }
  JsonObjectConst patch = doc["config"].as<JsonObjectConst>();
  if (patch.isNull()) {
    patch = doc.as<JsonObjectConst>();
  }
  if (patch.isNull()) {
    sendError("set_config", "invalid_config", id, writer);
    return;
  }

  if (isMqtt) {
    for (auto kv : patch) {
      const char *key = kv.key().c_str();
      if (!isMqttWritableConfigField(key)) {
        sendError("set_config", "security_write_restricted_over_mqtt", id, writer);
        return;
      }
    }
  }
  String error;
  bool networkChanged = false;
  bool otaAuthChanged = false;
  if (!applySettingsPatch(patch, *config_, error, networkChanged,
                          otaAuthChanged)) {
    sendError("set_config", error.c_str(), id, writer);
    return;
  }
  JsonDocument out;
  out["cmd"] = "set_config";
  if (id[0] != '\0')
    out["id"] = id;
  out["network_restarted"] = networkChanged;
  out["rebooting"] = otaAuthChanged;
  sendOk(out, writer);

  if (on_apply_) {
    delay(50); // Allow TX buffers to flush response
    on_apply_(on_apply_ctx_, networkChanged, otaAuthChanged);
  }
}

void AdminExecutor::handleFactoryReset(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("factory_reset", "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr) {
    sendError("factory_reset", "config_unavailable", id, writer);
    return;
  }
  const bool keepFleetKey =
      parseBoolField(doc["keep_shared_fleet_key"], false);
  const bool keepWifi = parseBoolField(doc["keep_wifi_credentials"], false);
  if (!config_->factoryReset(keepFleetKey, keepWifi)) {
    sendError("factory_reset", "save_failed", id, writer);
    return;
  }
  JsonDocument out;
  out["cmd"] = "factory_reset";
  if (id[0] != '\0')
    out["id"] = id;
  out["keep_shared_fleet_key"] = keepFleetKey;
  out["keep_wifi_credentials"] = keepWifi;
  sendOk(out, writer);
  delay(100);
  ESP.restart();
}

void AdminExecutor::handleConfigureGateway(JsonDocument &doc, ResponseWriter writer) {
  if (!requireAdmin(doc)) {
    sendError("configure_gateway", "auth_failed", requestId(doc), writer);
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError("configure_gateway", "runtime_unavailable", requestId(doc), writer);
    return;
  }

  const char *fleetKey = doc["fleet_passphrase"] | doc["fleet_key"] | "";
  if (strlen(fleetKey) < kMinDeploymentKeyLen ||
      runtime_utils::isDefaultDeploymentKey(fleetKey)) {
    sendError("configure_gateway", "invalid_fleet_key", requestId(doc), writer);
    return;
  }

  const uint8_t maxRemotes =
      clampMaxRemotes(doc["max_remotes"] | 1);
  auto &cfg = config_->settings();
  if (cfg.commissioned && cfg.role_tx &&
      !runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str()) &&
      !cfg.fleet_passphrase.equals(fleetKey)) {
    sendError("configure_gateway", "fleet_key_mismatch", requestId(doc), writer);
    return;
  }
  const bool preserveTargets = cfg.commissioned && cfg.role_tx &&
                               cfg.fleet_passphrase.equals(fleetKey) &&
                               cfg.known_peer_count > 0;
  cfg.commissioned = true;
  cfg.mode = "paired";
  cfg.role = "gateway";
  cfg.role_tx = true;
  const int requestedLocal = doc["local_address"] | runtime_utils::kGatewayAddress;
  cfg.local_address = (requestedLocal >= runtime_utils::kMinAddress && requestedLocal <= runtime_utils::kMaxAddress)
                          ? static_cast<uint8_t>(requestedLocal)
                          : runtime_utils::kGatewayAddress;
  if (cfg.local_address == 0 || cfg.local_address == 255)
    cfg.local_address = runtime_utils::kGatewayAddress;
  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  if (!preserveTargets) clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
  clearAddressList(cfg.allowed_controller_addresses,
                   cfg.allowed_controller_count);

  if (!config_->save()) {
    sendError("configure_gateway", "save_failed", requestId(doc), writer);
    return;
  }
  if (on_apply_)
    on_apply_(on_apply_ctx_, false, false);

  JsonDocument out;
  out["cmd"] = "configure_gateway";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["local_address"] = cfg.local_address;
  out["max_remotes"] = maxRemotes;
  out["known_peer_count"] = cfg.known_peer_count;
  out["preserved_targets"] = preserveTargets;
  sendOk(out, writer);
}

void AdminExecutor::handleWifiScan(JsonDocument &doc, ResponseWriter writer) {
  if (!requireAdmin(doc)) {
    sendError("wifi_scan", "auth_failed", requestId(doc), writer);
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
  sendOk(out, writer);
}

void AdminExecutor::handleConfigureWifi(JsonDocument &doc, ResponseWriter writer) {
  if (!requireAdmin(doc)) {
    sendError("configure_wifi", "auth_failed", requestId(doc), writer);
    return;
  }
  if (config_ == nullptr) {
    sendError("configure_wifi", "runtime_unavailable", requestId(doc), writer);
    return;
  }
  const char *ssid = nullptr;
  if (doc["wifi_sta_ssid"].is<const char *>() && doc["wifi_sta_ssid"].as<const char *>()[0] != '\0') {
    ssid = doc["wifi_sta_ssid"].as<const char *>();
  } else if (doc["ssid"].is<const char *>() && doc["ssid"].as<const char *>()[0] != '\0') {
    ssid = doc["ssid"].as<const char *>();
  }
  if (ssid == nullptr) ssid = "";
  const char *pass = nullptr;
  if (doc["wifi_sta_password"].is<const char *>()) {
    pass = doc["wifi_sta_password"].as<const char *>();
  } else if (doc["password_value"].is<const char *>()) {
    pass = doc["password_value"].as<const char *>();
  }
  if (pass == nullptr) pass = "";
  const size_t ssidLen = strlen(ssid);
  const size_t passLen = strlen(pass);
  if (ssidLen == 0U) {
    sendError("configure_wifi", "ssid_required", requestId(doc), writer);
    return;
  }
  if (ssidLen > 32U || passLen > 64U) {
    sendError("configure_wifi", "credentials_too_long", requestId(doc), writer);
    return;
  }

  auto &cfg = config_->settings();
  cfg.wifi_sta_ssid = ssid;
  cfg.wifi_sta_password = pass;
  if (!config_->save()) {
    sendError("configure_wifi", "save_failed", requestId(doc), writer);
    return;
  }
  if (on_apply_)
    on_apply_(on_apply_ctx_, true, false);

  JsonDocument out;
  out["cmd"] = "configure_wifi";
  const char *id = requestId(doc);
  if (id[0] != '\0')
    out["id"] = id;
  out["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  sendOk(out, writer);
}

void AdminExecutor::handleProvisionFleetWifi(JsonDocument &doc, ResponseWriter writer) {
  if (!requireAdmin(doc)) {
    sendError("provision_fleet_wifi", "auth_failed", requestId(doc), writer);
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError("provision_fleet_wifi", "runtime_unavailable", requestId(doc), writer);
    return;
  }

  const auto &cfg = config_->settings();
  const char *requestedSsid = nullptr;
  if (doc["wifi_sta_ssid"].is<const char *>() && doc["wifi_sta_ssid"].as<const char *>()[0] != '\0') {
    requestedSsid = doc["wifi_sta_ssid"].as<const char *>();
  } else if (doc["ssid"].is<const char *>() && doc["ssid"].as<const char *>()[0] != '\0') {
    requestedSsid = doc["ssid"].as<const char *>();
  }
  const char *requestedPass = nullptr;
  if (doc["wifi_sta_password"].is<const char *>()) {
    requestedPass = doc["wifi_sta_password"].as<const char *>();
  } else if (doc["password_value"].is<const char *>()) {
    requestedPass = doc["password_value"].as<const char *>();
  }
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
  LRS_LOGI(SYS,
           "event=provision_fleet_wifi_request wifi_sta_ssid_present=%d ssid_present=%d cfg_ssid_len=%u selected_ssid_len=%u",
           (doc["wifi_sta_ssid"].is<const char *>() && doc["wifi_sta_ssid"].as<const char *>()[0] != '\0') ? 1 : 0,
           (doc["ssid"].is<const char *>() && doc["ssid"].as<const char *>()[0] != '\0') ? 1 : 0,
           static_cast<unsigned>(cfg.wifi_sta_ssid.length()),
           static_cast<unsigned>(ssidLen));
  if (ssidLen == 0U) {
    {
      JsonDocument errDoc;
      errDoc["ok"] = false;
      errDoc["cmd"] = "provision_fleet_wifi";
      const char *id = requestId(doc);
      if (id != nullptr && id[0] != '\0') errDoc["id"] = id;
      errDoc["error"] = "ssid_required";
      errDoc["detail"] = String("wifi_sta_ssid_present=") +
          ((doc["wifi_sta_ssid"].is<const char *>() && doc["wifi_sta_ssid"].as<const char *>()[0] != '\0') ? "1" : "0") +
          " ssid_present=" +
          ((doc["ssid"].is<const char *>() && doc["ssid"].as<const char *>()[0] != '\0') ? "1" : "0") +
          " cfg_ssid_len=" + String(static_cast<unsigned>(cfg.wifi_sta_ssid.length()));
      String output;
      serializeJson(errDoc, output);
      writer(output);
    }
    return;
  }
  if (ssidLen > 32U || passLen > 64U) {
    sendError("provision_fleet_wifi", "credentials_too_long", requestId(doc), writer);
    return;
  }
  if (runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    sendError("provision_fleet_wifi", "fleet_key_default", requestId(doc), writer);
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
    String output;
    serializeJson(out, output);
    writer(output);
    return;
  }

  const uint8_t targetAddress = doc["target_address"] | 255;
  if (targetAddress == 0) {
    sendError("provision_fleet_wifi", "invalid_target_address", requestId(doc), writer);
    return;
  }
  if (!sm_->sendFleetWifiProvision(String(ssid), String(pass), targetAddress)) {
    sendError("provision_fleet_wifi", "send_failed", requestId(doc), writer);
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
  sendOk(out, writer);
  char maskedPass[32];
  lrslog::maskSecret(maskedPass, sizeof(maskedPass), pass);
  LRS_LOGI(API,
           "event=serial_fleet_wifi_provision_tx target=%u ssid=%s "
           "password=%s packets=%lu",
           static_cast<unsigned>(targetAddress), ssid,
           maskedPass,
           static_cast<unsigned long>(chunks + 2U));
}

void AdminExecutor::handleIdentify(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("identify", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("identify", "runtime_unavailable", id, writer);
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
  sendOk(out, writer);
  LRS_LOGI(API, "event=serial_identify_led duration_ms=%lu",
           static_cast<unsigned long>(durationMs));
}

void AdminExecutor::handleStartLoraInventory(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("start_lora_inventory", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("start_lora_inventory", "runtime_unavailable", id, writer);
    return;
  }
  if (config_ == nullptr) {
    sendError("start_lora_inventory", "config_unavailable", id, writer);
    return;
  }
  const auto &cfg = config_->settings();
  if (!cfg.commissioned) {
    sendError("start_lora_inventory", "not_commissioned", id, writer);
    return;
  }
  if (runtime_utils::isDefaultDeploymentKey(cfg.fleet_passphrase.c_str())) {
    sendError("start_lora_inventory", "factory_fleet_key", id, writer);
    return;
  }
  int start = doc["start_address"] | runtime_utils::kMinAddress;
  int end = doc["end_address"] | Settings::kAddressListCap;
  uint16_t intervalMs = static_cast<uint16_t>(doc["interval_ms"] | 1500);
  if (start < runtime_utils::kMinAddress)
    start = runtime_utils::kMinAddress;
  if (end > runtime_utils::kMaxAddress)
    end = runtime_utils::kMaxAddress;
  if (end < start || !sm_->fleetScanStart(static_cast<uint8_t>(start),
                                          static_cast<uint8_t>(end),
                                          intervalMs)) {
    sendError("start_lora_inventory", "start_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "start_lora_inventory";
  if (id[0] != '\0')
    out["id"] = id;
  out["start_address"] = start;
  out["end_address"] = end;
  out["interval_ms"] = intervalMs;
  sendOk(out, writer);
}

void AdminExecutor::handleLoraInventoryStatus(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *id = requestId(doc);
  if (sm_ == nullptr) {
    sendError("lora_inventory_status", "runtime_unavailable", id, writer);
    return;
  }

  static uint32_t lastLogMs = 0;
  const uint32_t nowMs = millis();
  bool shouldLog = (lastLogMs == 0 || nowMs - lastLogMs >= 30000UL);
  if (shouldLog) {
    lastLogMs = nowMs;
#if defined(ESP8266)
    LRS_LOGI(API, "lora_inventory_status HEAP before: free=%lu max_block=%lu frag=%u",
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMaxFreeBlockSize(),
             (unsigned)ESP.getHeapFragmentation());
#endif
  }

  JsonDocument out;
  out["cmd"] = "lora_inventory_status";
  if (id[0] != '\0')
    out["id"] = id;
  const uint32_t now = millis();

  FleetScanSnapshot scan{};
  sm_->fleetScanSnapshot(scan);
  JsonObject s = out["scan"].to<JsonObject>();
  if (scan.active) {
    s["active"] = true;
  }
  s["start_address"] = scan.start_address;
  s["end_address"] = scan.end_address;
  s["next_address"] = scan.next_address;
  s["interval_ms"] = scan.interval_ms;
  s["started_ms"] = scan.started_ms;
  s["last_tx_ms"] = scan.last_tx_ms;
  s["sent"] = scan.sent;
  s["now_ms"] = now;

  JsonArray devices = out["devices"].to<JsonArray>();

  uint8_t targets[Settings::kAddressListCap]{};
  uint8_t targetCount = 0;

  if (config_ != nullptr) {
    const auto &cfg = config_->settings();
    if (cfg.role_tx) {
      const bool isPairedMode = (cfg.mode == "paired");
      targetCount = runtime_utils::resolveGatewayTargets(
        isPairedMode,
        cfg.local_address,
        cfg.known_peer_count,
        cfg.known_peer_addresses,
        targets,
        Settings::kAddressListCap
      );
    }
  }

  for (uint8_t i = 0; i < targetCount; ++i) {
    const uint8_t addr = targets[i];
    if (addr < 1 || addr > 12) {
      continue;
    }

    PeerStatusSnapshot p{};
    bool hasCached = sm_->peerByAddress(addr, p);

    JsonObject row = devices.add<JsonObject>();
    row["address"] = addr;

    if (isMqtt) {
      if (hasCached && p.chip_id != 0) {
        char chipBuf[9];
        snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(p.chip_id & 0xFFFFFFUL));
        row["chip_id"] = chipBuf;
      }
      continue;
    }

    row["role"] = "remote";
    row["mode"] = "paired";
    if (hasCached && p.chip_id != 0) {
      char chipBuf[9];
      snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(p.chip_id & 0xFFFFFFUL));
      row["chip_id"] = chipBuf;
    }
  }

  JsonArray candidates = out["candidates"].to<JsonArray>();
  size_t cCount = sm_->candidateCount();
  size_t addedCount = 0;
  for (size_t i = 0; i < cCount; ++i) {
    if (isMqtt && addedCount >= 4) {
      break;
    }
    DiscoveryCandidate c{};
    if (sm_->candidateByIndex(i, c)) {
      JsonObject row = candidates.add<JsonObject>();
      row["address"] = c.address;
      if (c.chip_id != 0) {
        char chipBuf[9];
        snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(c.chip_id & 0xFFFFFFUL));
        row["chip_id"] = chipBuf;
      }
      row["rssi"] = c.rssi;
      if (!isMqtt) {
        row["last_seen_ms"] = c.last_seen_ms;
        if (c.last_seen_ms != 0) {
          row["age_ms"] = now - c.last_seen_ms;
        }
      }
      row["reason"] = candidateReasonToString(c.reason);
      row["state"] = candidateStateToString(c.state);
      addedCount++;
    }
  }

  out["candidate_total"] = cCount;
  out["candidate_truncated"] = isMqtt && (cCount > 4);

  if (sm_->isAdoptionActive()) {
    JsonObject adoptObj = out["adoption"].to<JsonObject>();
    adoptObj["active"] = true;
    char chipBuf[9];
    snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(sm_->adoptionChipId() & 0xFFFFFFUL));
    adoptObj["chip_id"] = chipBuf;
    adoptObj["assigned_address"] = sm_->adoptionAddress();
  }

  if (shouldLog) {

    size_t jsonSize = measureJson(out);
#if defined(ESP8266)
    LRS_LOGI(API, "lora_inventory_status HEAP after: free=%lu max_block=%lu frag=%u size=%u",
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)ESP.getMaxFreeBlockSize(),
             (unsigned)ESP.getHeapFragmentation(),
             (unsigned)jsonSize);
#else
    LRS_LOGI(API, "lora_inventory_status size=%u", (unsigned)jsonSize);
#endif
  }

  sendOk(out, writer);
}

void AdminExecutor::handleLoraInventoryPeer(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *id = requestId(doc);
  const uint8_t addr = static_cast<uint8_t>(doc["address"] | 0);
  const bool includeDiagnostics = doc["include_diagnostics"] | false;
  if (sm_ == nullptr) {
    sendError("lora_inventory_peer", "runtime_unavailable", id, writer);
    return;
  }
  if (addr < 1 || addr > Settings::kAddressListCap) {
    sendError("lora_inventory_peer", "invalid_address", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "lora_inventory_peer";
  if (id[0] != '\0')
    out["id"] = id;

  const uint32_t now = millis();
  PeerStatusSnapshot p{};
  const bool hasCached = sm_->peerByAddress(addr, p);
  JsonObject row = out["device"].to<JsonObject>();
  row["address"] = addr;

  if (isMqtt) {
    if (hasCached && p.chip_id != 0) {
      char chipBuf[9];
      snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(p.chip_id & 0xFFFFFFUL));
      row["chip_id"] = chipBuf;
    }
    sendOk(out, writer);
    return;
  }

  row["role"] = "remote";
  row["mode"] = "paired";
  if (!hasCached) {
    row["ota_reason"] = "wifi_status_unknown";
    sendOk(out, writer);
    return;
  }

  if (p.wifi_state_known)
    row["wifi_enabled_known"] = true;
  if (p.wifi_enabled)
    row["wifi_enabled"] = true;
  if (p.wifi_connected_known)
    row["wifi_connected_known"] = true;
  if (p.wifi_connected)
    row["wifi_connected"] = true;
  if (p.wifi_connected && p.ip[0] != 0) {
    char ipBuf[16];
    snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);
    row["ip"] = ipBuf;
  }
  if (p.mqtt_state_known)
    row["mqtt_known"] = true;
  if (p.mqtt_enabled)
    row["mqtt_enabled"] = true;
  if (p.mqtt_connected)
    row["mqtt_connected"] = true;
  if (p.power_save_listen_only)
    row["power_save_listen_only"] = true;
  if (p.power_save_active)
    row["power_save_active"] = true;
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
    }
  }
  if (p.uptime_ms > 0)
    row["uptime_ms"] = p.uptime_ms;
  if (p.last_seen_ms != 0) {
    row["relay_state"] = p.relay_state;
    if (p.input_state_known) {
      row["input_state_known"] = true;
      row["input_state"] = p.input_state;
    }
    if (p.sensors.count() > 0) {
      JsonArray sensorsArr = row["sensors"].to<JsonArray>();
      for (uint8_t j = 0; j < p.sensors.count(); ++j) {
        SensorReading r{};
        if (p.sensors.byIndex(j, r)) {
          JsonObject sObj = sensorsArr.add<JsonObject>();
          sObj["kind"] = sensorKindToString(r.kind);
          sObj["state"] = sensorStateToString(r.state);
          sObj["instance"] = r.instance;
          if (r.state == SensorState::Ok || r.state == SensorState::Overrange) {
            if (r.scale == 0) {
              sObj["value"] = r.value;
            } else {
              float divisor = 1.0f;
              for (uint8_t s = 0; s < r.scale; ++s) divisor *= 10.0f;
              sObj["value"] = static_cast<float>(r.value) / divisor;
            }
          }
          sObj["unit"] = sensorKindToUnit(r.kind);
        }
      }
    }
  }
  if (p.wifi_connected && p.wifi_rssi_dbm != 0)
    row["wifi_rssi_dbm"] = p.wifi_rssi_dbm;
  if (includeDiagnostics && p.maintenance_debug_known) {
    row["maintenance_debug_known"] = true;
    row["heap_free"] = p.heap_free;
    row["heap_max_block"] = p.heap_max_block;
    row["heap_frag_pct"] = p.heap_frag_pct;
    row["debug_uptime_ms"] = p.debug_uptime_ms;
  }
  row["rssi"] = p.uplink_rssi;
  if (p.last_seen_ms != 0) {
    row["last_seen_ms"] = p.last_seen_ms;
    row["age_ms"] = now - p.last_seen_ms;
  }
  if (p.poll_pending)
    row["poll_pending"] = true;

  const bool otaEligible = p.wifi_connected_known && p.wifi_connected && p.ip[0] != 0;
  if (otaEligible) {
    row["ota_eligible"] = true;
    row["ota_reason"] = "ready";
  } else if (p.wifi_connected_known) {
    row["ota_reason"] = p.wifi_connected ? "ip_missing" : "wifi_offline";
  } else {
    row["ota_reason"] = "wifi_status_unknown";
  }
  sendOk(out, writer);
}

void AdminExecutor::handleRefreshLoraPeer(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("refresh_lora_peer", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("refresh_lora_peer", "runtime_unavailable", id, writer);
    return;
  }
  uint8_t address = doc["address"] | 0;
  if (address < runtime_utils::kMinAddress || address > Settings::kAddressListCap) {
    sendError("refresh_lora_peer", "invalid_address", id, writer);
    return;
  }

  uint32_t sentCounter = 0;
  if (!sm_->sendMaintenanceRequest(address, false, &sentCounter)) {
    sendError("refresh_lora_peer", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "refresh_lora_peer";
  if (id[0] != '\0')
    out["id"] = id;
  out["address"] = address;
  out["counter"] = sentCounter;
  sendOk(out, writer);
}

void AdminExecutor::handleCancelLoraInventory(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("cancel_lora_inventory", "auth_failed", id, writer);
    return;
  }
  if (sm_ != nullptr)
    sm_->fleetScanCancel();
  JsonDocument out;
  out["cmd"] = "cancel_lora_inventory";
  if (id[0] != '\0')
    out["id"] = id;
  sendOk(out, writer);
}

void AdminExecutor::handleUdpLogControl(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("udp_log_control", "auth_failed", id, writer);
    return;
  }
  if (!isMqtt) {
    sendError("udp_log_control", "mqtt_required", id, writer);
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
    sendError("udp_log_control", "invalid_target", id, writer);
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
  sendOk(out, writer);
}

void AdminExecutor::handleRemoteUdpLogControl(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_udp_log_control", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_udp_log_control", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_udp_log_control", "invalid_address", id, writer);
    return;
  }
  const uint8_t addr = static_cast<uint8_t>(rawAddr);
  const bool enabled = parseBoolField(doc["enabled"], true);
  if (enabled && !sm_->isPeerUdpLogsEligible(addr)) {
    sendError("remote_udp_log_control", "peer_wifi_unavailable", id, writer);
    return;
  }
  uint32_t ttlS = doc["ttl_s"] | 300UL;
  if (ttlS > 3600UL)
    ttlS = 3600UL;

  IPAddress host;
  const uint16_t port = static_cast<uint16_t>(doc["port"] | 5514);
  const char *hostStr = doc["host"] | "";
  if (enabled && (port == 0 || !host.fromString(hostStr))) {
    sendError("remote_udp_log_control", "invalid_target", id, writer);
    return;
  }

  if (!sm_->mqttSetPeerUdpLogControl(addr, enabled, host, port, ttlS)) {
    sendError("remote_udp_log_control", "send_failed", id, writer);
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
  sendOk(out, writer);
}

void AdminExecutor::handlePollDiagnostics(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("poll_diagnostics", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("poll_diagnostics", "runtime_unavailable", id, writer);
    return;
  }
  uint8_t address = doc["address"] | 0;
  if (address < runtime_utils::kMinAddress || address > runtime_utils::kMaxAddress) {
    sendError("poll_diagnostics", "invalid_address", id, writer);
    return;
  }

  uint32_t sentCounter = 0;
  if (!sm_->sendMaintenanceRequest(address, true, &sentCounter)) {
    sendError("poll_diagnostics", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "poll_diagnostics";
  if (id[0] != '\0')
    out["id"] = id;
  out["address"] = address;
  out["counter"] = sentCounter;
  sendOk(out, writer);
}

void AdminExecutor::handleRemoteOtaPull(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_ota_pull", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_ota_pull", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_ota_pull", "invalid_address", id, writer);
    return;
  }

  IPAddress host;
  const uint16_t port = static_cast<uint16_t>(doc["port"] | 0);
  const char *hostStr = doc["host"] | "";
  if (port == 0 || !host.fromString(hostStr)) {
    sendError("remote_ota_pull", "invalid_target", id, writer);
    return;
  }

  const char *sha256 = doc["sha256"] | "";
  if (sha256[0] == '\0') {
    sendError("remote_ota_pull", "sha256_required", id, writer);
    return;
  }
  if (!isSha256Hex(sha256)) {
    sendError("remote_ota_pull", "invalid_sha256", id, writer);
    return;
  }

  if (sm_->isOtaPullTxActive()) {
    sendError("remote_ota_pull", "gateway_busy", id, writer);
    return;
  }

  if (!sm_->sendPeerOtaPullControl(static_cast<uint8_t>(rawAddr), host, port, sha256)) {
    sendError("remote_ota_pull", "send_failed", id, writer);
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
  sendOk(out, writer);
}

void AdminExecutor::otaStatusCallback(const char *status, void *ctx) {
  auto *self = static_cast<AdminExecutor *>(ctx);
  if (self && self->ota_status_publisher_) {
    self->ota_status_publisher_(self->ota_status_publisher_ctx_, status);
  }
}

void AdminExecutor::handleOtaPull(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("ota_pull", "auth_failed", id, writer);
    return;
  }
  const char *url = doc["url"] | "";
  const char *sha256 = doc["sha256"] | "";
  String error;
  if (!otaPullFromUrl(url, sha256, error, AdminExecutor::otaStatusCallback, this)) {
    if (ota_status_publisher_) {
      char errBuf[128];
      snprintf(errBuf, sizeof(errBuf), "failed:%s", error.c_str());
      ota_status_publisher_(ota_status_publisher_ctx_, errBuf);
    }
    sendError("ota_pull", error.c_str(), id, writer);
    return;
  }

  if (ota_status_publisher_) {
    ota_status_publisher_(ota_status_publisher_ctx_, "rebooting");
  }

  if (config_ != nullptr) {
    if (!config_->writePostOtaWifiFastMarker()) {
      LRS_LOGE(SYS, "event=post_ota_wifi_fast_marker_write_failed");
    }
  }

  JsonDocument out;
  out["cmd"] = "ota_pull";
  if (id[0] != '\0')
    out["id"] = id;
  out["rebooting"] = true;
  sendOk(out, writer);
  delay(150);
  ESP.restart();
}

void AdminExecutor::handleRemoteReboot(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_reboot", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_reboot", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_reboot", "invalid_address", id, writer);
    return;
  }

  if (!sm_->sendPeerReboot(static_cast<uint8_t>(rawAddr))) {
    sendError("remote_reboot", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_reboot";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  sendOk(out, writer);
}

void AdminExecutor::handleRemoteSensorConfig(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_sensor_config", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_sensor_config", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_sensor_config", "invalid_address", id, writer);
    return;
  }

  if (doc["sensor_temp_enabled"].isNull() || doc["sensor_tank_enabled"].isNull()) {
    sendError("remote_sensor_config", "missing_parameters", id, writer);
    return;
  }

  const bool tempEnabled = doc["sensor_temp_enabled"] | false;
  const bool tankEnabled = doc["sensor_tank_enabled"] | false;
  const bool powerSaveEnabled = doc["power_save_listen_only"] | false;
  const bool powerSaveBootGrace = true;

  if (!sm_->sendPeerSensorConfig(static_cast<uint8_t>(rawAddr), tempEnabled, tankEnabled, powerSaveEnabled, powerSaveBootGrace)) {
    sendError("remote_sensor_config", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_sensor_config";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  out["sensor_temp_enabled"] = tempEnabled;
  out["sensor_tank_enabled"] = tankEnabled;
  out["power_save_listen_only"] = powerSaveEnabled;
  sendOk(out, writer);
}

void AdminExecutor::handleRemoteFleetKeyChange(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_fleet_key_change", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_fleet_key_change", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_fleet_key_change", "invalid_address", id, writer);
    return;
  }

  const char *newKey = doc["fleet_passphrase"] | doc["new_fleet_passphrase"] | doc["fleet_key"] | "";
  const size_t keyLen = strlen(newKey);
  if (keyLen < kMinDeploymentKeyLen) {
    sendError("remote_fleet_key_change", "fleet_passphrase_too_short", id, writer);
    return;
  }
  if (keyLen > 64) {
    sendError("remote_fleet_key_change", "fleet_passphrase_too_long", id, writer);
    return;
  }
  if (runtime_utils::isDefaultDeploymentKey(newKey)) {
    sendError("remote_fleet_key_change", "fleet_passphrase_default_blocked", id, writer);
    return;
  }

  if (!sm_->sendPeerFleetKeyChange(static_cast<uint8_t>(rawAddr), String(newKey))) {
    sendError("remote_fleet_key_change", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_fleet_key_change";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  sendOk(out, writer);
}

void AdminExecutor::handleRemoteFactoryReset(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  if (!requireAdmin(doc)) {
    sendError("remote_factory_reset", "auth_failed", id, writer);
    return;
  }
  if (sm_ == nullptr) {
    sendError("remote_factory_reset", "runtime_unavailable", id, writer);
    return;
  }
  const int rawAddr = doc["addr"] | doc["address"] | doc["target_address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError("remote_factory_reset", "invalid_address", id, writer);
    return;
  }

  const bool keepFleetKey = doc["keep_shared_fleet_key"] | doc["keep_fleet_key"] | false;
  const bool keepWifi = doc["keep_wifi_credentials"] | doc["keep_wifi"] | false;

  if (!sm_->sendPeerFactoryReset(static_cast<uint8_t>(rawAddr), keepFleetKey, keepWifi)) {
    sendError("remote_factory_reset", "send_failed", id, writer);
    return;
  }

  JsonDocument out;
  out["cmd"] = "remote_factory_reset";
  const char *reqId = requestId(doc);
  if (reqId[0] != '\0') out["id"] = reqId;
  out["target_address"] = rawAddr;
  out["keep_shared_fleet_key"] = keepFleetKey;
  out["keep_wifi_credentials"] = keepWifi;
  sendOk(out, writer);
}

void AdminExecutor::handleSetGatewayTargets(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  const char *cmd = "set_gateway_targets";
  if (!requireAdmin(doc)) {
    sendError(cmd, "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError(cmd, "runtime_unavailable", id, writer);
    return;
  }
  JsonArrayConst arr = doc["addresses"].as<JsonArrayConst>();
  if (arr.isNull() || arr.size() == 0 ||
      arr.size() > Settings::kAddressListCap) {
    sendError(cmd, "invalid_addresses", id, writer);
    return;
  }
  auto &cfg = config_->settings();
  const bool replaceTargets = doc["replace"] | false;
  bool used[256]{};
  used[0] = true;
  used[255] = true;
  used[cfg.local_address] = true;
  uint8_t targetAddresses[Settings::kAddressListCap]{};
  uint8_t targetCount = 0;
  auto appendTarget = [&](uint8_t addr) -> bool {
    if (addr < runtime_utils::kMinAddress || addr > runtime_utils::kMaxAddress || addr == cfg.local_address) return false;
    if (used[addr]) return true;
    if (targetCount >= Settings::kAddressListCap) return false;
    used[addr] = true;
    targetAddresses[targetCount++] = addr;
    return true;
  };
  if (!replaceTargets) {
    for (uint8_t i = 0; i < cfg.known_peer_count && i < Settings::kAddressListCap; ++i) {
      if (!appendTarget(cfg.known_peer_addresses[i])) {
        sendError(cmd, "too_many_targets", id, writer);
        return;
      }
    }
  }
  for (JsonVariantConst v : arr) {
    const int raw = v.as<int>();
    if (raw < runtime_utils::kMinAddress || raw > runtime_utils::kMaxAddress || raw == cfg.local_address) {
      sendError(cmd, "invalid_addresses", id, writer);
      return;
    }
    if (!appendTarget(static_cast<uint8_t>(raw))) {
      sendError(cmd, "too_many_targets", id, writer);
      return;
    }
  }
  if (targetCount == 0) {
    sendError(cmd, "invalid_addresses", id, writer);
    return;
  }

  uint8_t  prevAddresses[Settings::kAddressListCap]{};
  uint32_t prevChipIds[Settings::kAddressListCap]{};
  const uint8_t prevCount = cfg.known_peer_count < Settings::kAddressListCap
                                ? cfg.known_peer_count
                                : Settings::kAddressListCap;
  memcpy(prevAddresses, cfg.known_peer_addresses, sizeof(prevAddresses));
  memcpy(prevChipIds, cfg.known_peer_chip_ids, sizeof(prevChipIds));

  clearAddressList(cfg.known_peer_addresses, cfg.known_peer_count);
  memset(cfg.known_peer_chip_ids, 0, sizeof(cfg.known_peer_chip_ids));
  for (uint8_t i = 0; i < targetCount; ++i) {
    const uint8_t addr = targetAddresses[i];
    cfg.known_peer_addresses[cfg.known_peer_count++] = addr;

    uint32_t chipId = sm_->resolveChipIdForAddress(addr);
    if (chipId == 0) chipId = sm_->activePeerChipIdForAddress(addr);
    if (chipId == 0) {
      for (uint8_t j = 0; j < prevCount; ++j) {
        if (prevAddresses[j] == addr) { chipId = prevChipIds[j]; break; }
      }
    }
    cfg.known_peer_chip_ids[cfg.known_peer_count - 1] = chipId;
  }
  if (!config_->save()) {
    sendError(cmd, "save_failed", id, writer);
    return;
  }
  if (on_apply_)
    on_apply_(on_apply_ctx_, false, false);
  JsonDocument out;
  out["cmd"] = cmd;
  if (id[0] != '\0')
    out["id"] = id;
  out["target_count"] = cfg.known_peer_count;
  out["replace"] = replaceTargets;
  sendOk(out, writer);
}

void AdminExecutor::handleForgetGatewayTarget(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  const char *cmd = "forget_gateway_target";
  if (!requireAdmin(doc)) {
    sendError(cmd, "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError(cmd, "runtime_unavailable", id, writer);
    return;
  }
  int rawAddr = doc["address"] | 0;
  if (rawAddr < runtime_utils::kMinAddress || rawAddr > runtime_utils::kMaxAddress) {
    sendError(cmd, "invalid_address", id, writer);
    return;
  }
  const uint8_t addr = static_cast<uint8_t>(rawAddr);
  auto &cfg = config_->settings();
  bool found = false;

  uint32_t chipId = 0;
  for (uint8_t i = 0; i < cfg.known_peer_count; ++i) {
    if (cfg.known_peer_addresses[i] == addr) {
      chipId = cfg.known_peer_chip_ids[i];
      for (uint8_t j = i; j + 1 < cfg.known_peer_count; ++j) {
        cfg.known_peer_addresses[j] = cfg.known_peer_addresses[j + 1];
        cfg.known_peer_chip_ids[j] = cfg.known_peer_chip_ids[j + 1];
      }
      cfg.known_peer_addresses[--cfg.known_peer_count] = 0;
      cfg.known_peer_chip_ids[cfg.known_peer_count] = 0;
      found = true;
      break;
    }
  }

  MqttBridge::clearPeerRetained(addr, chipId);
  sm_->mqttForgetPeer(addr);

  if (found) {
    if (!config_->save()) {
      sendError(cmd, "save_failed", id, writer);
      return;
    }
    if (on_apply_)
      on_apply_(on_apply_ctx_, false, false);
  }

  JsonDocument out;
  out["cmd"] = cmd;
  if (id[0] != '\0')
    out["id"] = id;
  out["forgotten"] = found;
  out["target_count"] = cfg.known_peer_count;
  sendOk(out, writer);
}

void AdminExecutor::handleCommand(JsonDocument &doc, ResponseWriter writer, bool isMqtt) {
  const char *cmd = cmdName(doc);
  const char *id = requestId(doc);
  if (cmd == nullptr || cmd[0] == '\0') {
    sendError("unknown", "missing_cmd", id, writer);
    return;
  }

  if (strcmp(cmd, "admin_challenge") == 0) {
    handleAdminChallenge(doc, writer);
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
    sendOk(out, writer);
    return;
  }

  if (strcmp(cmd, "identity") == 0) {
    if (config_ == nullptr) {
      sendError(cmd, "config_unavailable", id, writer);
      return;
    }
    const auto &cfg = config_->settings();
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["chip_id"] = config_->chipIdHex();
    out["ap_ssid"] = config_->apSsid();
    out["mode"] = cfg.mode;
    out["role"] = cfg.role_tx ? "gateway" : "remote";
    out["role_tx"] = cfg.role_tx;
    out["local_address"] = cfg.local_address;
    if (!cfg.role_tx) out["controller_address"] = cfg.controller_address;
    out["fw_version"] = LRS_FW_VERSION;
    sendOk(out, writer);
    return;
  }

  if (strcmp(cmd, "status") == 0) {
    handleStatus(doc, writer);
    return;
  }

  if (strcmp(cmd, "get_config") == 0) {
    handleGetConfig(doc, writer, isMqtt);
    return;
  }

  if (strcmp(cmd, "set_config") == 0) {
    handleSetConfig(doc, writer, isMqtt);
    return;
  }

  if (strcmp(cmd, "factory_reset") == 0) {
    handleFactoryReset(doc, writer);
    return;
  }

  if (strcmp(cmd, "configure_gateway") == 0) {
    handleConfigureGateway(doc, writer);
    return;
  }

  if (strcmp(cmd, "wifi_scan") == 0) {
    handleWifiScan(doc, writer);
    return;
  }

  if (strcmp(cmd, "configure_wifi") == 0) {
    handleConfigureWifi(doc, writer);
    return;
  }

  if (strcmp(cmd, "provision_fleet_wifi") == 0) {
    handleProvisionFleetWifi(doc, writer);
    return;
  }

  if (strcmp(cmd, "identify") == 0) {
    handleIdentify(doc, writer);
    return;
  }

  if (strcmp(cmd, "start_lora_inventory") == 0) {
    handleStartLoraInventory(doc, writer);
    return;
  }

  if (strcmp(cmd, "lora_inventory_status") == 0) {
    handleLoraInventoryStatus(doc, writer, isMqtt);
    return;
  }

  if (strcmp(cmd, "lora_inventory_peer") == 0) {
    handleLoraInventoryPeer(doc, writer, isMqtt);
    return;
  }

  if (strcmp(cmd, "refresh_lora_peer") == 0) {
    handleRefreshLoraPeer(doc, writer);
    return;
  }

  if (strcmp(cmd, "cancel_lora_inventory") == 0) {
    handleCancelLoraInventory(doc, writer);
    return;
  }

  if (strcmp(cmd, "poll_diagnostics") == 0) {
    handlePollDiagnostics(doc, writer);
    return;
  }

  if (strcmp(cmd, "udp_log_control") == 0) {
    handleUdpLogControl(doc, writer, isMqtt);
    return;
  }

  if (strcmp(cmd, "remote_udp_log_control") == 0) {
    handleRemoteUdpLogControl(doc, writer);
    return;
  }

  if (strcmp(cmd, "remote_ota_pull") == 0) {
    handleRemoteOtaPull(doc, writer);
    return;
  }

  if (strcmp(cmd, "remote_reboot") == 0) {
    handleRemoteReboot(doc, writer);
    return;
  }

  if (strcmp(cmd, "remote_sensor_config") == 0) {
    handleRemoteSensorConfig(doc, writer);
    return;
  }

  if (strcmp(cmd, "remote_fleet_key_change") == 0) {
    handleRemoteFleetKeyChange(doc, writer);
    return;
  }

  if (strcmp(cmd, "remote_factory_reset") == 0) {
    handleRemoteFactoryReset(doc, writer);
    return;
  }

  if (strcmp(cmd, "ota_pull") == 0) {
    handleOtaPull(doc, writer);
    return;
  }

  if (strcmp(cmd, "set_gateway_targets") == 0) {
    handleSetGatewayTargets(doc, writer);
    return;
  }

  if (strcmp(cmd, "forget_gateway_target") == 0) {
    handleForgetGatewayTarget(doc, writer);
    return;
  }

  if (strcmp(cmd, "adopt_candidate") == 0) {
    handleAdoptCandidate(doc, writer);
    return;
  }


  if (strcmp(cmd, "start_discovery") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id, writer);
      return;
    }
    const uint8_t maxRemotes =
        clampMaxRemotes(doc["max_remotes"] | Settings::kAddressListCap);
    if (sm_ == nullptr || !sm_->provisioningStartDiscovery(maxRemotes)) {
      sendError(cmd, "start_failed", id, writer);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    out["max_remotes"] = maxRemotes;
    sendOk(out, writer);
    return;
  }

  if (strcmp(cmd, "provision_all") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id, writer);
      return;
    }
    if (sm_ == nullptr || !sm_->provisioningStartProvisionAll()) {
      sendError(cmd, "invalid_state", id, writer);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out, writer);
    return;
  }

  if (strcmp(cmd, "provisioning_status") == 0) {
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    buildProvisioningStatus(out, isMqtt);
    if (out["ok"].isNull())
      sendOk(out, writer);
    else {
      String output;
      serializeJson(out, output);
      writer(output);
    }
    return;
  }

  if (strcmp(cmd, "cancel_provisioning") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id, writer);
      return;
    }
    if (sm_ != nullptr)
      sm_->provisioningCancel();
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out, writer);
    return;
  }

  if (strcmp(cmd, "reboot") == 0) {
    if (!requireAdmin(doc)) {
      sendError(cmd, "auth_failed", id, writer);
      return;
    }
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0')
      out["id"] = id;
    sendOk(out, writer);
    delay(100);
    ESP.restart();
    return;
  }

  sendError(cmd, "unknown_cmd", id, writer);
}

void AdminExecutor::handleAdoptCandidate(JsonDocument &doc, ResponseWriter writer) {
  const char *id = requestId(doc);
  const char *cmd = "adopt_candidate";
  if (!requireAdmin(doc)) {
    sendError(cmd, "auth_failed", id, writer);
    return;
  }
  if (config_ == nullptr || sm_ == nullptr) {
    sendError(cmd, "runtime_unavailable", id, writer);
    return;
  }
  
  uint32_t chipId = 0;
  if (doc["chip_id"].is<const char*>()) {
    chipId = strtoul(doc["chip_id"].as<const char*>(), nullptr, 16);
  } else {
    chipId = doc["chip_id"] | 0;
  }
  
  if (chipId == 0) {
    sendError(cmd, "invalid_chip_id", id, writer);
    return;
  }
  
  DiscoveryCandidate c{};
  if (!sm_->candidateByChipId(chipId, c)) {
    sendError(cmd, "candidate_not_found", id, writer);
    return;
  }
  
  if (c.state != CandidateState::Identified && c.state != CandidateState::Failed) {
    sendError(cmd, "candidate_not_adoptable", id, writer);
    return;
  }
  
  const auto &cfg = config_->settings();
  uint8_t assignedAddress = runtime_utils::resolveAdoptionAddress(c.address, chipId, cfg.known_peer_count, cfg.known_peer_addresses, cfg.known_peer_chip_ids);
  bool isReset = (assignedAddress == 0);
  
  if (sm_->startAdoption(chipId, assignedAddress, isReset)) {
    JsonDocument out;
    out["cmd"] = cmd;
    if (id[0] != '\0') out["id"] = id;
    
    char chipBuf[9];
    snprintf(chipBuf, sizeof(chipBuf), "%06lx", static_cast<unsigned long>(chipId & 0xFFFFFFUL));
    out["chip_id"] = chipBuf;
    out["assigned_address"] = assignedAddress;
    out["reset_requested"] = isReset;
    sendOk(out, writer);
  } else {
    sendError(cmd, "adoption_start_failed", id, writer);
  }
}

bool AdminExecutor::addPeerToConfig(uint32_t chipId, uint8_t address) {
  if (config_ == nullptr) return false;
  auto &cfg = config_->settings();
  
  int existingIdx = -1;
  for (size_t i = 0; i < cfg.known_peer_count; ++i) {
    if (cfg.known_peer_chip_ids[i] == chipId) {
      existingIdx = static_cast<int>(i);
      break;
    }
  }
  
  if (existingIdx >= 0) {
    const uint8_t oldAddr = cfg.known_peer_addresses[existingIdx];
    if (oldAddr != address) {
      cfg.known_peer_addresses[existingIdx] = address;
    }
  } else {
    if (cfg.known_peer_count >= Settings::kAddressListCap) {
      return false;
    }
    cfg.known_peer_addresses[cfg.known_peer_count] = address;
    cfg.known_peer_chip_ids[cfg.known_peer_count] = chipId;
    cfg.known_peer_count++;
  }
  
  if (config_->save()) {
    if (on_apply_) {
      on_apply_(on_apply_ctx_, false, false);
    }
    return true;
  }
  return false;
}
