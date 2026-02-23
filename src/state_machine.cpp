#include "state_machine.h"

#include <ESP8266WiFi.h>

#include "build_info.h"
#include "log_buffer.h"
#include "logger.h"

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
constexpr uint32_t kMinValidUnixTimeS = 1704067200UL;  // 2024-01-01 UTC
constexpr uint8_t kFlagTimeAuthoritative = 0x01;
constexpr uint8_t kWifiProvisionOpStart = 1;
constexpr uint8_t kWifiProvisionOpData = 2;
constexpr uint8_t kWifiProvisionOpCommit = 3;
constexpr uint8_t kWifiProvisionChunkDataBytes = 7;
constexpr uint8_t kWifiProvisionBroadcastAddress = 255;
constexpr uint8_t kFactoryResetMagic0 = 0xA5;
constexpr uint8_t kFactoryResetMagic1 = 0x5A;
constexpr uint8_t kFactoryResetKeepFleetFlag = 0x01;
constexpr uint32_t kWifiProvisionCooldownMs = 60000;
constexpr uint8_t kProvOpDiscoverStart = 1;
constexpr uint8_t kProvOpAnnounce = 2;
constexpr uint8_t kProvOpAssign = 3;
constexpr uint8_t kProvOpKeyStart = 4;
constexpr uint8_t kProvOpKeyData = 5;
constexpr uint8_t kProvOpKeyCommit = 6;
constexpr uint8_t kProvOpApplyCommit = 7;
constexpr uint8_t kProvOpVerify = 8;
constexpr uint8_t kProvHwModelLrs = 1;
constexpr uint8_t kProvHwRevA1 = 0xA1;
constexpr uint8_t kProvRoleTxFlag = 0x01;
constexpr uint32_t kProvVerifyTimeoutMs = 15000;
constexpr uint8_t kProvMaxRetriesPerNode = 1;
constexpr uint8_t kProvKeyChunkBytes = 3;
constexpr uint8_t kProvBroadcastAddress = 255;
constexpr size_t kProvChunkBitmapMax = 31;
constexpr uint32_t kStartupPhaseTraceWindowMs = 15000;

inline void startupTxPhaseTrace(const char *phase) {
  const uint32_t now = millis();
  if (now > kStartupPhaseTraceWindowMs) return;
  static const char *lastPhase = nullptr;
  static uint32_t lastPhaseLogMs = 0;
  // Startup DEBUG traces are diagnostic-only; suppress repeated phase spam.
  if (lastPhase == phase && static_cast<uint32_t>(now - lastPhaseLogMs) < 250U) return;
  lastPhase = phase;
  lastPhaseLogMs = now;
  LRS_LOGD(LORA, "event=startup_tx_phase phase=%s ms=%lu", phase, static_cast<unsigned long>(now));
}

inline void startupTxSendTimingTrace(const char *phase, uint32_t startMs) {
  if (startMs > kStartupPhaseTraceWindowMs) return;
  const uint32_t endMs = millis();
  LRS_LOGD(LORA, "event=startup_tx_phase phase=%s ms=%lu dur_ms=%lu", phase, static_cast<unsigned long>(endMs),
           static_cast<unsigned long>(endMs - startMs));
}

bool isDefaultDeploymentKey(const String &v) {
  String key = v;
  key.trim();
  return key == "lora-default-passphrase";
}

uint16_t crc16Ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void encodeU16LE(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
}

uint16_t decodeU16LE(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void encodeU32LE(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFU);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFU);
}

uint32_t decodeU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void parseFwVersionPacked(uint8_t &major, uint8_t &minor, uint8_t &patch) {
  major = 0;
  minor = 0;
  patch = 0;
  const String s = String(LRS_FW_VERSION);
  int part = 0;
  int value = 0;
  bool inDigits = false;
  for (int i = 0; i < s.length(); ++i) {
    const char c = s[i];
    if (c >= '0' && c <= '9') {
      inDigits = true;
      value = (value * 10) + (c - '0');
      if (value > 255) value = 255;
      continue;
    }
    if (inDigits) {
      if (part == 0)
        major = static_cast<uint8_t>(value);
      else if (part == 1)
        minor = static_cast<uint8_t>(value);
      else if (part == 2) {
        patch = static_cast<uint8_t>(value);
        return;
      }
      part++;
      value = 0;
      inDigits = false;
    }
    if (c == '-') break;
  }
  if (inDigits) {
    if (part == 0)
      major = static_cast<uint8_t>(value);
    else if (part == 1)
      minor = static_cast<uint8_t>(value);
    else if (part == 2)
      patch = static_cast<uint8_t>(value);
  }
}

uint32_t fnv1a32(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h;
}

void fillProvisionPayloadBytes(const ProtocolMessage &msg, uint8_t out[7]) {
  out[0] = msg.sensor_digital0;
  out[1] = static_cast<uint8_t>(msg.sensor_analog0 & 0xFFU);
  out[2] = static_cast<uint8_t>((msg.sensor_analog0 >> 8) & 0xFFU);
  out[3] = static_cast<uint8_t>(msg.unix_time_s & 0xFFU);
  out[4] = static_cast<uint8_t>((msg.unix_time_s >> 8) & 0xFFU);
  out[5] = static_cast<uint8_t>((msg.unix_time_s >> 16) & 0xFFU);
  out[6] = static_cast<uint8_t>((msg.unix_time_s >> 24) & 0xFFU);
}

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
  last_wifi_prov_tx_ms_ = 0;
  tx_state_sync_pending_ = cfg_.role_tx && cfg_.tx_input_lora_control_enabled;
  tx_command_pending_ = false;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  rx_push_pending_ = false;
  rx_last_push_ms_ = 0;
  last_wifi_prov_tx_ms_ = 0;
  peer_count_ = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    peers_[i] = PeerRuntime{};
  }
  wifi_prov_rx_ = WifiProvisionRxTransfer{};
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_pending_src_ = 0;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_key_ = "";
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  prov_device_count_ = 0;
  for (size_t i = 0; i < kMaxProvisioningDevices; ++i) prov_devices_[i] = ProvisioningDevice{};
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
  peer_count_ = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    peers_[i] = PeerRuntime{};
  }
  wifi_prov_rx_ = WifiProvisionRxTransfer{};
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_pending_src_ = 0;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_key_ = "";
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  prov_device_count_ = 0;
  for (size_t i = 0; i < kMaxProvisioningDevices; ++i) prov_devices_[i] = ProvisioningDevice{};
  memset(last_seen_counter_by_src_, 0, sizeof(last_seen_counter_by_src_));
}

