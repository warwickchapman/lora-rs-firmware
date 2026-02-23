#include "app.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>

#include "build_info.h"
#include "logger.h"

namespace {
constexpr uint32_t kStaConnectTimeoutMs = 20000;
constexpr uint32_t kStaReconnectIntervalMs = 10000;
constexpr uint32_t kApDisableDelayAfterStaMs = 60000;
constexpr uint32_t kNtpPollNoFixMs = 5000;
constexpr uint32_t kNtpPollFixedMs = 60000;
constexpr uint32_t kNtpForceRefreshMs = 21600000;
constexpr uint32_t kMinValidUnixTimeS = 1704067200UL;  // 2024-01-01 UTC

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
  LRS_LOGI(SYS,
           "event=boot_banner fw=%s git=%s branch=%s dirty=%d built=\"%s %s\" reset_reason=%s",
           LRS_FW_VERSION,
           LRS_GIT_SHA,
           LRS_GIT_BRANCH,
           static_cast<int>(LRS_GIT_DIRTY),
           __DATE__,
           __TIME__,
           ESP.getResetReason().c_str());
  const bool fsReady = config_.begin();
  if (!fsReady) {
    LRS_LOGE(FS, "event=config_store_init_failed");
  }

  startNetworking();

  if (!radio_.begin(config_.settings(), &logs_)) {
    logs_.add("radio_start_failed", 0, 0, 0);
  }
  sensors_.begin(config_.settings(), &logs_);

  sm_.begin(config_.settings(), &radio_, &logs_);
  auto unixProvider = [this](uint32_t &unixTimeS) {
    if (!sm_.sharedUnixTimeValid()) return false;
    unixTimeS = sm_.sharedUnixTime();
    return (unixTimeS != 0);
  };
  logs_.setTimeProvider(unixProvider);
  lrslog::setUnixTimeProvider(unixProvider);
  mqtt_.begin(config_.settings(), config_.chipIdHex(), &sm_, &logs_);
  automation_.begin(&sm_, &logs_, config_.settings());

  web_.begin(&config_, &sm_, &sensors_, &logs_, &automation_, [this](bool restartNetwork, bool restartOtaAuth) {
    applyUpdatedConfig(restartNetwork, restartOtaAuth);
  });

  startOta();

  logs_.add("boot", 0, 0, 0);
}

void App::tick() {
  updateNetworking();
  tickTimeSync();
  refreshCaptiveDns();
  if (dns_running_) {
    dns_.processNextRequest();
  }
  refreshMdns();
  const TempSensorStatus ts = sensors_.tempStatus();
  sm_.setLocalTemperature(ts.valid, ts.celsius);
  sm_.tick();
  automation_.tick();
  {
    String provSsid;
    String provPassword;
    uint8_t provSrc = 0;
    if (sm_.consumePendingWifiProvision(provSsid, provPassword, provSrc)) {
      auto &cfg = config_.settings();
      const bool changed = (cfg.wifi_sta_ssid != provSsid) || (cfg.wifi_sta_password != provPassword);
      cfg.wifi_sta_ssid = provSsid;
      cfg.wifi_sta_password = provPassword;
      cfg.audit_last_saved_by = "lora_wifi_provision";
      cfg.audit_last_saved_ms = millis();
      if (config_.save()) {
        logs_.add("wifi_prov_applied", 0, provSrc, static_cast<uint8_t>(provSsid.length() & 0xFFU));
        if (changed) {
          applyUpdatedConfig(true, false);
        }
      } else {
        logs_.add("wifi_prov_save_fail", 0, provSrc, 0);
      }
    }
  }
  {
    bool keepFleetKey = true;
    uint8_t resetSrc = 0;
    if (sm_.consumePendingFactoryReset(keepFleetKey, resetSrc)) {
      logs_.add(keepFleetKey ? "factory_reset_exec_keep" : "factory_reset_exec_full", 0, resetSrc, 0);
      if (config_.factoryReset(keepFleetKey)) {
        delay(100);
        ESP.restart();
        return;
      }
      logs_.add("factory_reset_exec_save_fail", 0, resetSrc, 0);
    }
  }
  {
    uint16_t provSession = 0;
    uint8_t provAddr = 0;
    bool provRoleTx = false;
    String provFleetKey;
    if (sm_.consumePendingFleetProvisionApply(provSession, provAddr, provRoleTx, provFleetKey)) {
      auto &cfg = config_.settings();
      const bool changed = (cfg.local_address != provAddr) || (cfg.role_tx != provRoleTx) || (cfg.fleet_passphrase != provFleetKey);
      cfg.local_address = provAddr;
      cfg.role_tx = provRoleTx;
      cfg.fleet_passphrase = provFleetKey;
      cfg.fleet_setup_prompt_dismissed = !provFleetKey.isEmpty();
      cfg.audit_last_saved_by = "lora_fleet_provision";
      cfg.audit_last_saved_ms = millis();
      if (config_.save()) {
        logs_.add("fleet_prov_applied", 0, provSession, provAddr);
        if (changed) {
          applyUpdatedConfig(false, false);
        }
        sm_.sendProvisioningVerify(provSession, provAddr);
      } else {
        logs_.add("fleet_prov_save_fail", 0, provSession, provAddr);
      }
    }
  }
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
  ntp_started_ = false;
  ntp_time_valid_ = false;
  ntp_last_check_ms_ = 0;
  ntp_last_sync_ms_ = 0;
  ntp_last_epoch_s_ = 0;

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
      LRS_LOGI(WIFI,
               "event=sta_connected ssid=%s ip=%s rssi=%d",
               cfg.wifi_sta_ssid.c_str(),
               WiFi.localIP().toString().c_str(),
               rssi);
      startNtpClient();
      maybeDisableAp();
      return;
    }

    if (millis() - wifi_sta_started_ms_ > kStaConnectTimeoutMs) {
      sta_connected_ = false;
      wifi_sta_connecting_ = false;
      WiFi.disconnect();
      wifi_sta_retry_ms_ = millis();
      logs_.add("sta_connect_failed_fallback_ap", 0, 0, 0);
      LRS_LOGW(WIFI,
               "event=sta_connect_failed ssid=%s reason=%s status=%d ap_fallback=1",
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
      LRS_LOGI(WIFI,
               "event=sta_connected ssid=%s ip=%s rssi=%d",
               cfg.wifi_sta_ssid.c_str(),
               WiFi.localIP().toString().c_str(),
               rssi);
      startNtpClient();
    }
    maybeDisableAp();
    return;
  }

  if (sta_connected_) {
    sta_connected_ = false;
    logs_.add("sta_disconnected", 0, 0, 0);
    LRS_LOGW(WIFI, "event=sta_disconnected");
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
  automation_.applyConfig(config_.settings());

  if (restartOtaAuth) {
    // ESP8266 ArduinoOTA cannot replace password once initialized in-process.
    // Reboot is required to apply new OTA credentials reliably.
    logs_.add("ota_auth_changed_reboot", 0, 0, 0);
    LRS_LOGI(SYS, "event=config_apply restart_network=0 restart_ota_auth=1 action=reboot");
    delay(100);
    ESP.restart();
    return;
  }

  if (restartNetwork) {
    LRS_LOGI(SYS, "event=config_apply restart_network=1 restart_ota_auth=0");
    WiFi.disconnect();
    delay(50);
    startNetworking();
  } else {
    LRS_LOGI(SYS, "event=config_apply restart_network=0 restart_ota_auth=0");
  }
  refreshMdns();

  logs_.add("config_reloaded", 0, 0, 0);
}

