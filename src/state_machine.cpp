#include "state_machine.h"

#include "log_buffer.h"

namespace {
constexpr uint8_t kInputPin = 4;
constexpr uint8_t kRelayPin = 5;
constexpr uint8_t kLedPin = 2;
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kTxRelayEchoDelayMs = 500;
constexpr uint32_t kAckRetryScheduleMs[] = {3000, 5000, 8000, 13000, 21000, 34000, 55000};
constexpr uint32_t kMqttRetryScheduleMs[] = {1000, 2000, 3000, 5000, 8000, 13000, 21000, 34000, 55000};
constexpr uint32_t kDefaultRemotePollIntervalMs = 60000;
constexpr uint32_t kMinRemotePollIntervalMs = 60000;
constexpr uint32_t kMaxRemotePollIntervalMs = 3600000;
constexpr uint32_t kRssiGoodIntervalMs = 5000;
constexpr uint32_t kRssiMediumIntervalMs = 3000;
constexpr uint32_t kRssiLowIntervalMs = 1000;
constexpr uint32_t kNoLinkFastIntervalMs = 150;

uint8_t encodeTempCode(bool valid, float celsius) {
  if (!valid || !isfinite(celsius)) return 0xFF;
  int t = static_cast<int>(roundf(celsius));
  if (t < -127) t = -127;
  if (t > 126) t = 126;
  return static_cast<uint8_t>(static_cast<int8_t>(t));
}
}

bool NodeStateMachine::begin(const Settings &cfg, RadioProtocol *radio, LogBuffer *logs) {
  cfg_ = cfg;
  radio_ = radio;
  logs_ = logs;

  pinMode(kLedPin, OUTPUT);
  pinMode(kRelayPin, OUTPUT);
  pinMode(kInputPin, INPUT);

  digitalWrite(kRelayPin, LOW);
  digitalWrite(kLedPin, HIGH);

  input_state_ = localDryContactState();
  last_input_raw_ = input_state_;
  relay_state_ = digitalRead(kRelayPin);

  link_state_ = LinkState::Idle;
  last_heartbeat_ms_ = millis();
  wait_ack_since_ms_ = millis();
  last_led_toggle_ms_ = millis();
  last_packet_ms_ = 0;
  last_packet_rssi_ = -127;
  tx_state_sync_pending_ = cfg_.role_tx && cfg_.tx_input_lora_control_enabled;
  tx_command_pending_ = false;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  rx_push_pending_ = false;
  rx_last_push_ms_ = 0;
  remote_node_count_ = 0;
  for (size_t i = 0; i < kMaxRemoteNodes; ++i) {
    remote_nodes_[i] = RemoteNodeRuntime{};
  }
  memset(last_seen_counter_by_src_, 0, sizeof(last_seen_counter_by_src_));
  return true;
}

void NodeStateMachine::applyConfig(const Settings &cfg) {
  cfg_ = cfg;
  link_state_ = LinkState::Idle;
  wait_ack_since_ms_ = millis();
  last_heartbeat_ms_ = millis();
  tx_ack_pending_ = false;
  tx_state_sync_pending_ = cfg_.role_tx && cfg_.tx_input_lora_control_enabled;
  tx_command_pending_ = false;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  rx_push_pending_ = false;
  rx_last_push_ms_ = 0;
  remote_node_count_ = 0;
  for (size_t i = 0; i < kMaxRemoteNodes; ++i) {
    remote_nodes_[i] = RemoteNodeRuntime{};
  }
  memset(last_seen_counter_by_src_, 0, sizeof(last_seen_counter_by_src_));
}

void NodeStateMachine::tick() {
  tickReceive();

  if (cfg_.role_tx) {
    tickTransmitter();
  } else {
    tickReceiver();
  }

  tickLed();
}