void NodeStateMachine::tick() {
  resetRadioTxBudgetForTick();
  tickReceive();

  if (cfg_.role_tx) {
    tickTransmitter();
  } else {
    tickReceiver();
  }
  tickProvisioningTarget(millis());

  tickLed();
  finishRadioTxBudgetForTick();
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
void NodeStateMachine::setAuthoritativeUnixTime(uint32_t unixTimeS) {
  if (unixTimeS < kMinValidUnixTimeS) return;
  shared_time_valid_ = true;
  shared_time_authoritative_ = true;
  shared_time_sync_unix_s_ = unixTimeS;
  shared_time_sync_ms_ = millis();
  if (logs_) logs_->add("time_sync_ntp", 0, unixTimeS, 0);
}
bool NodeStateMachine::sharedUnixTimeValid() const { return shared_time_valid_; }
uint32_t NodeStateMachine::sharedUnixTime() const { return currentUnixTimeS(millis()); }
RxControlSource NodeStateMachine::lastRxControlSource() const { return last_rx_control_source_; }

size_t NodeStateMachine::peerCount() const { return peer_count_; }

bool NodeStateMachine::peerByIndex(size_t index, PeerStatusSnapshot &out) const {
  if (index >= peer_count_) return false;
  const PeerRuntime &node = peers_[index];
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
  const uint32_t unixTimeS = currentUnixTimeS(now);
  yield();  // Feed ESP8266 watchdog before retry/start-sync LoRa sends during startup loops.
  if (!radio_->send(type, relayState, inputState, txFlags(), last_counter_, cfg_.local_address, cfg_.remote_address, local_temp_code_,
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    link_state_ = LinkState::Idle;
    tx_command_pending_ = false;
    tx_retry_step_ = 0;
    tx_next_retry_ms_ = 0;
    return;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
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
  yield();  // LoRa send path yields too, but yield again after scheduling/logging to avoid tight retry loops.
}

bool NodeStateMachine::sendPeerMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter) {
  if (!radioTxBudgetAvailable()) return false;
  const uint8_t targetRelay = relayState ? 1 : 0;
  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::Mqtt, targetRelay, input_state_, txFlags(), last_counter_, cfg_.local_address, dstAddress,
                    local_temp_code_,
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  if (logs_) {
    logs_->add("mqtt_remote_relay_tx", 0, last_counter_, targetRelay);
  }
  return true;
}

bool NodeStateMachine::mqttSendPeerRelay(uint8_t dstAddress, uint8_t relayState) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
  if (node == nullptr) return false;

  uint32_t sentCounter = 0;
  if (!sendPeerMqttCommand(dstAddress, relayState, &sentCounter)) {
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
  node->ack_state = PeerAckState::Pending;
  node->last_cmd_counter = sentCounter;
  return true;
}

bool NodeStateMachine::mqttSetPeerPollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
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

bool NodeStateMachine::mqttPollPeerNow(uint8_t dstAddress) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
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

bool NodeStateMachine::mqttForgetPeer(uint8_t dstAddress) {
  if (!cfg_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  size_t idx = kMaxPeers;
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == dstAddress) {
      idx = i;
      break;
    }
  }
  if (idx >= peer_count_) return false;

  for (size_t i = idx; i + 1 < peer_count_; ++i) {
    peers_[i] = peers_[i + 1];
  }
  if (peer_count_ > 0) {
    peer_count_--;
    peers_[peer_count_] = PeerRuntime{};
  }
  if (logs_) {
    logs_->add("mqtt_remote_forget", 0, 0, dstAddress);
  }
  return true;
}

bool NodeStateMachine::sendFleetWifiProvision(const String &ssid, const String &password) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  if (fleetWifiProvisionCooldownRemainingMs() > 0) return false;
  if (ssid.length() == 0 || ssid.length() > 32) return false;
  if (password.length() > 64) return false;

  const size_t totalLen = static_cast<size_t>(ssid.length() + password.length());
  if (totalLen == 0 || totalLen > sizeof(wifi_prov_rx_.data)) return false;
  const uint8_t totalChunks =
      static_cast<uint8_t>((totalLen + (kWifiProvisionChunkDataBytes - 1U)) / kWifiProvisionChunkDataBytes);
  if (totalChunks == 0 || totalChunks > 31U) return false;

  uint8_t data[96]{};
  size_t pos = 0;
  for (size_t i = 0; i < static_cast<size_t>(ssid.length()); ++i) data[pos++] = static_cast<uint8_t>(ssid[i]);
  for (size_t i = 0; i < static_cast<size_t>(password.length()); ++i) data[pos++] = static_cast<uint8_t>(password[i]);
  const uint32_t hash = fnv1a32(data, totalLen);

  uint8_t transferId = static_cast<uint8_t>((millis() ^ last_counter_ ^ cfg_.local_address) & 0xFFU);
  if (transferId == 0) transferId = 1;

  uint8_t payload[12]{};
  payload[0] = kWifiProvisionOpStart;
  payload[1] = transferId;
  payload[3] = totalChunks;
  payload[4] = static_cast<uint8_t>(ssid.length());
  payload[5] = static_cast<uint8_t>(password.length());
  payload[6] = static_cast<uint8_t>(hash & 0xFFU);
  payload[7] = static_cast<uint8_t>((hash >> 8) & 0xFFU);
  payload[8] = static_cast<uint8_t>((hash >> 16) & 0xFFU);
  payload[9] = static_cast<uint8_t>((hash >> 24) & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, cfg_.local_address, kWifiProvisionBroadcastAddress, payload)) {
    return false;
  }

  for (uint8_t idx = 0; idx < totalChunks; ++idx) {
    memset(payload, 0, sizeof(payload));
    payload[0] = kWifiProvisionOpData;
    payload[1] = transferId;
    payload[2] = idx;
    payload[3] = totalChunks;
    const size_t offset = static_cast<size_t>(idx) * kWifiProvisionChunkDataBytes;
    size_t chunkLen = totalLen - offset;
    if (chunkLen > kWifiProvisionChunkDataBytes) chunkLen = kWifiProvisionChunkDataBytes;
    payload[4] = static_cast<uint8_t>(chunkLen);
    memcpy(payload + 5, data + offset, chunkLen);
    last_counter_++;
    if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, cfg_.local_address, kWifiProvisionBroadcastAddress, payload)) {
      return false;
    }
  }

  memset(payload, 0, sizeof(payload));
  payload[0] = kWifiProvisionOpCommit;
  payload[1] = transferId;
  payload[3] = totalChunks;
  last_counter_++;
  if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, cfg_.local_address, kWifiProvisionBroadcastAddress, payload)) {
    return false;
  }

  const uint32_t sentAt = millis();
  last_tx_ms_ = sentAt;
  markRadioTxSentThisTick();
  last_wifi_prov_tx_ms_ = sentAt;
  if (logs_) logs_->add("wifi_prov_tx", 0, last_counter_, totalChunks);
  return true;
}

