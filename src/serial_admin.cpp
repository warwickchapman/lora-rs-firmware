#include "serial_admin.h"

#include <cstring>

#include <ESP8266WiFi.h>

#include "build_info.h"
#include "logger.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

namespace {
constexpr size_t kMaxLineBytes = 1536;
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
