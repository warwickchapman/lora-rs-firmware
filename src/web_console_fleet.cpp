#include "web_console.h"

#include <ArduinoJson.h>

#include "config_store.h"
#include "logger.h"
#include "state_machine.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

namespace {
bool listHasAddress(const uint8_t *list, uint8_t count, uint8_t addr) {
  if (addr == 0 || addr == 255)
    return false;
  const uint8_t capped =
      (count > Settings::kAddressListCap) ? Settings::kAddressListCap : count;
  for (uint8_t i = 0; i < capped; ++i) {
    if (list[i] == addr)
      return true;
  }
  return false;
}

bool listAppendUnique(uint8_t *list, uint8_t &count, uint8_t addr) {
  if (addr == 0 || addr == 255)
    return false;
  if (listHasAddress(list, count, addr))
    return false;
  if (count >= Settings::kAddressListCap)
    return false;
  list[count++] = addr;
  return true;
}

bool listRemoveAddress(uint8_t *list, uint8_t &count, uint8_t addr) {
  if (list == nullptr || addr == 0 || addr == 255)
    return false;
  const uint8_t capped =
      (count > Settings::kAddressListCap) ? Settings::kAddressListCap : count;
  for (uint8_t i = 0; i < capped; ++i) {
    if (list[i] != addr)
      continue;
    for (uint8_t j = i; (j + 1) < capped; ++j) {
      list[j] = list[j + 1];
    }
    if (count > 0)
      --count;
    if (count < Settings::kAddressListCap)
      list[count] = 0;
    return true;
  }
  return false;
}

bool saveKnownPeerList(ConfigStore *config, const char *savedBy) {
  if (config == nullptr)
    return false;
  auto &cfg = config->settings();
  cfg.audit_last_saved_by = savedBy ? savedBy : "fleet_known_peers";
  cfg.audit_last_saved_ms = millis();
  return config->save();
}
} // namespace

