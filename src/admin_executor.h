#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#include "config_store.h"
#include "state_machine.h"
#include "admin_session.h"

class AdminExecutor {
public:
  using ConfigApplyCallback = void (*)(void *context, bool restartNetwork, bool restartOtaAuth);
  using ResponseWriter = std::function<void(const String& response)>;
  using OtaStatusPublisherFn = void (*)(void *context, const char *status);
  void setOtaStatusPublisher(OtaStatusPublisherFn publisher, void *context) {
    ota_status_publisher_ = publisher;
    ota_status_publisher_ctx_ = context;
  }

  bool begin(ConfigStore *config, NodeStateMachine *sm,
             ConfigApplyCallback onApply, void *context);
  void execute(const char *jsonCommand, size_t length, ResponseWriter writer, bool isMqtt = false);
  bool addPeerToConfig(uint32_t chipId, uint8_t address);

private:
  OtaStatusPublisherFn ota_status_publisher_ = nullptr;
  void *ota_status_publisher_ctx_ = nullptr;

  ConfigStore *config_ = nullptr;
  NodeStateMachine *sm_ = nullptr;
  ConfigApplyCallback on_apply_ = nullptr;
  void *on_apply_ctx_ = nullptr;

  AdminSession mqtt_session_;

  void handleAdminChallenge(JsonDocument &doc, ResponseWriter writer);
  static void otaStatusCallback(const char *status, void *ctx);

  // Command handlers
  void handleCommand(JsonDocument &doc, ResponseWriter writer, bool isMqtt);
  bool requireAdmin(const JsonDocument &doc);
  void sendError(const char *cmd, const char *error, const char *id, ResponseWriter writer);
  void sendOk(JsonDocument &doc, ResponseWriter writer);
  void buildProvisioningStatus(JsonDocument &doc, bool isMqtt = false);
  void handleStatus(JsonDocument &doc, ResponseWriter writer);
  void handleGetConfig(JsonDocument &doc, ResponseWriter writer, bool isMqtt);
  void handleSetConfig(JsonDocument &doc, ResponseWriter writer, bool isMqtt);
  void handleFactoryReset(JsonDocument &doc, ResponseWriter writer);
  void handleConfigureGateway(JsonDocument &doc, ResponseWriter writer);
  void handleWifiScan(JsonDocument &doc, ResponseWriter writer);
  void handleConfigureWifi(JsonDocument &doc, ResponseWriter writer);
  void handleProvisionFleetWifi(JsonDocument &doc, ResponseWriter writer);
  void handleIdentify(JsonDocument &doc, ResponseWriter writer);
  void handleStartLoraInventory(JsonDocument &doc, ResponseWriter writer);
  void handleLoraInventoryStatus(JsonDocument &doc, ResponseWriter writer, bool isMqtt = false);
  void handleCancelLoraInventory(JsonDocument &doc, ResponseWriter writer);
  void handlePollDiagnostics(JsonDocument &doc, ResponseWriter writer);
  void handleRemoteOtaPull(JsonDocument &doc, ResponseWriter writer);
  void handleRemoteReboot(JsonDocument &doc, ResponseWriter writer);
  void handleRemoteSensorConfig(JsonDocument &doc, ResponseWriter writer);
  void handleRemoteFleetKeyChange(JsonDocument &doc, ResponseWriter writer);
  void handleRemoteFactoryReset(JsonDocument &doc, ResponseWriter writer);
  void handleOtaPull(JsonDocument &doc, ResponseWriter writer);
  void handleUdpLogControl(JsonDocument &doc, ResponseWriter writer, bool isMqtt);
  void handleRemoteUdpLogControl(JsonDocument &doc, ResponseWriter writer);
  void handleSetGatewayTargets(JsonDocument &doc, ResponseWriter writer);
  void handleForgetGatewayTarget(JsonDocument &doc, ResponseWriter writer);
  void handleAdoptCandidate(JsonDocument &doc, ResponseWriter writer);
};
