#include "app.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#if LRS_ENABLE_MDNS
#include <ESP8266mDNS.h>
#endif
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
#if LRS_ENABLE_MDNS
constexpr uint32_t kMdnsSuspendFreeHeapBytes = 9000;
constexpr uint32_t kMdnsSuspendMaxBlockBytes = 3000;
constexpr uint32_t kMdnsResumeFreeHeapBytes = 12000;
constexpr uint32_t kMdnsResumeMaxBlockBytes = 5000;
#endif
constexpr uint32_t kStartupTraceWindowMs = 15000;
constexpr uint32_t kStartupTraceBreadcrumbMs = 1000;
constexpr uint32_t kStartupSlowTickWarnMs = 25;
constexpr uint32_t kStartupNonEssentialDeferralMs = 10000;
constexpr uint32_t kOtaStartupMinFreeHeapBytes = 3000;
constexpr uint32_t kOtaStartupMinMaxBlockBytes = 1200;
constexpr uint32_t kSteadySlowPhaseWarnMs = 50;
constexpr uint32_t kSteadySlowPhaseWarnRateLimitMs = 5000;
constexpr uint32_t kSteadySlowPhaseWarnImmediateMs = 250;

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
  web_.begin(&config_, &sm_, &sensors_, &logs_, [this](bool restartNetwork, bool restartOtaAuth) {
    applyUpdatedConfig(restartNetwork, restartOtaAuth);
  });

  startOta();
  startup_trace_until_ms_ = millis() + kStartupTraceWindowMs;
  startup_trace_next_breadcrumb_ms_ = 0;
  startup_defer_logged_ = false;
  slow_phase_last_log_ms_ = 0;
  slow_phase_suppressed_count_ = 0;

  logs_.add("boot", 0, 0, 0);
}

