#include "app.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "build_info.h"

namespace {
constexpr uint32_t kStaConnectTimeoutMs = 20000;
constexpr uint32_t kStaReconnectIntervalMs = 10000;
constexpr uint32_t kApDisableDelayAfterStaMs = 60000;

const char *wifiStatusText(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_found";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
#ifdef WL_WRONG_PASSWORD
    case WL_WRONG_PASSWORD:
      return "wrong_password";
#endif
#ifdef WL_NO_SHIELD
    case WL_NO_SHIELD:
      return "no_shield";
#endif
    default:
      return "unknown";
  }
}
}

void App::begin() {
  Serial.printf("[LRS] fw=%s git=%s branch=%s dirty=%d built=%s %s\n",
                LRS_FW_VERSION,
                LRS_GIT_SHA,
                LRS_GIT_BRANCH,
                static_cast<int>(LRS_GIT_DIRTY),
                __DATE__,
                __TIME__);
  const bool fsReady = config_.begin();
  if (!fsReady) {
    Serial.println("Config storage init failed");
  }

  startNetworking();

  if (!radio_.begin(config_.settings(), &logs_)) {
    logs_.add("radio_start_failed", 0, 0, 0);
  }
  sensors_.begin(config_.settings(), &logs_);

  sm_.begin(config_.settings(), &radio_, &logs_);
  mqtt_.begin(config_.settings(), config_.chipIdHex(), &sm_, &logs_);

  web_.begin(&config_, &sm_, &sensors_, &logs_, [this](bool restartNetwork, bool restartOtaAuth) {
    applyUpdatedConfig(restartNetwork, restartOtaAuth);
  });

  startOta();

  logs_.add("boot", 0, 0, 0);
}

void App::tick() {
  updateNetworking();
  refreshCaptiveDns();
  if (dns_running_) {
    dns_.processNextRequest();
  }
  refreshMdns();
  const TempSensorStatus ts = sensors_.tempStatus();
  sm_.setLocalTemperature(ts.valid, ts.celsius);
  sm_.tick();
  mqtt_.tick(WiFi.isConnected());
  sensors_.tick();
  web_.tick();
  MDNS.update();
  ArduinoOTA.handle();
}

void App::startNetworking() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setOutputPower(20.5f);
  sta_connected_ = false;
  wifi_sta_connecting_ = false;
  wifi_sta_started_ms_ = 0;
  wifi_sta_retry_ms_ = 0;
  sta_connected_since_ms_ = 0;
  ap_enabled_ = false;
  dns_running_ = false;

  ensureApEnabled();

  auto &cfg = config_.settings();
  if (cfg.wifi_sta_ssid.length() >= 1) {
    beginStaConnect();
  }
}

void App::updateNetworking() {
  auto &cfg = config_.settings();
  const wl_status_t st = WiFi.status();

  if (wifi_sta_connecting_) {
    if (st == WL_CONNECTED) {
      sta_connected_ = true;
      wifi_sta_connecting_ = false;
      sta_connected_since_ms_ = millis();
      const int rssi = WiFi.RSSI();
      logs_.add("sta_connected", rssi, 0, 0);
      Serial.printf("[LRS] STA connected ssid=%s ip=%s rssi=%d dBm\n",
                    cfg.wifi_sta_ssid.c_str(),
                    WiFi.localIP().toString().c_str(),
                    rssi);
      maybeDisableAp();
      return;
    }

    if (millis() - wifi_sta_started_ms_ > kStaConnectTimeoutMs) {
      sta_connected_ = false;
      wifi_sta_connecting_ = false;
      WiFi.disconnect();
      wifi_sta_retry_ms_ = millis();
      logs_.add("sta_connect_failed_fallback_ap", 0, 0, 0);
      Serial.printf("[LRS] STA connect failed ssid=%s reason=%s[%d], AP fallback active\n",
                    cfg.wifi_sta_ssid.c_str(),
                    wifiStatusText(st),
                    static_cast<int>(st));
      ensureApEnabled();
    }
    return;
  }

  if (st == WL_CONNECTED) {
    if (!sta_connected_) {
      sta_connected_ = true;
      sta_connected_since_ms_ = millis();
      const int rssi = WiFi.RSSI();
      logs_.add("sta_connected", rssi, 0, 0);
      Serial.printf("[LRS] STA connected ssid=%s ip=%s rssi=%d dBm\n",
                    cfg.wifi_sta_ssid.c_str(),
                    WiFi.localIP().toString().c_str(),
                    rssi);
    }
    maybeDisableAp();
    return;
  }

  if (sta_connected_) {
    sta_connected_ = false;
    logs_.add("sta_disconnected", 0, 0, 0);
    Serial.println("[LRS] STA disconnected");
    ensureApEnabled();
    wifi_sta_retry_ms_ = millis();
  }

  if (cfg.wifi_sta_ssid.length() >= 1 && millis() - wifi_sta_retry_ms_ >= kStaReconnectIntervalMs) {
    beginStaConnect();
  } else {
    ensureApEnabled();
  }
}

