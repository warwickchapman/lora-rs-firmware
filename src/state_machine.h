#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stddef.h>

#include "config_store.h"
#include "radio_protocol.h"
#include "sensor_status.h"
#include "sensor_registry.h"
#include "runtime_utils.h"
#include "pending_command_manager.h"


#ifndef LRS_PROVISIONING_MAX_DEVICES
#define LRS_PROVISIONING_MAX_DEVICES 12
#endif

#ifndef LRS_MAX_PEERS
#define LRS_MAX_PEERS 12
#endif

#ifndef LRS_REPLAY_TRACKED_SOURCES
#define LRS_REPLAY_TRACKED_SOURCES 16
#endif

enum class LinkState : uint8_t {
  Boot,
  Idle,
  WaitAck,
  Timeout,
};

enum class RxControlSource : uint8_t {
  None,
  LoRa,
  Mqtt,
  Failsafe,
};

enum class RxFailsafeMode : uint8_t {
  HoldLast,
  ForceOff,
  ForceOn,
};

enum class PairedGroupPhase : uint8_t {
  Idle,
  AwaitInitialAcks,
  PollMissingSequential,
  Complete,
};

#include "peer_manager.h"


struct FleetScanSnapshot {
  bool active = false;
  uint8_t start_address = 0;
  uint8_t end_address = 0;
  uint8_t next_address = 0;
  uint16_t interval_ms = 0;
  uint32_t started_ms = 0;
  uint32_t last_tx_ms = 0;
  uint32_t sent = 0;
};

enum class ProvisioningSessionState : uint8_t {
  Idle,
  Discovering,
  Ready,
  Provisioning,
  Complete,
  Error,
};

enum class ProvisioningDeviceState : uint8_t {
  Discovered,
  Assigned,
  Keying,
  AwaitVerify,
  Verified,
  AppliedUnconfirmed,
  Failed,
  Skipped,
};

struct ProvisioningSessionSnapshot {
  bool active = false;
  ProvisioningSessionState state = ProvisioningSessionState::Idle;
  uint16_t session_nonce = 0;
  uint16_t max_remotes = 0;
  uint32_t started_ms = 0;
  uint32_t phase_deadline_ms = 0;
  bool paused_normal_tx = false;
  size_t discovered_count = 0;
  size_t selected_count = 0;
  size_t conflict_count = 0;
  size_t verified_count = 0;
  size_t failed_count = 0;
};

struct ProvisioningDeviceSnapshot {
  uint32_t chip_id = 0;
  uint8_t current_address = 0;
  uint8_t assigned_address = 0;
  bool role_tx = false;
  uint8_t hw_model = 0;
  uint8_t hw_rev = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  uint8_t fw_patch = 0;
  uint16_t fw_build = 0;
  int rssi = -127;
  uint32_t first_seen_ms = 0;
  uint32_t last_seen_ms = 0;
  bool selected = false;
  bool address_conflict = false;
  ProvisioningDeviceState state = ProvisioningDeviceState::Discovered;
};

class NodeStateMachine {
 public:
  friend class AdminExecutor;
  static constexpr uint32_t kIdentifyLedDurationMs = 6000;
  static constexpr uint32_t kMaintenancePageGapMs = 200;
  static constexpr uint32_t kAdoptionRetryIntervalMs = 1000;
  static constexpr uint8_t kAdoptionMaxRetries = 5;

  bool begin(const Settings &cfg, RadioProtocol *radio);
  void applyConfig(const Settings &cfg);
  void tick(bool powerSaveActive = false);
  void setMqttConnected(bool connected);