void App::tick() {
  const uint32_t tickStartMs = millis();
  const bool startupTrace = static_cast<int32_t>(tickStartMs - startup_trace_until_ms_) < 0;
  bool emitStartupBreadcrumb = false;
  auto phaseSlowWarn = [&](const char *phase, uint32_t phaseStartMs) {
    const uint32_t endMs = millis();
    const uint32_t durMs = endMs - phaseStartMs;
    if (startupTrace) {
      if (durMs < kStartupSlowTickWarnMs) return;
      const uint32_t freeHeap = lrslog::heapFree();
      const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
      const uint32_t ratioPct = (freeHeap != 0U) ? ((maxBlock * 100UL) / freeHeap) : 0U;
      LRS_LOGW(SYS,
               "event=startup_tick_slow phase=%s dur_ms=%lu heap_free=%lu max_free_block=%lu max_block_ratio_pct=%lu",
               phase,
               static_cast<unsigned long>(durMs),
               static_cast<unsigned long>(freeHeap),
               static_cast<unsigned long>(maxBlock),
               static_cast<unsigned long>(ratioPct));
      return;
    }

    if (durMs < kSteadySlowPhaseWarnMs) return;

    const bool severe = durMs >= kSteadySlowPhaseWarnImmediateMs;
    const bool rateLimitActive =
        (slow_phase_last_log_ms_ != 0U) &&
        (static_cast<uint32_t>(endMs - slow_phase_last_log_ms_) < kSteadySlowPhaseWarnRateLimitMs);
    if (!severe && rateLimitActive) {
      if (slow_phase_suppressed_count_ != 0xFFFFU) {
        ++slow_phase_suppressed_count_;
      }
      return;
    }

    const uint32_t freeHeap = lrslog::heapFree();
    const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
    const uint32_t ratioPct = (freeHeap != 0U) ? ((maxBlock * 100UL) / freeHeap) : 0U;
    LRS_LOGW(SYS,
             "event=slow_phase phase=%s dur_ms=%lu heap_free=%lu max_free_block=%lu max_block_ratio_pct=%lu suppressed=%u",
             phase,
             static_cast<unsigned long>(durMs),
             static_cast<unsigned long>(freeHeap),
             static_cast<unsigned long>(maxBlock),
             static_cast<unsigned long>(ratioPct),
             static_cast<unsigned>(slow_phase_suppressed_count_));
    slow_phase_last_log_ms_ = endMs;
    slow_phase_suppressed_count_ = 0;
  };
  if (startupTrace && (startup_trace_next_breadcrumb_ms_ == 0 || static_cast<int32_t>(tickStartMs - startup_trace_next_breadcrumb_ms_) >= 0)) {
    startup_trace_next_breadcrumb_ms_ = tickStartMs + kStartupTraceBreadcrumbMs;
    emitStartupBreadcrumb = true;
    LRS_LOGD(SYS, "event=startup_tick phase=begin ms=%lu heap_free=%lu max_free_block=%lu",
             static_cast<unsigned long>(tickStartMs), static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  }

  uint32_t phaseStartMs = millis();
  updateNetworking();
  phaseSlowWarn("update_networking", phaseStartMs);
  phaseStartMs = millis();
  tickTimeSync();
  phaseSlowWarn("tick_time_sync", phaseStartMs);
  phaseStartMs = millis();
  refreshCaptiveDns();
  if (dns_running_) {
    dns_.processNextRequest();
  }
  phaseSlowWarn("dns", phaseStartMs);
  phaseStartMs = millis();
  refreshMdns();
  phaseSlowWarn("refresh_mdns_pre", phaseStartMs);
  const TempSensorStatus &ts = sensors_.tempStatus();
  sm_.setLocalTemperature(ts.valid, ts.celsius);
  if (emitStartupBreadcrumb) {
    LRS_LOGD(SYS, "event=startup_tick phase=sm_enter ms=%lu", static_cast<unsigned long>(millis()));
  }
  phaseStartMs = millis();
  sm_.tick();
  phaseSlowWarn("sm_tick", phaseStartMs);
  {
    if (sm_.hasPendingWifiProvision()) {
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
    if (sm_.hasPendingFleetProvisionApply()) {
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
  }
  const bool startupDeferNonEssential = !WiFi.isConnected() && (millis() < kStartupNonEssentialDeferralMs);
  if (startupDeferNonEssential) {
    if (!startup_defer_logged_) {
      startup_defer_logged_ = true;
      LRS_LOGI(SYS, "event=startup_defer_nonessential until_ms=%lu reason=wifi_not_connected",
               static_cast<unsigned long>(kStartupNonEssentialDeferralMs));
    }
  } else {
    startup_defer_logged_ = false;
    phaseStartMs = millis();
    mqtt_.tick(WiFi.isConnected());
    phaseSlowWarn("mqtt_tick", phaseStartMs);
  }
  phaseStartMs = millis();
  sensors_.tick();
  phaseSlowWarn("sensors_tick", phaseStartMs);
  if (emitStartupBreadcrumb) {
    LRS_LOGD(SYS, "event=startup_tick phase=web_enter ms=%lu", static_cast<unsigned long>(millis()));
  }
  phaseStartMs = millis();
  web_.tick();
  phaseSlowWarn("web_tick", phaseStartMs);
  // Re-check mDNS after web handlers because API requests can drop heap quickly.
  phaseStartMs = millis();
  refreshMdns();
  phaseSlowWarn("refresh_mdns_post", phaseStartMs);
#if LRS_ENABLE_MDNS
  {
    ProvisioningSessionSnapshot prov{};
    const bool provActive = sm_.provisioningSession(prov) && prov.active;
    if (!provActive && !mdns_suspended_for_low_heap_) {
      phaseStartMs = millis();
      MDNS.update();
      phaseSlowWarn("mdns_update", phaseStartMs);
    }
  }
#endif
  if (!startupDeferNonEssential) {
    if (ota_enabled_) {
      phaseStartMs = millis();
      ArduinoOTA.handle();
      phaseSlowWarn("ota_handle", phaseStartMs);
    }
  }
}

void App::startNetworking() {
  refreshCachedStaHostname();
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
  refreshCachedStaHostname();
  radio_.applyConfig(config_.settings());
  sm_.applyConfig(config_.settings());
  mqtt_.applyConfig(config_.settings(), config_.chipIdHex());
  sensors_.applyConfig(config_.settings());

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
  if (cached_sta_hostname_.length() == 0) {
    refreshCachedStaHostname();
  }
  const String &host = cached_sta_hostname_;
  WiFi.hostname(host);
  WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str());
  wifi_sta_connecting_ = true;
  wifi_sta_started_ms_ = millis();
  wifi_sta_retry_ms_ = millis();
  logs_.add("sta_connect_start", 0, 0, 0);
  LRS_LOGI(WIFI, "event=sta_connect_start ssid=%s host=%s", cfg.wifi_sta_ssid.c_str(), host.c_str());
}

void App::startOta() {
  ota_enabled_ = false;
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  if (freeHeap < kOtaStartupMinFreeHeapBytes || maxBlock < kOtaStartupMinMaxBlockBytes) {
    logs_.add("ota_disabled_heap", 0, 0, 0);
    LRS_LOGW(SYS,
             "event=ota_disabled reason=low_startup_heap heap_free=%lu max_free_block=%lu min_free=%lu min_max_block=%lu",
             static_cast<unsigned long>(freeHeap),
             static_cast<unsigned long>(maxBlock),
             static_cast<unsigned long>(kOtaStartupMinFreeHeapBytes),
             static_cast<unsigned long>(kOtaStartupMinMaxBlockBytes));
    return;
  }

  const auto &cfg = config_.settings();
  if (cached_sta_hostname_.length() == 0) {
    refreshCachedStaHostname();
  }
  const String &host = cached_sta_hostname_;
  ArduinoOTA.setHostname(host.c_str());
  ArduinoOTA.setPassword(cfg.admin_password.c_str());
  // Disable ArduinoOTA's internal mDNS to avoid extra heap pressure and mDNS parsing work.
  ArduinoOTA.begin(false);
  ota_enabled_ = true;
  logs_.add("ota_ready", 0, 0, 0);
}

void App::refreshMdns() {
#if !LRS_ENABLE_MDNS
  return;
#else
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxBlock = ESP.getMaxFreeBlockSize();
  const bool lowHeapNow = (freeHeap < kMdnsSuspendFreeHeapBytes) || (maxBlock < kMdnsSuspendMaxBlockBytes);
  const bool heapRecovered = (freeHeap >= kMdnsResumeFreeHeapBytes) && (maxBlock >= kMdnsResumeMaxBlockBytes);
  if (lowHeapNow) {
    if (!mdns_suspended_for_low_heap_) {
      MDNS.close();
      active_mdns_hostname_ = "";
      mdns_suspended_for_low_heap_ = true;
      logs_.add("mdns_paused_heap", 0, 0, 0);
      LRS_LOGW(MDNS,
               "event=mdns_paused reason=low_heap heap_free=%lu max_free_block=%lu",
               freeHeap,
               maxBlock);
    }
    return;
  }
  if (mdns_suspended_for_low_heap_) {
    if (!heapRecovered) {
      return;
    }
    mdns_suspended_for_low_heap_ = false;
    logs_.add("mdns_resume_heap", 0, 0, 0);
    LRS_LOGI(MDNS,
             "event=mdns_resumed reason=heap_recovered heap_free=%lu max_free_block=%lu",
             freeHeap,
             maxBlock);
  }

  {
    ProvisioningSessionSnapshot prov{};
    const bool provActive = sm_.provisioningSession(prov) && prov.active;
    if (provActive) {
      if (!mdns_suspended_for_provisioning_) {
        MDNS.close();
        active_mdns_hostname_ = "";
        mdns_suspended_for_provisioning_ = true;
        logs_.add("mdns_paused_prov", 0, prov.session_nonce, 0);
        LRS_LOGI(MDNS, "event=mdns_paused reason=provisioning session=%u", prov.session_nonce);
      }
      return;
    }
    if (mdns_suspended_for_provisioning_) {
      mdns_suspended_for_provisioning_ = false;
      logs_.add("mdns_resume_prov", 0, 0, 0);
      LRS_LOGI(MDNS, "event=mdns_resumed reason=provisioning_complete");
    }
  }

  const bool forceApHost = ap_enabled_ && WiFi.softAPgetStationNum() > 0;
  const char *desiredLiteral = nullptr;
  const String *desiredRef = nullptr;
  if (forceApHost || !sta_connected_) {
    desiredLiteral = "lrs";
  } else {
    if (cached_sta_hostname_.length() == 0) {
      refreshCachedStaHostname();
    }
    desiredRef = &cached_sta_hostname_;
  }

  const bool unchanged = (desiredLiteral != nullptr) ? (active_mdns_hostname_ == desiredLiteral)
                                                     : (desiredRef != nullptr && active_mdns_hostname_ == *desiredRef);
  if (unchanged) {
    return;
  }

  const char *desiredName = (desiredLiteral != nullptr) ? desiredLiteral : desiredRef->c_str();

  MDNS.close();
  if (!MDNS.begin(desiredName)) {
    logs_.add("mdns_failed", 0, 0, 0);
    LRS_LOGW(MDNS, "event=mdns_start_failed host=%s", desiredName);
    return;
  }
  MDNS.addService("http", "tcp", 80);
  active_mdns_hostname_ = desiredName;
  logs_.add(String("mdns_ready_") + active_mdns_hostname_, 0, 0, 0);
  LRS_LOGI(MDNS, "event=mdns_ready host=%s", desiredName);
#endif
}

void App::refreshCachedStaHostname() {
  const auto &cfg = config_.settings();
  if (cfg.lan_hostname.length() > 0) {
    cached_sta_hostname_ = normalizeHostname(cfg.lan_hostname);
    return;
  }
  String fallback = "lrs-";
  fallback += config_.chipIdHex();
  cached_sta_hostname_ = normalizeHostname(fallback);
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
