#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
#include "logger.h"
#include "web_console_internal.h"
#include "web_console_ui_assets.h"

using namespace webconsole_internal;

namespace {
bool parseRoleTxFromModeRole(const String &mode, const String &role,
                             bool &roleTx) {
  if (mode == "paired") {
    if (role == "transmitter") {
      roleTx = true;
      return true;
    }
    if (role == "receiver") {
      roleTx = false;
      return true;
    }
    return false;
  }
  if (mode == "mesh") {
    if (role == "coordinator") {
      roleTx = true;
      return true;
    }
    if (role == "node") {
      roleTx = false;
      return true;
    }
    return false;
  }
  if (mode == "standalone") {
    if (role == "none") {
      roleTx = true;
      return true;
    }
    return false;
  }
  return false;
}

const char *formatIp(const IPAddress &ip, char out[16]) {
  snprintf(out, 16, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return out;
}

constexpr size_t kProgmemHtmlChunkBytes = 768;

bool sendProgmemHtml(ESP8266WebServer &server, int code,
                     const char *contentType, PGM_P html) {
  const size_t len = strlen_P(html);
  server.setContentLength(len);
  server.send(code, contentType, "");

  char chunk[kProgmemHtmlChunkBytes];
  size_t sent = 0;
  while (sent < len) {
    const size_t n = (len - sent > kProgmemHtmlChunkBytes)
                         ? kProgmemHtmlChunkBytes
                         : (len - sent);
    memcpy_P(chunk, html + sent, n);

    size_t writtenTotal = 0;
    while (writtenTotal < n) {
      const size_t written = server.client().write(
          reinterpret_cast<const uint8_t *>(chunk + writtenTotal),
          n - writtenTotal);
      if (written == 0)
        break;
      writtenTotal += written;
    }
    if (writtenTotal < n)
      break;

    sent += writtenTotal;
    if ((sent % (kProgmemHtmlChunkBytes * 4)) == 0)
      delay(0);
  }
  return sent == len;
}
} // namespace

void WebConsole::handleIndex() {
  if (!requireAuth(false))
    return;
  if (needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/setup");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t maxBlockBefore = lrslog::heapMaxFreeBlock();
  const uint8_t fragBefore = lrslog::heapFragPercent();
  const bool heapTight = (heapBefore < kIndexLowHeapRejectFreeBytes) ||
                         (maxBlockBefore < kIndexLowHeapRejectMaxBlockBytes) ||
                         (fragBefore > 45U);
  const bool forceFull =
      server_.hasArg("force_full") && server_.arg("force_full") != "0";
  LRS_LOGI(
      WEB,
      "event=index_send_start heap_free=%lu heap_frag=%u max_free_block=%lu",
      static_cast<unsigned long>(heapBefore), static_cast<unsigned>(fragBefore),
      static_cast<unsigned long>(maxBlockBefore));
  if (!forceFull && heapTight) {
    LRS_LOGW(WEB,
             "event=index_send_reject_low_heap heap_free=%lu heap_frag=%u "
             "max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
    LRS_LOGI(WEB, "event=index_send_low_heap_fallback");
    markResponseStatus(200);
    setUiNoStoreHeaders();
    if (!sendProgmemHtml(server_, 200, "text/html", kIndexLowHeapHtml)) {
      LRS_LOGW(WEB, "event=index_low_heap_html_partial");
    }
    return;
  }
  if (forceFull && heapTight) {
    LRS_LOGW(WEB,
             "event=index_send_force_low_heap heap_free=%lu heap_frag=%u "
             "max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  if (!sendProgmemHtml(server_, 200, "text/html", kIndexHtml)) {
    LRS_LOGW(WEB, "event=index_html_partial");
  }
  LRS_LOGI(
      WEB,
      "event=index_send_done heap_free=%lu heap_frag=%u max_free_block=%lu",
      static_cast<unsigned long>(lrslog::heapFree()),
      static_cast<unsigned>(lrslog::heapFragPercent()),
      static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
}

void WebConsole::handleLoginPage() {
  if (hasSession() && cookieValue("lrs_session") == session_token_) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", needsFleetSetupPrompt() ? "/setup" : "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  if (!sendProgmemHtml(server_, 200, "text/html", kLoginHtml)) {
    LRS_LOGW(WEB, "event=login_html_partial");
  }
}

void WebConsole::handleFleetSetupPage() {
  if (!requireAuth(false))
    return;
  if (!needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  if (!sendProgmemHtml(server_, 200, "text/html", kFleetSetupHtml)) {
    LRS_LOGW(WEB, "event=setup_html_partial");
  }
}

void WebConsole::handleLoginApi() {
  char ip[16];
  formatIp(server_.client().remoteIP(), ip);
  if (locked_until_ms_ != 0 &&
      static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    sendTracked(429, "application/json",
                "{\"error\":\"Too many failed logins. Try again shortly.\"}");
    LRS_LOGW(API, "event=login_blocked ip=%s", ip);
    return;
  }

  DynamicJsonDocument doc(256);
  StaticJsonDocument<64> filter;
  filter["password"] = true;
  auto err = deserializeJson(doc, server_.arg("plain"),
                             DeserializationOption::Filter(filter));
  if (err) {
    sendTracked(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  const char *posted = doc["password"] | "";
  if (!config_->settings().admin_password.equals(posted)) {
    failed_auth_++;
    if (failed_auth_ >= 5) {
      locked_until_ms_ = millis() + 60000;
      failed_auth_ = 0;
    }
    sendTracked(401, "application/json", "{\"error\":\"Invalid password\"}");
    LRS_LOGW(
        API, "event=login_failed ip=%s remaining_lock_attempts=%u", ip,
        static_cast<unsigned>((failed_auth_ < 5) ? (5 - failed_auth_) : 0));
    return;
  }

  failed_auth_ = 0;
  locked_until_ms_ = 0;
  startSession();
  DynamicJsonDocument out(128);
  out["ok"] = true;
  out["setup_required"] = needsFleetSetupPrompt();
  const size_t len = measureJson(out);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(out, server_.client());
  LRS_LOGI(API, "event=login_ok ip=%s setup_required=%u", ip,
           needsFleetSetupPrompt() ? 1U : 0U);
}

void WebConsole::handleFleetSetupApi() {
  if (!requireAuth(true))
    return;

  DynamicJsonDocument doc(256);
  StaticJsonDocument<96> filter;
  filter["skip"] = true;
  filter["fleet_passphrase"] = true;
  auto err = deserializeJson(doc, server_.arg("plain"),
                             DeserializationOption::Filter(filter));
  if (err) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const bool skip = parseBoolField(doc["skip"], false);
  if (skip) {
    cfg.fleet_setup_prompt_dismissed = true;
    cfg.audit_last_saved_by = "first_login_skip";
    cfg.audit_last_saved_ms = millis();
    if (!config_->save()) {
      sendTracked(500, "application/json",
                  "{\"ok\":false,\"error\":\"save_failed\"}");
      return;
    }
    sendTracked(200, "application/json", "{\"ok\":true,\"skipped\":true}");
    LRS_LOGI(API, "event=fleet_setup_skip");
    return;
  }

  String fleetKey = doc["fleet_passphrase"] | "";
  fleetKey.trim();
  if (fleetKey.length() < kMinDeploymentKeyLen) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"deployment_key_too_short\"}");
    return;
  }
  if (isDefaultDeploymentKey(fleetKey)) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"deployment_key_default_blocked\"}");
    return;
  }

  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  cfg.audit_last_saved_by = "first_login_setup";
  cfg.audit_last_saved_ms = millis();
  if (!config_->save()) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  if (on_apply_)
    on_apply_(false, false);
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=fleet_key_set key=%s",
           lrslog::maskSecret(fleetKey).c_str());
}

void WebConsole::handleSetupCommissioningApi() {
  if (!requireAuth(true))
    return;

  DynamicJsonDocument doc(640);
  StaticJsonDocument<256> filter;
  filter["fleet_passphrase"] = true;
  filter["mode"] = true;
  filter["role"] = true;
  filter["mqtt_client_enabled"] = true;
  filter["mqtt_control_enabled"] = true;
  filter["input_control_paired_lora_enabled"] = true;
  filter["ap_always_on"] = true;
  filter["wifi_sta_ssid"] = true;
  filter["wifi_sta_password"] = true;
  filter["mqtt_controller_addresses"] = true;

  auto err = deserializeJson(doc, server_.arg("plain"),
                             DeserializationOption::Filter(filter));
  if (err) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  String fleetKey = doc["fleet_passphrase"] | "";
  fleetKey.trim();
  if (fleetKey.length() < kMinDeploymentKeyLen) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"deployment_key_too_short\"}");
    return;
  }
  if (isDefaultDeploymentKey(fleetKey)) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"deployment_key_default_blocked\"}");
    return;
  }

  String mode = doc["mode"] | "";
  String role = doc["role"] | "";
  mode.toLowerCase();
  role.toLowerCase();

  bool roleTx = true;
  if (!parseRoleTxFromModeRole(mode, role, roleTx)) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid_mode_role\"}");
    return;
  }

  const bool mqttClientEnabled =
      parseBoolField(doc["mqtt_client_enabled"], false);
  const bool mqttControlEnabled =
      parseBoolField(doc["mqtt_control_enabled"], false);
  const bool inputControlPairedLoRaEnabled =
      parseBoolField(doc["input_control_paired_lora_enabled"], false);
  if (mqttControlEnabled && !mqttClientEnabled) {
    sendTracked(
        400, "application/json",
        "{\"ok\":false,\"error\":\"mqtt_control_requires_mqtt_client\"}");
    return;
  }
  if (inputControlPairedLoRaEnabled && !(mode == "paired" && roleTx)) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"paired_input_requires_paired_"
                "transmitter\"}");
    return;
  }

  auto &cfg = config_->settings();
  const Settings prev = cfg;

  const String prevStaSsid = prev.wifi_sta_ssid;
  const String prevStaPassword = prev.wifi_sta_password;
  const String prevLanHost = prev.lan_hostname;
  const bool prevApAlwaysOn = prev.ap_always_on;

  cfg.mode = mode;
  cfg.role = role;
  cfg.role_tx = roleTx;
  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  cfg.commissioned = true;
  cfg.mqtt_client_enabled = mqttClientEnabled;
  cfg.mqtt_control_enabled = mqttControlEnabled;
  cfg.input_control_paired_lora_enabled = inputControlPairedLoRaEnabled;
  cfg.ap_always_on = parseBoolField(doc["ap_always_on"], cfg.ap_always_on);

  cfg.wifi_sta_ssid = doc["wifi_sta_ssid"] | cfg.wifi_sta_ssid.c_str();
  cfg.wifi_sta_password =
      doc["wifi_sta_password"] | cfg.wifi_sta_password.c_str();
  cfg.mqtt_controller_addresses =
      doc["mqtt_controller_addresses"] | cfg.mqtt_controller_addresses.c_str();
  if (cfg.lan_hostname.length() == 0) {
    cfg.lan_hostname = config_->defaultLanHostnameForRole(cfg.role_tx);
  }
  cfg.audit_last_saved_by = "first_login_commissioning";
  cfg.audit_last_saved_ms = millis();

  if (!config_->save()) {
    cfg = prev;
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }

  const bool networkChanged = (cfg.wifi_sta_ssid != prevStaSsid) ||
                              (cfg.wifi_sta_password != prevStaPassword) ||
                              (cfg.lan_hostname != prevLanHost) ||
                              (cfg.ap_always_on != prevApAlwaysOn);
  if (on_apply_)
    on_apply_(networkChanged, false);
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API,
           "event=commissioning_setup_saved mode=%s role=%s mqtt_client=%u "
           "mqtt_control=%u input_control=%u",
           cfg.mode.c_str(), cfg.role.c_str(),
           cfg.mqtt_client_enabled ? 1U : 0U,
           cfg.mqtt_control_enabled ? 1U : 0U,
           cfg.input_control_paired_lora_enabled ? 1U : 0U);
}

void WebConsole::handleLogoutApi() {
  char ip[16];
  formatIp(server_.client().remoteIP(), ip);
  clearSession();
  server_.sendHeader("Set-Cookie",
                     "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=logout ip=%s", ip);
}
