#include "web_console_internal.h"

#include <cstring>

#include "runtime_utils.h"

namespace webconsole_internal {

const uint32_t kMinHeartbeatMs = 60000;
const uint32_t kMaxHeartbeatMs = 3600000;
const uint32_t kMinAckTimeoutMs = 5 * 1000;
const uint32_t kMaxAckTimeoutMs = 600 * 1000;
const uint32_t kMinMqttRemoteRetryTimeoutMs = 5 * 1000;
const uint32_t kMaxMqttRemoteRetryTimeoutMs = 3600 * 1000;
const uint32_t kMinTxPollDefaultIntervalMs = 60 * 1000;
const uint32_t kMaxTxPollDefaultIntervalMs = 3600 * 1000;
const uint32_t kMinRxPushIntervalMs = 60 * 1000;
const uint32_t kMaxRxPushIntervalMs = 3600 * 1000;
const char *const kDefaultDeploymentKey = "lora-default-passphrase";
const size_t kMinDeploymentKeyLen = 16;
const char *const kHardwareVersion = "v1.2";
const char *const kHardwareBatch = "251101";
const uint32_t kLowHeapWarnThresholdBytes = 14000;
const uint32_t kLowHeapWarnMinIntervalMs = 5000;
const uint32_t kApiLightLowHeapRejectFreeBytes = 3000;
const uint32_t kApiLightLowHeapRejectMaxBlockBytes = 1200;
const uint32_t kApiLowHeapRejectFreeBytes = 6500;
const uint32_t kApiLowHeapRejectMaxBlockBytes = 2500;
const uint32_t kApiStatusLiveLowHeapRejectFreeBytes = 4500;
const uint32_t kApiStatusLiveLowHeapRejectMaxBlockBytes = 1800;
const uint32_t kApiStatusStaticLowHeapRejectFreeBytes = 4500;
const uint32_t kApiStatusStaticLowHeapRejectMaxBlockBytes = 1800;
const uint32_t kApiFleetLowHeapRejectFreeBytes = 4500;
const uint32_t kApiFleetLowHeapRejectMaxBlockBytes = 1800;
const uint32_t kApiProvStatusCompactFreeBytes = 3500;
const uint32_t kApiProvStatusCompactMaxBlockBytes = 1400;
// Full index HTML is large; use conservative gates so we prefer the low-heap page
// over risking partial/truncated HTML delivery.
const uint32_t kIndexLowHeapRejectFreeBytes = 7000;
const uint32_t kIndexLowHeapRejectMaxBlockBytes = 3200;
const uint32_t kStatusLiveCacheTtlMs = 1500;
const uint32_t kStatusStaticCacheTtlMs = 15000;
// Non-status pages poll status-lite for header badges; a longer TTL reduces JSON rebuild churn.
const uint32_t kStatusLiteCacheTtlMs = 5000;
const size_t kStatusCacheReserveBytes = 1600;
const size_t kStatusLiveCacheReserveBytes = 1024;
const size_t kStatusStaticCacheReserveBytes = 1024;
const size_t kStatusLiteCacheReserveBytes = 384;
const uint32_t kStatusLiveSseKeepAliveMs = 15000;
const uint32_t kStatusLiveSseConnectMinFreeBytes = 5000;
const uint32_t kStatusLiveSseConnectMinMaxBlockBytes = 2000;
const uint32_t kStatusLiveSsePushHealthyMs = 2000;
const uint32_t kStatusLiveSsePushWarnMs = 4000;
const uint32_t kStatusLiveSsePushPressureMs = 8000;
const uint32_t kStatusLiveSsePushSevereMs = 12000;
const uint32_t kStatusLiveSsePressureWindowMs = 12000;
const uint32_t kWebRequestPressureDurMs = 80;
const int kStaTestMaxAttempts = 40;
const size_t kFleetDocBaseBytes = 320;
const size_t kFleetDocPerPeerBytes = 192;
const size_t kFleetDocMinBytes = 768;
const size_t kFleetDocMaxBytes = 3072;

#ifdef REGION_US
const long kMinFrequencyHz = 902000000L;
const long kMaxFrequencyHz = 928000000L;
const long kDefaultFrequencyHz = 915000000L;
#else
const long kMinFrequencyHz = 433000000L;
const long kMaxFrequencyHz = 434790000L;
const long kDefaultFrequencyHz = 433000000L;
#endif

bool isOwnLrsSoftApLike(const String &ssid) {
  String s = ssid;
  s.toLowerCase();
  if (!s.startsWith("lrs-")) return false;
  if (s.endsWith("-tx") || s.endsWith("-rx")) return true;
  if (s.length() != 12) return false;
  for (size_t i = 4; i < 12; ++i) {
    const char c = s.charAt(i);
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!isHex) return false;
  }
  return true;
}

const char *wifiStatusText(wl_status_t st) {
  return runtime_utils::wifiStatusText(st);
}

const char *httpMethodText(HTTPMethod method) {
  switch (method) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    default: return "OTHER";
  }
}

const char *remoteAckStateText(PeerAckState s) {
  switch (s) {
    case PeerAckState::Pending: return "pending";
    case PeerAckState::Ok: return "ok";
    case PeerAckState::Timeout: return "timeout";
    case PeerAckState::Unknown:
    default: return "unknown";
  }
}

