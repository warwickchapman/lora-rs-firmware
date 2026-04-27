#include "app.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <cstring>
#include <time.h>
extern "C" {
#include <user_interface.h>
}

#include "build_info.h"
#include "logger.h"
#include "runtime_utils.h"

namespace {
constexpr uint32_t kStaConnectTimeoutMs = 20000;
constexpr uint32_t kApDisableDelayAfterStaMs = 60000;
constexpr uint32_t kNtpPollNoFixMs = 5000;
constexpr uint32_t kNtpPollFixedMs = 60000;
constexpr uint32_t kNtpForceRefreshMs = 21600000;
constexpr uint32_t kMinValidUnixTimeS = 1704067200UL; // 2024-01-01 UTC
constexpr uint32_t kStartupTraceWindowMs = 15000;
constexpr uint32_t kStartupTraceBreadcrumbMs = 1000;
constexpr uint32_t kStartupSlowTickWarnMs = 25;
constexpr uint32_t kStartupNonEssentialDeferralMs = 10000;
constexpr uint32_t kOtaStartupMinFreeHeapBytes = 3000;
constexpr uint32_t kOtaStartupMinMaxBlockBytes = 1200;
constexpr uint32_t kSteadySlowPhaseWarnMs = 50;
constexpr uint32_t kSteadySlowPhaseWarnRateLimitMs = 5000;
constexpr uint32_t kSteadySlowPhaseWarnImmediateMs = 250;
// Keep a critical-only guard for STA reconnect attempts. Removing this guard
// entirely can cause reconnect churn under fragmentation-heavy conditions.
constexpr uint32_t kStaReconnectCriticalMinFreeHeapBytes = 5000;
constexpr uint32_t kStaReconnectCriticalMinMaxBlockBytes = 2500;
constexpr uint32_t kStaReconnectHeapLogIntervalMs = 30000;
constexpr uint8_t kStaFailureResetThreshold = 10;
constexpr uint8_t kStaStackResetLimit = 10;
constexpr uint32_t kStaReconnectFibMaxDelayS = 300;
constexpr uint32_t kStaScanTimeoutMs = 15000;

} // namespace

void App::begin() {
  LRS_LOGI(SYS,
           "event=boot_banner fw=%s git=%s branch=%s dirty=%d built=\"%s %s\" "
           "reset_reason=%s",
           LRS_FW_VERSION, LRS_GIT_SHA, LRS_GIT_BRANCH,
           static_cast<int>(LRS_GIT_DIRTY), __DATE__, __TIME__,
           ESP.getResetReason().c_str());
  const bool fsReady = config_.begin();
  if (!fsReady) {
    LRS_LOGE(FS, "event=config_store_init_failed");
  }
  {
    bool keepFleetKey = false;
    bool keepWifiCredentials = false;
    if (config_.consumePostOtaFactoryReset(keepFleetKey, keepWifiCredentials)) {
      LRS_LOGW(SYS,
               "event=post_ota_factory_reset_exec keep_fleet_key=%u keep_wifi=%u",
               keepFleetKey ? 1U : 0U,
               keepWifiCredentials ? 1U : 0U);
      if (config_.factoryReset(keepFleetKey, keepWifiCredentials)) {
        delay(100);
        ESP.restart();
        return;
      }
      LRS_LOGE(SYS,
               "event=post_ota_factory_reset_exec_failed keep_fleet_key=%u keep_wifi=%u",
               keepFleetKey ? 1U : 0U,
               keepWifiCredentials ? 1U : 0U);
    }
  }

  startNetworking();

  if (!radio_.begin(config_.settings())) {
    lrslog::event("radio_start_failed", 0, 0, 0);
  }
  sensors_.begin(config_.settings());

  sm_.begin(config_.settings(), &radio_);
  auto unixProvider = [this](uint32_t &unixTimeS) {
    if (!sm_.sharedUnixTimeValid())
      return false;
    unixTimeS = sm_.sharedUnixTime();
    return (unixTimeS != 0);
  };
  lrslog::setUnixTimeProvider(unixProvider);
  mqtt_.begin(config_.settings(), config_.chipIdHex(), &sm_);
#if LRS_ENABLE_AUTOMATIONS
  automations_.begin();
#endif
  web_.begin(
      &config_, &sm_, &sensors_,
      [this](bool restartNetwork, bool restartOtaAuth) {
        applyUpdatedConfig(restartNetwork, restartOtaAuth);
      },
      [this]() {
#if LRS_ENABLE_AUTOMATIONS
        automations_.requestReload();
#endif
      });

  startOta();
  startup_trace_until_ms_ = millis() + kStartupTraceWindowMs;
  startup_trace_next_breadcrumb_ms_ = 0;
  startup_defer_logged_ = false;
  slow_phase_last_log_ms_ = 0;
  slow_phase_suppressed_count_ = 0;

  lrslog::event("boot", 0, 0, 0);
}

