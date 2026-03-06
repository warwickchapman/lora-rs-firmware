#include "web_console.h"

#include <ArduinoJson.h>
#include <cstring>

#include "config_store.h"
#include "logger.h"
#include "state_machine.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleProvisionFleetWifi() {
  if (!requireAuth(true))
    return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }

  auto sendJsonDoc = [this](int code, JsonDocument &doc) {
    const size_t len = measureJson(doc);
    server_.setContentLength(len);
    markResponseStatus(code);
    server_.send(code, "application/json", "");
    serializeJson(doc, server_.client());
  };

  JsonDocument body;
  JsonDocument filter;
  filter["wifi_sta_ssid"] = true;
  filter["wifi_sta_password"] = true;
  auto err = deserializeJson(body, server_.arg("plain"),
                             DeserializationOption::Filter(filter));
  if (err) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  const auto &cfg = config_->settings();
  const char *ssid = body["wifi_sta_ssid"] | cfg.wifi_sta_ssid.c_str();
  const char *pass = body["wifi_sta_password"] | cfg.wifi_sta_password.c_str();
  const size_t ssidLen = (ssid != nullptr) ? strlen(ssid) : 0U;
  const size_t passLen = (pass != nullptr) ? strlen(pass) : 0U;
  if (ssidLen == 0U) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }
  if (ssidLen > 32U || passLen > 64U) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"credentials_too_long\"}");
    return;
  }
  if (isDefaultDeploymentKey(cfg.fleet_passphrase)) {
    sendTracked(409, "application/json",
                "{\"ok\":false,\"error\":\"fleet_key_default\"}");
    return;
  }

  const uint32_t cooldownRemainingMs =
      sm_->fleetWifiProvisionCooldownRemainingMs();
  if (cooldownRemainingMs > 0) {
    JsonDocument cooldown;
    cooldown["ok"] = false;
    cooldown["error"] = "cooldown_active";
    cooldown["retry_after_ms"] = cooldownRemainingMs;
    cooldown["retry_after_s"] = (cooldownRemainingMs + 999U) / 1000U;
    sendJsonDoc(429, cooldown);
    return;
  }

  if (!sm_->sendFleetWifiProvision(String(ssid), String(pass))) {
    sendTracked(409, "application/json",
                "{\"ok\":false,\"error\":\"send_failed\"}");
    return;
  }

  const size_t totalLen = ssidLen + passLen;
  const size_t chunks = (totalLen + 6U) / 7U;
  JsonDocument out;
  out["ok"] = true;
  out["packets"] =
      static_cast<uint32_t>(chunks + 2U); // start + chunks + commit
  sendJsonDoc(200, out);
  LRS_LOGI(API, "event=fleet_wifi_provision_tx ssid=%s password=%s packets=%lu",
           ssid, lrslog::maskSecret(String(pass)).c_str(),
           static_cast<unsigned long>(chunks + 2U));
}