void WebConsole::buildFleetJson(JsonDocument &doc) {
  auto &cfg = config_->settings();
  const size_t peerCount = (cfg.role_tx && sm_ != nullptr) ? sm_->peerCount() : 0;
  size_t docCapacity = kFleetDocBaseBytes + (peerCount * kFleetDocPerPeerBytes);
  if (docCapacity < kFleetDocMinBytes)
    docCapacity = kFleetDocMinBytes;
  if (docCapacity > kFleetDocMaxBytes)
    docCapacity = kFleetDocMaxBytes;
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["tx_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_default_poll_interval_ms"] =
      cfg.tx_mqtt_remote_default_poll_interval_ms;
  const uint8_t knownCapped = (cfg.known_peer_count > Settings::kAddressListCap)
                                  ? Settings::kAddressListCap
                                  : cfg.known_peer_count;
  doc["known_peer_count"] = knownCapped;
  doc["known_peer_cap"] = Settings::kAddressListCap;
  JsonArray knownArr = doc["known_peer_addresses"].to<JsonArray>();
  for (uint8_t i = 0; i < knownCapped; ++i) {
    const uint8_t addr = cfg.known_peer_addresses[i];
    if (addr < 1 || addr > 254)
      continue;
    knownArr.add(addr);
  }
  const uint32_t now = millis();
  doc["uptime_ms"] = now;
  if (cfg.role_tx && sm_ != nullptr) {
    FleetScanSnapshot scan{};
    sm_->fleetScanSnapshot(scan);
    JsonObject s = doc["scan"].to<JsonObject>();
    s["active"] = scan.active;
    s["start_address"] = scan.start_address;
    s["end_address"] = scan.end_address;
    s["next_address"] = scan.next_address;
    s["interval_ms"] = scan.interval_ms;
    s["started_ms"] = scan.started_ms;
    s["last_tx_ms"] = scan.last_tx_ms;
    s["sent"] = scan.sent;
    const uint32_t total =
        (scan.end_address >= scan.start_address)
            ? static_cast<uint32_t>(scan.end_address - scan.start_address + 1U)
            : 0U;
    s["total"] = total;
    uint32_t scanned = 0;
    if (total > 0) {
      if (scan.active) {
        if (scan.next_address <= scan.start_address) {
          scanned = 0;
        } else if (scan.next_address > scan.end_address) {
          scanned = total;
        } else {
          scanned =
              static_cast<uint32_t>(scan.next_address - scan.start_address);
        }
      } else {
        scanned = scan.sent;
        if (scanned > total)
          scanned = total;
      }
    }
    s["scanned"] = scanned;
    s["progress_pct"] =
        (total > 0) ? static_cast<uint32_t>((scanned * 100U) / total) : 0U;
  }
  JsonArray arr = doc["devices"].to<JsonArray>();
  if (cfg.role_tx && sm_ != nullptr) {
    uint8_t activeAddrs[Settings::kAddressListCap]{};
    uint8_t activeCount = 0;
    bool knownChanged = false;

    for (size_t i = 0; i < peerCount; ++i) {
      PeerStatusSnapshot node{};
      if (!sm_->peerByIndex(i, node))
        continue;
      listAppendUnique(activeAddrs, activeCount, node.address);
      if (listAppendUnique(cfg.known_peer_addresses, cfg.known_peer_count,
                           node.address)) {
        knownChanged = true;
      }
      JsonObject r = arr.add<JsonObject>();
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
      const uint32_t seenAgeMs =
          (node.last_seen_ms > 0 && now >= node.last_seen_ms)
              ? (now - node.last_seen_ms)
              : 0;
      r["last_seen_age_ms"] = seenAgeMs;
      r["last_cmd_counter"] = node.last_cmd_counter;
      r["ack_state"] = remoteAckStateText(node.ack_state);
      r["poll_interval_ms"] = node.poll_interval_ms;
      r["poll_interval_s"] = node.poll_interval_ms / 1000U;
      r["last_poll_tx_ms"] = node.last_poll_tx_ms;
      const uint32_t pollAgeMs =
          (node.last_poll_tx_ms > 0 && now >= node.last_poll_tx_ms)
              ? (now - node.last_poll_tx_ms)
              : 0;
      r["last_poll_age_ms"] = pollAgeMs;
      r["poll_pending"] = node.poll_pending;
      r["poll_state"] = node.poll_pending ? "pending" : "idle";
      r["wifi_state_known"] = node.wifi_state_known;
      r["wifi_enabled"] = node.wifi_enabled;
      r["wifi_last_confirm_ms"] = node.wifi_last_confirm_ms;
      const uint32_t wifiConfirmAgeMs =
          (node.wifi_last_confirm_ms > 0 && now >= node.wifi_last_confirm_ms)
              ? (now - node.wifi_last_confirm_ms)
              : 0;
      r["wifi_last_confirm_age_ms"] = wifiConfirmAgeMs;
      const uint32_t expectedIntervalMs =
          (node.poll_interval_ms > 0) ? node.poll_interval_ms : 300000U;
      uint32_t staleAfterMs = expectedIntervalMs * 3U;
      if (staleAfterMs < 180000U)
        staleAfterMs = 180000U;
      r["expected_interval_ms"] = expectedIntervalMs;
      r["stale_after_ms"] = staleAfterMs;
      r["stale_threshold_ms"] = staleAfterMs;
      r["stale"] = (node.last_seen_ms == 0) || (seenAgeMs > staleAfterMs);
    }

    // Include cached known peers even before live scan replies arrive.
    for (uint8_t i = 0; i < knownCapped; ++i) {
      const uint8_t addr = cfg.known_peer_addresses[i];
      if (addr == 0 || addr == 255 || listHasAddress(activeAddrs, activeCount, addr)) {
        continue;
      }
      JsonObject r = arr.add<JsonObject>();
      r["address"] = addr;
      char addrHex[5];
      snprintf(addrHex, sizeof(addrHex), "0x%02X", addr);
      r["addr_hex"] = addrHex;
      r["relay_state"] = 0;
      r["input_state"] = 0;
      r["temp_valid"] = false;
      r["temp_c"] = 0;
      r["uplink_rssi"] = -127;
      r["downlink_rssi_valid"] = false;
      r["downlink_rssi"] = -127;
      r["last_seen_ms"] = 0;
      r["last_seen_age_ms"] = 0;
      r["last_cmd_counter"] = 0;
      r["ack_state"] = "unknown";
      r["poll_interval_ms"] = 0;
      r["poll_interval_s"] = 0;
      r["last_poll_tx_ms"] = 0;
      r["last_poll_age_ms"] = 0;
      r["poll_pending"] = false;
      r["poll_state"] = "idle";
      r["wifi_state_known"] = false;
      r["wifi_enabled"] = true;
      r["wifi_last_confirm_ms"] = 0;
      r["wifi_last_confirm_age_ms"] = 0;
      r["expected_interval_ms"] = 0;
      r["stale_after_ms"] = 0;
      r["stale_threshold_ms"] = 0;
      r["stale"] = true;
      r["known_only"] = true;
    }

    if (knownChanged) {
      // Known peers updated in-memory only. Persistence is deferred to avoid
      // flash writes on read paths (GET/SSE). The addresses will be saved
      // with the next explicit settings save.
      LRS_LOGD(API, "event=fleet_known_peers_updated count=%u",
               static_cast<unsigned>(cfg.known_peer_count));
    }
  }
  if (doc.overflowed()) {
    LRS_LOGW(API, "event=fleet_json_overflow peers=%u cap=%u",
             static_cast<unsigned>(peerCount),
             static_cast<unsigned>(docCapacity));
  }
}