void App::tick() {
  const uint32_t tickStartMs = millis();
  const bool startupTrace =
      static_cast<int32_t>(tickStartMs - startup_trace_until_ms_) < 0;
  bool emitStartupBreadcrumb = false;
  auto phaseSlowWarn = [&](const char *phase, uint32_t phaseStartMs) {
    const uint32_t endMs = millis();
    const uint32_t durMs = endMs - phaseStartMs;
    if (startupTrace) {
      if (durMs < kStartupSlowTickWarnMs)
        return;
      const uint32_t freeHeap = lrslog::heapFree();
      const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
      const uint32_t ratioPct =
          (freeHeap != 0U) ? ((maxBlock * 100UL) / freeHeap) : 0U;
      LRS_LOGW(SYS,
               "event=startup_tick_slow phase=%s dur_ms=%lu heap_free=%lu "
               "max_free_block=%lu max_block_ratio_pct=%lu",
               phase, static_cast<unsigned long>(durMs),
               static_cast<unsigned long>(freeHeap),
               static_cast<unsigned long>(maxBlock),
               static_cast<unsigned long>(ratioPct));
      return;
    }

    if (durMs < kSteadySlowPhaseWarnMs)
      return;

    const bool severe = durMs >= kSteadySlowPhaseWarnImmediateMs;
    const bool rateLimitActive =
        (slow_phase_last_log_ms_ != 0U) &&
        (static_cast<uint32_t>(endMs - slow_phase_last_log_ms_) <
         kSteadySlowPhaseWarnRateLimitMs);
    if (!severe && rateLimitActive) {
      if (slow_phase_suppressed_count_ != 0xFFFFU) {
        ++slow_phase_suppressed_count_;
      }
      return;
    }

    const uint32_t freeHeap = lrslog::heapFree();
    const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
    const uint32_t ratioPct =
        (freeHeap != 0U) ? ((maxBlock * 100UL) / freeHeap) : 0U;
    LRS_LOGW(SYS,
             "event=slow_phase phase=%s dur_ms=%lu heap_free=%lu "
             "max_free_block=%lu max_block_ratio_pct=%lu suppressed=%u",
             phase, static_cast<unsigned long>(durMs),
             static_cast<unsigned long>(freeHeap),
             static_cast<unsigned long>(maxBlock),
             static_cast<unsigned long>(ratioPct),
             static_cast<unsigned>(slow_phase_suppressed_count_));
    slow_phase_last_log_ms_ = endMs;
    slow_phase_suppressed_count_ = 0;
  };
  if (startupTrace &&
      (startup_trace_next_breadcrumb_ms_ == 0 ||
       static_cast<int32_t>(tickStartMs - startup_trace_next_breadcrumb_ms_) >=
           0)) {
    startup_trace_next_breadcrumb_ms_ = tickStartMs + kStartupTraceBreadcrumbMs;
    emitStartupBreadcrumb = true;
    LRS_LOGD(SYS,
             "event=startup_tick phase=begin ms=%lu heap_free=%lu "
             "max_free_block=%lu",
             static_cast<unsigned long>(tickStartMs),
             static_cast<unsigned long>(lrslog::heapFree()),
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
  const TempSensorStatus &ts = sensors_.tempStatus();
  sm_.setLocalTemperature(ts.valid, ts.celsius);
  if (emitStartupBreadcrumb) {
    LRS_LOGD(SYS, "event=startup_tick phase=sm_enter ms=%lu",
             static_cast<unsigned long>(millis()));
  }
  phaseStartMs = millis();
  sm_.tick();
  phaseSlowWarn("sm_tick", phaseStartMs);
#if LRS_ENABLE_AUTOMATIONS
  phaseStartMs = millis();
  automations_.tick(config_.settings(), sm_);
  phaseSlowWarn("automations_tick", phaseStartMs);
#endif
  {
    if (sm_.hasPendingWifiControl()) {
      bool wifiEnabled = true;
      uint8_t ctrlSrc = 0;
      uint32_t ctrlCounter = 0;
      if (sm_.consumePendingWifiControl(wifiEnabled, ctrlSrc, ctrlCounter)) {
        auto &cfg = config_.settings();
        const bool changed = (cfg.wifi_admin_enabled != wifiEnabled);
        cfg.wifi_admin_enabled = wifiEnabled;
        cfg.audit_last_saved_by = wifiEnabled ? "lora_wifi_enable" : "lora_wifi_disable";
        cfg.audit_last_saved_ms = millis();
        if (config_.save()) {
          const bool statusSent = sm_.sendWifiControlStatus(ctrlSrc, wifiEnabled, ctrlCounter);
          lrslog::event(wifiEnabled ? "wifi_control_enable_apply" : "wifi_control_disable_apply",
                        statusSent ? 1 : 0, ctrlSrc, changed ? 1 : 0);
          applyUpdatedConfig(true, false);
        } else {
          lrslog::event("wifi_control_save_fail", 0, ctrlSrc, 0);
        }
      }
    }
  }
  {
    if (sm_.hasPendingWifiProvision()) {
      String provSsid;
      String provPassword;
      uint8_t provSrc = 0;
      if (sm_.consumePendingWifiProvision(provSsid, provPassword, provSrc)) {
        auto &cfg = config_.settings();
        const bool changed = (cfg.wifi_sta_ssid != provSsid) ||
                             (cfg.wifi_sta_password != provPassword) ||
                             !cfg.wifi_admin_enabled;
        cfg.wifi_sta_ssid = provSsid;
        cfg.wifi_sta_password = provPassword;
        cfg.wifi_admin_enabled = true;
        cfg.audit_last_saved_by = "lora_wifi_provision";
        cfg.audit_last_saved_ms = millis();
        if (config_.save()) {
          lrslog::event("wifi_prov_applied", 0, provSrc,
                        static_cast<uint8_t>(provSsid.length() & 0xFFU));
          if (!changed) {
            LRS_LOGI(WIFI,
                     "event=wifi_prov_reapply reason=unchanged_credentials "
                     "action=restart_network");
          }
          // LoRa-driven WiFi provisioning must always restart networking so a
          // previously disabled/failed WiFi stack can recover without reboot.
          applyUpdatedConfig(true, false);
        } else {
          lrslog::event("wifi_prov_save_fail", 0, provSrc, 0);
        }
      }
    }
  }
  {
    bool keepFleetKey = true;
    uint8_t resetSrc = 0;
    if (sm_.consumePendingFactoryReset(keepFleetKey, resetSrc)) {
      lrslog::event(keepFleetKey ? "factory_reset_exec_keep"
                                 : "factory_reset_exec_full",
                    0, resetSrc, 0);
      if (config_.factoryReset(keepFleetKey)) {
        delay(100);
        ESP.restart();
        return;
      }
      lrslog::event("factory_reset_exec_save_fail", 0, resetSrc, 0);
    }
  }
  {
    if (sm_.hasPendingFleetProvisionApply()) {
      uint16_t provSession = 0;
      uint8_t provAddr = 0;
      bool provRoleTx = false;
      uint8_t provControllerAddr = 0;
      String provFleetKey;
      if (sm_.consumePendingFleetProvisionApply(provSession, provAddr,
                                                provRoleTx, provControllerAddr, provFleetKey)) {
        auto &cfg = config_.settings();
        const bool changed = (cfg.local_address != provAddr) ||
                             (cfg.role_tx != provRoleTx) ||
                             (cfg.fleet_passphrase != provFleetKey);
        cfg.local_address = provAddr;
        cfg.role_tx = provRoleTx;
        cfg.mode = "paired";
        cfg.role = provRoleTx ? "transmitter" : "receiver";
        if (provRoleTx) {
          cfg.paired_target_count = 0;
          memset(cfg.paired_target_addresses, 0, sizeof(cfg.paired_target_addresses));
        } else {
          if (provControllerAddr < 1 || provControllerAddr > 254) {
            provControllerAddr = cfg.remote_address;
          }
          cfg.allowed_controller_count = 1;
          memset(cfg.allowed_controller_addresses, 0, sizeof(cfg.allowed_controller_addresses));
          cfg.allowed_controller_addresses[0] = provControllerAddr;
          cfg.remote_address = provControllerAddr;
        }
        cfg.fleet_passphrase = provFleetKey;
        cfg.fleet_setup_prompt_dismissed = !provFleetKey.isEmpty();
        cfg.audit_last_saved_by = "lora_fleet_provision";
        cfg.audit_last_saved_ms = millis();
        if (config_.save()) {
          lrslog::event("fleet_prov_applied", 0, provSession, provAddr);
          if (changed) {
            applyUpdatedConfig(false, false);
          }
          sm_.sendProvisioningVerify(provSession, provAddr);
        } else {
          lrslog::event("fleet_prov_save_fail", 0, provSession, provAddr);
        }
      }
    }
  }
  const bool startupDeferNonEssential =
      !WiFi.isConnected() && (millis() < kStartupNonEssentialDeferralMs);
  if (startupDeferNonEssential) {
    if (!startup_defer_logged_) {
      startup_defer_logged_ = true;
      LRS_LOGI(SYS,
               "event=startup_defer_nonessential until_ms=%lu "
               "reason=wifi_not_connected",
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
    LRS_LOGD(SYS, "event=startup_tick phase=web_enter ms=%lu",
             static_cast<unsigned long>(millis()));
  }
  phaseStartMs = millis();
  web_.tick();
  phaseSlowWarn("web_tick", phaseStartMs);
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
  auto &cfg = config_.settings();
  const bool wasDisabled = wifi_stack_disabled_;
  wifi_stack_disabled_ = false;
  sta_connect_consecutive_failures_ = 0;
  sta_stack_reset_count_ = 0;
  resetStaReconnectFibonacci();
  applyWifiRuntimeSettings();
  sta_connected_ = false;
  wifi_sta_connecting_ = false;
  wifi_sta_scanning_ = false;
  wifi_sta_started_ms_ = 0;
  wifi_sta_connect_attempt_started_ms_ = 0;
  wifi_sta_scan_started_ms_ = 0;
  wifi_sta_retry_ms_ = 0;
  sta_connected_since_ms_ = 0;
  sta_reconnect_heap_block_log_ms_ = 0;
  ap_enabled_ = false;
  dns_running_ = false;
  ntp_started_ = false;
  ntp_time_valid_ = false;
  ntp_last_check_ms_ = 0;
  ntp_last_sync_ms_ = 0;
  ntp_last_epoch_s_ = 0;

  if (!cfg.wifi_admin_enabled) {
    stopWifiForAdminDisable();
    return;
  }

  if (shouldEnableSoftAp()) {
    ensureApEnabled();
  }

  if (wasDisabled) {
    LRS_LOGI(WIFI, "event=sta_stack_reenabled reason=network_restart");
  }
  if (cfg.wifi_sta_ssid.length() >= 1) {
    beginStaConnect();
  }
}

void App::updateNetworking() {
  if (wifi_stack_disabled_) {
    return;
  }

  auto &cfg = config_.settings();
  if (!cfg.wifi_admin_enabled) {
    stopWifiForAdminDisable();
    return;
  }
  const wl_status_t st = WiFi.status();

  if (wifi_sta_scanning_) {
    const int scanState = WiFi.scanComplete();
    if (scanState == WIFI_SCAN_RUNNING) {
      if (millis() - wifi_sta_scan_started_ms_ > kStaScanTimeoutMs) {
        WiFi.scanDelete();
        wifi_sta_scanning_ = false;
        failStaConnectAttempt("scan_timeout", WL_NO_SSID_AVAIL);
      }
      return;
    }
    wifi_sta_scanning_ = false;
    if (scanState < 0) {
      WiFi.scanDelete();
      failStaConnectAttempt("scan_failed", WL_NO_SSID_AVAIL);
      return;
    }
    finishStaScan(scanState);
    return;
  }

  if (wifi_sta_connecting_) {
    if (st == WL_CONNECTED) {
      sta_connected_ = true;
      wifi_sta_connecting_ = false;
      sta_connect_consecutive_failures_ = 0;
      sta_stack_reset_count_ = 0;
      resetStaReconnectFibonacci();
      sta_connected_since_ms_ = millis();
      sta_reconnect_heap_block_log_ms_ = 0;
      const int rssi = WiFi.RSSI();
      sta_last_sdk_status_ = wifi_station_get_connect_status();
      sta_last_connect_time_ms_ = millis() - wifi_sta_connect_attempt_started_ms_;
      lrslog::event("sta_connected", rssi, 0, 0);
      LRS_LOGI(WIFI, "event=sta_connected ssid=%s ip=%s rssi=%d sdk_status=%d connect_ms=%lu channel=%ld",
               cfg.wifi_sta_ssid.c_str(), WiFi.localIP().toString().c_str(),
               rssi, sta_last_sdk_status_, static_cast<unsigned long>(sta_last_connect_time_ms_),
               static_cast<long>(sta_target_channel_));
      startNtpClient();
      maybeDisableAp();
      return;
    }

    if (st == WL_CONNECT_FAILED ||
        millis() - wifi_sta_started_ms_ > kStaConnectTimeoutMs) {
      failStaConnectAttempt((millis() - wifi_sta_started_ms_ > kStaConnectTimeoutMs) ? "connect_timeout" : runtime_utils::wifiStatusText(st), st);
    }
    return;
  }

  if (st == WL_CONNECTED) {
    if (!sta_connected_) {
      sta_connected_ = true;
      sta_connect_consecutive_failures_ = 0;
      sta_stack_reset_count_ = 0;
      resetStaReconnectFibonacci();
      sta_connected_since_ms_ = millis();
      sta_reconnect_heap_block_log_ms_ = 0;
      const int rssi = WiFi.RSSI();
      sta_last_sdk_status_ = wifi_station_get_connect_status();
      lrslog::event("sta_connected", rssi, 0, 0);
      LRS_LOGI(WIFI, "event=sta_connected ssid=%s ip=%s rssi=%d sdk_status=%d",
               cfg.wifi_sta_ssid.c_str(), WiFi.localIP().toString().c_str(),
               rssi, sta_last_sdk_status_);
      startNtpClient();
    }
    maybeDisableAp();
    return;
  }

  if (sta_connected_) {
    sta_connected_ = false;
    lrslog::event("sta_disconnected", 0, 0, 0);
    LRS_LOGW(WIFI, "event=sta_disconnected");
    if (shouldEnableSoftAp()) {
      ensureApEnabled();
    }
    wifi_sta_retry_ms_ = millis();
  }

  const uint32_t reconnectDelayMs = sta_reconnect_fib_curr_s_ * 1000UL;
  if (cfg.wifi_sta_ssid.length() >= 1 &&
      millis() - wifi_sta_retry_ms_ >= reconnectDelayMs) {
    const uint32_t freeHeap = lrslog::heapFree();
    const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
    const bool criticalLowHeapForReconnect =
        (freeHeap < kStaReconnectCriticalMinFreeHeapBytes) ||
        (maxBlock < kStaReconnectCriticalMinMaxBlockBytes);
    if (criticalLowHeapForReconnect) {
      const uint32_t nowMs = millis();
      if (sta_reconnect_heap_block_log_ms_ == 0U ||
          static_cast<int32_t>(nowMs - sta_reconnect_heap_block_log_ms_) >=
              static_cast<int32_t>(kStaReconnectHeapLogIntervalMs)) {
        sta_reconnect_heap_block_log_ms_ = nowMs;
        LRS_LOGW(WIFI,
                 "event=sta_reconnect_deferred reason=critical_low_heap "
                 "heap_free=%lu max_free_block=%lu min_free=%lu "
                 "min_max_block=%lu reconnect_delay_ms=%lu",
                 static_cast<unsigned long>(freeHeap),
                 static_cast<unsigned long>(maxBlock),
                 static_cast<unsigned long>(kStaReconnectCriticalMinFreeHeapBytes),
                 static_cast<unsigned long>(kStaReconnectCriticalMinMaxBlockBytes),
                 static_cast<unsigned long>(reconnectDelayMs));
      }
      if (shouldEnableSoftAp()) {
        ensureApEnabled();
      }
    } else {
      beginStaConnect();
      sta_reconnect_heap_block_log_ms_ = 0;
    }
  } else {
    if (shouldEnableSoftAp()) {
      ensureApEnabled();
    }
  }
}

void App::advanceStaReconnectFibonacci() {
  uint32_t next = sta_reconnect_fib_prev_s_ + sta_reconnect_fib_curr_s_;
  if (next > kStaReconnectFibMaxDelayS)
    next = kStaReconnectFibMaxDelayS;
  sta_reconnect_fib_prev_s_ = sta_reconnect_fib_curr_s_;
  sta_reconnect_fib_curr_s_ = next;
}

void App::resetStaReconnectFibonacci() {
  sta_reconnect_fib_prev_s_ = 0;
  sta_reconnect_fib_curr_s_ = 1;
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
    lrslog::event("ota_auth_changed_reboot", 0, 0, 0);
    LRS_LOGI(SYS, "event=config_apply restart_network=0 restart_ota_auth=1 "
                  "action=reboot");
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
  lrslog::event("config_reloaded", 0, 0, 0);
}

void App::startNtpClient() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  ntp_started_ = true;
  ntp_last_check_ms_ = 0;
  lrslog::event("ntp_start", 0, 0, 0);
  LRS_LOGI(
      NTP,
      "event=ntp_start servers=pool.ntp.org,time.nist.gov,time.google.com");
}

void App::tickTimeSync() {
  if (!sta_connected_) {
    return;
  }
  if (!ntp_started_) {
    startNtpClient();
  }

  const uint32_t nowMs = millis();
  const uint32_t pollInterval =
      ntp_time_valid_ ? kNtpPollFixedMs : kNtpPollNoFixMs;
  if ((nowMs - ntp_last_check_ms_) < pollInterval) {
    return;
  }
  ntp_last_check_ms_ = nowMs;

  const time_t nowUnix = time(nullptr);
  if (nowUnix < static_cast<time_t>(kMinValidUnixTimeS)) {
    return;
  }

  const bool hadValidTime = ntp_time_valid_;
  const bool refreshDue =
      ntp_time_valid_ && ((nowMs - ntp_last_sync_ms_) >= kNtpForceRefreshMs);
  const uint32_t unixTimeS = static_cast<uint32_t>(nowUnix);
  bool shouldPushToStateMachine = !sm_.sharedUnixTimeValid();
  if (!shouldPushToStateMachine) {
    const uint32_t shared = sm_.sharedUnixTime();
    const uint32_t delta =
        (shared > unixTimeS) ? (shared - unixTimeS) : (unixTimeS - shared);
    shouldPushToStateMachine = delta > 2U;
  }
  ntp_time_valid_ = true;
  ntp_last_epoch_s_ = unixTimeS;
  ntp_last_sync_ms_ = nowMs;
  if (shouldPushToStateMachine) {
    sm_.setAuthoritativeUnixTime(unixTimeS);
  }

  if (!hadValidTime || refreshDue) {
    LRS_LOGI(NTP, "event=ntp_sync_ok unix=%lu push_state=%u refresh_due=%u",
             static_cast<unsigned long>(unixTimeS),
             shouldPushToStateMachine ? 1U : 0U, refreshDue ? 1U : 0U);
  }

  if (refreshDue) {
    startNtpClient();
  }
}

void App::ensureApEnabled() {
  if (ap_enabled_)
    return;
  WiFi.mode(WIFI_AP_STA);
  applyWifiRuntimeSettings();
  const String apSsid = config_.apSsid();
  const String apPass = config_.apPassword();
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  ap_enabled_ = true;
  LRS_LOGI(WIFI, "event=ap_active ssid=%s ip=%s", apSsid.c_str(),
           WiFi.softAPIP().toString().c_str());
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
  if (dns_running_)
    return;
  dns_.start(53, "*", WiFi.softAPIP());
  dns_running_ = true;
}

void App::beginStaConnect() {
  if (wifi_stack_disabled_)
    return;
  auto &cfg = config_.settings();
  if (cfg.wifi_sta_ssid.length() == 0)
    return;
  if (!cfg.wifi_admin_enabled)
    return;
  if (cached_sta_hostname_.length() == 0) {
    refreshCachedStaHostname();
  }
  startStaScan();
}

void App::startStaScan() {
  auto &cfg = config_.settings();
  resetWifiStaAttempt();
  applyWifiRuntimeSettings();
  WiFi.hostname(cached_sta_hostname_);
  WiFi.scanDelete();
  const int scanStart = WiFi.scanNetworks(true, true);
  if (scanStart == WIFI_SCAN_FAILED) {
    failStaConnectAttempt("scan_start_failed", WL_NO_SSID_AVAIL);
    return;
  }
  wifi_sta_scanning_ = true;
  wifi_sta_scan_started_ms_ = millis();
  wifi_sta_started_ms_ = millis();
  wifi_sta_retry_ms_ = millis();
  lrslog::event("sta_connect_start", 0, 0, 0);
  LRS_LOGI(WIFI,
           "event=sta_scan_start ssid=%s host=%s phy=%s tx_power_dbm=%.2f sleep=%u channel_override=%u static_ip=%u",
           cfg.wifi_sta_ssid.c_str(), cached_sta_hostname_.c_str(),
           configuredWifiPhyModeText(), static_cast<double>(cfg.wifi_tx_power_dbm),
           cfg.wifi_sleep_enabled ? 1U : 0U,
           static_cast<unsigned>(cfg.wifi_channel_override),
           cfg.wifi_static_ip_enabled ? 1U : 0U);
}

void App::finishStaScan(int scanCount) {
  auto &cfg = config_.settings();
  int bestIndex = -1;
  int bestRssi = -1000;
  for (int i = 0; i < scanCount; ++i) {
    if (!WiFi.SSID(i).equals(cfg.wifi_sta_ssid)) continue;
    const int32_t channel = WiFi.channel(i);
    if (cfg.wifi_channel_override != 0 && channel != cfg.wifi_channel_override) continue;
    const int rssi = WiFi.RSSI(i);
    if (bestIndex < 0 || rssi > bestRssi) {
      bestIndex = i;
      bestRssi = rssi;
    }
  }

  if (bestIndex < 0) {
    WiFi.scanDelete();
    failStaConnectAttempt("ssid_not_found", WL_NO_SSID_AVAIL);
    return;
  }

  sta_target_channel_ = WiFi.channel(bestIndex);
  const uint8_t *bssid = WiFi.BSSID(bestIndex);
  if (bssid != nullptr) {
    memcpy(sta_target_bssid_, bssid, sizeof(sta_target_bssid_));
  } else {
    memset(sta_target_bssid_, 0, sizeof(sta_target_bssid_));
  }
  WiFi.scanDelete();
  applyWifiRuntimeSettings();
  WiFi.hostname(cached_sta_hostname_);
  wifi_sta_connecting_ = true;
  wifi_sta_connect_attempt_started_ms_ = millis();
  wifi_sta_started_ms_ = wifi_sta_connect_attempt_started_ms_;
  wifi_sta_retry_ms_ = wifi_sta_connect_attempt_started_ms_;
  WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str(),
             sta_target_channel_, sta_target_bssid_);
  char bssidText[18];
  snprintf(bssidText, sizeof(bssidText), "%02X:%02X:%02X:%02X:%02X:%02X",
           sta_target_bssid_[0], sta_target_bssid_[1], sta_target_bssid_[2],
           sta_target_bssid_[3], sta_target_bssid_[4], sta_target_bssid_[5]);
  LRS_LOGI(WIFI,
           "event=sta_connect_start ssid=%s host=%s channel=%ld bssid=%s rssi=%d",
           cfg.wifi_sta_ssid.c_str(), cached_sta_hostname_.c_str(),
           static_cast<long>(sta_target_channel_), bssidText, bestRssi);
}

void App::failStaConnectAttempt(const char *reason, wl_status_t status) {
  auto &cfg = config_.settings();
  sta_connected_ = false;
  wifi_sta_connecting_ = false;
  wifi_sta_scanning_ = false;
  WiFi.disconnect();
  wifi_sta_retry_ms_ = millis();
  sta_last_sdk_status_ = wifi_station_get_connect_status();
  if (sta_connect_consecutive_failures_ != 0xFFU) {
    ++sta_connect_consecutive_failures_;
  }
  lrslog::event("sta_connect_failed_fallback_ap", 0, 0, 0);
  LRS_LOGW(WIFI,
           "event=sta_connect_failed ssid=%s reason=%s status=%d sdk_status=%ld ap_fallback=%u consecutive_failures=%u",
           cfg.wifi_sta_ssid.c_str(), reason ? reason : runtime_utils::wifiStatusText(status),
           static_cast<int>(status), static_cast<long>(sta_last_sdk_status_),
           shouldEnableSoftAp() ? 1U : 0U,
           static_cast<unsigned>(sta_connect_consecutive_failures_));
  if (shouldEnableSoftAp()) {
    ensureApEnabled();
  }
  if (sta_connect_consecutive_failures_ >= kStaFailureResetThreshold) {
    if (sta_stack_reset_count_ < kStaStackResetLimit) {
      ++sta_stack_reset_count_;
      LRS_LOGW(WIFI,
               "event=sta_stack_reset reason=consecutive_failures failures=%u reset_count=%u",
               static_cast<unsigned>(sta_connect_consecutive_failures_),
               static_cast<unsigned>(sta_stack_reset_count_));
      WiFi.disconnect(true);
      delay(50);
      WiFi.mode(WIFI_OFF);
      delay(100);
      ap_enabled_ = false;
      dns_running_ = false;
      applyWifiRuntimeSettings();
      if (shouldEnableSoftAp()) {
        ensureApEnabled();
      }
      resetWifiStaAttempt();
      sta_connect_consecutive_failures_ = 0;
    } else {
      wifi_stack_disabled_ = true;
      stopWifiForAdminDisable();
      LRS_LOGE(WIFI,
               "event=sta_stack_disabled reason=reset_limit_reached reset_count=%u",
               static_cast<unsigned>(sta_stack_reset_count_));
      lrslog::event("sta_stack_disabled", 0, sta_stack_reset_count_, 0);
    }
  }
  advanceStaReconnectFibonacci();
}

void App::resetWifiStaAttempt() {
  wifi_sta_connecting_ = false;
  wifi_sta_scanning_ = false;
  wifi_sta_started_ms_ = 0;
  wifi_sta_connect_attempt_started_ms_ = 0;
  wifi_sta_scan_started_ms_ = 0;
  memset(sta_target_bssid_, 0, sizeof(sta_target_bssid_));
  sta_target_channel_ = 0;
}

void App::applyWifiRuntimeSettings() {
  const auto &cfg = config_.settings();
  WiFi.mode((ap_enabled_ || shouldEnableSoftAp()) ? WIFI_AP_STA : WIFI_STA);
  WiFi.setPhyMode(configuredWifiPhyMode());
  WiFi.setOutputPower(cfg.wifi_tx_power_dbm);
  WiFi.setSleepMode(cfg.wifi_sleep_enabled ? WIFI_LIGHT_SLEEP : WIFI_NONE_SLEEP);
  wifi_set_sleep_type(cfg.wifi_sleep_enabled ? LIGHT_SLEEP_T : NONE_SLEEP_T);
  if (cfg.wifi_static_ip_enabled) {
    IPAddress local;
    IPAddress gateway;
    IPAddress subnet;
    if (parseIpAddress(cfg.wifi_static_ip, local) &&
        parseIpAddress(cfg.wifi_static_gateway, gateway) &&
        parseIpAddress(cfg.wifi_static_subnet, subnet)) {
      WiFi.config(local, gateway, subnet);
    } else {
      LRS_LOGW(WIFI, "event=wifi_static_ip_invalid local=%s gateway=%s subnet=%s",
               cfg.wifi_static_ip.c_str(), cfg.wifi_static_gateway.c_str(),
               cfg.wifi_static_subnet.c_str());
    }
  }
}

void App::stopWifiForAdminDisable() {
  wifi_stack_disabled_ = true;
  WiFi.disconnect(true);
  delay(50);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  ap_enabled_ = false;
  if (dns_running_) {
    dns_.stop();
    dns_running_ = false;
  }
  resetWifiStaAttempt();
  sta_connected_ = false;
  LRS_LOGW(WIFI, "event=wifi_admin_disabled");
}

bool App::shouldEnableSoftAp() const {
  const auto &cfg = config_.settings();
  if (!cfg.wifi_admin_enabled) return false;
  if (cfg.wifi_sta_ssid.length() == 0) return true;
  return cfg.wifi_ap_fallback_policy != "secure_sta_only";
}

bool App::parseIpAddress(const String &raw, IPAddress &out) const {
  String trimmed = raw;
  trimmed.trim();
  return trimmed.length() > 0 && out.fromString(trimmed);
}

WiFiPhyMode_t App::configuredWifiPhyMode() const {
  String mode = config_.settings().wifi_phy_mode;
  mode.trim();
  mode.toLowerCase();
  if (mode == "11g") return WIFI_PHY_MODE_11G;
  if (mode == "11n") return WIFI_PHY_MODE_11N;
  return WIFI_PHY_MODE_11B;
}

const char *App::configuredWifiPhyModeText() const {
  switch (configuredWifiPhyMode()) {
  case WIFI_PHY_MODE_11G:
    return "11g";
  case WIFI_PHY_MODE_11N:
    return "11n";
  default:
    return "11b";
  }
}

void App::startOta() {
  ota_enabled_ = false;
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  if (freeHeap < kOtaStartupMinFreeHeapBytes ||
      maxBlock < kOtaStartupMinMaxBlockBytes) {
    lrslog::event("ota_disabled_heap", 0, 0, 0);
    LRS_LOGW(SYS,
             "event=ota_disabled reason=low_startup_heap heap_free=%lu "
             "max_free_block=%lu min_free=%lu min_max_block=%lu",
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
  // Disable ArduinoOTA's internal service advertisement to keep heap usage
  // predictable on ESP8266.
  ArduinoOTA.begin(false);
  ota_enabled_ = true;
  lrslog::event("ota_ready", 0, 0, 0);
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
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      out += c;
    } else if (c == ' ' || c == '_' || c == '.') {
      out += '-';
    }
  }
  while (out.startsWith("-"))
    out.remove(0, 1);
  while (out.endsWith("-"))
    out.remove(out.length() - 1);
  if (out.length() == 0)
    return "lrs";
  if (out.length() > 31)
    out.remove(31);
  return out;
}
