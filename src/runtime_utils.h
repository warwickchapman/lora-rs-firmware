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
};

enum class RemoteReaddressState : uint8_t {
  Idle,
  PendingSave,
  PendingReset,
  Completed,
  Failed
};

namespace runtime_utils {


#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx);
#endif
bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx);
const char *wifiStatusText(wl_status_t st);

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

} // namespace runtime_utils