const char *provisioningSessionStateText(ProvisioningSessionState s) {
  switch (s) {
    case ProvisioningSessionState::Discovering: return "discovering";
    case ProvisioningSessionState::Ready: return "ready";
    case ProvisioningSessionState::Provisioning: return "provisioning";
    case ProvisioningSessionState::Complete: return "complete";
    case ProvisioningSessionState::Error: return "error";
    case ProvisioningSessionState::Idle:
    default: return "idle";
  }
}

const char *provisioningDeviceStateText(ProvisioningDeviceState s) {
  switch (s) {
    case ProvisioningDeviceState::Assigned: return "assigned";
    case ProvisioningDeviceState::Keying: return "keying";
    case ProvisioningDeviceState::AwaitVerify: return "await_verify";
    case ProvisioningDeviceState::Verified: return "verified";
    case ProvisioningDeviceState::AppliedUnconfirmed: return "applied_unconfirmed";
    case ProvisioningDeviceState::Failed: return "failed";
    case ProvisioningDeviceState::Skipped: return "skipped";
    case ProvisioningDeviceState::Discovered:
    default: return "discovered";
  }
}

uint8_t parseAddressField(const JsonVariantConst &value, uint8_t fallback) {
  if (value.isNull()) return fallback;
  if (value.is<uint8_t>() || value.is<int>()) {
    const int n = value.as<int>();
    if (n >= 1 && n <= 254) return static_cast<uint8_t>(n);
    return fallback;
  }
  String text = String(static_cast<const char *>(value.as<const char *>()));
  text.trim();
  if (text.length() == 0) return fallback;
  long n = -1;
  if (text.startsWith("0x") || text.startsWith("0X")) n = strtol(text.c_str(), nullptr, 16);
  else n = strtol(text.c_str(), nullptr, 10);
  if (n < 1 || n > 254) return fallback;
  return static_cast<uint8_t>(n);
}

uint8_t parseAddressText(const String &text, uint8_t fallback) {
  String t = text;
  t.trim();
  if (t.length() == 0) return fallback;
  long n = -1;
  if (t.startsWith("0x") || t.startsWith("0X")) n = strtol(t.c_str(), nullptr, 16);
  else n = strtol(t.c_str(), nullptr, 10);
  if (n < 1 || n > 254) return fallback;
  return static_cast<uint8_t>(n);
}

bool parseBoolField(const JsonVariantConst &value, bool fallback) {
  if (value.isNull()) return fallback;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<int>()) return value.as<int>() != 0;
  const char *raw = value.as<const char *>();
  if (!raw) return fallback;
  String text(raw);
  text.trim();
  text.toLowerCase();
  if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
  if (text == "false" || text == "0" || text == "no" || text == "off") return false;
  return fallback;
}

bool parseIpField(const JsonVariantConst &value, IPAddress &out) {
  if (value.isNull()) return false;
  const char *raw = value.as<const char *>();
  if (!raw) return false;
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') raw++;
  if (*raw == '\0') return false;
  const char *end = raw + strlen(raw);
  while (end > raw && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
  char buf[32];
  const size_t n = static_cast<size_t>(end - raw);
  if (n == 0 || n >= sizeof(buf)) return false;
  memcpy(buf, raw, n);
  buf[n] = '\0';
  IPAddress ip;
  if (!ip.fromString(buf)) return false;
  out = ip;
  return true;
}

bool parseUint16Field(const JsonVariantConst &value, uint16_t &out) {
  if (value.isNull()) return false;
  long parsed = -1;
  if (value.is<uint16_t>() || value.is<int>()) {
    parsed = static_cast<long>(value.as<int>());
  } else {
    const char *raw = value.as<const char *>();
    if (!raw) return false;
    char *end = nullptr;
    parsed = strtol(raw, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (!end || *end != '\0') return false;
  }
  if (parsed < 1 || parsed > 65535) return false;
  out = static_cast<uint16_t>(parsed);
  return true;
}

bool parseUint32FieldRange(const JsonVariantConst &value, uint32_t minValue, uint32_t maxValue, uint32_t &out) {
  if (value.isNull()) return false;
  long parsed = -1;
  if (value.is<uint32_t>() || value.is<int>()) {
    parsed = static_cast<long>(value.as<int>());
  } else {
    const char *raw = value.as<const char *>();
    if (!raw) return false;
    char *end = nullptr;
    parsed = strtol(raw, &end, 10);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (!end || *end != '\0') return false;
  }
  if (parsed < 0) return false;
  const uint32_t v = static_cast<uint32_t>(parsed);
  if (v < minValue || v > maxValue) return false;
  out = v;
  return true;
}

bool softApActiveNow() {
  const IPAddress apIp = WiFi.softAPIP();
  return apIp[0] != 0;
}

bool isDefaultDeploymentKey(const String &v) {
  String k = v;
  k.trim();
  return k == kDefaultDeploymentKey;
}

const char *linkStateText(LinkState st) {
  switch (st) {
    case LinkState::Boot: return "boot";
    case LinkState::Idle: return "idle";
    case LinkState::WaitAck: return "wait_ack";
    case LinkState::Timeout: return "timeout";
  }
  return "unknown";
}

}  // namespace webconsole_internal
