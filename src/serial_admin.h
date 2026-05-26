#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#include "config_store.h"
#include "state_machine.h"

class SerialAdmin {
public:
  bool begin(ConfigStore *config, NodeStateMachine *sm,
             std::function<void(bool, bool)> onApply);
  void tick();

private:
  ConfigStore *config_ = nullptr;
  NodeStateMachine *sm_ = nullptr;
  std::function<void(bool, bool)> on_apply_;
  String input_;

  void handleLine(const String &line);
  void handleCommand(JsonDocument &doc);
  bool requireAdmin(const JsonDocument &doc);
  void sendError(const char *cmd, const char *error, const char *id = nullptr);
  void sendOk(JsonDocument &doc);
  void buildProvisioningStatus(JsonDocument &doc);
  void handleStatus(JsonDocument &doc);
  void handleGetConfig(JsonDocument &doc);
  void handleSetConfig(JsonDocument &doc);
  void handleFactoryReset(JsonDocument &doc);
  void handleConfigureGateway(JsonDocument &doc);
  void handleWifiScan(JsonDocument &doc);
  void handleConfigureWifi(JsonDocument &doc);
  void handleProvisionFleetWifi(JsonDocument &doc);
  void handleIdentify(JsonDocument &doc);
  void handleStartLoraInventory(JsonDocument &doc);
  void handleLoraInventoryStatus(JsonDocument &doc);
  void handleCancelLoraInventory(JsonDocument &doc);
  void handleUdpLogControl(JsonDocument &doc);
  void handleRemoteUdpLogControl(JsonDocument &doc);
  void handleRemoteOtaPull(JsonDocument &doc);
  void handleRemoteReboot(JsonDocument &doc);
  void handleRemoteSensorConfig(JsonDocument &doc);
  void handleRemoteFactoryReset(JsonDocument &doc);
  void handleOtaPull(JsonDocument &doc);
};