void WebConsole::handleFleet() {
  HeapProbeGuard heapProbe(this, "/api/fleet");
  if (rejectApiIfLowHeap("/api/fleet", kApiFleetLowHeapRejectFreeBytes,
                         kApiFleetLowHeapRejectMaxBlockBytes))
    return;
  JsonDocument doc;
  buildFleetJson(doc);
  auto &cfg = config_->settings();
  if (cfg.mode == "standalone") {
    doc["role"] = cfg.role_tx ? "tx" : "rx";
    doc["mode"] = cfg.mode;
    doc["ok"] = false;
    doc["error"] = "fleet_disabled_in_standalone";
    doc["devices"].to<JsonArray>().clear();
    doc["scan"].to<JsonObject>().clear();
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

bool WebConsole::handleFleetDeviceActionRoute(const String &uri) {
  const String prefix = "/api/fleet/";
  if (!uri.startsWith(prefix))
    return false;
  const String suffix = uri.substring(prefix.length());
  if (suffix == "actions/wifi-disable-all") {
    if (!requireAuth(true))
      return true;
    auto &cfg = config_->settings();
    if (cfg.mode == "standalone") {
      server_.send(409, "application/json",
                   "{\"ok\":false,\"error\":\"fleet_disabled_in_standalone\"}");
      return true;
    }
    if (!cfg.role_tx || sm_ == nullptr) {
      server_.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"tx_only\"}");
      return true;
    }
    if (!sm_->sendBroadcastWifiDisable()) {
      server_.send(409, "application/json",
                   "{\"ok\":false,\"error\":\"action_failed\"}");
      return true;
    }
    server_.send(200, "application/json", "{\"ok\":true}");
    return true;
  }
  const int slash = suffix.indexOf('/');
  if (slash <= 0) {
    server_.send(404, "application/json",
                 "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!requireAuth(true))
    return true;
  auto &cfg = config_->settings();
  if (cfg.mode == "standalone") {
    server_.send(409, "application/json",
                 "{\"ok\":false,\"error\":\"fleet_disabled_in_standalone\"}");
    return true;
  }
  if (!cfg.role_tx || sm_ == nullptr) {
    server_.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"tx_only\"}");
    return true;
  }

  const uint8_t addr = parseAddressText(suffix.substring(0, slash), 0);
  if (addr == 0 || addr == 255) {
    server_.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"invalid_address\"}");
    return true;
  }

  const String actionPrefix = "actions/";
  const String tail = suffix.substring(slash + 1);
  if (!tail.startsWith(actionPrefix)) {
    server_.send(404, "application/json",
                 "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }
  const String action = tail.substring(actionPrefix.length());

  JsonDocument doc;
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json",
                   "{\"ok\":false,\"error\":\"invalid_json\"}");
      return true;
    }
  }

  bool ok = false;
  if (action == "poll-now") {
    ok = sm_->mqttPollPeerNow(addr);
  } else if (action == "wifi-enable" || action == "wifi-disable") {
    ok = sm_->mqttSetPeerWifi(addr, action == "wifi-enable");
  } else if (action == "forget" || action == "delete") {
    const bool runtimeForgot = sm_->mqttForgetPeer(addr);
    const bool knownRemoved = listRemoveAddress(cfg.known_peer_addresses, cfg.known_peer_count, addr);
    if (knownRemoved && !saveKnownPeerList(config_, "fleet_known_delete")) {
      server_.send(500, "application/json",
                   "{\"ok\":false,\"error\":\"save_failed\"}");
      return true;
    }
    ok = runtimeForgot || knownRemoved || action == "delete";
  } else if (action == "add") {
    if (listHasAddress(cfg.known_peer_addresses, cfg.known_peer_count, addr)) {
      ok = true;
    } else if (!listAppendUnique(cfg.known_peer_addresses, cfg.known_peer_count, addr)) {
      server_.send(409, "application/json",
                   "{\"ok\":false,\"error\":\"known_list_full\"}");
      return true;
    } else if (!saveKnownPeerList(config_, "fleet_known_add")) {
      server_.send(500, "application/json",
                   "{\"ok\":false,\"error\":\"save_failed\"}");
      return true;
    } else {
      ok = true;
    }
  } else if (action == "poll-interval") {
    uint32_t sec = doc["interval_s"] | 0;
    if (sec > 0 && sec < 60U)
      sec = 60U;
    if (sec > 3600U)
      sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, sec * 1000U);
  } else if (action == "schedule") {
    const bool enabled = parseBoolField(doc["enabled"], true);
    uint32_t sec = doc["interval_s"] |
                   (cfg.tx_mqtt_remote_default_poll_interval_ms / 1000U);
    if (sec > 0 && sec < 60U)
      sec = 60U;
    if (sec > 3600U)
      sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, enabled ? (sec * 1000U) : 0U);
  } else if (action == "factory-reset") {
    const bool keepSharedFleetKey =
        parseBoolField(doc["keep_shared_fleet_key"], true);
    ok = sm_->sendPeerFactoryReset(addr, keepSharedFleetKey);
  } else {
    server_.send(404, "application/json",
                 "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!ok) {
    server_.send(409, "application/json",
                 "{\"ok\":false,\"error\":\"action_failed\"}");
    return true;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
  return true;
}

void WebConsole::handleFleetScan() {
  if (!requireAuth(true))
    return;
  auto &cfg = config_->settings();
  if (cfg.mode == "standalone") {
    sendTracked(409, "application/json",
                "{\"ok\":false,\"error\":\"fleet_disabled_in_standalone\"}");
    return;
  }
  if (!cfg.role_tx || sm_ == nullptr) {
    sendTracked(400, "application/json",
                "{\"ok\":false,\"error\":\"tx_only\"}");
    return;
  }

  JsonDocument body;
  JsonDocument filter;
  filter["start_address"] = true;
  filter["end_address"] = true;
  filter["interval_ms"] = true;
  filter["cancel"] = true;
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(body, server_.arg("plain"),
                               DeserializationOption::Filter(filter));
    if (err) {
      sendTracked(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid_json\"}");
      return;
    }
  }

  const bool cancel = parseBoolField(body["cancel"], false);
  if (cancel) {
    sm_->fleetScanCancel();
  } else {
    const uint8_t startAddress = parseAddressField(body["start_address"], 1);
    const uint8_t endAddress = parseAddressField(body["end_address"], 32);
    uint16_t intervalMs = body["interval_ms"] | 120;
    if (intervalMs < 80U)
      intervalMs = 80U;
    if (intervalMs > 2000U)
      intervalMs = 2000U;
    if (!sm_->fleetScanStart(startAddress, endAddress, intervalMs)) {
      sendTracked(409, "application/json",
                  "{\"ok\":false,\"error\":\"start_failed\"}");
      return;
    }
  }

  FleetScanSnapshot scan{};
  sm_->fleetScanSnapshot(scan);
  JsonDocument out;
  out["ok"] = true;
  out["active"] = scan.active;
  out["start_address"] = scan.start_address;
  out["end_address"] = scan.end_address;
  out["next_address"] = scan.next_address;
  out["interval_ms"] = scan.interval_ms;
  out["sent"] = scan.sent;
  const uint32_t total =
      (scan.end_address >= scan.start_address)
          ? static_cast<uint32_t>(scan.end_address - scan.start_address + 1U)
          : 0U;
  out["total"] = total;
  const size_t len = measureJson(out);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(out, server_.client());
}