LinkState NodeStateMachine::linkState() const { return link_state_; }
uint8_t NodeStateMachine::relayState() const { return relay_state_; }
uint8_t NodeStateMachine::inputState() const { return input_state_; }
uint8_t NodeStateMachine::localDryContactState() const { return digitalRead(kInputPin) ? 1 : 0; }
int NodeStateMachine::lastPacketRssi() const { return last_packet_rssi_; }
uint32_t NodeStateMachine::lastPacketMs() const { return last_packet_ms_; }
uint32_t NodeStateMachine::lastTxMs() const { return last_tx_ms_; }
void NodeStateMachine::setLocalTemperature(bool valid, float celsius) { local_temp_code_ = encodeTempCode(valid, celsius); }
bool NodeStateMachine::localTemperatureValid() const { return local_temp_code_ != 0xFF; }
float NodeStateMachine::localTemperatureC() const { return static_cast<float>(static_cast<int8_t>(local_temp_code_)); }
bool NodeStateMachine::remoteTemperatureValid() const { return remote_temp_valid_; }
float NodeStateMachine::remoteTemperatureC() const { return static_cast<float>(remote_temp_c_); }
uint32_t NodeStateMachine::remoteTemperatureMs() const { return remote_temp_ms_; }
RxControlSource NodeStateMachine::lastRxControlSource() const { return last_rx_control_source_; }

size_t NodeStateMachine::remoteNodeCount() const { return remote_node_count_; }

bool NodeStateMachine::remoteNodeByIndex(size_t index, RemoteNodeStatusSnapshot &out) const {
  if (index >= remote_node_count_) return false;
  const RemoteNodeRuntime &node = remote_nodes_[index];
  if (!node.in_use) return false;
  out.address = node.address;
  out.relay_state = node.relay_state;
  out.input_state = node.input_state;
  out.temp_valid = node.temp_valid;
  out.temp_c = node.temp_c;
  out.uplink_rssi = node.uplink_rssi;
  out.downlink_rssi_valid = node.downlink_rssi_valid;
  out.downlink_rssi = node.downlink_rssi;
  out.last_seen_ms = node.last_seen_ms;
  out.last_cmd_counter = node.last_cmd_counter;
  out.ack_state = node.ack_state;
  out.poll_interval_ms = node.poll_interval_ms;
  out.last_poll_tx_ms = node.last_poll_tx_ms;
  out.poll_pending = node.poll_pending;
  return true;
}

void NodeStateMachine::mqttSetLocalRelay(uint8_t relayState) {
  relay_state_ = relayState ? 1 : 0;
  digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
  if (logs_) {
    logs_->add("mqtt_local_relay", 0, last_counter_, relay_state_);
  }
}

void NodeStateMachine::sendTxState(MessageType type, uint8_t relayState, uint8_t inputState, const char *logEvent) {
  const uint32_t now = millis();
  last_counter_++;
  if (!radio_->send(type, relayState, inputState, 0, last_counter_, cfg_.local_address, cfg_.remote_address, local_temp_code_)) {
    link_state_ = LinkState::Idle;
    tx_command_pending_ = false;
    tx_retry_step_ = 0;
    tx_next_retry_ms_ = 0;
    return;
  }
  last_tx_ms_ = now;
  wait_ack_since_ms_ = now;
  link_state_ = LinkState::WaitAck;

  tx_command_pending_ = true;
  tx_pending_relay_state_ = relayState ? 1 : 0;
  tx_pending_input_state_ = inputState ? 1 : 0;
  const uint8_t idx = tx_retry_step_ < (sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0]))
                          ? tx_retry_step_
                          : (sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0])) - 1;
  tx_next_retry_ms_ = now + kAckRetryScheduleMs[idx];
  if (tx_retry_step_ < ((sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0])) - 1)) {
    tx_retry_step_++;
  }

  if (logs_ && logEvent != nullptr) {
    logs_->add(logEvent, 0, last_counter_, relayState ? 1 : 0);
  }
}