bool NodeStateMachine::consumePendingWifiProvision(String &ssid, String &password, uint8_t &src) {
  if (!wifi_prov_pending_) return false;
  ssid = wifi_prov_pending_ssid_;
  password = wifi_prov_pending_password_;
  src = wifi_prov_pending_src_;
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::hasPendingWifiProvision() const { return wifi_prov_pending_; }

uint32_t NodeStateMachine::fleetWifiProvisionCooldownRemainingMs() const {
  if (last_wifi_prov_tx_ms_ == 0) return 0;
  const uint32_t now = millis();
  const uint32_t elapsed = now - last_wifi_prov_tx_ms_;
  if (elapsed >= kWifiProvisionCooldownMs) return 0;
  return kWifiProvisionCooldownMs - elapsed;
}

bool NodeStateMachine::sendPeerFactoryReset(uint8_t dstAddress, bool keepSharedFleetKey) {
  if (!radioTxBudgetAvailable()) return false;
  if (!cfg_.role_tx) return false;
  if (radio_ == nullptr) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  uint8_t payload[12]{};
  payload[0] = kFactoryResetMagic0;
  payload[1] = kFactoryResetMagic1;
  payload[2] = keepSharedFleetKey ? kFactoryResetKeepFleetFlag : 0;
  payload[3] = static_cast<uint8_t>(cfg_.local_address);
  payload[4] = static_cast<uint8_t>(millis() & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::FactoryReset, last_counter_, cfg_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (logs_) logs_->add(keepSharedFleetKey ? "factory_reset_peer_tx_keep" : "factory_reset_peer_tx_full", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::consumePendingFactoryReset(bool &keepSharedFleetKey, uint8_t &src) {
  if (!factory_reset_pending_) return false;
  keepSharedFleetKey = factory_reset_keep_fleet_pending_;
  src = factory_reset_pending_src_;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::isDefaultFleetKey() const { return isDefaultDeploymentKey(cfg_.fleet_passphrase); }

bool NodeStateMachine::provisioningStartDiscovery(uint16_t estimatedCount, bool retryOnce) {
  if (!cfg_.role_tx || radio_ == nullptr) return false;
  if (estimatedCount == 0) estimatedCount = 1;
  if (estimatedCount > 250) estimatedCount = 250;
  if (isDefaultFleetKey()) return false;  // coordinator must have a production key

  prov_ = ProvisioningSessionRuntime{};
  prov_.active = true;
  prov_.state = ProvisioningSessionState::Discovering;
  prov_.session_nonce = static_cast<uint16_t>((millis() ^ last_counter_ ^ cfg_.local_address ^ random(1, 65535)) & 0xFFFFU);
  if (prov_.session_nonce == 0) prov_.session_nonce = 1;
  prov_.estimated_count = estimatedCount;
  prov_.started_ms = millis();
  prov_.retry_enabled = retryOnce;
  prov_.pause_normal_tx = true;
  prov_.phase_deadline_ms = prov_.started_ms + (10000UL + static_cast<uint32_t>(estimatedCount - 1) * 500UL);
  const uint32_t cap = prov_.started_ms + 120000UL;
  if (prov_.phase_deadline_ms > cap) prov_.phase_deadline_ms = cap;
  prov_.provision_all_requested = false;
  prov_.current_index = 0;
  prov_device_count_ = 0;
  for (size_t i = 0; i < kMaxProvisioningDevices; ++i) prov_devices_[i] = ProvisioningDevice{};

  uint8_t payload[12]{};
  payload[0] = kProvOpDiscoverStart;
  encodeU16LE(payload + 1, prov_.session_nonce);
  const uint16_t windowTicks = static_cast<uint16_t>((prov_.phase_deadline_ms - prov_.started_ms) / 100U);
  encodeU16LE(payload + 3, windowTicks);
  payload[5] = retryOnce ? 1 : 0;
  last_counter_++;
  if (!radio_->sendProvisioningRaw(last_counter_, cfg_.local_address, kProvBroadcastAddress, payload, true)) {
    prov_ = ProvisioningSessionRuntime{};
    return false;
  }
  if (logs_) logs_->add("prov_discover_start", 0, prov_.session_nonce, estimatedCount);
  return true;
}

bool NodeStateMachine::provisioningStartProvisionAll() {
  if (!prov_.active) return false;
  if (!(prov_.state == ProvisioningSessionState::Ready || prov_.state == ProvisioningSessionState::Complete ||
        prov_.state == ProvisioningSessionState::Error)) {
    return false;
  }
  recomputeProvisioningConflictsAndAssignments();
  prov_.provision_all_requested = true;
  prov_.state = ProvisioningSessionState::Provisioning;
  prov_.current_index = 0;
  prov_.phase_deadline_ms = millis();
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (!prov_devices_[i].in_use) continue;
    prov_devices_[i].selected = true;
    if (prov_devices_[i].state == ProvisioningDeviceState::Verified) continue;
    prov_devices_[i].state = ProvisioningDeviceState::Discovered;
    prov_devices_[i].retries = 0;
    prov_devices_[i].key_total_chunks = 0;
    prov_devices_[i].key_len = 0;
    prov_devices_[i].key_crc16 = 0;
    prov_devices_[i].key_next_chunk = 0;
    prov_devices_[i].key_start_sent = false;
    prov_devices_[i].key_commit_sent = false;
  }
  return true;
}

void NodeStateMachine::provisioningCancel() {
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  prov_device_count_ = 0;
  for (size_t i = 0; i < kMaxProvisioningDevices; ++i) prov_devices_[i] = ProvisioningDevice{};
}

bool NodeStateMachine::provisioningSession(ProvisioningSessionSnapshot &out) const {
  out = ProvisioningSessionSnapshot{};
  out.active = prov_.active;
  out.state = prov_.state;
  out.session_nonce = prov_.session_nonce;
  out.estimated_count = prov_.estimated_count;
  out.started_ms = prov_.started_ms;
  out.phase_deadline_ms = prov_.phase_deadline_ms;
  out.retry_enabled = prov_.retry_enabled;
  out.retry_used = prov_.retry_used;
  out.paused_normal_tx = prov_.pause_normal_tx;
  out.discovered_count = prov_device_count_;
  size_t conflicts = 0, selected = 0, verified = 0, failed = 0;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    const ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (d.selected) selected++;
    if (d.address_conflict) conflicts++;
    if (d.state == ProvisioningDeviceState::Verified) verified++;
    if (d.state == ProvisioningDeviceState::Failed) failed++;
  }
  out.selected_count = selected;
  out.conflict_count = conflicts;
  out.verified_count = verified;
  out.failed_count = failed;
  return true;
}

size_t NodeStateMachine::provisioningDeviceCount() const { return prov_device_count_; }

bool NodeStateMachine::provisioningDeviceByIndex(size_t index, ProvisioningDeviceSnapshot &out) const {
  if (index >= prov_device_count_) return false;
  const ProvisioningDevice &d = prov_devices_[index];
  if (!d.in_use) return false;
  out.chip_id = d.chip_id;
  out.current_address = d.current_address;
  out.assigned_address = d.assigned_address;
  out.role_tx = d.role_tx;
  out.hw_model = d.hw_model;
  out.hw_rev = d.hw_rev;
  out.fw_major = d.fw_major;
  out.fw_minor = d.fw_minor;
  out.fw_patch = d.fw_patch;
  out.rssi = d.rssi;
  out.first_seen_ms = d.first_seen_ms;
  out.last_seen_ms = d.last_seen_ms;
  out.selected = d.selected;
  out.address_conflict = d.address_conflict;
  out.state = d.state;
  return true;
}

bool NodeStateMachine::consumePendingFleetProvisionApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx, String &fleetKey) {
  if (!fleet_prov_apply_pending_) return false;
  sessionNonce = fleet_prov_apply_session_nonce_;
  newAddress = fleet_prov_apply_address_;
  roleTx = fleet_prov_apply_role_tx_;
  fleetKey = fleet_prov_apply_key_;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_key_ = "";
  return true;
}

bool NodeStateMachine::hasPendingFleetProvisionApply() const { return fleet_prov_apply_pending_; }

bool NodeStateMachine::sendProvisioningVerify(uint16_t sessionNonce, uint8_t assignedAddress) {
  return sendProvisioningVerifyPacket(sessionNonce, assignedAddress);
}

NodeStateMachine::ProvisioningDevice *NodeStateMachine::findProvisioningDeviceByChip(uint32_t chipId) {
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (prov_devices_[i].in_use && prov_devices_[i].chip_id == chipId) return &prov_devices_[i];
  }
  return nullptr;
}

NodeStateMachine::ProvisioningDevice *NodeStateMachine::upsertProvisioningDevice(uint32_t chipId) {
  if (chipId == 0) return nullptr;
  if (ProvisioningDevice *d = findProvisioningDeviceByChip(chipId)) return d;
  if (prov_device_count_ >= kMaxProvisioningDevices) return nullptr;
  ProvisioningDevice &d = prov_devices_[prov_device_count_++];
  d = ProvisioningDevice{};
  d.in_use = true;
  d.chip_id = chipId;
  d.selected = true;
  d.state = ProvisioningDeviceState::Discovered;
  return &d;
}

void NodeStateMachine::recomputeProvisioningConflictsAndAssignments() {
  bool used[256]{};
  used[0] = true;
  used[255] = true;
  used[cfg_.local_address] = true;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    prov_devices_[i].address_conflict = false;
    prov_devices_[i].assigned_address = prov_devices_[i].current_address;
  }
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (!prov_devices_[i].in_use) continue;
    for (size_t j = i + 1; j < prov_device_count_; ++j) {
      if (!prov_devices_[j].in_use) continue;
      if (prov_devices_[i].current_address != 0 && prov_devices_[i].current_address == prov_devices_[j].current_address) {
        prov_devices_[i].address_conflict = true;
        prov_devices_[j].address_conflict = true;
      }
    }
  }
  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (!d.address_conflict && d.current_address >= 1 && d.current_address <= 254 && !used[d.current_address]) {
      d.assigned_address = d.current_address;
      used[d.assigned_address] = true;
    }
  }
  uint8_t next = 1;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (!d.address_conflict && d.assigned_address >= 1 && d.assigned_address <= 254) continue;
    while (next < 255 && used[next]) next++;
    if (next >= 255) {
      d.assigned_address = 0;
      d.state = ProvisioningDeviceState::Failed;
      continue;
    }
    d.assigned_address = next;
    used[next] = true;
    next++;
  }
}

