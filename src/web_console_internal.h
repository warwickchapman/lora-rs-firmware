#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "state_machine.h"

namespace webconsole_internal {

extern const uint32_t kMinHeartbeatMs;
extern const uint32_t kMaxHeartbeatMs;
extern const uint32_t kMinAckTimeoutMs;
extern const uint32_t kMaxAckTimeoutMs;
extern const uint32_t kMinMqttRemoteRetryTimeoutMs;
extern const uint32_t kMaxMqttRemoteRetryTimeoutMs;
extern const uint32_t kMinTxPollDefaultIntervalMs;
extern const uint32_t kMaxTxPollDefaultIntervalMs;
extern const uint32_t kMinRxPushIntervalMs;
extern const uint32_t kMaxRxPushIntervalMs;
extern const char *const kDefaultDeploymentKey;
extern const size_t kMinDeploymentKeyLen;
extern const char *const kHardwareVersion;
extern const char *const kHardwareBatch;
extern const uint32_t kLowHeapWarnThresholdBytes;
extern const uint32_t kLowHeapWarnMinIntervalMs;
extern const uint32_t kApiLightLowHeapRejectFreeBytes;
extern const uint32_t kApiLightLowHeapRejectMaxBlockBytes;
extern const uint32_t kApiLowHeapRejectFreeBytes;
extern const uint32_t kApiLowHeapRejectMaxBlockBytes;
extern const uint32_t kApiStatusLiveLowHeapRejectFreeBytes;
extern const uint32_t kApiStatusLiveLowHeapRejectMaxBlockBytes;
extern const uint32_t kApiStatusStaticLowHeapRejectFreeBytes;
extern const uint32_t kApiStatusStaticLowHeapRejectMaxBlockBytes;
extern const uint32_t kApiFleetLowHeapRejectFreeBytes;
extern const uint32_t kApiFleetLowHeapRejectMaxBlockBytes;
extern const uint32_t kApiProvStatusCompactFreeBytes;
extern const uint32_t kApiProvStatusCompactMaxBlockBytes;
extern const uint32_t kIndexLowHeapRejectFreeBytes;
extern const uint32_t kIndexLowHeapRejectMaxBlockBytes;
extern const uint32_t kStatusLiveCacheTtlMs;
extern const uint32_t kStatusStaticCacheTtlMs;
extern const uint32_t kStatusLiteCacheTtlMs;
extern const size_t kStatusCacheReserveBytes;
extern const size_t kStatusLiveCacheReserveBytes;
extern const size_t kStatusStaticCacheReserveBytes;
extern const size_t kStatusLiteCacheReserveBytes;
extern const uint32_t kStatusLiveSseKeepAliveMs;
extern const uint32_t kStatusLiveSseConnectMinFreeBytes;
extern const uint32_t kStatusLiveSseConnectMinMaxBlockBytes;
extern const uint32_t kStatusLiveSsePushHealthyMs;
extern const uint32_t kStatusLiveSsePushWarnMs;
extern const uint32_t kStatusLiveSsePushPressureMs;
extern const uint32_t kStatusLiveSsePushSevereMs;
extern const uint32_t kStatusLiveSsePressureWindowMs;
extern const uint32_t kWebRequestPressureDurMs;
extern const int kStaTestMaxAttempts;
extern const size_t kFleetDocBaseBytes;
extern const size_t kFleetDocPerPeerBytes;
extern const size_t kFleetDocMinBytes;
extern const size_t kFleetDocMaxBytes;
extern const long kMinFrequencyHz;
extern const long kMaxFrequencyHz;
extern const long kDefaultFrequencyHz;

bool isOwnLrsSoftApLike(const String &ssid);
const char *wifiStatusText(wl_status_t st);
const char *httpMethodText(HTTPMethod method);
const char *remoteAckStateText(PeerAckState s);
const char *provisioningSessionStateText(ProvisioningSessionState s);
const char *provisioningDeviceStateText(ProvisioningDeviceState s);
uint8_t parseAddressField(const JsonVariantConst &value, uint8_t fallback);
uint8_t parseAddressText(const String &text, uint8_t fallback);
bool parseBoolField(const JsonVariantConst &value, bool fallback);
bool parseIpField(const JsonVariantConst &value, IPAddress &out);
bool parseUint16Field(const JsonVariantConst &value, uint16_t &out);
bool parseUint32FieldRange(const JsonVariantConst &value, uint32_t minValue, uint32_t maxValue, uint32_t &out);
bool softApActiveNow();
bool isDefaultDeploymentKey(const String &v);
const char *linkStateText(LinkState st);

}  // namespace webconsole_internal