bool NodeStateMachine::sendRemoteMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter) {
  const uint8_t targetRelay = relayState ? 1 : 0;
  last_counter_++;
  if (!radio_->send(MessageType::Mqtt, targetRelay, input_state_, 0, last_counter_, cfg_.local_address, dstAddress, local_temp_code_)) {
    return false;
  }
  last_tx_ms_ = millis();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  if (logs_) {
    logs_->add("mqtt_remote_relay_tx", 0, last_counter_, targetRelay);
  }
  return true;
}

bool NodeStateMachine::mqttSendRemoteRelay(uint8_t dstAddress, uint8_t relayState) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  RemoteNodeRuntime *node = findOrCreateRemoteNode(dstAddress);
  if (node == nullptr) return false;

  uint32_t sentCounter = 0;
  if (!sendRemoteMqttCommand(dstAddress, relayState, &sentCounter)) {
    return false;
  }

  const uint32_t now = millis();
  if (cfg_.tx_mqtt_remote_polling_enabled && node->poll_interval_ms == 0) {
    uint32_t interval = cfg_.tx_mqtt_remote_default_poll_interval_ms;
    if (interval < kMinRemotePollIntervalMs) interval = kDefaultRemotePollIntervalMs;
    if (interval > kMaxRemotePollIntervalMs) interval = kMaxRemotePollIntervalMs;
    node->poll_interval_ms = interval;
    node->next_poll_ms = now + interval;
  }
  node->pending = true;
  node->pending_relay = relayState ? 1 : 0;
  node->retry_step = 0;
  node->next_retry_ms = now + kMqttRetryScheduleMs[0];
  node->pending_counter = sentCounter;
  node->pending_deadline_ms = now + cfg_.mqtt_remote_retry_timeout_ms;
  node->ack_state = RemoteAckState::Pending;
  node->last_cmd_counter = sentCounter;
  return true;
}

bool NodeStateMachine::mqttSetRemotePollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  RemoteNodeRuntime *node = findOrCreateRemoteNode(dstAddress);
  if (node == nullptr) return false;

  if (pollIntervalMs > 0 && pollIntervalMs < kMinRemotePollIntervalMs) pollIntervalMs = kMinRemotePollIntervalMs;
  if (pollIntervalMs > kMaxRemotePollIntervalMs) pollIntervalMs = kMaxRemotePollIntervalMs;
  node->poll_interval_ms = pollIntervalMs;
  if (pollIntervalMs == 0) {
    node->poll_pending = false;
    node->next_poll_ms = 0;
  } else {
    node->next_poll_ms = millis() + 1000;
  }
  return true;
}

bool NodeStateMachine::mqttPollRemoteNow(uint8_t dstAddress) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  RemoteNodeRuntime *node = findOrCreateRemoteNode(dstAddress);
  if (node == nullptr) return false;

  uint32_t sentCounter = 0;
  if (!sendPollRequest(dstAddress, &sentCounter)) return false;
  const uint32_t now = millis();
  const uint32_t pollResponseDeadlineMs = (cfg_.ack_timeout_ms >= 2000U) ? cfg_.ack_timeout_ms : 2000U;
  node->poll_pending = true;
  node->poll_counter = sentCounter;
  node->poll_deadline_ms = now + pollResponseDeadlineMs;
  node->last_poll_tx_ms = now;
  if (node->poll_interval_ms > 0) {
    node->next_poll_ms = now + node->poll_interval_ms;
  }
  return true;
}

bool NodeStateMachine::mqttForgetRemote(uint8_t dstAddress) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  size_t idx = kMaxRemoteNodes;
  for (size_t i = 0; i < remote_node_count_; ++i) {
    if (remote_nodes_[i].in_use && remote_nodes_[i].address == dstAddress) {
      idx = i;
      break;
    }
  }
  if (idx >= remote_node_count_) return false;

  for (size_t i = idx; i + 1 < remote_node_count_; ++i) {
    remote_nodes_[i] = remote_nodes_[i + 1];
  }
  if (remote_node_count_ > 0) {
    remote_node_count_--;
    remote_nodes_[remote_node_count_] = RemoteNodeRuntime{};
  }
  if (logs_) {
    logs_->add("mqtt_remote_forget", 0, 0, dstAddress);
  }
  return true;
}

