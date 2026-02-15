#pragma once

#include <Arduino.h>

#include "config_store.h"

class LogBuffer;

enum class MessageType : uint8_t {
  Ack = 'A',
  Change = 'C',
  Heartbeat = 'H',
  Mqtt = 'M',
};

struct ProtocolMessage {
  MessageType type;
  uint8_t relay_state;
  uint8_t input_state;
  uint8_t flags;
  uint8_t temp_code;
  uint8_t sensor_mask;
  uint8_t sensor_digital0;
  uint16_t sensor_analog0;
  uint32_t counter;
  uint8_t src;
  uint8_t dst;
  int rssi;
};

class RadioProtocol {
 public:
  bool begin(const Settings &cfg, LogBuffer *logs);
  void applyConfig(const Settings &cfg);

  bool send(MessageType type, uint8_t relay, uint8_t input, uint8_t flags, uint32_t counter, uint8_t src, uint8_t dst,
            uint8_t temp_code = 0xFF, uint8_t sensor_mask = 0, uint8_t sensor_digital0 = 0xFF, uint16_t sensor_analog0 = 0xFFFF);
  bool receive(ProtocolMessage &msg);

 private:
  Settings cfg_{};
  LogBuffer *logs_ = nullptr;
  bool lora_enabled_ = true;
  uint8_t enc_key_[16]{};
  uint8_t mac_key_[32]{};

  void deriveKeys();
  void refreshRadioRuntimeState();
};
