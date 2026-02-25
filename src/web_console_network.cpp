#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "config_store.h"
#include "web_console_internal.h"

using namespace webconsole_internal;

void WebConsole::handleWifiScan() {
  if (!requireAuth(true)) return;
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.createNestedArray("networks");
  const int count = WiFi.scanNetworks(false, true);
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
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleTestSta() {
  if (!requireAuth(true)) return;
  if (!needsFleetSetupPrompt()) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"setup_only\"}");
    return;
  }
  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  const String ssid = String(static_cast<const char *>(body["wifi_sta_ssid"] | config_->settings().wifi_sta_ssid.c_str()));
  const String pass = String(static_cast<const char *>(body["wifi_sta_password"] | config_->settings().wifi_sta_password.c_str()));
  if (ssid.length() == 0) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }

  const auto &cfg = config_->settings();
  const bool alreadyConnectedSameSsid = WiFi.isConnected() && WiFi.SSID() == ssid;
  const bool sameAsConfigured = (ssid == cfg.wifi_sta_ssid) && (pass == cfg.wifi_sta_password);
  if (alreadyConnectedSameSsid && sameAsConfigured) {
    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    doc["status_code"] = static_cast<int>(WL_CONNECTED);
    doc["status_text"] = wifiStatusText(WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
    return;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
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

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);

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