NodeStateMachine::RemoteNodeRuntime *NodeStateMachine::findOrCreateRemoteNode(uint8_t address) {
  for (size_t i = 0; i < remote_node_count_; ++i) {
    if (remote_nodes_[i].in_use && remote_nodes_[i].address == address) {
      return &remote_nodes_[i];
    }
  }
  if (remote_node_count_ >= kMaxRemoteNodes) {
    return nullptr;
  }
  RemoteNodeRuntime &node = remote_nodes_[remote_node_count_++];
  node = RemoteNodeRuntime{};
  node.in_use = true;
  node.address = address;
  uint32_t interval = cfg_.tx_mqtt_remote_default_poll_interval_ms;
  if (interval < kMinRemotePollIntervalMs) interval = kDefaultRemotePollIntervalMs;
  if (interval > kMaxRemotePollIntervalMs) interval = kMaxRemotePollIntervalMs;
  if (!cfg_.tx_mqtt_remote_polling_enabled) {
    interval = 0;
  }
  node.poll_interval_ms = interval;
  node.next_poll_ms = (interval > 0) ? (millis() + interval) : 0;
  return &node;
}

bool NodeStateMachine::sendPollRequest(uint8_t dstAddress, uint32_t *sentCounter) {
  last_counter_++;
  if (!radio_->send(MessageType::PollRequest, 0, input_state_, 0, last_counter_, cfg_.local_address, dstAddress, local_temp_code_)) {
    return false;
  }
  last_tx_ms_ = millis();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  if (logs_) {
    logs_->add("tx_poll_request", 0, last_counter_, 0);
  }
  return true;
}

void NodeStateMachine::tickRemoteMqttCommands(uint32_t now) {
  if (!cfg_.role_tx) return;
  for (size_t i = 0; i < remote_node_count_; ++i) {
    RemoteNodeRuntime &node = remote_nodes_[i];
    if (!node.in_use || !node.pending) continue;

    if (static_cast<int32_t>(now - node.pending_deadline_ms) >= 0) {
      node.pending = false;
      node.ack_state = RemoteAckState::Timeout;
      if (logs_) {
        logs_->add("mqtt_remote_ack_timeout", 0, node.pending_counter, node.pending_relay);
      }
      continue;
    }

    if (static_cast<int32_t>(now - node.next_retry_ms) < 0) {
      continue;
    }

    uint32_t sentCounter = 0;
    if (sendRemoteMqttCommand(node.address, node.pending_relay, &sentCounter)) {
      const uint8_t idx = node.retry_step < (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0]))
                              ? node.retry_step
                              : (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1;
      node.next_retry_ms = now + kMqttRetryScheduleMs[idx];
      if (node.retry_step < ((sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1)) {
        node.retry_step++;
      }
      node.pending_counter = sentCounter;
      node.last_cmd_counter = sentCounter;
      node.ack_state = RemoteAckState::Pending;
    } else if (logs_) {
      logs_->add("mqtt_remote_retry_send_fail", 0, node.pending_counter, node.pending_relay);
    }
  }
}