void App::applyUpdatedConfig(bool restartNetwork, bool restartOtaAuth) {
  radio_.applyConfig(config_.settings());
  sm_.applyConfig(config_.settings());
  mqtt_.applyConfig(config_.settings(), config_.chipIdHex());
  sensors_.applyConfig(config_.settings());

  if (restartOtaAuth) {
    // ESP8266 ArduinoOTA cannot replace password once initialized in-process.
    // Reboot is required to apply new OTA credentials reliably.
    logs_.add("ota_auth_changed_reboot", 0, 0, 0);
    delay(100);
    ESP.restart();
    return;
  }

  if (restartNetwork) {
    WiFi.disconnect();
    delay(50);
    startNetworking();
  }
  refreshMdns();

  logs_.add("config_reloaded", 0, 0, 0);
}

void App::ensureApEnabled() {
  if (ap_enabled_) return;
  const String apSsid = config_.apSsid();
  const String apPass = config_.apPassword();
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  ap_enabled_ = true;
  Serial.printf("[LRS] AP active ssid=%s ip=%s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void App::maybeDisableAp() {
  if (config_.settings().ap_always_on || !ap_enabled_ || !sta_connected_) {
    return;
  }
  if (millis() - sta_connected_since_ms_ < kApDisableDelayAfterStaMs) {
    return;
  }
  WiFi.softAPdisconnect(true);
  ap_enabled_ = false;
  if (dns_running_) {
    dns_.stop();
    dns_running_ = false;
  }
  Serial.println("[LRS] AP disabled (STA stable)");
}

void App::refreshCaptiveDns() {
  if (!ap_enabled_) {
    if (dns_running_) {
      dns_.stop();
      dns_running_ = false;
    }
    return;
  }
  if (dns_running_) return;
  dns_.start(53, "*", WiFi.softAPIP());
  dns_running_ = true;
}

void App::beginStaConnect() {
  auto &cfg = config_.settings();
  if (cfg.wifi_sta_ssid.length() == 0) return;
  const String host = normalizeHostname(cfg.lan_hostname.length() ? cfg.lan_hostname : String("lrs-") + config_.chipIdHex());
  WiFi.hostname(host);
  WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str());
  wifi_sta_connecting_ = true;
  wifi_sta_started_ms_ = millis();
  wifi_sta_retry_ms_ = millis();
  logs_.add("sta_connect_start", 0, 0, 0);
  Serial.printf("[LRS] STA connect start ssid=%s host=%s\n", cfg.wifi_sta_ssid.c_str(), host.c_str());
}

void App::startOta() {
  const auto &cfg = config_.settings();
  const String host = normalizeHostname(cfg.lan_hostname.length() ? cfg.lan_hostname : String("lrs-") + config_.chipIdHex());
  ArduinoOTA.setHostname(host.c_str());
  ArduinoOTA.setPassword(cfg.admin_password.c_str());
  ArduinoOTA.begin();
  logs_.add("ota_ready", 0, 0, 0);
}

void App::refreshMdns() {
  String desired;
  if (ap_enabled_ && WiFi.softAPgetStationNum() > 0) {
    desired = "lrs";
  } else if (sta_connected_) {
    desired = normalizeHostname(config_.settings().lan_hostname.length()
                                    ? config_.settings().lan_hostname
                                    : (String("lrs-") + config_.chipIdHex()));
  } else {
    desired = "lrs";
  }

  if (desired == active_mdns_hostname_) {
    return;
  }

  MDNS.close();
  if (!MDNS.begin(desired.c_str())) {
    logs_.add("mdns_failed", 0, 0, 0);
    return;
  }
  MDNS.addService("http", "tcp", 80);
  active_mdns_hostname_ = desired;
  logs_.add(String("mdns_ready_") + desired, 0, 0, 0);
}

String App::normalizeHostname(const String &input) const {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      out += c;
    } else if (c == ' ' || c == '_' || c == '.') {
      out += '-';
    }
  }
  while (out.startsWith("-")) out.remove(0, 1);
  while (out.endsWith("-")) out.remove(out.length() - 1);
  if (out.length() == 0) return "lrs";
  if (out.length() > 31) out.remove(31);
  return out;
}