bool NodeStateMachine::sendProvisioningCoordinatorPacketFactory(const uint8_t payload[12], uint8_t dst) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, cfg_.local_address, dst, payload, true)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::sendProvisioningCoordinatorPacketProd(const uint8_t payload[12], uint8_t dst) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, cfg_.local_address, dst, payload, false)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::sendProvisioningAnnounce(uint16_t sessionNonce) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  uint8_t payload[12]{};
  payload[0] = kProvOpAnnounce;
  encodeU16LE(payload + 1, sessionNonce);
  encodeU32LE(payload + 3, ESP.getChipId());
  payload[7] = cfg_.local_address;
  payload[8] = (cfg_.role_tx ? kProvRoleTxFlag : 0) | ((kProvHwModelLrs & 0x0F) << 4);
  payload[9] = kProvHwRevA1;
  uint8_t maj = 0, min = 0, patch = 0;
  parseFwVersionPacked(maj, min, patch);
  payload[10] = static_cast<uint8_t>(((maj & 0x0F) << 4) | (min & 0x0F));
  payload[11] = patch;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, cfg_.local_address, kProvBroadcastAddress, payload, true)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::sendProvisioningVerifyPacket(uint16_t sessionNonce, uint8_t assignedAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  if (isDefaultFleetKey()) return false;
  uint8_t payload[12]{};
  payload[0] = kProvOpVerify;
  encodeU16LE(payload + 1, sessionNonce);
  encodeU32LE(payload + 3, ESP.getChipId());
  payload[7] = assignedAddress;
  payload[8] = (cfg_.role_tx ? kProvRoleTxFlag : 0) | ((kProvHwModelLrs & 0x0F) << 4);
  payload[9] = kProvHwRevA1;
  uint8_t maj = 0, min = 0, patch = 0;
  parseFwVersionPacked(maj, min, patch);
  payload[10] = static_cast<uint8_t>(((maj & 0x0F) << 4) | (min & 0x0F));
  payload[11] = patch;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, cfg_.local_address, kProvBroadcastAddress, payload, false)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

NodeStateMachine::PeerRuntime *NodeStateMachine::findOrCreatePeer(uint8_t address) {
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return &peers_[i];
    }
  }
  if (peer_count_ >= kMaxPeers) {
    return nullptr;
  }
  PeerRuntime &node = peers_[peer_count_++];
  node = PeerRuntime{};
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
  if (!radioTxBudgetAvailable()) return false;
  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::PollRequest, 0, input_state_, txFlags(), last_counter_, cfg_.local_address, dstAddress,
                    local_temp_code_,
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  if (logs_) {
    logs_->add("tx_poll_request", 0, last_counter_, 0);
  }
  return true;
}

void NodeStateMachine::tickPeerMqttCommands(uint32_t now) {
  if (!cfg_.role_tx) return;
  for (size_t i = 0; i < peer_count_; ++i) {
    PeerRuntime &node = peers_[i];
    if (!node.in_use || !node.pending) continue;

    if (static_cast<int32_t>(now - node.pending_deadline_ms) >= 0) {
      node.pending = false;
      node.ack_state = PeerAckState::Timeout;
      if (logs_) {
        logs_->add("mqtt_remote_ack_timeout", 0, node.pending_counter, node.pending_relay);
      }
      continue;
    }

    if (static_cast<int32_t>(now - node.next_retry_ms) < 0) {
      continue;
    }
    if (!radioTxBudgetAvailable()) {
      return;
    }

    uint32_t sentCounter = 0;
    if (sendPeerMqttCommand(node.address, node.pending_relay, &sentCounter)) {
      const uint8_t idx = node.retry_step < (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0]))
                              ? node.retry_step
                              : (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1;
      node.next_retry_ms = now + kMqttRetryScheduleMs[idx];
      if (node.retry_step < ((sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1)) {
        node.retry_step++;
      }
      node.pending_counter = sentCounter;
      node.last_cmd_counter = sentCounter;
      node.ack_state = PeerAckState::Pending;
    } else if (logs_) {
      logs_->add("mqtt_remote_retry_send_fail", 0, node.pending_counter, node.pending_relay);
    }
  }
}

