#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "build_info.h"
#include "config_store.h"
#include "logger.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

bool WebConsole::buildStatusLiveCache() {
  if (!config_ || !sm_) return false;
  if (status_live_cache_building_) return status_live_cache_.body.length() > 0;
  status_live_cache_building_ = true;

  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["chip_id"] = config_->chipIdHex();
  doc["factory_serial"] = cfg.factory_serial;
  doc["mode"] = cfg.mode;
  doc["role_name"] = cfg.role;
  const wl_status_t st = WiFi.status();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["link_state"] = linkStateText(sm_->linkState());
  doc["relay_state"] = sm_->relayState();
  doc["input_state"] = sm_->inputState();
  doc["local_input_state"] = sm_->localDryContactState();
  doc["lora_last_rssi"] = sm_->lastPacketRssi();
  doc["lora_last_packet_ms"] = sm_->lastPacketMs();
  doc["lora_last_tx_ms"] = sm_->lastTxMs();
  doc["lora_remote_temp_valid"] = sm_->remoteTemperatureValid();
  doc["lora_remote_temp_c"] = sm_->remoteTemperatureC();
  doc["lora_remote_temp_ms"] = sm_->remoteTemperatureMs();
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["sta_ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  String relayReason = "boot";
  if (cfg.role_tx) {
    if (sm_->relayState() == 0) {
      if (sm_->inputState() == 0) {
        relayReason = "input_open";
      } else if (sm_->linkState() == LinkState::Timeout) {
        relayReason = "ack_timeout";
      } else if (sm_->linkState() == LinkState::WaitAck) {
        relayReason = "wait_ack";
      } else {
        relayReason = "no_lora_link";
      }
    } else {
      relayReason = "ok";
    }
  } else {
    const bool relayOn = sm_->relayState() != 0;
    switch (sm_->lastRxControlSource()) {
      case RxControlSource::Automation:
        relayReason = relayOn ? "automation_on" : "automation_off";
        break;
      case RxControlSource::Mqtt:
        relayReason = relayOn ? "mqtt_on" : "mqtt_off";
        break;
      case RxControlSource::LoRa:
        relayReason = relayOn ? "lora_on" : "lora_off";
        break;
      default:
        relayReason = "boot";
        break;
    }
  }
  doc["relay_reason"] = relayReason;

  if (sensors_) {
    const TempSensorStatus &ts = sensors_->tempStatus();
    doc["sensor_temp_enabled"] = ts.enabled;
    doc["sensor_temp_detected"] = ts.detected;
    doc["sensor_temp_valid"] = ts.valid;
    doc["sensor_temp_c"] = ts.celsius;
    doc["sensor_temp_addr"] = ts.address;
    doc["sensor_temp_error"] = ts.error;
    doc["sensor_temp_last_read_ms"] = ts.last_read_ms;
  }

  serializeJson(doc, status_live_cache_.body);
  status_live_cache_.built_ms = millis();
  status_live_cache_building_ = false;
  return status_live_cache_.body.length() > 0;
}

bool WebConsole::buildStatusStaticCache() {
  if (!config_) return false;
  if (status_static_cache_building_) return status_static_cache_.body.length() > 0;
  status_static_cache_building_ = true;

  StaticJsonDocument<768> doc;
  auto &cfg = config_->settings();
  doc["chip_id"] = config_->chipIdHex();
  doc["factory_serial"] = cfg.factory_serial;
  doc["mode"] = cfg.mode;
  doc["role_name"] = cfg.role;
  doc["sta_target_ssid"] = cfg.wifi_sta_ssid;
  doc["deployment_key"] = cfg.fleet_passphrase.length() ? lrslog::maskSecret(cfg.fleet_passphrase) : String("");
  doc["deployment_key_set"] = (cfg.fleet_passphrase.length() > 0);
  doc["deployment_key_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["fleet_setup_required"] = needsFleetSetupPrompt();
  doc["ap_ssid"] = config_->apSsid();
  doc["ap_ip"] = WiFi.softAPIP().toString();
#if LRS_ENABLE_MDNS
  doc["mdns_ap"] = "lrs.local";
  doc["mdns_lan"] = config_->settings().lan_hostname + ".local";
#endif
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  char fwDisplay[96];
  if (LRS_GIT_DIRTY == 0) {
    snprintf(fwDisplay, sizeof(fwDisplay), "%s (%s)", LRS_FW_VERSION, LRS_GIT_SHA);
  } else {
    snprintf(fwDisplay, sizeof(fwDisplay), "%s (%s, dirty)", LRS_FW_VERSION, LRS_GIT_SHA);
  }
  doc["fw_display"] = fwDisplay;
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["session_remaining_s"] = sessionRemainingS();
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  serializeJson(doc, status_static_cache_.body);
  status_static_cache_.built_ms = millis();
  status_static_cache_building_ = false;
  return status_static_cache_.body.length() > 0;
}

bool WebConsole::buildStatusLiteCache() {
  if (!config_) return false;
  if (status_lite_cache_building_) return status_lite_cache_.body.length() > 0;
  status_lite_cache_building_ = true;

  StaticJsonDocument<384> doc;
  auto &cfg = config_->settings();
  doc["chip_id"] = config_->chipIdHex();
  doc["factory_serial"] = cfg.factory_serial;
  doc["mode"] = cfg.mode;
  doc["role_name"] = cfg.role;
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["relay_state"] = sm_ ? sm_->relayState() : 0;
  doc["lora_last_rssi"] = sm_ ? sm_->lastPacketRssi() : 0;
  doc["lora_last_packet_ms"] = sm_ ? sm_->lastPacketMs() : 0;
  doc["lora_last_tx_ms"] = sm_ ? sm_->lastTxMs() : 0;
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  serializeJson(doc, status_lite_cache_.body);
  status_lite_cache_.built_ms = millis();
  status_lite_cache_building_ = false;
  return status_lite_cache_.body.length() > 0;
}

void WebConsole::tickStatusLiveSse() {
  if (!status_live_sse_active_) return;
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }

  const uint32_t now = millis();
  const uint32_t pushIntervalMs = computeStatusLiveSseIntervalMs();
  status_live_sse_last_interval_ms_ = pushIntervalMs;

  if (status_live_sse_last_keepalive_ms_ == 0 || (now - status_live_sse_last_keepalive_ms_) >= kStatusLiveSseKeepAliveMs) {
    if (status_live_sse_client_.print(F(": keepalive\n\n")) == 0) {
      closeStatusLiveSse();
      return;
    }
    status_live_sse_last_keepalive_ms_ = now;
  }

  if (status_live_sse_last_push_ms_ != 0 && (now - status_live_sse_last_push_ms_) < pushIntervalMs) {
    return;
  }

  bool cacheUpdated = false;
  if (status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) {
    if (apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
      cacheUpdated = buildStatusLiveCache();
    }
  }

  if (status_live_cache_.body.length() == 0) {
    return;
  }

  if (!cacheUpdated && status_live_cache_.built_ms == status_live_sse_last_sent_cache_ms_) {
    return;
  }

  if (status_live_sse_client_.print(F("event: status\nid: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.built_ms) == 0 ||
      status_live_sse_client_.print(F("\ndata: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.body) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }

  status_live_sse_last_push_ms_ = now;
  status_live_sse_last_sent_cache_ms_ = status_live_cache_.built_ms;
}

void WebConsole::handleStatus() {
  LRS_LOGW(API,
           "event=status_compat_removed ip=%s",
           server_.client().remoteIP().toString().c_str());
  sendTracked(410,
              "application/json",
              "{\"ok\":false,\"error\":\"deprecated\",\"use\":[\"/api/status-live\",\"/api/status-static\"]}");
}

void WebConsole::handleStatusLive() {
  if (tryServeCachedJson("/api/status-live",
                         kApiStatusLiveLowHeapRejectFreeBytes,
                         kApiStatusLiveLowHeapRejectMaxBlockBytes,
                         kStatusLiveCacheTtlMs,
                         status_live_cache_))
    return;
  if (!buildStatusLiveCache()) {
    if (status_live_cache_.body.length() > 0) {
      sendTracked(200, "application/json", status_live_cache_.body);
      return;
    }
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_live_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_live_cache_.body);
}

void WebConsole::handleStatusLiveEvents() {
  const bool connectHeapHealthy =
      apiHeapHealthy(kStatusLiveSseConnectMinFreeBytes, kStatusLiveSseConnectMinMaxBlockBytes);

  if (status_live_sse_active_) {
    closeStatusLiveSse();
  }

  const uint32_t now = millis();
  if ((status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) &&
      apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
    buildStatusLiveCache();
  }

  WiFiClient client = server_.client();
  client.setNoDelay(true);
  client.setSync(true);
  status_live_sse_client_ = client;
  status_live_sse_active_ = true;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  markResponseStatus(200);
  server_.sendContent_P(PSTR("HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
                             "Pragma: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "X-Accel-Buffering: no\r\n"
                             "\r\n"));
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }
  if (status_live_sse_client_.print(F("retry: ")) == 0 ||
      status_live_sse_client_.print(connectHeapHealthy ? 3000U : 10000U) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }
  tickStatusLiveSse();
  LRS_LOGI(API,
           "event=status_live_sse_open ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu heap_ok=%u",
           server_.client().remoteIP().toString().c_str(),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
           connectHeapHealthy ? 1U : 0U);
}

void WebConsole::handleStatusStatic() {
  HeapProbeGuard heapProbe(this, "/api/status-static");
  if (tryServeCachedJson("/api/status-static",
                         kApiStatusStaticLowHeapRejectFreeBytes,
                         kApiStatusStaticLowHeapRejectMaxBlockBytes,
                         kStatusStaticCacheTtlMs,
                         status_static_cache_))
    return;
  if (!buildStatusStaticCache()) {
    if (status_static_cache_.body.length() > 0) {
      sendTracked(200, "application/json", status_static_cache_.body);
      return;
    }
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_static_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_static_cache_.body);
}

void WebConsole::handleStatusLite() {
  HeapProbeGuard heapProbe(this, "/api/status-lite");
  if (tryServeCachedJson("/api/status-lite",
                         kApiLightLowHeapRejectFreeBytes,
                         kApiLightLowHeapRejectMaxBlockBytes,
                         kStatusLiteCacheTtlMs,
                         status_lite_cache_))
    return;
  if (!buildStatusLiteCache()) {
    if (status_lite_cache_.body.length() > 0) {
      sendTracked(200, "application/json", status_lite_cache_.body);
      return;
    }
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_lite_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_lite_cache_.body);
}
