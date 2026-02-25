#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
#include "logger.h"
#include "state_machine.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleFleet() {
  HeapProbeGuard heapProbe(this, "/api/fleet");
  if (rejectApiIfLowHeap("/api/fleet", kApiFleetLowHeapRejectFreeBytes, kApiFleetLowHeapRejectMaxBlockBytes)) return;
  auto &cfg = config_->settings();
  const size_t peerCount = (cfg.role_tx && sm_ != nullptr) ? sm_->peerCount() : 0;
  size_t docCapacity = kFleetDocBaseBytes + (peerCount * kFleetDocPerPeerBytes);
  if (docCapacity < kFleetDocMinBytes) docCapacity = kFleetDocMinBytes;
  if (docCapacity > kFleetDocMaxBytes) docCapacity = kFleetDocMaxBytes;
  DynamicJsonDocument doc(docCapacity);
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["tx_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  const uint32_t now = millis();
  doc["uptime_ms"] = now;
  JsonArray arr = doc.createNestedArray("devices");
  if (cfg.role_tx && sm_ != nullptr) {
    for (size_t i = 0; i < peerCount; ++i) {
      PeerStatusSnapshot node{};
      if (!sm_->peerByIndex(i, node)) continue;
      JsonObject r = arr.createNestedObject();
      r["address"] = node.address;
      char addrHex[5];
      snprintf(addrHex, sizeof(addrHex), "0x%02X", node.address);
      r["addr_hex"] = addrHex;
      r["relay_state"] = node.relay_state;
      r["input_state"] = node.input_state;
      r["temp_valid"] = node.temp_valid;
      r["temp_c"] = node.temp_c;
      r["uplink_rssi"] = node.uplink_rssi;
      r["downlink_rssi_valid"] = node.downlink_rssi_valid;
      r["downlink_rssi"] = node.downlink_rssi;
      r["last_seen_ms"] = node.last_seen_ms;
      const uint32_t seenAgeMs = (node.last_seen_ms > 0 && now >= node.last_seen_ms) ? (now - node.last_seen_ms) : 0;
      r["last_seen_age_ms"] = seenAgeMs;
      r["last_cmd_counter"] = node.last_cmd_counter;
      r["ack_state"] = remoteAckStateText(node.ack_state);
      r["poll_interval_ms"] = node.poll_interval_ms;
      r["poll_interval_s"] = node.poll_interval_ms / 1000U;
      r["last_poll_tx_ms"] = node.last_poll_tx_ms;
      const uint32_t pollAgeMs = (node.last_poll_tx_ms > 0 && now >= node.last_poll_tx_ms) ? (now - node.last_poll_tx_ms) : 0;
      r["last_poll_age_ms"] = pollAgeMs;
      r["poll_pending"] = node.poll_pending;
      r["poll_state"] = node.poll_pending ? "pending" : "idle";
      const uint32_t expectedIntervalMs = (node.poll_interval_ms > 0) ? node.poll_interval_ms : 300000U;
      uint32_t staleAfterMs = expectedIntervalMs * 3U;
      if (staleAfterMs < 180000U) staleAfterMs = 180000U;
      r["expected_interval_ms"] = expectedIntervalMs;
      r["stale_after_ms"] = staleAfterMs;
      r["stale_threshold_ms"] = staleAfterMs;
      r["stale"] = (node.last_seen_ms == 0) || (seenAgeMs > staleAfterMs);
    }
  }
  if (doc.overflowed()) {
    LRS_LOGW(API, "event=fleet_json_overflow peers=%u cap=%u",
             static_cast<unsigned>(peerCount),
             static_cast<unsigned>(docCapacity));
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

bool WebConsole::handleFleetDeviceActionRoute(const String &uri) {
  const String prefix = "/api/fleet/";
  if (!uri.startsWith(prefix)) return false;
  const String suffix = uri.substring(prefix.length());
  const int slash = suffix.indexOf('/');
  if (slash <= 0) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!requireAuth(true)) return true;
  auto &cfg = config_->settings();
  if (!cfg.role_tx || sm_ == nullptr) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"tx_only\"}");
    return true;
  }

  const uint8_t addr = parseAddressText(suffix.substring(0, slash), 0);
  if (addr == 0 || addr == 255) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_address\"}");
    return true;
  }

  const String actionPrefix = "actions/";
  const String tail = suffix.substring(slash + 1);
  if (!tail.startsWith(actionPrefix)) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }
  const String action = tail.substring(actionPrefix.length());

  DynamicJsonDocument doc(256);
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return true;
    }
  }

  bool ok = false;
  if (action == "poll-now") {
    ok = sm_->mqttPollPeerNow(addr);
  } else if (action == "forget") {
    ok = sm_->mqttForgetPeer(addr);
  } else if (action == "poll-interval") {
    uint32_t sec = doc["interval_s"] | 0;
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, sec * 1000U);
  } else if (action == "schedule") {
    const bool enabled = parseBoolField(doc["enabled"], true);
    uint32_t sec = doc["interval_s"] | (cfg.tx_mqtt_remote_default_poll_interval_ms / 1000U);
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, enabled ? (sec * 1000U) : 0U);
  } else if (action == "factory-reset") {
    const bool keepSharedFleetKey = parseBoolField(doc["keep_shared_fleet_key"], true);
    ok = sm_->sendPeerFactoryReset(addr, keepSharedFleetKey);
  } else {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!ok) {
    server_.send(409, "application/json", "{\"ok\":false,\"error\":\"action_failed\"}");
    return true;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
  return true;
}
