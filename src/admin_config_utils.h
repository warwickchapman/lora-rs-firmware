#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

#include "state_machine.h"
#include "lora_config.h"

namespace admin_config_utils {

extern const uint32_t kMinHeartbeatMs;
extern const uint32_t kMaxHeartbeatMs;
extern const uint32_t kMinAckTimeoutMs;
extern const uint32_t kMaxAckTimeoutMs;
extern const uint32_t kMinTxPollDefaultIntervalMs;
extern const uint32_t kMaxTxPollDefaultIntervalMs;
extern const size_t kMinDeploymentKeyLen;
extern const long kMinFrequencyHz;
extern const long kMaxFrequencyHz;

bool isOwnLrsSoftApLike(const String &ssid);
const char *wifiStatusText(wl_status_t st);
const char *provisioningSessionStateText(ProvisioningSessionState s);
const char *provisioningDeviceStateText(ProvisioningDeviceState s);
uint8_t parseAddressField(const JsonVariantConst &value, uint8_t fallback);
uint8_t parseAddressText(const String &text, uint8_t fallback);
bool parseBoolField(const JsonVariantConst &value, bool fallback);
bool softApActiveNow();
const char *linkStateText(LinkState st);

}  // namespace admin_config_utils
