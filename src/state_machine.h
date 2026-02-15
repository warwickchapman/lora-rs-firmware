#pragma once

#include <Arduino.h>

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
  void mqttSetLocalRelay(uint8_t relayState);
  bool mqttSendRemoteRelay(uint8_t dstAddress, uint8_t relayState);

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

  void tickTransmitter();
  void tickReceiver();
  void tickReceive();
  void tickLed();
  void captureRemoteTemp(uint8_t tempCode);
  void sendTxState(MessageType type, uint8_t relayState, uint8_t inputState, const char *logEvent);
};
