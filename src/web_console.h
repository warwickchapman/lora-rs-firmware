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
  bool isSoftApActive() const;
  void handleCaptiveProbe();

  void handleIndex();
  void handleLoginPage();
  void handleLoginApi();
  void handleLogoutApi();
  void handleSessionApi();
  void handleStatus();
  void handleFactory();
  void handleGetSettings();
  void handlePostSettings();
  void handleExportSettings();
  void handleImportSettings();
  void handleRemotes();
  void handleRemoteAction();
  void handleDiagnostics();
  void handleTestSta();
  void handleTestMqtt();
  void handleOtaUpload();
  void handleOtaUploadChunk();
  void handleLogsCsv();
  void handleLogsText();
  void handleReboot();
};
