#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <functional>

class ConfigStore;
class NodeStateMachine;
class LogBuffer;
class SensorManager;

class WebConsole {
 public:
  bool begin(ConfigStore *config,
             NodeStateMachine *sm,
             SensorManager *sensors,
             LogBuffer *logs,
             std::function<void(bool, bool)> onApply);
  void tick();

 private:
  ESP8266WebServer server_{80};
  ConfigStore *config_ = nullptr;
  NodeStateMachine *sm_ = nullptr;
  SensorManager *sensors_ = nullptr;
  LogBuffer *logs_ = nullptr;
  std::function<void(bool, bool)> on_apply_;

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
  void handleLogoutApi();
  void handleSessionApi();
  void handleStatus();
  void handleStatusLite();
  void handleFactory();
  void handleGetSettings();
  void handlePostSettings();
  void handleExportSettings();
  void handleImportSettings();
  void handleFleet();
  bool handleFleetDeviceActionRoute(const String &uri);
  void handleDiagnostics();
  void handleTestSta();
  void handleProvisionFleetWifi();
  void handleProvisioningStart();
  void handleProvisioningStatus();
  void handleProvisioningProvisionAll();
  void handleProvisioningCancel();
  void handleTestMqtt();
  void handleOtaUpload();
  void handleOtaUploadChunk();
  void handleLogsCsv();
  void handleLogsText();
  void handleFactoryReset();
  void handleReboot();
  bool needsFleetSetupPrompt() const;

  struct RequestLogState {
    bool active = false;
    bool api = false;
    bool poll = false;
    bool heap_diag = false;
    uint32_t started_ms = 0;
    int status = 0;
    String path;
    String client_ip;
  };
  RequestLogState request_log_{};
  uint32_t last_low_heap_warn_ms_ = 0;
  uint8_t last_logged_prov_state_ = 0xFF;
};
