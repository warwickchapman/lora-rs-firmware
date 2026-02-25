#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Updater.h>
#include <cstring>

#include "build_info.h"
#include "config_store.h"
#include "logger.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleFactory() {
  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["serial"] = cfg.factory_serial;
  doc["chip_id"] = config_->chipIdHex();
  doc["mac"] = WiFi.softAPmacAddress();
  doc["factory_role"] = cfg.role_tx ? "tx" : "rx";
  doc["factory_local_address"] = cfg.local_address;
  doc["factory_remote_address"] = cfg.remote_address;
  doc["factory_ap_ssid"] = config_->apSsid();
  doc["factory_ap_password"] = config_->apPassword();
  doc["hardware_version"] = kHardwareVersion;
  doc["hardware_batch"] = kHardwareBatch;
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}
void WebConsole::handleDiagnostics() {
  const uint32_t heapFree = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const bool compact = (heapFree < kApiLowHeapRejectFreeBytes || maxBlock < kApiLowHeapRejectMaxBlockBytes);
  DynamicJsonDocument doc(compact ? 320 : 768);
  auto &cfg = config_->settings();
  const wl_status_t st = WiFi.status();
  doc["compact"] = compact;
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["fw_display"] = String(LRS_FW_VERSION) + " (" + String(LRS_GIT_SHA) + (LRS_GIT_DIRTY == 0 ? "" : ", dirty") + ")";
  doc["uptime_ms"] = millis();
  doc["free_heap_bytes"] = ESP.getFreeHeap();
  if (!compact) {
    doc["build_date"] = __DATE__;
    doc["build_time"] = __TIME__;
    doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
    doc["chip_id"] = config_->chipIdHex();
    doc["flash_real_size"] = ESP.getFlashChipRealSize();
    doc["flash_ide_size"] = ESP.getFlashChipSize();
    doc["sdk_version"] = ESP.getSdkVersion();
    doc["core_version"] = ESP.getCoreVersion();
    doc["lora_tx_packets"] = nullptr;
    doc["ack_ok"] = nullptr;
    doc["ack_timeout"] = nullptr;
    doc["replay_drop"] = nullptr;
    doc["wifi_connect_attempts"] = nullptr;
    doc["wifi_connect_fail"] = nullptr;
    doc["wifi_disconnects"] = nullptr;
    doc["log_history_available"] = false;
  }
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  if (!compact) {
    doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
    doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
    doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
    doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
    doc["audit_boot_count"] = cfg.audit_boot_count;
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}
void WebConsole::handleTestMqtt() {
  if (!requireAuth(true)) return;
  DynamicJsonDocument body(256);
  StaticJsonDocument<160> filter;
  filter["mqtt_host"] = true;
  filter["mqtt_port"] = true;
  filter["mqtt_user"] = true;
  filter["mqtt_password"] = true;
  auto err = deserializeJson(body, server_.arg("plain"), DeserializationOption::Filter(filter));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }
  const String host = String(static_cast<const char *>(body["mqtt_host"] | config_->settings().mqtt_host.c_str()));
  uint16_t port = config_->settings().mqtt_port;
  if (!body["mqtt_port"].isNull()) {
    long parsed = -1;
    if (body["mqtt_port"].is<uint16_t>()) {
      parsed = static_cast<long>(body["mqtt_port"].as<uint16_t>());
    } else {
      const String raw = String(static_cast<const char *>(body["mqtt_port"] | ""));
      if (raw.length() > 0) parsed = raw.toInt();
    }
    if (parsed < 1 || parsed > 65535) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mqtt_port\"}");
      return;
    }
    port = static_cast<uint16_t>(parsed);
  }
  const String user = String(static_cast<const char *>(body["mqtt_user"] | config_->settings().mqtt_user.c_str()));
  const String pass = String(static_cast<const char *>(body["mqtt_password"] | config_->settings().mqtt_password.c_str()));
  if (!WiFi.isConnected()) {
    server_.send(200, "application/json", "{\"ok\":false,\"state\":-2}");
    return;
  }
  WiFiClient client;
  PubSubClient mqtt(client);
  mqtt.setServer(host.c_str(), port);
  mqtt.setSocketTimeout(2);
  const String clientId = "lrs-test-" + config_->chipIdHex();
  bool ok = false;
  if (user.length() > 0) {
    ok = mqtt.connect(clientId.c_str(), user.c_str(), pass.c_str());
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  DynamicJsonDocument doc(256);
  doc["ok"] = ok;
  doc["state"] = mqtt.state();
  doc["host"] = host;
  doc["port"] = port;
  if (ok) mqtt.disconnect();
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleUdpLogging() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument body(256);
  StaticJsonDocument<128> filter;
  filter["enabled"] = true;
  filter["host"] = true;
  filter["port"] = true;
  filter["ttl_s"] = true;
  auto err = deserializeJson(body, server_.arg("plain"), DeserializationOption::Filter(filter));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  const bool enabled = parseBoolField(body["enabled"], true);
  if (!enabled) {
    lrslog::disableUdpMirror();
    const IPAddress rip = server_.client().remoteIP();
    char ripbuf[16];
    snprintf(ripbuf,
             sizeof(ripbuf),
             "%u.%u.%u.%u",
             static_cast<unsigned>(rip[0]),
             static_cast<unsigned>(rip[1]),
             static_cast<unsigned>(rip[2]),
             static_cast<unsigned>(rip[3]));
    LRS_LOGI(API, "event=udp_log_mirror_disabled ip=%s", ripbuf);
    sendTracked(200, "application/json", "{\"ok\":true,\"enabled\":false,\"ttl_ms\":0}");
    return;
  }

  IPAddress target = server_.client().remoteIP();
  if (!parseIpField(body["host"], target) && !body["host"].isNull()) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_host\"}");
    return;
  }

  uint16_t port = 5514;
  if (!body["port"].isNull() && !parseUint16Field(body["port"], port)) {
      sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_port\"}");
      return;
  }

  uint32_t ttlS = 300;
  if (!body["ttl_s"].isNull() && !parseUint32FieldRange(body["ttl_s"], 1, 1800, ttlS)) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_ttl_s\"}");
    return;
  }

  lrslog::setUdpMirror(target, port, ttlS * 1000UL);

  char ipbuf[16];
  snprintf(ipbuf,
           sizeof(ipbuf),
           "%u.%u.%u.%u",
           static_cast<unsigned>(target[0]),
           static_cast<unsigned>(target[1]),
           static_cast<unsigned>(target[2]),
           static_cast<unsigned>(target[3]));
  LRS_LOGI(API,
           "event=udp_log_mirror_enabled host=%s port=%u ttl_s=%lu",
           ipbuf,
           static_cast<unsigned>(port),
           static_cast<unsigned long>(ttlS));

  char resp[192];
  snprintf(resp,
           sizeof(resp),
           "{\"ok\":true,\"enabled\":true,\"host\":\"%s\",\"port\":%u,\"ttl_ms\":%lu,\"remaining_ms\":%lu}",
           ipbuf,
           static_cast<unsigned>(port),
           static_cast<unsigned long>(ttlS * 1000UL),
           static_cast<unsigned long>(lrslog::udpMirrorRemainingMs()));
  sendTracked(200, "application/json", resp);
}