void NodeStateMachine::tickPeerPolling(uint32_t now) {
  if (!cfg_.role_tx) return;
  if (!cfg_.tx_mqtt_remote_polling_enabled) {
    for (size_t i = 0; i < peer_count_; ++i) {
      PeerRuntime &node = peers_[i];
      if (!node.in_use) continue;
      node.poll_pending = false;
      node.next_poll_ms = 0;
    }
    return;
  }
  const uint32_t pollResponseDeadlineMs = (cfg_.ack_timeout_ms >= 2000U) ? cfg_.ack_timeout_ms : 2000U;
  for (size_t i = 0; i < peer_count_; ++i) {
    PeerRuntime &node = peers_[i];
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
    if (!radioTxBudgetAvailable()) {
      return;
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

  if (prov_.active) {
    startupTxPhaseTrace("prov_coord_before");
    tickProvisioningCoordinator(now);
    return;
  }

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
      if (!radioTxBudgetAvailable()) {
        tx_state_sync_pending_ = true;  // Defer change sync to next tick when a radio TX budget slot is available.
        return;
      }
      tx_state_sync_pending_ = false;
      tx_retry_step_ = 0;
      sendTxState(MessageType::Change, input_state_, input_state_, "tx_change");
      return;
    }
  }

  if (cfg_.tx_input_lora_control_enabled && tx_state_sync_pending_ && !tx_command_pending_) {
    if (!radioTxBudgetAvailable()) {
      return;
    }
    tx_state_sync_pending_ = false;
    tx_retry_step_ = 0;
    startupTxPhaseTrace("tx_start_sync_before_send");
    const uint32_t sendStartMs = millis();
    sendTxState(MessageType::Change, input_state_, input_state_, "tx_start_sync");
    startupTxSendTimingTrace("tx_start_sync_after_send", sendStartMs);
    return;
  }

  if (cfg_.tx_input_lora_control_enabled && tx_command_pending_ && static_cast<int32_t>(now - tx_next_retry_ms_) >= 0) {
    if (!radioTxBudgetAvailable()) {
      return;
    }
    startupTxPhaseTrace("tx_retry_before_send");
    const uint32_t sendStartMs = millis();
    sendTxState(MessageType::Change, tx_pending_relay_state_, tx_pending_input_state_, "tx_retry");
    startupTxSendTimingTrace("tx_retry_after_send", sendStartMs);
    return;
  }

  if (cfg_.tx_input_lora_control_enabled && (now - last_heartbeat_ms_) >= cfg_.heartbeat_ms) {
    if (!radioTxBudgetAvailable()) {
      return;
    }
    last_heartbeat_ms_ = now;
    last_counter_++;
    const uint32_t unixTimeS = currentUnixTimeS(now);
    if (radio_->send(MessageType::Heartbeat, input_state_, input_state_, txFlags(), last_counter_, cfg_.local_address,
                     cfg_.remote_address,
                     local_temp_code_, 0, 0xFF, 0xFFFF, unixTimeS)) {
      last_tx_ms_ = now;
      markRadioTxSentThisTick();
      if (logs_) {
        logs_->add("tx_heartbeat", 0, last_counter_, input_state_);
      }
      return;
    }
  }

  tickPeerMqttCommands(now);
  tickPeerPolling(now);
  startupTxPhaseTrace("after_peer_polling");

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
  if (!radioTxBudgetAvailable()) {
    return;
  }

  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(now);
  if (radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), txFlags(), last_counter_, cfg_.local_address,
                   cfg_.remote_address, local_temp_code_, 0, 0xFF, 0xFFFF, unixTimeS)) {
    last_tx_ms_ = now;
    markRadioTxSentThisTick();
    rx_last_push_ms_ = now;
    rx_push_pending_ = false;
    if (logs_) logs_->add("rx_push_on_change", 0, last_counter_, input_state_);
  }
}

