#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <functional>

#include "feature_flags.h"

class ConfigStore;
class NodeStateMachine;
class SensorManager;

class WebConsole {
 public:
  bool begin(ConfigStore *config,
             NodeStateMachine *sm,
             SensorManager *sensors,
             std::function<void(bool, bool)> onApply,
             std::function<void()> onAutomationsSaved = {});
  void tick();

 private:
  ESP8266WebServer server_{80};
  ConfigStore *config_ = nullptr;
  NodeStateMachine *sm_ = nullptr;
  SensorManager *sensors_ = nullptr;
  std::function<void(bool, bool)> on_apply_;
  std::function<void()> on_automations_saved_;

  uint16_t failed_auth_ = 0;
  uint32_t locked_until_ms_ = 0;
  String session_token_;
  uint32_t session_expires_ms_ = 0;
  bool ota_upload_ok_ = false;
  String ota_upload_error_;

  bool requireAuth(bool api = true);
  bool hasSession() const;
  void clearSession();
  String cookieValue(const String &name) const;
  String randomToken() const;
  void startSession();
  uint32_t sessionRemainingS() const;
  void routes();
  void beginRequestLog(const char *path, bool api, bool poll = false, bool heapDiag = false);
  void finishRequestLog();
  void markResponseStatus(int status);
  void sendTracked(int code, const char *contentType, const char *body);
  void sendTracked(int code, const char *contentType, const String &body);
  bool rejectApiIfLowHeap(const char *path, uint32_t minFreeBytes, uint32_t minMaxBlockBytes = 0);
  bool isSoftApActive() const;
  void handleCaptiveProbe();

  void handleIndex();
  void handleLoginPage();
  void handleFleetSetupPage();
  void handleLoginApi();
  void handleFleetSetupApi();
  void handleSetupCommissioningApi();
  void handleLogoutApi();
  void handleSessionApi();
  void handleStatus();
  void handleStatusLive();
  void handleStatusLiveEvents();
  void handleStatusStatic();
  void handleStatusLite();
  void handleFactory();
  void handleGetSettings();
  void handlePostSettings();
  void handleExportSettings();
  void handleImportSettings();
  void handleFleet();
  void handleGetAutomationRules();
  void handlePostAutomationRules();
  bool handleFleetDeviceActionRoute(const String &uri);
  void handleDiagnostics();
  void handleTestSta();
  void handleWifiScan();
  void handleProvisionFleetWifi();
  void handleProvisioningStart();
  void handleProvisioningStatus();
  void handleProvisioningProvisionAll();
  void handleProvisioningCancel();
  void handleTestMqtt();
  void handleUdpLogging();
  void handleOtaUpload();
  void handleOtaUploadChunk();
  void handleLogsCsv();
  void handleLogsText();
  void handleFactoryReset();
  void handleReboot();
  bool needsFleetSetupPrompt() const;

  struct JsonResponseCache {
    String body;
    uint32_t built_ms = 0;
  };

  bool apiHeapHealthy(uint32_t minFreeBytes, uint32_t minMaxBlockBytes) const;
  bool tryServeCachedJson(const char *path,
                          uint32_t minFreeBytes,
                          uint32_t minMaxBlockBytes,
                          uint32_t ttlMs,
                          JsonResponseCache &cache);
  void initStatusCaches();
  void setUiNoStoreHeaders();
  void tickStatusLiveSse();
  void closeStatusLiveSse();
  uint32_t computeStatusLiveSseIntervalMs() const;
  bool buildStatusLiveCache();
  bool buildStatusStaticCache();
  bool buildStatusLiteCache();

  struct RequestLogState {
    bool active = false;
    bool api = false;
    bool poll = false;
    bool heap_diag = false;
    uint32_t started_ms = 0;
    int status = 0;
    const char *path = nullptr;
    uint8_t client_ip[4] = {0, 0, 0, 0};
  };
  RequestLogState request_log_{};
  uint32_t last_low_heap_warn_ms_ = 0;
  uint8_t last_logged_prov_state_ = 0xFF;
  JsonResponseCache status_live_cache_{};
  JsonResponseCache status_static_cache_{};
  JsonResponseCache status_lite_cache_{};
  WiFiClient status_live_sse_client_{};
  bool status_live_sse_active_ = false;
  uint32_t status_live_sse_last_push_ms_ = 0;
  uint32_t status_live_sse_last_keepalive_ms_ = 0;
  uint32_t status_live_sse_last_sent_cache_ms_ = 0;
  uint32_t status_live_sse_last_interval_ms_ = 0;
  uint32_t last_web_pressure_ms_ = 0;
};
