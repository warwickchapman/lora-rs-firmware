#pragma once
#include <cstdint>


struct Settings;

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

enum class MaintenanceRequestSource : uint8_t {
  FleetScan,
  CandidateProbe,
  AdminPeerRefresh,
  AdminDiagnostics,
};

enum class MaintAttemptResult : uint8_t {
  Empty = 0,
  GroupActive = 1,
  RadioBudget = 2,
  SendFailed = 3,
  Success = 4
};

enum class MaintBlockReason : uint8_t {
  None = 0,
  GroupActive = 1,
  RadioBudget = 2,
  SendFailed = 3
};

struct MaintBlockState {
  MaintBlockReason reason = MaintBlockReason::None;
  uint8_t address = 0;
  MaintenanceRequestSource source = MaintenanceRequestSource::FleetScan; // Ignored if reason is None
};

struct MaintBlockTransition {
  MaintBlockState next_state;
  bool should_emit_event;
};

MaintBlockTransition evaluateMaintBlockTransition(
  MaintAttemptResult attempt_result,
  uint8_t pending_address,
  MaintenanceRequestSource pending_source,
  const MaintBlockState& prev_state
);

const char* maintBlockReasonName(MaintBlockReason reason);


namespace runtime_utils {

constexpr uint8_t kMinAddress = 1;
constexpr uint8_t kMaxAddress = 254;
constexpr uint8_t kGatewayAddress = 254;
constexpr uint8_t kFirstRemoteAddress = 1;
// Maintenance identity b2.7/b6/b7 semantics are a protocol-generation boundary.
constexpr uint8_t kMaintenancePayloadVersion = 4;
constexpr uint32_t kGroupAckLeadMs = 250;
constexpr uint32_t kGroupAckSlotMs = 180;
constexpr uint32_t kGroupAckSlotJitterMaxMs = 40;
constexpr uint32_t kGroupAckAirtimeBudgetMs = 100;
constexpr uint32_t kGroupAckWindowMarginMs = 20;

constexpr uint32_t kUnitTestChipId = 0x0048CB85UL;

inline uint32_t canonicalEspChipId() {
#if defined(UNIT_TEST)
  return kUnitTestChipId;
#else
  return ESP.getChipId() & 0x00FFFFFFUL;
#endif
}

inline bool isValidRemotePeerIdentity(uint8_t addr, uint32_t chipId) {
  return addr >= kMinAddress && addr <= kMaxAddress && addr != kGatewayAddress && chipId != 0;
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

// Authorize an MQTT control packet source for this device.
// Remotes accept their configured controller_address; gateways never receive
// remote-style controller authority. Explicit allowed_controller_addresses and
// mqtt_controller_addresses entries are still honored when configured.
bool isAuthorizedMqttController(bool roleTx, uint8_t controllerAddress,
                                const Settings *settings, uint8_t src);

// Identity page byte b6 carries normal operational WiFi RSSI as signed dBm.
// A non-negative value is unavailable, including identity pages from older
// firmware that used b6 for packed firmware version information.
int16_t decodeMaintenanceIdentityWifiRssi(uint8_t rawRssi, bool wifiConnected);

// A valid identity page always carries the remote's sampled dry-contact state.
constexpr uint8_t kMaintenanceIdentityInputStateMask = 0x80;

// Identity page byte b7 uses 0xA0/0xA1 for a confirmed relay state. Other
// values, including pages from older firmware, are deliberately unknown.
bool decodeMaintenanceIdentityRelayState(uint8_t rawRelay, uint8_t &relayState);

// Leave the gateway enough time to return to receive mode before the first
// remote ACKs a broadcast. Subsequent remotes retain their deterministic slots.
uint32_t staggeredAckDelayMs(uint8_t rank, uint32_t slotMs,
                             uint32_t jitterMs, uint32_t initialGuardMs);
uint32_t groupInitialAckWindowMs(uint8_t targetCount);

struct GatewayControlSchedule {
  // Receive-side poll and maintenance responses are always queued. They never
  // take the radio directly from the receive phase.
  bool queue_received_observability = true;
  bool run_group_control = false;
  bool allow_observability = true;
};

enum class ReceiveObservabilityKind : uint8_t {
  PollResponse,
  MaintenanceStatus,
};

enum class ReceiveObservabilityAction : uint8_t {
  Queue,
};

// Normal replies received over LoRa are always queued for the scheduler; the
// receive path must never consume the radio transmit budget for them.
ReceiveObservabilityAction receiveObservabilityAction(ReceiveObservabilityKind kind);

// Gateway scheduler contract: a debounced paired input transition and an
// already-active paired command reserve the control step ahead of telemetry.
GatewayControlSchedule gatewayControlSchedule(bool pairedInputControlEnabled,
                                              bool debouncedInputTransition,
                                              bool groupActive);

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