void NodeStateMachine::tickReceive() {
  ProtocolMessage msg{};
  if (!radio_->receive(msg)) return;

  const bool isWifiProvision = (msg.type == MessageType::WifiProvision);
  const bool isFactoryReset = (msg.type == MessageType::FactoryReset);
  const bool isProvisioning = (msg.type == MessageType::Provisioning);
  if (isProvisioning) {
    handleProvisioningFrame(msg);
    return;
  }
  if (!isWifiProvision && msg.dst != cfg_.local_address) {
    if (logs_) logs_->add("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  if (isWifiProvision && msg.dst != cfg_.local_address && msg.dst != kWifiProvisionBroadcastAddress) {
    if (logs_) logs_->add("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }

  if (!isWifiProvision && cfg_.role_tx) {
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
  } else if (!isWifiProvision && msg.src != cfg_.remote_address) {
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
  if (isWifiProvision) {
    handleWifiProvisionFrame(msg);
    return;
  }
  if (isFactoryReset) {
    handleFactoryResetFrame(msg);
    return;
  }
  captureRemoteTemp(msg.temp_code);
  updateSharedTimeFromPeer(msg.unix_time_s, (msg.flags & kFlagTimeAuthoritative) != 0U);

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
      PeerRuntime *node = findOrCreatePeer(msg.src);
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
        node->ack_state = PeerAckState::Ok;
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
    if (!radioTxBudgetAvailable()) {
      return;
    }
    const uint8_t sensorMask = 0x04;  // includes downlink RSSI in sensor_analog0
    const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
    const uint32_t unixTimeS = currentUnixTimeS(millis());
    last_counter_++;
    if (radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), txFlags(), last_counter_, cfg_.local_address,
                     msg.src,
                     local_temp_code_, sensorMask, 0xFF, downlinkRssiEnc, unixTimeS)) {
      last_tx_ms_ = millis();
      markRadioTxSentThisTick();
      if (logs_) {
        logs_->add("rx_poll_response_tx", msg.rssi, last_counter_, relay_state_);
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
      if (!radioTxBudgetAvailable()) {
        if (logs_) logs_->add("rx_apply_no_ack_budget", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      const uint32_t unixTimeS = currentUnixTimeS(millis());
      last_counter_++;
      if (radio_->send(MessageType::Ack, relay_state_, input_state_, txFlags(), last_counter_, cfg_.local_address, msg.src,
                       local_temp_code_,
                       0, 0xFF, 0xFFFF, unixTimeS)) {
        last_tx_ms_ = millis();
        markRadioTxSentThisTick();
      }
    } else {
      if (!radioTxBudgetAvailable()) {
        if (logs_) logs_->add("rx_apply_no_status_budget", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      const uint8_t sensorMask = 0x04;  // includes downlink RSSI in sensor_analog0
      const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
      const uint32_t unixTimeS = currentUnixTimeS(millis());
      last_counter_++;
      if (radio_->send(MessageType::MqttStatus, relay_state_, localDryContactState(), txFlags(), last_counter_, cfg_.local_address,
                       msg.src, local_temp_code_, sensorMask, 0xFF, downlinkRssiEnc, unixTimeS)) {
        last_tx_ms_ = millis();
        markRadioTxSentThisTick();
      }
    }

    if (logs_) {
      logs_->add(msg.type == MessageType::Mqtt ? "rx_apply_mqtt" : "rx_apply_and_ack", msg.rssi, msg.counter, msg.relay_state);
    }
  }
}

bool NodeStateMachine::handleWifiProvisionFrame(const ProtocolMessage &msg) {
  const uint8_t op = msg.relay_state;
  const uint8_t transferId = msg.input_state;
  const uint8_t chunkIndex = msg.flags;
  const uint8_t totalChunks = msg.temp_code;
  const uint8_t valueLen = msg.sensor_mask;
  uint8_t bytes[7]{};
  fillProvisionPayloadBytes(msg, bytes);

  if (op == kWifiProvisionOpStart) {
    const uint8_t ssidLen = valueLen;
    const uint8_t passLen = bytes[0];
    const size_t totalLen = static_cast<size_t>(ssidLen) + static_cast<size_t>(passLen);
    const uint32_t expectedHash = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                                  (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24);
    const uint8_t expectedChunks =
        static_cast<uint8_t>((totalLen + (kWifiProvisionChunkDataBytes - 1U)) / kWifiProvisionChunkDataBytes);
    if (transferId == 0 || ssidLen == 0 || ssidLen > 32 || passLen > 64 || totalLen == 0 || totalLen > sizeof(wifi_prov_rx_.data) ||
        totalChunks == 0 || totalChunks > 31 || totalChunks != expectedChunks) {
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      if (logs_) logs_->add("wifi_prov_rx_bad_start", msg.rssi, msg.counter, op);
      return false;
    }
    wifi_prov_rx_ = WifiProvisionRxTransfer{};
    wifi_prov_rx_.active = true;
    wifi_prov_rx_.src = msg.src;
    wifi_prov_rx_.transfer_id = transferId;
    wifi_prov_rx_.total_chunks = totalChunks;
    wifi_prov_rx_.ssid_len = ssidLen;
    wifi_prov_rx_.pass_len = passLen;
    wifi_prov_rx_.expected_hash = expectedHash;
    if (logs_) logs_->add("wifi_prov_rx_start", msg.rssi, msg.counter, totalChunks);
    return true;
  }

  if (!wifi_prov_rx_.active || wifi_prov_rx_.src != msg.src || wifi_prov_rx_.transfer_id != transferId ||
      wifi_prov_rx_.total_chunks != totalChunks) {
    if (logs_) logs_->add("wifi_prov_rx_orphan", msg.rssi, msg.counter, op);
    return false;
  }

  const size_t totalLen = static_cast<size_t>(wifi_prov_rx_.ssid_len) + static_cast<size_t>(wifi_prov_rx_.pass_len);
  if (op == kWifiProvisionOpData) {
    if (chunkIndex >= wifi_prov_rx_.total_chunks || valueLen > kWifiProvisionChunkDataBytes) {
      if (logs_) logs_->add("wifi_prov_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    const size_t offset = static_cast<size_t>(chunkIndex) * kWifiProvisionChunkDataBytes;
    if (offset >= totalLen || (offset + valueLen) > totalLen) {
      if (logs_) logs_->add("wifi_prov_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    memcpy(wifi_prov_rx_.data + offset, bytes, valueLen);
    wifi_prov_rx_.received_bitmap |= (1UL << chunkIndex);
    return true;
  }

  if (op == kWifiProvisionOpCommit) {
    const uint32_t wantBitmap = (1UL << wifi_prov_rx_.total_chunks) - 1UL;
    if ((wifi_prov_rx_.received_bitmap & wantBitmap) != wantBitmap) {
      if (logs_) logs_->add("wifi_prov_rx_incomplete", msg.rssi, msg.counter, wifi_prov_rx_.total_chunks);
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      return false;
    }
    const uint32_t gotHash = fnv1a32(wifi_prov_rx_.data, totalLen);
    if (gotHash != wifi_prov_rx_.expected_hash) {
      if (logs_) logs_->add("wifi_prov_rx_hash_fail", msg.rssi, msg.counter, 0);
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      return false;
    }
    char ssidBuf[33]{};
    char passBuf[65]{};
    memcpy(ssidBuf, wifi_prov_rx_.data, wifi_prov_rx_.ssid_len);
    memcpy(passBuf, wifi_prov_rx_.data + wifi_prov_rx_.ssid_len, wifi_prov_rx_.pass_len);
    wifi_prov_pending_ssid_ = String(ssidBuf);
    wifi_prov_pending_password_ = String(passBuf);
    wifi_prov_pending_src_ = wifi_prov_rx_.src;
    wifi_prov_pending_ = (wifi_prov_pending_ssid_.length() > 0);
    if (logs_) logs_->add("wifi_prov_rx_ready", msg.rssi, msg.counter, wifi_prov_rx_.total_chunks);
    wifi_prov_rx_ = WifiProvisionRxTransfer{};
    return wifi_prov_pending_;
  }

  if (logs_) logs_->add("wifi_prov_rx_unknown", msg.rssi, msg.counter, op);
  return false;
}

bool NodeStateMachine::handleFactoryResetFrame(const ProtocolMessage &msg) {
  if (msg.relay_state != kFactoryResetMagic0 || msg.input_state != kFactoryResetMagic1) {
    if (logs_) logs_->add("factory_reset_rx_bad", msg.rssi, msg.counter, 0);
    return false;
  }
  factory_reset_keep_fleet_pending_ = (msg.flags & kFactoryResetKeepFleetFlag) != 0U;
  factory_reset_pending_src_ = msg.src;
  factory_reset_pending_ = true;
  if (logs_) {
    logs_->add(factory_reset_keep_fleet_pending_ ? "factory_reset_rx_keep" : "factory_reset_rx_full",
               msg.rssi, msg.counter, msg.src);
  }
  return true;
}

void NodeStateMachine::tickProvisioningTarget(uint32_t now) {
  if (!radioTxBudgetAvailable()) return;
  if (!isDefaultFleetKey()) return;
  if (!prov_rx_.discover_pending) return;
  if (static_cast<int32_t>(now - prov_rx_.announce_at_ms) < 0) return;
  if (sendProvisioningAnnounce(prov_rx_.session_nonce)) {
    prov_rx_.discover_pending = false;
    if (logs_) logs_->add("prov_announce_tx", 0, prov_rx_.session_nonce, cfg_.local_address);
  } else {
    prov_rx_.announce_at_ms = now + 500U;
  }
}

void NodeStateMachine::tickProvisioningCoordinator(uint32_t now) {
  if (!radioTxBudgetAvailable()) return;
  if (!prov_.active) return;

  if ((prov_.state == ProvisioningSessionState::Discovering || prov_.state == ProvisioningSessionState::DiscoveryRetry) &&
      static_cast<int32_t>(now - prov_.phase_deadline_ms) >= 0) {
    if (prov_.retry_enabled && !prov_.retry_used) {
      prov_.retry_used = true;
      prov_.state = ProvisioningSessionState::DiscoveryRetry;
      const uint32_t retryWindowMs = 10000UL;
      prov_.phase_deadline_ms = now + retryWindowMs;
      uint8_t payload[12]{};
      payload[0] = kProvOpDiscoverStart;
      encodeU16LE(payload + 1, prov_.session_nonce);
      encodeU16LE(payload + 3, static_cast<uint16_t>(retryWindowMs / 100U));
      payload[5] = 0;
      if (sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) {
        if (logs_) logs_->add("prov_discover_retry", 0, prov_.session_nonce, 0);
      }
      return;
    }
    recomputeProvisioningConflictsAndAssignments();
    prov_.state = ProvisioningSessionState::Ready;
    return;
  }

  if (prov_.state != ProvisioningSessionState::Provisioning || !prov_.provision_all_requested) return;

  while (prov_.current_index < prov_device_count_) {
    ProvisioningDevice &d = prov_devices_[prov_.current_index];
    if (!d.in_use || !d.selected || d.state == ProvisioningDeviceState::Verified || d.state == ProvisioningDeviceState::Skipped) {
      prov_.current_index++;
      continue;
    }
    if (d.assigned_address == 0) {
      d.state = ProvisioningDeviceState::Failed;
      prov_.current_index++;
      continue;
    }

    if (d.state == ProvisioningDeviceState::AwaitVerify) {
      if (static_cast<int32_t>(now - prov_.phase_deadline_ms) < 0) return;
      if (d.retries < kProvMaxRetriesPerNode) {
        d.retries++;
        d.state = ProvisioningDeviceState::Discovered;
        d.key_total_chunks = 0;
        d.key_len = 0;
        d.key_crc16 = 0;
        d.key_next_chunk = 0;
        d.key_start_sent = false;
        d.key_commit_sent = false;
      } else {
        d.state = ProvisioningDeviceState::Failed;
        prov_.current_index++;
      }
      continue;
    }

    if (d.state == ProvisioningDeviceState::Discovered || d.state == ProvisioningDeviceState::Assigned ||
        d.state == ProvisioningDeviceState::Keying) {
      const String fleetKey = cfg_.fleet_passphrase;
      const size_t keyLen = static_cast<size_t>(fleetKey.length());
      if (keyLen == 0 || keyLen > (kProvChunkBitmapMax * kProvKeyChunkBytes)) {
        d.state = ProvisioningDeviceState::Failed;
        prov_.state = ProvisioningSessionState::Error;
        prov_.active = false;
        prov_.pause_normal_tx = false;
        if (logs_) logs_->add("prov_key_len_bad", 0, d.chip_id & 0xFFFFU, static_cast<uint8_t>(keyLen & 0xFFU));
        return;
      }
      const uint8_t totalChunks = static_cast<uint8_t>((keyLen + (kProvKeyChunkBytes - 1U)) / kProvKeyChunkBytes);
      const uint16_t keyCrc = crc16Ccitt(reinterpret_cast<const uint8_t *>(fleetKey.c_str()), keyLen);

      if (d.state == ProvisioningDeviceState::Discovered) {
        d.key_total_chunks = totalChunks;
        d.key_len = static_cast<uint8_t>(keyLen);
        d.key_crc16 = keyCrc;
        d.key_next_chunk = 0;
        d.key_start_sent = false;
        d.key_commit_sent = false;
      }

      uint8_t payload[12]{};
      if (d.state == ProvisioningDeviceState::Discovered) {
        payload[0] = kProvOpAssign;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.assigned_address;
        payload[8] = 0;  // RX for v1
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        d.state = ProvisioningDeviceState::Assigned;
        return;
      }

      if (d.state == ProvisioningDeviceState::Assigned && !d.key_start_sent) {
        payload[0] = kProvOpKeyStart;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.key_total_chunks;
        payload[8] = d.key_len;
        encodeU16LE(payload + 9, d.key_crc16);
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        d.key_start_sent = true;
        d.state = ProvisioningDeviceState::Keying;
        return;
      }

      if (d.state == ProvisioningDeviceState::Keying && d.key_next_chunk < d.key_total_chunks) {
        const uint8_t idx = d.key_next_chunk;
        payload[0] = kProvOpKeyData;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = idx;
        const size_t offset = static_cast<size_t>(idx) * kProvKeyChunkBytes;
        size_t chunkLen = static_cast<size_t>(d.key_len) - offset;
        if (chunkLen > kProvKeyChunkBytes) chunkLen = kProvKeyChunkBytes;
        payload[8] = static_cast<uint8_t>(chunkLen);
        memcpy(payload + 9, fleetKey.c_str() + offset, chunkLen);
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        d.key_next_chunk++;
        return;
      }

      if (d.state == ProvisioningDeviceState::Keying && !d.key_commit_sent) {
        payload[0] = kProvOpKeyCommit;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.key_total_chunks;
        payload[8] = d.key_len;
        encodeU16LE(payload + 9, d.key_crc16);
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        d.key_commit_sent = true;
        return;
      }

      if (d.state == ProvisioningDeviceState::Keying && d.key_commit_sent) {
        payload[0] = kProvOpApplyCommit;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.assigned_address;
        payload[8] = 0;  // RX
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        d.state = ProvisioningDeviceState::AwaitVerify;
        prov_.phase_deadline_ms = now + kProvVerifyTimeoutMs;
        if (logs_) logs_->add("prov_node_tx", 0, static_cast<uint32_t>(d.chip_id & 0xFFFFU), d.assigned_address);
        return;
      }
    }

    if (d.state == ProvisioningDeviceState::Failed || d.state == ProvisioningDeviceState::Verified) {
      prov_.current_index++;
      continue;
    }
    return;
  }

  prov_.state = ProvisioningSessionState::Complete;
  prov_.active = false;
  prov_.pause_normal_tx = false;
}

bool NodeStateMachine::radioTxBudgetAvailable() const {
  return !radio_tx_budget_active_ || !radio_tx_used_this_tick_;
}

void NodeStateMachine::resetRadioTxBudgetForTick() {
  // One expensive outbound LoRa action per state-machine tick keeps loop-time predictable on ESP8266.
  radio_tx_budget_active_ = true;
  radio_tx_used_this_tick_ = false;
}

void NodeStateMachine::finishRadioTxBudgetForTick() { radio_tx_budget_active_ = false; }

void NodeStateMachine::markRadioTxSentThisTick() {
  if (radio_tx_budget_active_) {
    radio_tx_used_this_tick_ = true;
  }
}

bool NodeStateMachine::handleProvisioningFrame(const ProtocolMessage &msg) {
  uint8_t payload[12]{};
  payload[0] = msg.relay_state;
  payload[1] = msg.input_state;
  payload[2] = msg.flags;
  payload[3] = msg.temp_code;
  payload[4] = msg.sensor_mask;
  payload[5] = msg.sensor_digital0;
  payload[6] = static_cast<uint8_t>(msg.sensor_analog0 & 0xFFU);
  payload[7] = static_cast<uint8_t>((msg.sensor_analog0 >> 8) & 0xFFU);
  payload[8] = static_cast<uint8_t>(msg.unix_time_s & 0xFFU);
  payload[9] = static_cast<uint8_t>((msg.unix_time_s >> 8) & 0xFFU);
  payload[10] = static_cast<uint8_t>((msg.unix_time_s >> 16) & 0xFFU);
  payload[11] = static_cast<uint8_t>((msg.unix_time_s >> 24) & 0xFFU);

  const uint8_t op = payload[0];
  const uint16_t sessionNonce = decodeU16LE(payload + 1);
  const uint32_t now = millis();

  // Coordinator-side handling
  if (cfg_.role_tx && prov_.active) {
    if (op == kProvOpAnnounce && msg.via_factory_key && (msg.dst == cfg_.local_address || msg.dst == kProvBroadcastAddress)) {
      if (sessionNonce != prov_.session_nonce) return false;
      const uint32_t chipId = decodeU32LE(payload + 3);
      ProvisioningDevice *d = upsertProvisioningDevice(chipId);
      if (d == nullptr) return false;
      d->current_address = payload[7];
      d->role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
      d->hw_model = static_cast<uint8_t>((payload[8] >> 4) & 0x0FU);
      d->hw_rev = payload[9];
      d->fw_major = static_cast<uint8_t>((payload[10] >> 4) & 0x0FU);
      d->fw_minor = static_cast<uint8_t>(payload[10] & 0x0FU);
      d->fw_patch = payload[11];
      d->rssi = msg.rssi;
      if (d->first_seen_ms == 0) d->first_seen_ms = now;
      d->last_seen_ms = now;
      if (d->state == ProvisioningDeviceState::Failed) d->state = ProvisioningDeviceState::Discovered;
      recomputeProvisioningConflictsAndAssignments();
      if (logs_) logs_->add("prov_announce_rx", msg.rssi, static_cast<uint32_t>(chipId & 0xFFFFU), d->current_address);
      return true;
    }

    if (op == kProvOpVerify && !msg.via_factory_key && (msg.dst == cfg_.local_address || msg.dst == kProvBroadcastAddress)) {
      if (sessionNonce != prov_.session_nonce) return false;
      const uint32_t chipId = decodeU32LE(payload + 3);
      ProvisioningDevice *d = findProvisioningDeviceByChip(chipId);
      if (d == nullptr) return false;
      d->current_address = payload[7];
      d->assigned_address = payload[7];
      d->role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
      d->hw_model = static_cast<uint8_t>((payload[8] >> 4) & 0x0FU);
      d->hw_rev = payload[9];
      d->fw_major = static_cast<uint8_t>((payload[10] >> 4) & 0x0FU);
      d->fw_minor = static_cast<uint8_t>(payload[10] & 0x0FU);
      d->fw_patch = payload[11];
      d->rssi = msg.rssi;
      d->last_seen_ms = now;
      d->state = ProvisioningDeviceState::Verified;
      if (prov_.state == ProvisioningSessionState::Provisioning && prov_.current_index < prov_device_count_ &&
          prov_devices_[prov_.current_index].chip_id == chipId) {
        prov_.current_index++;
      }
      recomputeProvisioningConflictsAndAssignments();
      if (logs_) logs_->add("prov_verify_rx", msg.rssi, static_cast<uint32_t>(chipId & 0xFFFFU), payload[7]);
      return true;
    }
  }

  // Target-side factory-key provisioning (only when currently on factory key).
  if (!isDefaultFleetKey()) {
    return false;
  }
  if (!msg.via_factory_key) {
    return false;
  }
  if (!(msg.dst == cfg_.local_address || msg.dst == kProvBroadcastAddress)) {
    return false;
  }
  const uint32_t localChip = ESP.getChipId();
  const uint32_t targetChip = decodeU32LE(payload + 3);

  if (op == kProvOpDiscoverStart) {
    const uint16_t windowTicks = decodeU16LE(payload + 3);
    uint32_t windowMs = static_cast<uint32_t>(windowTicks) * 100U;
    if (windowMs < 1000U) windowMs = 1000U;
    if (windowMs > 180000U) windowMs = 180000U;
    prov_rx_.session_nonce = sessionNonce;
    prov_rx_.discover_pending = true;
    prov_rx_.announce_at_ms = now + static_cast<uint32_t>(random(0, static_cast<long>(windowMs + 1U)));
    if (logs_) logs_->add("prov_discover_rx", msg.rssi, sessionNonce, 0);
    return true;
  }

  if (sessionNonce == 0 || targetChip != localChip) {
    return false;
  }

  if (op == kProvOpAssign) {
    const uint8_t newAddr = payload[7];
    if (newAddr == 0 || newAddr == 255) return false;
    prov_rx_.have_staged_assignment = true;
    prov_rx_.staged_address = newAddr;
    prov_rx_.staged_role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
    prov_rx_.key_session_nonce = sessionNonce;
    return true;
  }

  if (op == kProvOpKeyStart) {
    const uint8_t totalChunks = payload[7];
    const uint8_t keyLen = payload[8];
    const uint16_t crc = decodeU16LE(payload + 9);
    if (totalChunks == 0 || totalChunks > kProvChunkBitmapMax || keyLen == 0 || keyLen > sizeof(prov_rx_.key_data)) return false;
    prov_rx_.key_transfer_active = true;
    prov_rx_.key_session_nonce = sessionNonce;
    prov_rx_.key_total_chunks = totalChunks;
    prov_rx_.key_len = keyLen;
    prov_rx_.key_crc16 = crc;
    prov_rx_.key_bitmap = 0;
    prov_rx_.key_committed = false;
    memset(prov_rx_.key_data, 0, sizeof(prov_rx_.key_data));
    return true;
  }

  if (!prov_rx_.key_transfer_active || prov_rx_.key_session_nonce != sessionNonce) {
    return false;
  }

  if (op == kProvOpKeyData) {
    const uint8_t idx = payload[7];
    const uint8_t len = payload[8];
    if (idx >= prov_rx_.key_total_chunks || len == 0 || len > kProvKeyChunkBytes) return false;
    const size_t offset = static_cast<size_t>(idx) * kProvKeyChunkBytes;
    if ((offset + len) > prov_rx_.key_len) return false;
    memcpy(prov_rx_.key_data + offset, payload + 9, len);
    prov_rx_.key_bitmap |= (1UL << idx);
    return true;
  }

  if (op == kProvOpKeyCommit) {
    const uint8_t totalChunks = payload[7];
    const uint8_t keyLen = payload[8];
    const uint16_t crc = decodeU16LE(payload + 9);
    if (totalChunks != prov_rx_.key_total_chunks || keyLen != prov_rx_.key_len || crc != prov_rx_.key_crc16) return false;
    const uint32_t want = (1UL << prov_rx_.key_total_chunks) - 1UL;
    if ((prov_rx_.key_bitmap & want) != want) return false;
    const uint16_t got = crc16Ccitt(reinterpret_cast<const uint8_t *>(prov_rx_.key_data), prov_rx_.key_len);
    if (got != prov_rx_.key_crc16) return false;
    prov_rx_.key_committed = true;
    return true;
  }

  if (op == kProvOpApplyCommit) {
    if (!prov_rx_.have_staged_assignment) return false;
    if (!prov_rx_.key_transfer_active) return false;
    if (!prov_rx_.key_committed) return false;
    if (prov_rx_.key_session_nonce != sessionNonce) return false;
    const uint8_t newAddr = payload[7];
    if (newAddr != prov_rx_.staged_address) return false;
    String newKey;
    newKey.reserve(prov_rx_.key_len);
    for (uint8_t i = 0; i < prov_rx_.key_len; ++i) newKey += prov_rx_.key_data[i];
    fleet_prov_apply_session_nonce_ = sessionNonce;
    fleet_prov_apply_address_ = newAddr;
    fleet_prov_apply_role_tx_ = (payload[8] & kProvRoleTxFlag) != 0U;
    fleet_prov_apply_key_ = newKey;
    fleet_prov_apply_pending_ = (fleet_prov_apply_key_.length() > 0);
    prov_rx_.key_transfer_active = false;
    prov_rx_.discover_pending = false;
    if (logs_) logs_->add("prov_apply_rx", msg.rssi, sessionNonce, newAddr);
    return fleet_prov_apply_pending_;
  }

  return false;
}

void NodeStateMachine::captureRemoteTemp(uint8_t tempCode) {
  if (tempCode == 0xFF) return;
  remote_temp_valid_ = true;
  remote_temp_c_ = static_cast<int8_t>(tempCode);
  remote_temp_ms_ = millis();
}

uint8_t NodeStateMachine::txFlags() const {
  return shared_time_authoritative_ ? kFlagTimeAuthoritative : 0U;
}

void NodeStateMachine::updateSharedTimeFromPeer(uint32_t unixTimeS, bool authoritative) {
  if (!authoritative) return;
  if (unixTimeS < kMinValidUnixTimeS) return;
  const uint32_t now = millis();
  if (!shared_time_valid_) {
    shared_time_valid_ = true;
    shared_time_authoritative_ = false;
    shared_time_sync_unix_s_ = unixTimeS;
    shared_time_sync_ms_ = now;
    if (logs_) logs_->add("time_sync_peer", 0, unixTimeS, 0);
    return;
  }
  if (shared_time_authoritative_) {
    return;
  }
  const uint32_t current = currentUnixTimeS(now);
  if (unixTimeS > current || (current > unixTimeS && (current - unixTimeS) > 30U)) {
    shared_time_sync_unix_s_ = unixTimeS;
    shared_time_sync_ms_ = now;
    if (logs_) logs_->add("time_sync_peer", 0, unixTimeS, 0);
  }
}

uint32_t NodeStateMachine::currentUnixTimeS(uint32_t nowMs) const {
  if (!shared_time_valid_) return 0;
  return shared_time_sync_unix_s_ + ((nowMs - shared_time_sync_ms_) / 1000U);
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
