#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "config_store.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleWifiScan() {
  if (!requireAuth(true)) return;
  const int scanState = WiFi.scanComplete();
  if (scanState == WIFI_SCAN_RUNNING) {
    sendTracked(200, "application/json", "{\"ok\":true,\"status\":\"scanning\"}");
    return;
  }
  if (scanState == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    sendTracked(200, "application/json", "{\"ok\":true,\"status\":\"scanning\"}");
    return;
  }

  DynamicJsonDocument doc(2048);
  doc["ok"] = true;
  doc["status"] = "ready";
  JsonArray arr = doc.createNestedArray("networks");
  const int count = scanState;
  for (int i = 0; i < count; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    if (isOwnLrsSoftApLike(ssid)) continue;
    JsonObject n = arr.createNestedObject();
    n["ssid"] = ssid;
    n["rssi"] = WiFi.RSSI(i);
    n["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleTestSta() {
  if (!requireAuth(true)) return;
  if (!needsFleetSetupPrompt()) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"setup_only\"}");
    return;
  }
  DynamicJsonDocument body(256);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  const auto &cfg = config_->settings();
  const char *ssid = body["wifi_sta_ssid"] | cfg.wifi_sta_ssid.c_str();
  const char *pass = body["wifi_sta_password"] | cfg.wifi_sta_password.c_str();
  if (ssid == nullptr || ssid[0] == '\0') {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }

  const bool alreadyConnectedSameSsid = WiFi.isConnected() && WiFi.SSID().equals(ssid);
  const bool sameAsConfigured = cfg.wifi_sta_ssid.equals(ssid) && cfg.wifi_sta_password.equals(pass);
  if (alreadyConnectedSameSsid && sameAsConfigured) {
    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    doc["status_code"] = static_cast<int>(WL_CONNECTED);
    doc["status_text"] = wifiStatusText(WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    const size_t len = measureJson(doc);
    server_.setContentLength(len);
    server_.send(200, "application/json", "");
    serializeJson(doc, server_.client());
    return;
  }

  WiFi.begin(ssid, pass);
  wl_status_t st = WL_IDLE_STATUS;
  for (int i = 0; i < kStaTestMaxAttempts; i++) {
    delay(100);
    yield();
    st = WiFi.status();
    if (st == WL_CONNECTED) break;
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
  }

  DynamicJsonDocument doc(256);
  const bool ok = (st == WL_CONNECTED);
  doc["ok"] = ok;
  doc["status_code"] = static_cast<int>(st);
  doc["status_text"] = wifiStatusText(st);
  if (ok) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  }

  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());

  // If test credentials differ from persisted settings, restore configured STA
  // after replying so the HTTP response has a chance to reach the browser.
  if (!sameAsConfigured) {
    delay(80);
    WiFi.disconnect();
    delay(20);
    if (cfg.wifi_sta_ssid.length() > 0) {
      WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str());
    }
  }
}
