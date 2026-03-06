#pragma once

#include <Arduino.h>
#include <stddef.h>

#include "config_store.h"
#include "radio_protocol.h"

#ifndef LRS_PROVISIONING_MAX_DEVICES
#define LRS_PROVISIONING_MAX_DEVICES 8
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
  Automation,
  Failsafe,
};

enum class RxFailsafeMode : uint8_t {
  HoldLast,
  ForceOff,
  ForceOn,
};

enum class PeerAckState : uint8_t {
  Unknown,
  Pending,
  Ok,
  Timeout,
};

struct PeerStatusSnapshot {
  uint8_t address = 0;
  uint8_t relay_state = 0;
  uint8_t input_state = 0;
  bool temp_valid = false;
  int8_t temp_c = 0;
  int uplink_rssi = -127;
  bool downlink_rssi_valid = false;
  int downlink_rssi = -127;
  uint32_t last_seen_ms = 0;
  uint32_t last_cmd_counter = 0;
  PeerAckState ack_state = PeerAckState::Unknown;
  uint32_t poll_interval_ms = 0;
  uint32_t last_poll_tx_ms = 0;
  bool poll_pending = false;
};

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
  uint16_t estimated_count = 0;
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
  int rssi = -127;
  uint32_t first_seen_ms = 0;
  uint32_t last_seen_ms = 0;
  bool selected = false;
  bool address_conflict = false;
  ProvisioningDeviceState state = ProvisioningDeviceState::Discovered;
};

class NodeStateMachine {
 public:
  bool begin(const Settings &cfg, RadioProtocol *radio);
  void applyConfig(const Settings &cfg);
  void tick();

