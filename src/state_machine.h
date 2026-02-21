#pragma once

#include <Arduino.h>
#include <stddef.h>

#include "config_store.h"
#include "radio_protocol.h"

class LogBuffer;

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
};

enum class RemoteAckState : uint8_t {
  Unknown,
  Pending,
  Ok,
  Timeout,
};

struct RemoteNodeStatusSnapshot {
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
  RemoteAckState ack_state = RemoteAckState::Unknown;
  uint32_t poll_interval_ms = 0;
  uint32_t last_poll_tx_ms = 0;
  bool poll_pending = false;
};

class NodeStateMachine {
 public:
  bool begin(const Settings &cfg, RadioProtocol *radio, LogBuffer *logs);
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
  RxControlSource lastRxControlSource() const;
  size_t remoteNodeCount() const;
  bool remoteNodeByIndex(size_t index, RemoteNodeStatusSnapshot &out) const;
  void mqttSetLocalRelay(uint8_t relayState);
  bool mqttSendRemoteRelay(uint8_t dstAddress, uint8_t relayState);
  bool mqttSetRemotePollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs);
  bool mqttPollRemoteNow(uint8_t dstAddress);
  bool mqttForgetRemote(uint8_t dstAddress);

 private:
  Settings cfg_{};
  RadioProtocol *radio_ = nullptr;
  LogBuffer *logs_ = nullptr;

  LinkState link_state_ = LinkState::Boot;
  uint8_t relay_state_ = 0;
  uint8_t input_state_ = 0;
  int last_input_raw_ = LOW;

  uint32_t last_heartbeat_ms_ = 0;
  uint32_t wait_ack_since_ms_ = 0;
  uint32_t last_counter_ = 0;
  uint32_t last_seen_counter_by_src_[256] = {0};

  uint32_t last_debounce_ms_ = 0;
  uint32_t last_packet_ms_ = 0;
  uint32_t last_tx_ms_ = 0;
  int last_packet_rssi_ = -127;
  uint32_t last_led_toggle_ms_ = 0;
  bool led_on_ = false;
  uint8_t local_temp_code_ = 0xFF;
  bool remote_temp_valid_ = false;
  int8_t remote_temp_c_ = 0;
  uint32_t remote_temp_ms_ = 0;
  RxControlSource last_rx_control_source_ = RxControlSource::None;

  bool tx_ack_pending_ = false;
  uint32_t tx_ack_apply_ms_ = 0;
  uint8_t tx_ack_relay_state_ = 0;
  bool tx_state_sync_pending_ = false;
  bool tx_command_pending_ = false;
  uint8_t tx_pending_relay_state_ = 0;
  uint8_t tx_pending_input_state_ = 0;
  uint8_t tx_retry_step_ = 0;
  uint32_t tx_next_retry_ms_ = 0;
  bool rx_push_pending_ = false;
  uint32_t rx_last_push_ms_ = 0;

  struct RemoteNodeRuntime {
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
    RemoteAckState ack_state = RemoteAckState::Unknown;
    bool pending = false;
    uint8_t pending_relay = 0;
    uint8_t retry_step = 0;
    uint32_t next_retry_ms = 0;
    uint32_t pending_counter = 0;
    uint32_t pending_deadline_ms = 0;
    uint32_t poll_interval_ms = 0;
    uint32_t next_poll_ms = 0;
    bool poll_pending = false;
    uint8_t poll_retry_step = 0;
    uint32_t poll_next_retry_ms = 0;
    uint32_t poll_counter = 0;
    uint32_t poll_deadline_ms = 0;
    uint32_t last_poll_tx_ms = 0;
  };
  static constexpr size_t kMaxRemoteNodes = 16;
  RemoteNodeRuntime remote_nodes_[kMaxRemoteNodes]{};
  size_t remote_node_count_ = 0;

  void tickTransmitter();
  void tickReceiver();
  void tickReceive();
  void tickLed();
  void captureRemoteTemp(uint8_t tempCode);
  void sendTxState(MessageType type, uint8_t relayState, uint8_t inputState, const char *logEvent);
  void tickRemoteMqttCommands(uint32_t now);
  void tickRemotePolling(uint32_t now);
  bool sendRemoteMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter = nullptr);
  bool sendPollRequest(uint8_t dstAddress, uint32_t *sentCounter = nullptr);
  RemoteNodeRuntime *findOrCreateRemoteNode(uint8_t address);
};
