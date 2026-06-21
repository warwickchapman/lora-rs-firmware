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

CandidateReason evaluateCandidateReason(
    uint8_t address,
    uint32_t chipId,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    const uint32_t *knownPeerChipIds
) {
  if (address < 1 || address > 12) {
    return CandidateReason::OutOfRange;
  }
  if (knownPeerAddresses != nullptr) {
    for (size_t i = 0; i < knownPeerCount; ++i) {
      if (knownPeerAddresses[i] == address) {
        if (knownPeerChipIds != nullptr && knownPeerChipIds[i] != chipId) {
          return CandidateReason::Conflict;
        }
      }
    }
  }
  return CandidateReason::Ok;
}

uint8_t resolveAdoptionAddress(
    uint8_t currentAddress,
    uint32_t chipId,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    const uint32_t *knownPeerChipIds
) {
  // Check if currentAddress is free and in 1..12
  bool currentFree = false;
  if (currentAddress >= 1 && currentAddress <= 12) {
    bool exists = false;
    if (knownPeerAddresses != nullptr) {
      for (size_t i = 0; i < knownPeerCount; ++i) {
        if (knownPeerAddresses[i] == currentAddress) {
          if (knownPeerChipIds == nullptr || knownPeerChipIds[i] != chipId) {
            exists = true;
            break;
          }
        }
      }
    }
    if (!exists) {
      currentFree = true;
    }
  }
  if (currentFree) {
    return currentAddress;
  }
  // Find the next free address in 1..12
  for (uint8_t addr = 1; addr <= 12; ++addr) {
    bool exists = false;
    if (knownPeerAddresses != nullptr) {
      for (size_t i = 0; i < knownPeerCount; ++i) {
        if (knownPeerAddresses[i] == addr) {
          if (knownPeerChipIds == nullptr || knownPeerChipIds[i] != chipId) {
            exists = true;
            break;
          }
        }
      }
    }
    if (!exists) {
      return addr; // found a free address
    }
  }
  // No free address
  return 0;
}

bool validateGatewayConfirm(
    uint32_t targetChipId,
    uint8_t newAddress,
    uint8_t msgSrc,
    bool adoptionActive,
    uint32_t adoptionChipId,
    uint8_t adoptionAddress,
    uint8_t adoptionDstAddr
) {
  if (!adoptionActive) return false;
  if (targetChipId != adoptionChipId) return false;
  if (newAddress != adoptionAddress) return false;
  uint8_t expectedSrc = (adoptionAddress == 0) ? adoptionDstAddr : adoptionAddress;
  return (msgSrc == expectedSrc);
}

RemoteReaddressState transitionRemoteReaddress(
    RemoteReaddressState currentState,
    bool rxRequest,
    uint8_t rxNewAddress,
    bool saveSucceeded,
    bool &outSendConfirm,
    uint8_t &outConfirmAddress
) {
  outSendConfirm = false;
  if (currentState == RemoteReaddressState::Idle) {
    if (rxRequest) {
      if (rxNewAddress == 0) {
        return RemoteReaddressState::PendingReset;
      } else {
        return RemoteReaddressState::PendingSave;
      }
    }
  } else if (currentState == RemoteReaddressState::PendingSave) {
    if (saveSucceeded) {
      outSendConfirm = true;
      outConfirmAddress = rxNewAddress;
      return RemoteReaddressState::Completed;
    } else {
      return RemoteReaddressState::Failed;
    }
  } else if (currentState == RemoteReaddressState::PendingReset) {
    if (saveSucceeded) {
      outSendConfirm = true;
      outConfirmAddress = 0;
      return RemoteReaddressState::Completed;
    } else {
      return RemoteReaddressState::Failed;
    }
  }
  return currentState;
}

bool transitionAdoptionStart(
    CandidateState &cState,
    bool &adoptionActive,
    bool isReset,
    bool txSuccess
) {
  if (cState != CandidateState::Identified && cState != CandidateState::Failed) {
    return false;
  }
  cState = isReset ? CandidateState::ResetRequested : CandidateState::Readdressing;
  adoptionActive = true;
  if (!txSuccess) {
    cState = CandidateState::Identified;
    adoptionActive = false;
    return false;
  }
  return true;
}

bool tickCandidateProbe(
    DiscoveryCandidate &c,
    uint32_t now,
    uint32_t &last_global_probe_ms,
    bool &outSendProbe
) {
  if (!c.in_use || c.state != CandidateState::SeenAddressOnly) {
    return false;
  }
  if (last_global_probe_ms != 0 && static_cast<int32_t>(now - last_global_probe_ms) < static_cast<int32_t>(kCandidateProbeGlobalGapMs)) {
    return false;
  }
  if (c.last_probe_ms != 0 && static_cast<int32_t>(now - c.last_probe_ms) < static_cast<int32_t>(kCandidateProbeIntervalMs)) {
    return false;
  }

  if (c.probe_attempt_count < kCandidateProbeMaxAttempts) {
    c.probe_attempt_count++;
    c.last_probe_ms = now;
    last_global_probe_ms = now;
    outSendProbe = true;
    return true;
  } else {
    c.state = CandidateState::Failed;
    c.last_seen_ms = now;
    outSendProbe = false;
    return true;
  }
}

} // namespace runtime_utils