void WebConsole::handleOtaUpload() {
  if (!requireAuth(true)) return;
  if (!ota_upload_ok_) {
    server_.send(500, "text/plain", ota_upload_error_.length() ? ota_upload_error_ : "OTA failed");
    return;
  }
  server_.send(200, "text/plain", "ok");
  delay(150);
  ESP.restart();
}

void WebConsole::handleOtaUploadChunk() {
  if (!requireAuth(true)) return;
  HTTPUpload &upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    ota_upload_ok_ = false;
    ota_upload_error_ = "";
    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      ota_upload_error_ = "Cannot start OTA";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      ota_upload_error_ = "Write failed";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      ota_upload_error_ = "Finalize failed";
      ota_upload_ok_ = false;
      return;
    }
    ota_upload_ok_ = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    ota_upload_error_ = "Upload aborted";
    ota_upload_ok_ = false;
  }
}

void WebConsole::handleLogsCsv() {
  if (!requireAuth()) return;
  server_.send(410, "text/plain", "in-memory log history disabled");
}

void WebConsole::handleLogsText() {
  if (!requireAuth()) return;
  server_.send(410, "text/plain", "in-memory log history disabled");
}

void WebConsole::handleFactoryReset() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument body(256);
  StaticJsonDocument<128> filter;
  filter["admin_password"] = true;
  filter["keep_shared_fleet_key"] = true;
  filter["keep_wifi_credentials"] = true;
  auto err = deserializeJson(body, server_.arg("plain"), DeserializationOption::Filter(filter));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const String postedPassword = String(static_cast<const char *>(body["admin_password"] | ""));
  if (postedPassword != cfg.admin_password) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"invalid_password\"}");
    return;
  }
  const bool keepSharedFleetKey = parseBoolField(body["keep_shared_fleet_key"], false);
  const bool keepWifiCredentials = parseBoolField(body["keep_wifi_credentials"], false);

  if (!config_->factoryReset(keepSharedFleetKey, keepWifiCredentials)) {
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"reset_save_failed\"}");
    return;
  }

  clearSession();
  server_.sendHeader("Set-Cookie", "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(120);
  ESP.restart();
}

void WebConsole::handleReboot() {
  if (!requireAuth(true)) return;
  auto &cfg = config_->settings();
  cfg.audit_last_reboot_reason = "web_reboot";
  cfg.audit_last_reboot_ms = millis();
  config_->save();
  server_.send(200, "text/plain", "rebooting");
  delay(150);
  ESP.restart();
}
