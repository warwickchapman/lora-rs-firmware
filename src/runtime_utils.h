#pragma once
#include <cstdint>


#if defined(UNIT_TEST)
using wl_status_t = int;
constexpr wl_status_t WL_IDLE_STATUS = 0;
constexpr wl_status_t WL_NO_SSID_AVAIL = 1;
constexpr wl_status_t WL_SCAN_COMPLETED = 2;
constexpr wl_status_t WL_CONNECTED = 3;
constexpr wl_status_t WL_CONNECT_FAILED = 4;
constexpr wl_status_t WL_CONNECTION_LOST = 5;
constexpr wl_status_t WL_DISCONNECTED = 6;
constexpr wl_status_t WL_WRONG_PASSWORD = 7;
constexpr wl_status_t WL_NO_SHIELD = 8;
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
#endif

enum class CandidateReason : uint8_t {
  Ok,
  KnownChipMoved,
  Conflict,
  OutOfRange,
  Full,
};

enum class CandidateState : uint8_t {
  SeenAddressOnly,
  Identified,
  Readdressing,
  Adopted,
  Failed,
  ResetRequested
};

struct DiscoveryCandidate {
  uint32_t chip_id = 0;
  uint8_t address = 0;
  uint32_t last_seen_ms = 0;
  int rssi = -127;
  CandidateReason reason = CandidateReason::Ok;
  CandidateState state = CandidateState::SeenAddressOnly;
  bool in_use = false;
  uint8_t probe_attempt_count = 0;
  uint32_t last_probe_ms = 0;
};

enum class RemoteReaddressState : uint8_t {
  Idle,
  PendingSave,
  PendingReset,
  Completed,
  Failed
};

namespace runtime_utils {

constexpr uint8_t kMinAddress = 1;
constexpr uint8_t kMaxAddress = 254;
constexpr uint8_t kGatewayAddress = 254;
constexpr uint8_t kFirstRemoteAddress = 1;

constexpr uint32_t kUnitTestChipId = 0x0048CB85UL;

inline uint32_t canonicalEspChipId() {
#if defined(UNIT_TEST)
  return kUnitTestChipId;
#else
  return ESP.getChipId() & 0x00FFFFFFUL;
#endif
}



#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx);
#endif
bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx);
const char *wifiStatusText(wl_status_t st);

// Config schema 4 used remote_address for both roles. Schema 5 keeps it only
// as a one-time remote migration input; gateways have no controller address.
uint8_t migrateControllerAddress(bool roleTx, bool hasControllerAddress,
                                 uint8_t controllerAddress,
                                 bool hasLegacyRemoteAddress,
                                 uint8_t legacyRemoteAddress);

uint8_t resolveGatewayTargets(
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t *outTargets,
    uint8_t maxTargets
);

CandidateReason evaluateCandidateReason(
    uint8_t address,
    uint32_t chipId,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    const uint32_t *knownPeerChipIds
);

uint8_t resolveAdoptionAddress(
    uint8_t currentAddress,
    uint32_t chipId,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    const uint32_t *knownPeerChipIds
);

bool validateGatewayConfirm(
    uint32_t targetChipId,
    uint8_t newAddress,
    uint8_t msgSrc,
    bool adoptionActive,
    uint32_t adoptionChipId,
    uint8_t adoptionAddress,
    uint8_t adoptionDstAddr
);

RemoteReaddressState transitionRemoteReaddress(
    RemoteReaddressState currentState,
    bool rxRequest,
    uint8_t rxNewAddress,
    bool saveSucceeded,
    bool &outSendConfirm,
    uint8_t &outConfirmAddress
);

bool transitionAdoptionStart(
    CandidateState &cState,
    bool &adoptionActive,
    bool isReset,
    bool txSuccess
);

constexpr uint32_t kCandidateProbeGlobalGapMs = 500;
constexpr uint32_t kCandidateProbeIntervalMs = 1500;
constexpr uint8_t kCandidateProbeMaxAttempts = 4;

bool tickCandidateProbe(
    DiscoveryCandidate &c,
    uint32_t now,
    uint32_t &last_global_probe_ms,
    bool &outSendProbe
);

extern const char *const kDefaultDeploymentKey;
bool isDefaultDeploymentKey(const char *v);
#if !defined(UNIT_TEST)
bool isDefaultDeploymentKey(const String &v);
#endif

} // namespace runtime_utils
