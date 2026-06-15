#include "runtime_utils.h"

#include <cstring>

namespace runtime_utils {

#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx) {
  return parseRoleTxFromModeRole(mode.c_str(), role.c_str(), roleTx);
}
#endif

bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx) {
  if (mode == nullptr || role == nullptr) return false;
  if (strcmp(mode, "paired") == 0) {
    if (strcmp(role, "transmitter") == 0) {
      roleTx = true;
      return true;
    }
    if (strcmp(role, "receiver") == 0) {
      roleTx = false;
      return true;
    }
    return false;
  }
  if (strcmp(mode, "standalone") == 0 && strcmp(role, "none") == 0) {
    roleTx = true;
    return true;
  }
  return false;
}

const char *wifiStatusText(wl_status_t st) {
  switch (st) {
  case WL_IDLE_STATUS:
    return "idle";
  case WL_NO_SSID_AVAIL:
    return "ssid_not_found";
  case WL_SCAN_COMPLETED:
    return "scan_completed";
  case WL_CONNECTED:
    return "connected";
  case WL_CONNECT_FAILED:
    return "connect_failed";
  case WL_CONNECTION_LOST:
    return "connection_lost";
  case WL_DISCONNECTED:
    return "disconnected";
  case WL_WRONG_PASSWORD:
    return "wrong_password";
  case WL_NO_SHIELD:
    return "no_shield";
  default:
    return "unknown";
  }
}

uint8_t resolveGatewayTargets(
    bool isPairedMode,
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t pairedTargetCount,
    const uint8_t *pairedTargetAddresses,
    uint8_t remoteAddress,
    uint8_t *outTargets,
    uint8_t maxTargets
) {
  if (outTargets == nullptr || maxTargets == 0) return 0;

  uint8_t targetCount = 0;

  auto addTarget = [&](uint8_t addr) {
    if (addr >= 1 && addr <= 254 && addr != localAddress && targetCount < maxTargets) {
      for (uint8_t i = 0; i < targetCount; ++i) {
        if (outTargets[i] == addr) return;
      }
      outTargets[targetCount++] = addr;
    }
  };

  if (isPairedMode) {
    if (knownPeerCount > 0 && knownPeerAddresses != nullptr) {
      for (size_t i = 0; i < knownPeerCount && i < maxTargets; ++i) {
        addTarget(knownPeerAddresses[i]);
      }
    }
  } else {
    if (knownPeerCount > 0 && knownPeerAddresses != nullptr) {
      for (size_t i = 0; i < knownPeerCount && i < maxTargets; ++i) {
        addTarget(knownPeerAddresses[i]);
      }
    }
    if (targetCount == 0 && pairedTargetCount > 0 && pairedTargetAddresses != nullptr) {
      for (size_t i = 0; i < pairedTargetCount && i < maxTargets; ++i) {
        addTarget(pairedTargetAddresses[i]);
      }
    }
    if (targetCount == 0 && remoteAddress != 0) {
      addTarget(remoteAddress);
    }
  }

  return targetCount;
}

} // namespace runtime_utils
