#include "admin_config_utils.h"

#include <cstring>

#include "runtime_utils.h"

namespace admin_config_utils {

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

}  // namespace admin_config_utils