void WebConsole::buildProvisioningStatusJson(JsonDocument &doc) {
  if (sm_ == nullptr) {
    doc["ok"] = false;
    doc["error"] = "state_machine_unavailable";
    return;
  }
  ProvisioningSessionSnapshot sess{};
  sm_->provisioningSession(sess);
  const size_t totalDevices = sm_->provisioningDeviceCount();

  const uint32_t heapFree = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const bool veryLowHeap = (heapFree < kApiProvStatusCompactFreeBytes ||
                            maxBlock < kApiProvStatusCompactMaxBlockBytes);

  doc["ok"] = true;

  constexpr size_t maxCompactRows = 8;
  const bool activeSession = sess.active;
  bool compact = activeSession || veryLowHeap;
  const size_t returnedDevices =
      compact
          ? ((totalDevices < maxCompactRows) ? totalDevices : maxCompactRows)
          : totalDevices;
  const bool truncated = returnedDevices < totalDevices;
  const char *compactReason = nullptr;
  if (compact) {
    compactReason = activeSession ? "active_session" : "low_heap";
  } else if (truncated) {
    compact = true;
    compactReason = "truncated_rows";
  }
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
  s["devices_total"] = totalDevices;
  s["devices_returned"] = returnedDevices;
  s["devices_truncated"] = truncated;
  s["compact"] = compact;
  if (compactReason != nullptr) {
    s["compact_reason"] = compactReason;
  }
  if (last_logged_prov_state_ != static_cast<uint8_t>(sess.state)) {
    last_logged_prov_state_ = static_cast<uint8_t>(sess.state);
    LRS_LOGI(API,
             "event=provisioning_phase state=%s discovered=%u conflicts=%u "
             "verified=%u failed=%u",
             provisioningSessionStateText(sess.state),
             static_cast<unsigned>(sess.discovered_count),
             static_cast<unsigned>(sess.conflict_count),
             static_cast<unsigned>(sess.verified_count),
             static_cast<unsigned>(sess.failed_count));
  }

  JsonArray arr = doc["devices"].to<JsonArray>();
  for (size_t i = 0; i < returnedDevices; ++i) {
    ProvisioningDeviceSnapshot d{};
    if (!sm_->provisioningDeviceByIndex(i, d))
      continue;
    JsonObject o = arr.add<JsonObject>();
    char chipHex[11];
    snprintf(chipHex, sizeof(chipHex), "0x%08lx",
             static_cast<unsigned long>(d.chip_id));
    o["chip_id_hex"] = chipHex;
    o["current_address"] = d.current_address;
    o["assigned_address"] = d.assigned_address;
    o["fw_major"] = d.fw_major;
    o["fw_minor"] = d.fw_minor;
    o["fw_patch"] = d.fw_patch;
    o["rssi"] = d.rssi;
    o["state"] = provisioningDeviceStateText(d.state);
    o["address_conflict"] = d.address_conflict;
  }
  if (compact) {
    LRS_LOGW(API,
             "event=provisioning_status_compact reason=%s heap_free=%lu "
             "heap_frag=%u max_free_block=%lu",
             compactReason ? compactReason : "unknown",
             static_cast<unsigned long>(heapFree),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(maxBlock));
  }
}

void WebConsole::handleProvisioningStatus() {
  HeapProbeGuard heapProbe(this, "/api/provisioning/status");
  if (!requireAuth(true))
    return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  const uint32_t heapFree = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const bool veryLowHeap = (heapFree < kApiProvStatusCompactFreeBytes ||
                            maxBlock < kApiProvStatusCompactMaxBlockBytes);
  if (veryLowHeap && rejectApiIfLowHeap("/api/provisioning/status",
                                        kApiProvStatusCompactFreeBytes,
                                        kApiProvStatusCompactMaxBlockBytes)) {
    return;
  }
  JsonDocument doc;
  buildProvisioningStatusJson(doc);
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleProvisioningStart() {
  if (!requireAuth(true))
    return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  JsonDocument body;
  if (server_.arg("plain").length() > 0) {
    JsonDocument filter;
    filter["estimated_count"] = true;
    auto err = deserializeJson(body, server_.arg("plain"),
                               DeserializationOption::Filter(filter));
    if (err) {
      sendTracked(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid_json\"}");
      return;
    }
  }
  uint16_t estimated = 2;
  if (!body["estimated_count"].isNull()) {
    int v = body["estimated_count"].as<int>();
    if (v < 1)
      v = 1;
    if (v > 8)
      v = 8;
    estimated = static_cast<uint16_t>(v);
  }
  if (!sm_->provisioningStartDiscovery(estimated)) {
    sendTracked(409, "application/json",
                "{\"ok\":false,\"error\":\"start_failed\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_start estimated=%u heap_free=%lu "
           "heap_frag=%u max_free_block=%lu",
           static_cast<unsigned>(estimated),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningProvisionAll() {
  if (!requireAuth(true))
    return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  if (!sm_->provisioningStartProvisionAll()) {
    sendTracked(409, "application/json",
                "{\"ok\":false,\"error\":\"invalid_state\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_provision_all heap_free=%lu heap_frag=%u "
           "max_free_block=%lu",
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningCancel() {
  if (!requireAuth(true))
    return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json",
                "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  sm_->provisioningCancel();
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=provisioning_cancel");
}
