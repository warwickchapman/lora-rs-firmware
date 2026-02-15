#include "state_machine.h"

#include "log_buffer.h"

namespace {
constexpr uint8_t kInputPin = 4;
constexpr uint8_t kRelayPin = 5;
constexpr uint8_t kLedPin = 2;
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kTxRelayEchoDelayMs = 500;
constexpr uint32_t kAckRetryScheduleMs[] = {3000, 5000, 8000, 13000, 21000, 34000, 55000};
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
uint8_t NodeStateMachine::localDryContactState() const {
  return digitalRead(kInputPin) ? 1 : 0;
}
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
  radio_->send(type, relayState, inputState, 0, last_counter_, cfg_.local_address, cfg_.remote_address, local_temp_code_);
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

bool NodeStateMachine::mqttSendRemoteRelay(uint8_t dstAddress, uint8_t relayState) {
  if (!cfg_.role_tx) {
    return false;
  }

  last_counter_++;
  radio_->send(MessageType::Mqtt, relayState ? 1 : 0, input_state_, 0, last_counter_, cfg_.local_address, dstAddress, local_temp_code_);
  last_tx_ms_ = millis();
  if (logs_) {
    logs_->add("mqtt_remote_relay_tx", 0, last_counter_, relayState ? 1 : 0);
  }
  return true;
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

  if ((now - last_heartbeat_ms_) >= cfg_.heartbeat_ms) {
    last_heartbeat_ms_ = now;
    last_counter_++;
    const uint8_t heartbeatRelay = cfg_.tx_input_lora_control_enabled ? input_state_ : relay_state_;
    radio_->send(MessageType::Heartbeat, heartbeatRelay, input_state_, 0, last_counter_, cfg_.local_address, cfg_.remote_address,
                 local_temp_code_);
    last_tx_ms_ = now;
    if (logs_) {
      logs_->add("tx_heartbeat", 0, last_counter_, input_state_);
    }
  }

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
  // RX behavior is mostly event-driven from packet processing.
}

void NodeStateMachine::tickReceive() {
  ProtocolMessage msg{};
  if (!radio_->receive(msg)) {
    return;
  }

  if (msg.dst != cfg_.local_address) {
    if (logs_) {
      logs_->add("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    }
    return;
  }

  if (cfg_.role_tx && msg.src != cfg_.remote_address) {
    if (logs_) {
      logs_->add("rx_wrong_source", msg.rssi, msg.counter, msg.relay_state);
    }
    return;
  }

  if (!cfg_.role_tx && msg.src != cfg_.remote_address) {
    if (logs_) {
      logs_->add("rx_filtered_source", msg.rssi, msg.counter, msg.relay_state);
    }
    return;
  }

  if (msg.counter <= last_seen_counter_by_src_[msg.src]) {
    if (logs_) {
      logs_->add("rx_replay_drop", msg.rssi, msg.counter, msg.relay_state);
    }
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
      if (logs_) {
        logs_->add("tx_ack", msg.rssi, msg.counter, msg.relay_state);
      }
    }
    return;
  }

  if (msg.type == MessageType::Change || msg.type == MessageType::Heartbeat || msg.type == MessageType::Mqtt) {
    relay_state_ = msg.relay_state;
    input_state_ = msg.input_state;
    last_rx_control_source_ = (msg.type == MessageType::Mqtt) ? RxControlSource::Mqtt : RxControlSource::LoRa;
    digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
    if (msg.type != MessageType::Mqtt) {
      radio_->send(MessageType::Ack, relay_state_, input_state_, 0, msg.counter, cfg_.local_address, msg.src, local_temp_code_);
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