  LinkState linkState() const;
  uint8_t relayState() const;
  uint8_t relayFeedbackState() const;
  uint8_t inputState() const;
  uint8_t localInputState() const;
  int lastPacketRssi() const;
  uint32_t lastPacketMs() const;
  uint32_t lastTxMs() const;
  void setLocalSensors(const SensorRegistry &registry);
  const SensorRegistry &localSensors() const;
  uint8_t localTempCodeToSend() const;
  void setAuthoritativeUnixTime(uint32_t unixTimeS);
  bool sharedUnixTimeValid() const;
  uint32_t sharedUnixTime() const;
  RxControlSource lastRxControlSource() const;
  size_t peerCount() const;
  bool peerByIndex(size_t index, PeerStatusSnapshot &out) const;
  bool peerByAddress(uint8_t address, PeerStatusSnapshot &out) const;
  bool isConfiguredOperationalPeer(uint8_t address) const;
  bool isPeerUdpLogsEligible(uint8_t address) const;
  void mqttSetLocalRelay(uint8_t relayState);
  bool mqttSendPeerRelay(uint8_t dstAddress, uint8_t relayState);
  bool mqttSetPeerPollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs);
  bool mqttPollPeerNow(uint8_t dstAddress);
  bool mqttForgetPeer(uint8_t dstAddress);
  uint32_t resolveChipIdForAddress(uint8_t address) const;
  uint32_t activePeerChipIdForAddress(uint8_t address) const;
  bool mqttSetPeerWifi(uint8_t dstAddress, bool enabled);
  bool mqttSetPeerUdpLogControl(uint8_t dstAddress, bool enabled, IPAddress host, uint16_t port, uint32_t ttlS);
  bool sendPeerOtaPullControl(uint8_t dstAddress, IPAddress host, uint16_t port,
                              const char *sha256Hex);
  bool isOtaPullTxActive() const { return ota_pull_tx_.active; }
  bool isOtaPullActive() const { return ota_pull_active_; }
  void clearOtaPullActive() {
    ota_pull_active_ = false;
    ota_silence_until_ms_ = 0;
  }
  bool sendBroadcastWifiDisable();
  bool hasPendingWifiControl() const;
  bool consumePendingWifiControl(bool &enabled, uint8_t &src, uint32_t &commandCounter);
  bool sendWifiControlStatus(uint8_t dstAddress, bool enabled, uint32_t commandCounter);
  bool hasPendingUdpLogControl() const;
  bool consumePendingUdpLogControl(bool &enabled, IPAddress &host, uint16_t &port, uint32_t &ttlS, uint8_t &src);
  bool consumePendingOtaPull(IPAddress &host, uint16_t &port, char *sha256HexDest, size_t destSize,
                             uint8_t &src);
  bool fleetScanStart(uint8_t startAddress, uint8_t endAddress, uint16_t intervalMs);
  void fleetScanCancel();
  bool fleetScanSnapshot(FleetScanSnapshot &out) const;
  size_t candidateCount() const;
  bool candidateByIndex(size_t index, DiscoveryCandidate &out) const;
  bool candidateByChipId(uint32_t chipId, DiscoveryCandidate &out) const;
  void recordDiscoveryCandidate(uint8_t address, uint32_t chipId, int rssi);
  void removeDiscoveryCandidate(uint32_t chipId);
  CandidateReason evaluateCandidateReason(uint8_t address, uint32_t chipId) const;
  void evaluateAllCandidateReasons();
  bool startAdoption(uint32_t chipId, uint8_t assignedAddress, bool isReset);
  void cancelAdoption();
  bool consumePendingPeerSync(uint32_t &chipId, uint8_t &address);
  bool hasPendingReaddress() const;
  bool consumePendingReaddress(uint8_t &outNewAddress, uint8_t &outGwAddr);
  bool sendReaddressConfirm(uint8_t gwAddr, uint8_t newAddress);
  bool handleReaddressFrame(const ProtocolMessage &msg);

  bool isAdoptionActive() const { return adoption_active_; }
  uint32_t adoptionChipId() const { return adoption_chip_id_; }
  uint8_t adoptionAddress() const { return adoption_address_; }
  bool sendPeerReaddress(uint8_t dstAddress, uint32_t chipId, uint8_t newAddress, uint8_t op);

  bool sendFleetWifiProvision(const String &ssid, const String &password, uint8_t targetAddress = 255);
  bool hasPendingWifiProvision() const;
  bool consumePendingWifiProvision(char *ssidDest, size_t ssidSize, char *passwordDest, size_t passwordSize, uint8_t &src);
  uint32_t fleetWifiProvisionCooldownRemainingMs() const;
  bool sendPeerReboot(uint8_t dstAddress);
  bool hasPendingReboot() const { return pending_commands_.hasPendingReboot(); }
  bool consumePendingReboot();
  bool sendPeerSensorConfig(uint8_t dstAddress, bool tempEnabled, bool tankEnabled, bool powerSaveEnabled, bool powerSaveBootGrace);
  bool hasPendingSensorConfig() const { return pending_commands_.hasPendingSensorConfig(); }
  bool consumePendingSensorConfig(bool &tempEnabled, bool &tankEnabled, bool &powerSaveEnabled, bool &powerSaveBootGrace);
  bool sendPeerFactoryReset(uint8_t dstAddress, bool keepSharedFleetKey, bool keepWifiCredentials);
  bool consumePendingFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials, uint8_t &src);
  bool sendPeerFleetKeyChange(uint8_t targetAddress, const String &newFleetKey);
  bool hasPendingFleetKeyChange() const;
  bool consumePendingFleetKeyChange(char *keyDest, size_t keySize, uint8_t &src);
  bool provisioningStartDiscovery(uint16_t estimatedCount);
  bool provisioningStartProvisionAll();
  void provisioningCancel();
  bool provisioningSession(ProvisioningSessionSnapshot &out) const;
  size_t provisioningDeviceCount() const;
  bool provisioningDeviceByIndex(size_t index, ProvisioningDeviceSnapshot &out) const;
  size_t provisioningLogCount() const;
  bool provisioningLogByIndex(size_t index, uint32_t &timestampMs, char outMsg[56]) const;
  bool hasPendingFleetProvisionApply() const;
  bool consumePendingFleetProvisionApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx, uint8_t &controllerAddress,
                                         char *fleetKeyDest, size_t keySize);
  bool sendProvisioningVerify(uint16_t sessionNonce, uint8_t assignedAddress);
  void triggerIdentify(uint32_t durationMs = kIdentifyLedDurationMs);

  static size_t peerRuntimeSize();
  static size_t pollRuntimeSize();
  static size_t replaySourceStateSize();

 private:
  static constexpr size_t kMaxPeers = LRS_MAX_PEERS;
  static constexpr size_t kReplayTrackedSources = LRS_REPLAY_TRACKED_SOURCES;
  static_assert(kMaxPeers > 0, "LRS_MAX_PEERS must be > 0");
  static_assert(kMaxPeers <= 32, "LRS_MAX_PEERS must be <= 32 on ESP8266");
  static_assert(kReplayTrackedSources >= kMaxPeers, "LRS_REPLAY_TRACKED_SOURCES must be >= LRS_MAX_PEERS");
  static_assert(kReplayTrackedSources <= 64, "LRS_REPLAY_TRACKED_SOURCES must be <= 64 on ESP8266");

  struct RuntimeCfg {
    bool role_tx = false;
    uint8_t local_address = 0;
    uint8_t remote_address = 0;
    uint32_t heartbeat_ms = 60000;
    bool heartbeat_enabled = true;
    uint32_t ack_timeout_ms = 5000;
    uint32_t mqtt_remote_retry_timeout_ms = 5000;
    bool tx_mqtt_remote_polling_enabled = false;
    uint32_t tx_mqtt_remote_default_poll_interval_ms = 60000;
    bool rx_push_on_change_enabled = false;
    uint32_t rx_push_min_interval_ms = 60000;
    bool input_control_paired_lora_enabled = false;
    bool mqtt_control_enabled = false;
    RxFailsafeMode rx_failsafe_mode = RxFailsafeMode::HoldLast;
    uint32_t tx_command_retry_timeout_ms = 180000;
    uint32_t rx_failsafe_timeout_ms = 180000;
  };
  static constexpr uint8_t kLedPin = 2;

  const Settings *settings_ = nullptr;
  RuntimeCfg runtime_{};
  RadioProtocol *radio_ = nullptr;

  LinkState link_state_ = LinkState::Boot;
  uint8_t relay_state_ = 0;
  uint8_t input_state_ = 0;
  int last_input_raw_ = LOW;
  uint32_t identify_led_started_ms_ = 0;
  uint32_t identify_led_until_ms_ = 0;

  uint32_t last_heartbeat_ms_ = 0;
  uint32_t wait_ack_since_ms_ = 0;
  uint32_t last_counter_ = 0;
  struct ReplaySourceState {
    bool in_use = false;
    uint8_t src = 0;
    uint32_t boot_nonce = 0;
    uint32_t counter = 0;
    uint32_t last_seen_ms = 0;
  };
  ReplaySourceState replay_sources_[kReplayTrackedSources]{};
  uint32_t replay_table_evictions_ = 0;
  uint32_t replay_table_stale_evictions_ = 0;
  uint32_t replay_table_full_drops_ = 0;
  uint8_t replay_table_peak_used_ = 0;

  uint32_t last_debounce_ms_ = 0;
  uint32_t last_packet_ms_ = 0;
  uint32_t last_tx_ms_ = 0;
  uint32_t last_wifi_prov_tx_ms_ = 0;
  int last_packet_rssi_ = -127;
  uint32_t last_led_toggle_ms_ = 0;
  bool led_on_ = false;
  SensorRegistry local_sensors_;
  bool shared_time_valid_ = false;
  bool shared_time_authoritative_ = false;
  uint32_t shared_time_sync_unix_s_ = 0;
  uint32_t shared_time_sync_ms_ = 0;
  bool paired_input_slave_mode_ = false;
  RxControlSource last_rx_control_source_ = RxControlSource::None;

  bool tx_ack_pending_ = false;
  uint32_t tx_ack_apply_ms_ = 0;
  uint8_t tx_ack_relay_state_ = 0;
  bool radio_tx_budget_active_ = false;
  bool radio_tx_used_this_tick_ = false;
  bool tx_state_sync_pending_ = false;
  uint32_t tx_state_sync_due_ms_ = 0;
  bool tx_command_pending_ = false;
  uint8_t tx_pending_relay_state_ = 0;
  uint8_t tx_pending_input_state_ = 0;
  uint32_t tx_pending_command_counter_ = 0;
  uint8_t tx_retry_step_ = 0;
  uint32_t tx_next_retry_ms_ = 0;
  uint32_t tx_command_retry_deadline_ms_ = 0;
  PairedGroupPhase tx_group_phase_ = PairedGroupPhase::Idle;
  uint32_t tx_group_command_id_ = 0;
  uint32_t tx_group_expected_bitmap_ = 0;
  uint32_t tx_group_acked_bitmap_ = 0;
  uint32_t tx_group_retry_bitmap_ = 0;
  uint8_t tx_group_targets[Settings::kAddressListCap]{};
  uint8_t tx_group_target_count_ = 0;
  uint32_t tx_group_window_deadline_ms_ = 0;
  uint8_t tx_group_initial_send_cursor_ = 0;
  uint8_t tx_group_retry_cursor_ = 0;
  uint8_t tx_group_retry_addr_ = 0;
  uint32_t tx_group_retry_deadline_ms_ = 0;
  uint8_t tx_group_desired_relay_state_ = 0;
  uint8_t tx_group_desired_input_state_ = 0;
  bool rx_deferred_ack_pending_ = false;
  uint32_t rx_deferred_ack_due_ms_ = 0;
  uint32_t rx_deferred_ack_command_id_ = 0;
  uint8_t rx_deferred_ack_dst_ = 0;
  uint8_t rx_deferred_ack_relay_ = 0;
  uint8_t rx_deferred_ack_input_ = 0;
  bool rx_push_pending_ = false;
  uint32_t rx_last_push_ms_ = 0;
  uint32_t last_rx_control_ms_ = 0;
  bool maintenance_debug_pending_ = false;
  uint8_t maintenance_debug_dst_ = 0;
  bool maintenance_sensor_pending_ = false;
  uint8_t maintenance_sensor_dst_ = 0;
  uint8_t maintenance_sensor_page_index_ = 0;
  bool maintenance_version_pending_ = false;
  uint8_t maintenance_version_dst_ = 0;
  uint32_t last_maint_page_tx_ms_ = 0;

  PeerManager peer_manager_;
  DiscoveryCandidate discovery_candidates_[Settings::kAddressListCap]{};
  bool adoption_active_ = false;
  uint32_t adoption_chip_id_ = 0;
  uint8_t adoption_address_ = 0;
  uint8_t adoption_dst_addr_ = 0;
  uint32_t adoption_sent_ms_ = 0;
  uint8_t adoption_retry_count_ = 0;
  bool adoption_is_reset_ = false;


  bool fleet_scan_active_ = false;
  uint8_t fleet_scan_start_address_ = 1;
  uint8_t fleet_scan_end_address_ = 80;
  uint8_t fleet_scan_next_address_ = 1;
  uint16_t fleet_scan_interval_ms_ = 1500;
  uint32_t fleet_scan_next_ms_ = 0;
  uint32_t fleet_scan_started_ms_ = 0;
  uint32_t fleet_scan_last_tx_ms_ = 0;
  uint32_t fleet_scan_sent_ = 0;
  uint32_t next_peer_maintenance_ms_ = 0;
  uint8_t peer_maintenance_cursor_ = 0;

  struct WifiProvisionRxTransfer {
    bool active = false;
    uint8_t src = 0;
    uint8_t transfer_id = 0;
    uint8_t total_chunks = 0;
    uint8_t ssid_len = 0;
    uint8_t pass_len = 0;
    uint32_t expected_hash = 0;
    uint32_t received_bitmap = 0;
    uint8_t data[96]{};
  };
  WifiProvisionRxTransfer wifi_prov_rx_{};


  struct FleetKeyControlRxTransfer {
    bool active = false;
    uint8_t src = 0;
    uint8_t transfer_id = 0;
    uint8_t total_chunks = 0;
    uint8_t key_len = 0;
    uint32_t expected_hash = 0;
    uint32_t received_bitmap = 0;
    uint8_t data[64]{};
  };
  FleetKeyControlRxTransfer fleet_key_rx_{};

  struct OtaPullRxTransfer {
    bool active = false;
    uint8_t src = 0;
    uint8_t transfer_id = 0;
    IPAddress host;
    uint16_t port = 0;
    uint32_t received_bitmap = 0;
    uint8_t sha256[32]{};
  };
  struct OtaPullTxTransfer {
    bool active = false;
    uint8_t dst = 0;
    uint8_t transfer_id = 0;
    IPAddress host;
    uint16_t port = 0;
    uint8_t frame_index = 0;
    uint32_t next_tx_ms = 0;
    uint8_t sha256[32]{};
  };
  OtaPullTxTransfer ota_pull_tx_{};
  OtaPullRxTransfer ota_pull_rx_{};

  uint32_t ota_silence_until_ms_ = 0;
  bool ota_pull_active_ = false;
  uint32_t ota_pull_start_ms_ = 0;
  PendingCommandManager pending_commands_;
  bool mqtt_connected_ = false;

  struct ProvisioningDevice {
    bool in_use = false;
    uint32_t chip_id = 0;
    uint8_t current_address = 0;
    uint8_t assigned_address = 0;
    bool role_tx = false;
    uint8_t hw_model = 0;
    uint8_t hw_rev = 0;
    uint8_t fw_major = 0;
    uint8_t fw_minor = 0;
    uint8_t fw_patch = 0;
    uint16_t fw_build = 0;
    int rssi = -127;
    uint32_t first_seen_ms = 0;
    uint32_t last_seen_ms = 0;
    bool selected = true;
    bool address_conflict = false;
    ProvisioningDeviceState state = ProvisioningDeviceState::Discovered;
    uint8_t retries = 0;
    uint8_t key_total_chunks = 0;
    uint8_t key_len = 0;
    uint16_t key_crc16 = 0;
    uint8_t key_next_chunk = 0;
    bool key_start_sent = false;
    bool key_commit_sent = false;
    bool late_verify_probe_sent = false;
  };
  static constexpr size_t kMaxProvisioningDevices = static_cast<size_t>(LRS_PROVISIONING_MAX_DEVICES);
  ProvisioningDevice *prov_devices_ = nullptr;
  size_t prov_device_capacity_ = 0;
  size_t prov_device_count_ = 0;
  struct ProvisionedAddressEntry {
    bool in_use = false;
    uint32_t chip_id = 0;
    uint8_t assigned_address = 0;
    uint32_t updated_ms = 0;
  };
  ProvisionedAddressEntry provisioned_addrs_[kMaxPeers]{};

  struct ProvisioningSessionRuntime {
    bool active = false;
    ProvisioningSessionState state = ProvisioningSessionState::Idle;
    uint16_t session_nonce = 0;
    uint16_t max_remotes = 0;
    uint32_t started_ms = 0;
    uint32_t phase_deadline_ms = 0;
    bool pause_normal_tx = false;
    bool provision_all_requested = false;
    size_t current_index = 0;
    uint8_t key_transfer_chunks = 0;
    uint8_t discover_broadcast_remaining = 0;
    uint32_t discover_broadcast_window_ms = 0;
    uint32_t discover_reply_window_ms = 0;
    uint32_t next_discover_broadcast_ms = 0;
    uint32_t watchdog_last_log_ms = 0;
  };
  ProvisioningSessionRuntime prov_{};

  struct ProvTargetRxState {
    bool discover_pending = false;
    uint16_t session_nonce = 0;
    uint32_t announce_at_ms = 0;
    uint32_t announce_second_at_ms = 0;
    uint8_t announce_remaining = 0;
    bool have_staged_assignment = false;
    uint8_t staged_address = 0;
    bool staged_role_tx = false;
    bool key_transfer_active = false;
    uint16_t key_session_nonce = 0;
    uint8_t key_total_chunks = 0;
    uint8_t key_len = 0;
    uint16_t key_crc16 = 0;
    uint32_t key_bitmap = 0;
    bool key_committed = false;
    char key_data[94]{};
  };
  ProvTargetRxState prov_rx_{};

  void tickTransmitter();
  void tickReceiver();
  void tickReceive();
  void tickFleetScan(uint32_t now);
  void tickCandidatesAndAdoption(uint32_t now);
  void tickLed(bool powerSaveActive = false);

  bool tickIdentifyLed(uint32_t now);
  void tickProvisioningCoordinator(uint32_t now);
  void enterProvisioningQuietMode();
  void exitProvisioningCoordinatorMode(ProvisioningSessionState endState);
  void tickProvisioningTarget(uint32_t now);
  void refreshRuntimeCfg(const Settings &cfg);
  uint8_t txFlags() const;
  void updateSharedTimeFromPeer(uint32_t unixTimeS, bool authoritative);
  uint32_t currentUnixTimeS(uint32_t nowMs) const;
  bool radioTxBudgetAvailable() const;
  void resetRadioTxBudgetForTick();
  void finishRadioTxBudgetForTick();
  void markRadioTxSentThisTick();
  void tickPeerMqttCommands(uint32_t now);
  void tickPeerPolling(uint32_t now);
  void tickPeerMaintenance(uint32_t now);
  bool sendPeerMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter = nullptr);
  void tickPendingOtaPullControl(uint32_t now);
  bool sendQueuedOtaPullControlFrame();
  bool sendPollRequest(uint8_t dstAddress, uint32_t *sentCounter = nullptr);
  bool sendMaintenanceRequest(uint8_t dstAddress, bool requestDiagnostics = false, uint32_t *sentCounter = nullptr);
  bool sendMaintenanceStatus(uint8_t dstAddress, bool requestDiagnostics = false);
  bool sendMaintenanceVersionStatus(uint8_t dstAddress);
  bool sendMaintenanceSensorStatus(uint8_t dstAddress);
  bool sendMaintenanceDebugStatus(uint8_t dstAddress);
  bool handleMaintenanceStatus(const ProtocolMessage &msg);
  void tickPendingMaintenancePages();
  PeerRuntime *findOrCreatePeer(uint8_t address);
  void prePopulateGatewayPeerCache();

  bool handleWifiProvisionFrame(const ProtocolMessage &msg);
  bool handleWifiControlFrame(const ProtocolMessage &msg);
  bool handleUdpLogControlFrame(const ProtocolMessage &msg);
  bool handleOtaPullControlFrame(const ProtocolMessage &msg);
  bool handleFactoryResetFrame(const ProtocolMessage &msg);
  bool handleRebootFrame(const ProtocolMessage &msg);
  bool handleSensorConfigFrame(const ProtocolMessage &msg);
  bool handleFleetKeyControlFrame(const ProtocolMessage &msg);
  bool handleProvisioningFrame(const ProtocolMessage &msg);
  bool isAuthorizedMqttController(uint8_t src) const;
  bool isAuthorizedPairedSource(uint8_t src) const;
  bool isDefaultFleetKey() const;
  bool shouldAcceptReplayAndUpdate(const ProtocolMessage &msg, bool trustedSourceHint);
  bool isTrustedReplaySource(uint8_t src, bool commissioningTraffic) const;
  uint8_t preferredProvisionedAddressForChip(uint32_t chipId) const;
  bool hasDiscoveredProvisioningChip(uint32_t chipId) const;
  void rememberProvisionedAddress(uint32_t chipId, uint8_t assignedAddress);
  ProvisioningDevice *findProvisioningDeviceByChip(uint32_t chipId);
  ProvisioningDevice *upsertProvisioningDevice(uint32_t chipId);
  bool ensureProvisioningStorage();
  void resetProvisioningStorage();
  void freeProvisioningStorage();
  void recomputeProvisioningConflictsAndAssignments();
  bool sendProvisioningCoordinatorPacketFactory(const uint8_t payload[12], uint8_t dst);
  bool sendProvisioningDiscoverStart(uint16_t sessionNonce, uint32_t replyWindowMs, uint32_t broadcastWindowMs);
  bool sendProvisioningAnnounce(uint16_t sessionNonce);
  bool sendProvisioningVerifyPacket(uint16_t sessionNonce, uint8_t assignedAddress);
  bool confirmProvisioningByFleetResponse(const ProtocolMessage &msg);
  bool ackMatchesPendingCommand(const ProtocolMessage &msg) const;
  bool isPairedTargetAddress(uint8_t addr) const;
  bool buildTxGroupTargets();
  uint8_t txGroupTargetIndexForAddress(uint8_t addr) const;
  uint32_t txGroupMissingBitmap() const;
  bool txGroupHasMissingTargets() const;
  bool isGroupActive() const;
  void resetTxGroupState();
  void startTxGroupCommand(uint8_t relayState, uint8_t inputState, const char *reasonEvent = nullptr);
  void tickTxGroupCommand(uint32_t now);
  bool sendTxGroupChangeToAddress(uint8_t addr, const char *eventName, const char *phase);
  bool sendTxGroupPollToAddress(uint8_t addr, const char *eventName, const char *phase);
  void finishTxGroupSuccess();
  void finishTxGroupPartial();
  void updatePeerAckStatus(uint8_t src, uint8_t relayState, uint8_t inputState, PeerAckState ackState, int rssi = -127);
  uint8_t pairedAckRankForLocalAddress() const;
  void scheduleDeferredAck(uint8_t dst, uint8_t relayState, uint8_t inputState, uint32_t commandId);
  void tickDeferredAck(uint32_t now);
  void applyReceiverFailsafe(uint32_t now);
  uint32_t tick_watchdog_last_log_ms_ = 0;
  bool power_save_active_ = false;

  struct ProvLogEntry {
    uint32_t timestamp_ms;
    char message[56];
  };
  static constexpr size_t kMaxProvLogs = 16;
  ProvLogEntry prov_logs_[kMaxProvLogs]{};
  size_t prov_log_head_ = 0;
  size_t prov_log_count_ = 0;
  void addProvLog(const char *fmt, ...);
};