void App::startNtpClient() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  ntp_started_ = true;
  ntp_last_check_ms_ = 0;
  logs_.add("ntp_start", 0, 0, 0);
  LRS_LOGI(NTP, "event=ntp_start servers=pool.ntp.org,time.nist.gov,time.google.com");
}

void App::tickTimeSync() {
  if (!sta_connected_) {
    return;
  }
  if (!ntp_started_) {
    startNtpClient();
  }

  const uint32_t nowMs = millis();
  const uint32_t pollInterval = ntp_time_valid_ ? kNtpPollFixedMs : kNtpPollNoFixMs;
  if ((nowMs - ntp_last_check_ms_) < pollInterval) {
    return;
  }
  ntp_last_check_ms_ = nowMs;

  const time_t nowUnix = time(nullptr);
  if (nowUnix < static_cast<time_t>(kMinValidUnixTimeS)) {
    return;
  }

  const bool hadValidTime = ntp_time_valid_;
  const bool refreshDue = ntp_time_valid_ && ((nowMs - ntp_last_sync_ms_) >= kNtpForceRefreshMs);
  const uint32_t unixTimeS = static_cast<uint32_t>(nowUnix);
  bool shouldPushToStateMachine = !sm_.sharedUnixTimeValid();
  if (!shouldPushToStateMachine) {
    const uint32_t shared = sm_.sharedUnixTime();
    const uint32_t delta = (shared > unixTimeS) ? (shared - unixTimeS) : (unixTimeS - shared);
    shouldPushToStateMachine = delta > 2U;
  }
  ntp_time_valid_ = true;
  ntp_last_epoch_s_ = unixTimeS;
  ntp_last_sync_ms_ = nowMs;
  if (shouldPushToStateMachine) {
    sm_.setAuthoritativeUnixTime(unixTimeS);
  }

  if (!hadValidTime || refreshDue) {
    LRS_LOGI(NTP,
             "event=ntp_sync_ok unix=%lu push_state=%u refresh_due=%u",
             static_cast<unsigned long>(unixTimeS),
             shouldPushToStateMachine ? 1U : 0U,
             refreshDue ? 1U : 0U);
  }

  if (refreshDue) {
    startNtpClient();
  }
}

void App::ensureApEnabled() {
  if (ap_enabled_) return;
  const String apSsid = config_.apSsid();
  const String apPass = config_.apPassword();
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  ap_enabled_ = true;
  LRS_LOGI(WIFI, "event=ap_active ssid=%s ip=%s", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
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
  LRS_LOGI(WIFI, "event=ap_disabled reason=sta_stable");
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
  LRS_LOGI(WIFI, "event=sta_connect_start ssid=%s host=%s", cfg.wifi_sta_ssid.c_str(), host.c_str());
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
    LRS_LOGW(MDNS, "event=mdns_start_failed host=%s", desired.c_str());
    return;
  }
  MDNS.addService("http", "tcp", 80);
  active_mdns_hostname_ = desired;
  logs_.add(String("mdns_ready_") + desired, 0, 0, 0);
  LRS_LOGI(MDNS, "event=mdns_ready host=%s", desired.c_str());
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