void NodeStateMachine::tickRemotePolling(uint32_t now) {
  if (!cfg_.role_tx) return;
  if (!cfg_.tx_mqtt_remote_polling_enabled) {
    for (size_t i = 0; i < remote_node_count_; ++i) {
      RemoteNodeRuntime &node = remote_nodes_[i];
      if (!node.in_use) continue;
      node.poll_pending = false;
      node.next_poll_ms = 0;
    }
    return;
  }
  const uint32_t pollResponseDeadlineMs = (cfg_.ack_timeout_ms >= 2000U) ? cfg_.ack_timeout_ms : 2000U;
  for (size_t i = 0; i < remote_node_count_; ++i) {
    RemoteNodeRuntime &node = remote_nodes_[i];
    if (!node.in_use || node.poll_interval_ms == 0) continue;

    if (node.poll_pending && static_cast<int32_t>(now - node.poll_deadline_ms) >= 0) {
      node.poll_pending = false;
      if (logs_) logs_->add("tx_poll_timeout", 0, node.poll_counter, 0);
      node.next_poll_ms = now + node.poll_interval_ms;
    }

    // Keep exactly one in-flight poll per node to avoid overlap ambiguity.
    if (node.poll_pending) {
      continue;
    }

    if (static_cast<int32_t>(now - node.next_poll_ms) < 0) {
      continue;
    }

    uint32_t sentCounter = 0;
    if (sendPollRequest(node.address, &sentCounter)) {
      node.poll_pending = true;
      node.poll_counter = sentCounter;
      node.poll_deadline_ms = now + pollResponseDeadlineMs;
      node.last_poll_tx_ms = now;
      node.next_poll_ms = now + node.poll_interval_ms;
    } else {
      // Retry soon if radio send fails.
      node.next_poll_ms = now + 1000U;
    }
  }
}

void NodeStateMachine::tickTransmitter() {
  const uint32_t now = millis();

  if (tx_ack_pending_ && static_cast<int32_t>(now - tx_ack_apply_ms_) >= 0) {
    relay_state_ = tx_ack_relay_state_;
    digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
    tx_ack_pending_ = false;
  }

  int inputLogical = static_cast<int>(localDryContactState());
  if (inputLogical != last_input_raw_) {
    last_debounce_ms_ = now;
    last_input_raw_ = inputLogical;
  }

  if ((now - last_debounce_ms_) > kDebounceMs && inputLogical != input_state_) {
    input_state_ = static_cast<uint8_t>(inputLogical);
    if (cfg_.tx_input_lora_control_enabled) {
      tx_state_sync_pending_ = false;
      tx_retry_step_ = 0;
      sendTxState(MessageType::Change, input_state_, input_state_, "tx_change");
    }
  }

  if (cfg_.tx_input_lora_control_enabled && tx_state_sync_pending_ && !tx_command_pending_) {
    tx_state_sync_pending_ = false;
    tx_retry_step_ = 0;
    sendTxState(MessageType::Change, input_state_, input_state_, "tx_start_sync");
  }

  if (cfg_.tx_input_lora_control_enabled && tx_command_pending_ && static_cast<int32_t>(now - tx_next_retry_ms_) >= 0) {
    sendTxState(MessageType::Change, tx_pending_relay_state_, tx_pending_input_state_, "tx_retry");
  }

  if (cfg_.tx_input_lora_control_enabled && (now - last_heartbeat_ms_) >= cfg_.heartbeat_ms) {
    last_heartbeat_ms_ = now;
    last_counter_++;
    if (radio_->send(MessageType::Heartbeat, input_state_, input_state_, 0, last_counter_, cfg_.local_address, cfg_.remote_address,
                     local_temp_code_)) {
      last_tx_ms_ = now;
      if (logs_) {
        logs_->add("tx_heartbeat", 0, last_counter_, input_state_);
      }
    }
  }

  tickRemoteMqttCommands(now);
  tickRemotePolling(now);

  if (link_state_ == LinkState::WaitAck && (now - wait_ack_since_ms_) >= cfg_.ack_timeout_ms) {
    relay_state_ = 0;
    digitalWrite(kRelayPin, LOW);
    link_state_ = LinkState::Timeout;
    if (logs_) {
      logs_->add("tx_ack_timeout", 0, last_counter_, relay_state_);
    }
  }
}