  LinkState linkState() const;
  uint8_t relayState() const;
  uint8_t inputState() const;
  uint8_t localDryContactState() const;
  int lastPacketRssi() const;
  uint32_t lastPacketMs() const;
  uint32_t lastTxMs() const;
  void setLocalTemperature(bool valid, float celsius);
  bool localTemperatureValid() const;
  float localTemperatureC() const;
  bool remoteTemperatureValid() const;
  float remoteTemperatureC() const;
  uint32_t remoteTemperatureMs() const;
  void setAuthoritativeUnixTime(uint32_t unixTimeS);
  bool sharedUnixTimeValid() const;
  uint32_t sharedUnixTime() const;
  RxControlSource lastRxControlSource() const;
  size_t peerCount() const;
  bool peerByIndex(size_t index, PeerStatusSnapshot &out) const;
  void mqttSetLocalRelay(uint8_t relayState);
  void automationSetLocalRelay(uint8_t relayState);
  bool mqttSendPeerRelay(uint8_t dstAddress, uint8_t relayState);
  bool mqttSetPeerPollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs);
  bool mqttPollPeerNow(uint8_t dstAddress);
  bool mqttForgetPeer(uint8_t dstAddress);
  bool fleetScanStart(uint8_t startAddress, uint8_t endAddress, uint16_t intervalMs);
  void fleetScanCancel();
  bool fleetScanSnapshot(FleetScanSnapshot &out) const;
  bool sendFleetWifiProvision(const String &ssid, const String &password);
  bool hasPendingWifiProvision() const;
  bool consumePendingWifiProvision(String &ssid, String &password, uint8_t &src);
  uint32_t fleetWifiProvisionCooldownRemainingMs() const;
  bool sendPeerFactoryReset(uint8_t dstAddress, bool keepSharedFleetKey);
  bool consumePendingFactoryReset(bool &keepSharedFleetKey, uint8_t &src);
  bool provisioningStartDiscovery(uint16_t estimatedCount);
  bool provisioningStartProvisionAll();
  void provisioningCancel();
  bool provisioningSession(ProvisioningSessionSnapshot &out) const;
  size_t provisioningDeviceCount() const;
  bool provisioningDeviceByIndex(size_t index, ProvisioningDeviceSnapshot &out) const;
  bool hasPendingFleetProvisionApply() const;
  bool consumePendingFleetProvisionApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx, String &fleetKey);
 bool sendProvisioningVerify(uint16_t sessionNonce, uint8_t assignedAddress);

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
  uint8_t local_temp_code_ = 0xFF;
  bool remote_temp_valid_ = false;
  int8_t remote_temp_c_ = 0;
  uint32_t remote_temp_ms_ = 0;
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
  bool tx_command_pending_ = false;
  uint8_t tx_pending_relay_state_ = 0;
  uint8_t tx_pending_input_state_ = 0;
  uint32_t tx_pending_command_counter_ = 0;
  uint8_t tx_retry_step_ = 0;
  uint32_t tx_next_retry_ms_ = 0;
  uint32_t tx_command_retry_deadline_ms_ = 0;
  bool rx_push_pending_ = false;
  uint32_t rx_last_push_ms_ = 0;
  uint32_t last_rx_control_ms_ = 0;

  struct PeerRuntime {
    bool in_use = false;
    uint8_t address = 0;
    uint8_t relay_state = 0;
    uint8_t input_state = 0;
    bool temp_valid = false;
    int8_t temp_c = 0;
    int uplink_rssi = -127;
    bool downlink_rssi_valid = false;
    int downlink_rssi = -127;
    uint32_t last_seen_ms = 0;
    uint32_t last_cmd_counter = 0;
    PeerAckState ack_state = PeerAckState::Unknown;
    bool pending = false;
    uint8_t pending_relay = 0;
    uint8_t retry_step = 0;
    uint32_t next_retry_ms = 0;
    uint32_t pending_counter = 0;
    uint32_t pending_deadline_ms = 0;
    uint32_t poll_interval_ms = 0;
  };
  struct PollRuntime {
    uint32_t next_poll_ms = 0;
    bool poll_pending = false;
    uint8_t poll_retry_step = 0;
    uint32_t poll_next_retry_ms = 0;
    uint32_t poll_counter = 0;
    uint32_t poll_deadline_ms = 0;
    uint32_t last_poll_tx_ms = 0;
  };
  // Managed peer slots on ESP8266 (telemetry/poll/retry history). Remote commands
  // may still be sent to uncached peers as transient fire-and-forget operations.
  PeerRuntime peers_[kMaxPeers]{};
  size_t peer_count_ = 0;
  PollRuntime *poll_states_ = nullptr;
  size_t poll_state_capacity_ = 0;

  bool fleet_scan_active_ = false;
  uint8_t fleet_scan_start_address_ = 1;
  uint8_t fleet_scan_end_address_ = 80;
  uint8_t fleet_scan_next_address_ = 1;
  uint16_t fleet_scan_interval_ms_ = 120;
  uint32_t fleet_scan_next_ms_ = 0;
  uint32_t fleet_scan_started_ms_ = 0;
  uint32_t fleet_scan_last_tx_ms_ = 0;
  uint32_t fleet_scan_sent_ = 0;

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
  bool wifi_prov_pending_ = false;
  String wifi_prov_pending_ssid_;
  String wifi_prov_pending_password_;
  uint8_t wifi_prov_pending_src_ = 0;
  bool factory_reset_pending_ = false;
  bool factory_reset_keep_fleet_pending_ = true;
  uint8_t factory_reset_pending_src_ = 0;
  bool fleet_prov_apply_pending_ = false;
  uint16_t fleet_prov_apply_session_nonce_ = 0;
  uint8_t fleet_prov_apply_address_ = 0;
  bool fleet_prov_apply_role_tx_ = false;
  String fleet_prov_apply_key_;

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

  struct ProvisioningSessionRuntime {
    bool active = false;
    ProvisioningSessionState state = ProvisioningSessionState::Idle;
    uint16_t session_nonce = 0;
    uint16_t estimated_count = 0;
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
  };
  ProvisioningSessionRuntime prov_{};

  struct ProvTargetRxState {
    bool discover_pending = false;
    uint16_t session_nonce = 0;
    uint32_t announce_at_ms = 0;
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
  void tickLed();
  void tickProvisioningCoordinator(uint32_t now);
  void tickProvisioningTarget(uint32_t now);
  void refreshRuntimeCfg(const Settings &cfg);
  void captureRemoteTemp(uint8_t tempCode);
  uint8_t txFlags() const;
  void updateSharedTimeFromPeer(uint32_t unixTimeS, bool authoritative);
  uint32_t currentUnixTimeS(uint32_t nowMs) const;
  bool radioTxBudgetAvailable() const;
  void resetRadioTxBudgetForTick();
  void finishRadioTxBudgetForTick();
  void markRadioTxSentThisTick();
  void sendTxState(MessageType type, uint8_t relayState, uint8_t inputState, const char *logEvent, bool resetRetryWindow = true);
  void tickPeerMqttCommands(uint32_t now);
  void tickPeerPolling(uint32_t now);
  bool sendPeerMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter = nullptr);
  bool sendPollRequest(uint8_t dstAddress, uint32_t *sentCounter = nullptr);
  PeerRuntime *findOrCreatePeer(uint8_t address);
  PollRuntime *pollStateForIndex(size_t index);
  const PollRuntime *pollStateForIndex(size_t index) const;
  bool ensurePollStorage();
  void resetPollStorage();
  void freePollStorage();
  bool handleWifiProvisionFrame(const ProtocolMessage &msg);
  bool handleFactoryResetFrame(const ProtocolMessage &msg);
  bool handleProvisioningFrame(const ProtocolMessage &msg);
  bool isAuthorizedMqttController(uint8_t src) const;
  bool isDefaultFleetKey() const;
  bool shouldAcceptReplayAndUpdate(const ProtocolMessage &msg, bool trustedSourceHint);
  bool isTrustedReplaySource(uint8_t src, bool commissioningTraffic) const;
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
  void applyReceiverFailsafe(uint32_t now);
};
