#include "runtime_utils.h"

#include <cstdlib>
#include <cstring>

#include "config_store.h"

namespace runtime_utils {

namespace {

bool csvContainsAddress(const char *raw, uint8_t src) {
  if (raw == nullptr) return false;
  const char *cursor = raw;
  while (*cursor != '\0') {
    while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0') break;

    char *tail = nullptr;
    const long parsed = strtol(cursor, &tail, 0);
    if (tail != cursor) {
      while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') {
        ++tail;
      }
      if ((*tail == ',' || *tail == '\0') && parsed > 0 && parsed < 255 &&
          static_cast<uint8_t>(parsed) == src) {
        return true;
      }
    }

    while (*cursor != '\0' && *cursor != ',') {
      ++cursor;
    }
    if (*cursor == ',') ++cursor;
  }
  return false;
}

bool fixedListContainsAddress(const uint8_t *values, uint8_t count,
                              uint8_t src) {
  if (values == nullptr || src == 0 || src == 255) return false;
  if (count > Settings::kAddressListCap) count = Settings::kAddressListCap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] == src) return true;
  }
  return false;
}

}  // namespace

#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx) {
  return parseRoleTxFromModeRole(mode.c_str(), role.c_str(), roleTx);
}
#endif

bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx) {
  if (mode == nullptr || role == nullptr) return false;
  if (strcmp(mode, "paired") == 0) {
    if (strcmp(role, "transmitter") == 0 || strcmp(role, "gateway") == 0) {
      roleTx = true;
      return true;
    }
    if (strcmp(role, "receiver") == 0 || strcmp(role, "remote") == 0) {
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

uint8_t migrateControllerAddress(bool roleTx, bool hasControllerAddress,
                                 uint8_t controllerAddress,
                                 bool hasLegacyRemoteAddress,
                                 uint8_t legacyRemoteAddress) {
  if (roleTx) return 0;
  if (hasControllerAddress) return controllerAddress;
  if (hasLegacyRemoteAddress) return legacyRemoteAddress;
  return kGatewayAddress;
}

bool isProvisioningConfiguredAddressReserved(
    uint32_t configuredChipId, const uint32_t *discoveredChipIds,
    size_t discoveredChipCount) {
  if (configuredChipId == 0) return true;
  if (discoveredChipIds == nullptr) return true;
  for (size_t i = 0; i < discoveredChipCount; ++i) {
    if (discoveredChipIds[i] == configuredChipId) return false;
  }
  return true;
}

void sortProvisioningChipIds(uint32_t *chipIds, size_t chipCount) {
  if (chipIds == nullptr || chipCount < 2) return;
  for (size_t i = 1; i < chipCount; ++i) {
    const uint32_t current = chipIds[i];
    size_t insertAt = i;
    while (insertAt > 0 && chipIds[insertAt - 1] > current) {
      chipIds[insertAt] = chipIds[insertAt - 1];
      --insertAt;
    }
    chipIds[insertAt] = current;
  }
}

bool isAuthorizedMqttController(bool roleTx, uint8_t controllerAddress,
                                const Settings *settings, uint8_t src) {
  if (settings == nullptr || src == 0 || src == 255) return false;
  if (!roleTx && src == controllerAddress) return true;
  if (fixedListContainsAddress(settings->allowed_controller_addresses,
                               settings->allowed_controller_count, src)) {
    return true;
  }
  return csvContainsAddress(settings->mqtt_controller_addresses.c_str(), src);
}

int16_t decodeMaintenanceIdentityWifiRssi(uint8_t rawRssi, bool wifiConnected) {
  const int8_t rssi = static_cast<int8_t>(rawRssi);
  return wifiConnected && rssi < 0 ? rssi : 0;
}

bool decodeMaintenanceIdentityRelayState(uint8_t rawRelay, uint8_t &relayState) {
  constexpr uint8_t kRelayMarkerMask = 0xFE;
  constexpr uint8_t kRelayMarker = 0xA0;
  if ((rawRelay & kRelayMarkerMask) != kRelayMarker) return false;
  relayState = rawRelay & 0x01U;
  return true;
}

uint32_t staggeredAckDelayMs(uint8_t rank, uint32_t slotMs,
                             uint32_t jitterMs, uint32_t initialGuardMs) {
  return initialGuardMs + static_cast<uint32_t>(rank) * slotMs + jitterMs;
}

uint32_t groupInitialAckWindowMs(uint8_t targetCount) {
  if (targetCount == 0) return 0;
  const uint32_t minimum = kGroupAckLeadMs +
      static_cast<uint32_t>(targetCount - 1U) * kGroupAckSlotMs +
      kGroupAckSlotJitterMaxMs + kGroupAckAirtimeBudgetMs +
      kGroupAckWindowMarginMs;
  const uint32_t established = static_cast<uint32_t>(targetCount) *
      (kGroupAckSlotMs + kGroupAckSlotJitterMaxMs) + 120U;
  return minimum > established ? minimum : established;
}

GatewayControlSchedule gatewayControlSchedule(bool pairedInputControlEnabled,
                                              bool debouncedInputTransition,
                                              bool groupActive) {
  GatewayControlSchedule schedule{};
  schedule.run_group_control = pairedInputControlEnabled &&
      (debouncedInputTransition || groupActive);
  schedule.allow_observability = !schedule.run_group_control;
  return schedule;
}

ReceiveObservabilityAction receiveObservabilityAction(ReceiveObservabilityKind kind) {
  switch (kind) {
  case ReceiveObservabilityKind::PollResponse:
  case ReceiveObservabilityKind::MaintenanceStatus:
    return ReceiveObservabilityAction::Queue;
  }
  return ReceiveObservabilityAction::Queue;
}

uint8_t resolveGatewayTargets(
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t *outTargets,
    uint8_t maxTargets
) {
  if (outTargets == nullptr || maxTargets == 0) return 0;

  uint8_t targetCount = 0;

  auto addTarget = [&](uint8_t addr) {
    if (addr >= kMinAddress && addr <= kMaxAddress && addr != localAddress && targetCount < maxTargets) {
      for (uint8_t i = 0; i < targetCount; ++i) {
        if (outTargets[i] == addr) return;
      }
      outTargets[targetCount++] = addr;
    }
  };

  if (knownPeerCount > 0 && knownPeerAddresses != nullptr) {
    for (size_t i = 0; i < knownPeerCount && i < maxTargets; ++i) {
      addTarget(knownPeerAddresses[i]);
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
  if (resolveAdoptionAddress(address, chipId, knownPeerCount, knownPeerAddresses,
                             knownPeerChipIds) == 0) {
    return CandidateReason::Full;
  }
  if (knownPeerAddresses != nullptr) {
    for (size_t i = 0; i < knownPeerCount; ++i) {
      if (knownPeerAddresses[i] == address) {
        if (chipId != 0 && knownPeerChipIds != nullptr && knownPeerChipIds[i] != chipId) {
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

uint32_t operationalRefreshCycleMs(bool observerActive, bool periodicActive,
                                   uint32_t periodicCycleMs,
                                   uint32_t observerCycleMs) {
  if (!observerActive) return periodicActive ? periodicCycleMs : 0U;
  if (!periodicActive || observerCycleMs <= periodicCycleMs) return observerCycleMs;
  return periodicCycleMs;
}

uint32_t operationalRefreshStepMs(uint32_t cycleMs, uint8_t peerCount,
                                  uint32_t minimumStepMs) {
  if (cycleMs == 0 || peerCount == 0) return 0;
  const uint32_t step = cycleMs / peerCount;
  return step < minimumStepMs ? minimumStepMs : step;
}

bool operationalStateIsFresh(uint32_t now, uint32_t updatedMs,
                             uint32_t cycleMs) {
  return updatedMs != 0 && cycleMs != 0 &&
         static_cast<uint32_t>(now - updatedMs) < cycleMs;
}

const char *const kDefaultDeploymentKey = "lora-default-passphrase";

bool isDefaultDeploymentKey(const char *v) {
  if (v == nullptr) return false;
  while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') ++v;
  size_t len = strlen(v);
  while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t' || v[len - 1] == '\r' || v[len - 1] == '\n')) --len;
  return strlen(kDefaultDeploymentKey) == len && strncmp(v, kDefaultDeploymentKey, len) == 0;
}

#if !defined(UNIT_TEST)
bool isDefaultDeploymentKey(const String &v) {
  return isDefaultDeploymentKey(v.c_str());
}
#endif

} // namespace runtime_utils

MaintBlockTransition evaluateMaintBlockTransition(
  MaintAttemptResult attempt_result,
  uint8_t pending_address,
  MaintenanceRequestSource pending_source,
  const MaintBlockState& prev_state
) {
  MaintBlockState next_state = prev_state;
  bool should_emit = false;

  MaintBlockReason new_reason = MaintBlockReason::None;
  switch (attempt_result) {
    case MaintAttemptResult::Empty:
    case MaintAttemptResult::Success:
      new_reason = MaintBlockReason::None;
      break;
    case MaintAttemptResult::GroupActive:
      new_reason = MaintBlockReason::GroupActive;
      break;
    case MaintAttemptResult::RadioBudget:
      new_reason = MaintBlockReason::RadioBudget;
      break;
    case MaintAttemptResult::SendFailed:
      new_reason = MaintBlockReason::SendFailed;
      break;
  }

  if (new_reason == MaintBlockReason::None) {
    if (prev_state.reason != MaintBlockReason::None) {
      next_state.reason = MaintBlockReason::None;
      should_emit = false;
    }
  } else {
    if (prev_state.reason != new_reason || prev_state.address != pending_address || prev_state.source != pending_source) {
      next_state.reason = new_reason;
      next_state.address = pending_address;
      next_state.source = pending_source;
      should_emit = true;
    }
  }

  return { next_state, should_emit };
}

const char* maintBlockReasonName(MaintBlockReason reason) {
  switch (reason) {
    case MaintBlockReason::None: return "none";
    case MaintBlockReason::GroupActive: return "control recovery";
    case MaintBlockReason::RadioBudget: return "radio budget";
    case MaintBlockReason::SendFailed: return "send failed";
    default: return "unknown";
  }
}