void NodeStateMachine::tickReceiver() {
  const uint32_t now = millis();
  int inputLogical = static_cast<int>(localDryContactState());
  if (inputLogical != last_input_raw_) {
    last_debounce_ms_ = now;
    last_input_raw_ = inputLogical;
  }

  if ((now - last_debounce_ms_) > kDebounceMs && inputLogical != input_state_) {
    input_state_ = static_cast<uint8_t>(inputLogical);
    rx_push_pending_ = true;
  }

  if (!cfg_.rx_push_on_change_enabled || !rx_push_pending_) {
    return;
  }
  if (cfg_.remote_address == 0 || cfg_.remote_address == 255) {
    return;
  }

  const uint32_t minIntervalMs = cfg_.rx_push_min_interval_ms < 60000U ? 60000U : cfg_.rx_push_min_interval_ms;
  const bool firstPush = (rx_last_push_ms_ == 0);
  if (!firstPush && (now - rx_last_push_ms_) < minIntervalMs) {
    return;
  }

  last_counter_++;
  if (radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), 0, last_counter_, cfg_.local_address,
                   cfg_.remote_address, local_temp_code_)) {
    last_tx_ms_ = now;
    rx_last_push_ms_ = now;
    rx_push_pending_ = false;
    if (logs_) logs_->add("rx_push_on_change", 0, last_counter_, input_state_);
  }
}

void NodeStateMachine::tickReceive() {
  ProtocolMessage msg{};
  if (!radio_->receive(msg)) return;

  if (msg.dst != cfg_.local_address) {
    if (logs_) logs_->add("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }

  if (cfg_.role_tx) {
    const bool fromPaired = (msg.src == cfg_.remote_address);
    const bool mqttStatus = (msg.type == MessageType::MqttStatus);
    const bool pollResponse = (msg.type == MessageType::PollResponse);
    if (!mqttStatus && !pollResponse && !fromPaired) {
      if (logs_) logs_->add("rx_wrong_source", msg.rssi, msg.counter, msg.relay_state);
      return;
    }
    if (msg.type == MessageType::Ack && !fromPaired) {
      if (logs_) logs_->add("rx_wrong_source", msg.rssi, msg.counter, msg.relay_state);
      return;
    }
  } else if (msg.src != cfg_.remote_address) {
    if (logs_) logs_->add("rx_filtered_source", msg.rssi, msg.counter, msg.relay_state);
    return;
  }

  if (msg.counter <= last_seen_counter_by_src_[msg.src]) {
    if (logs_) logs_->add("rx_replay_drop", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  last_seen_counter_by_src_[msg.src] = msg.counter;
  last_packet_ms_ = millis();
  last_packet_rssi_ = msg.rssi;
  captureRemoteTemp(msg.temp_code);

  if (cfg_.role_tx) {
    if (msg.type == MessageType::Ack) {
      tx_ack_pending_ = true;
      tx_ack_apply_ms_ = millis() + kTxRelayEchoDelayMs;
      tx_ack_relay_state_ = msg.relay_state;
      tx_command_pending_ = false;
      tx_retry_step_ = 0;
      tx_next_retry_ms_ = 0;
      link_state_ = LinkState::Idle;
      if (logs_) logs_->add("tx_ack", msg.rssi, msg.counter, msg.relay_state);
      return;
    }

    if (msg.type == MessageType::MqttStatus || msg.type == MessageType::PollResponse) {
      RemoteNodeRuntime *node = findOrCreateRemoteNode(msg.src);
      if (node == nullptr) {
        if (logs_) logs_->add("mqtt_remote_node_limit", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      node->relay_state = msg.relay_state ? 1 : 0;
      // Prefer explicit digital sensor payload when present; fallback to legacy input byte.
      const bool digitalPresent = (msg.sensor_mask & 0x01U) != 0U;
      if (digitalPresent && msg.sensor_digital0 != 0xFFU) {
        node->input_state = (msg.sensor_digital0 != 0U) ? 1 : 0;
      } else {
        node->input_state = msg.input_state ? 1 : 0;
      }
      node->uplink_rssi = msg.rssi;
      node->last_seen_ms = millis();
      node->last_cmd_counter = msg.counter;
      if (msg.temp_code != 0xFF) {
        node->temp_valid = true;
        node->temp_c = static_cast<int8_t>(msg.temp_code);
      }
      if ((msg.sensor_mask & 0x04) != 0) {
        node->downlink_rssi_valid = true;
        node->downlink_rssi = static_cast<int>(static_cast<int16_t>(msg.sensor_analog0));
      }
      if (msg.type == MessageType::MqttStatus) {
        // Retries can overlap and responses may arrive out of order.
        // Any valid status from this node confirms link health and should unblock polling.
        if (node->pending) {
          node->pending = false;
          if (logs_) logs_->add("mqtt_remote_ack_ok", msg.rssi, msg.counter, msg.relay_state);
        }
        node->ack_state = RemoteAckState::Ok;
        if (logs_) logs_->add("mqtt_remote_status_rx", msg.rssi, msg.counter, msg.relay_state);
      } else {
        // Clear pending on any valid response from this node; retries can overlap counters.
        node->poll_pending = false;
        node->next_poll_ms = millis() + node->poll_interval_ms;
        if (logs_) logs_->add("tx_poll_response", msg.rssi, msg.counter, msg.relay_state);
      }
    }
    return;
  }

  if (msg.type == MessageType::PollRequest) {
    const uint8_t sensorMask = 0x04;  // includes downlink RSSI in sensor_analog0
    const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
    last_counter_++;
    radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), 0, last_counter_, cfg_.local_address, msg.src,
                 local_temp_code_, sensorMask, 0xFF, downlinkRssiEnc);
    last_tx_ms_ = millis();
    if (logs_) {
      logs_->add("rx_poll_response_tx", msg.rssi, last_counter_, relay_state_);
    }
    return;
  }

  if (msg.type == MessageType::Change || msg.type == MessageType::Heartbeat || msg.type == MessageType::Mqtt) {
    relay_state_ = msg.relay_state;
    input_state_ = msg.input_state;
    last_rx_control_source_ = (msg.type == MessageType::Mqtt) ? RxControlSource::Mqtt : RxControlSource::LoRa;
    digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
    if (msg.type != MessageType::Mqtt) {
      last_counter_++;
      radio_->send(MessageType::Ack, relay_state_, input_state_, 0, last_counter_, cfg_.local_address, msg.src, local_temp_code_);
      last_tx_ms_ = millis();
    } else {
      const uint8_t sensorMask = 0x04;  // includes downlink RSSI in sensor_analog0
      const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
      last_counter_++;
      radio_->send(MessageType::MqttStatus, relay_state_, localDryContactState(), 0, last_counter_, cfg_.local_address, msg.src,
                   local_temp_code_, sensorMask, 0xFF, downlinkRssiEnc);
      last_tx_ms_ = millis();
    }

    if (logs_) {
      logs_->add(msg.type == MessageType::Mqtt ? "rx_apply_mqtt" : "rx_apply_and_ack", msg.rssi, msg.counter, msg.relay_state);
    }
  }
}

void NodeStateMachine::captureRemoteTemp(uint8_t tempCode) {
  if (tempCode == 0xFF) return;
  remote_temp_valid_ = true;
  remote_temp_c_ = static_cast<int8_t>(tempCode);
  remote_temp_ms_ = millis();
}

void NodeStateMachine::tickLed() {
  const uint32_t now = millis();
  uint32_t blinkInterval = kNoLinkFastIntervalMs;

  if (last_packet_ms_ != 0 && (now - last_packet_ms_) <= (cfg_.heartbeat_ms * 2U)) {
    if (last_packet_rssi_ >= -80) {
      blinkInterval = kRssiGoodIntervalMs;
    } else if (last_packet_rssi_ >= -100) {
      blinkInterval = kRssiMediumIntervalMs;
    } else {
      blinkInterval = kRssiLowIntervalMs;
    }
  }

  if ((now - last_led_toggle_ms_) >= blinkInterval) {
    last_led_toggle_ms_ = now;
    led_on_ = !led_on_;
    digitalWrite(kLedPin, led_on_ ? LOW : HIGH);
  }
}
