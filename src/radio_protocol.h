#pragma once

#include <Arduino.h>

#include "config_store.h"

enum class MessageType : uint8_t {
  Ack = 'A',
  Change = 'C',
  Heartbeat = 'H',
  Mqtt = 'M',
  MqttStatus = 'S',
  PollRequest = 'P',
  PollResponse = 'R',
  MaintenanceRequest = 'Q',
  MaintenanceStatus = 'T',
  WifiProvision = 'W',
  WifiControl = 'Y',
  UdpLogControl = 'U',
  OtaPullControl = 'O',
  OtaPullStatus = 'N',
  FactoryReset = 'E',
  FactoryResetStatus = 'F',
  Identify = 'I',
  Provisioning = 'V',
  Reboot = 'B',
  SensorConfig = 'K',
  FleetKeyControl = 'Z',
  Readdress = 'D',
  ReaddressStatus = 'G',
};


constexpr uint8_t kFlagTimeAuthoritative = 0x01;
constexpr uint8_t kFlagPairedInputSlave = 0x02;
constexpr uint8_t kFlagMqttTransaction = 0x04;

struct ProtocolMessage {
  MessageType type;
  uint8_t relay_state;
  uint8_t input_state;
  uint8_t flags;
  uint8_t temp_code;
  uint8_t sensor_mask;
  uint8_t sensor_digital0;
  uint16_t sensor_analog0;
  uint32_t unix_time_s; // For Mqtt/MqttStatus with kFlagMqttTransaction set, b8..b11 represents mqtt_command_id
  uint8_t raw_payload[12]{};
  uint32_t counter;
  uint32_t boot_nonce = 0;
  uint8_t src;
  uint8_t dst;
  int rssi;
  bool via_factory_key = false;
};

namespace radio_protocol_helpers {
inline void encodeInputFields(uint8_t legacy_input, uint8_t sensor_mask, uint8_t sensor_digital0, uint8_t& out_mask, uint8_t& out_digital0) {
  out_mask = sensor_mask;
  out_digital0 = sensor_digital0;
  if (sensor_digital0 == 0xFF) {
    out_digital0 = legacy_input;
    out_mask |= 0x01;
  }
}

inline uint8_t resolveInputState(uint8_t legacy_input, uint8_t sensor_mask, uint8_t sensor_digital0) {
  const bool digitalPresent = (sensor_mask & 0x01U) != 0U;
  if (digitalPresent && sensor_digital0 != 0xFFU) {
    return (sensor_digital0 != 0U) ? 1 : 0;
  }
  return legacy_input ? 1 : 0;
}

inline bool validateInputFields(uint8_t sensor_mask) {
  // b5 cannot simultaneously mean physical input (0x01) and WiFi state (0x08)
  if ((sensor_mask & 0x01U) != 0 && (sensor_mask & 0x08U) != 0) {
    return false;
  }
  return true;
}

inline bool usesOperationalInputFields(MessageType type) {
  switch (type) {
    case MessageType::Ack:
    case MessageType::Change:
    case MessageType::Heartbeat:
    case MessageType::Mqtt:
    case MessageType::MqttStatus:
    case MessageType::PollRequest:
    case MessageType::PollResponse:
    case MessageType::WifiControl:
      return true;
    default:
      return false;
  }
}
} // namespace radio_protocol_helpers

class RadioProtocol {
 public:
  bool begin(const Settings &cfg);
  void applyConfig(const Settings &cfg);

  bool send(MessageType type, uint8_t relay, uint8_t input, uint8_t flags, uint32_t counter, uint8_t src, uint8_t dst,
            uint8_t temp_code = 0xFF, uint8_t sensor_mask = 0, uint8_t sensor_digital0 = 0xFF, uint16_t sensor_analog0 = 0xFFFF,
            uint32_t unix_time_s = 0);
  bool sendRaw(MessageType type, uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12]);
  bool sendProvisioningRaw(uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12], bool useFactoryKey);
 bool receive(ProtocolMessage &msg);

 private:
  struct RuntimeCfg {
    long lora_frequency_hz = 0;
    uint8_t lora_tx_power = 0;
    uint8_t lora_spreading_factor = 0;
    long lora_bandwidth_hz = 0;
    uint8_t lora_coding_rate = 0;
  };

  const Settings *settings_ = nullptr;
  RuntimeCfg runtime_{};
  bool lora_enabled_ = true;
  bool default_key_configured_ = false;
  uint32_t boot_nonce_ = 0;
  uint8_t enc_key_[16]{};
  uint8_t mac_key_[32]{};
  uint8_t factory_enc_key_[16]{};
  uint8_t factory_mac_key_[32]{};

  void deriveKeys();
  void refreshRuntimeCfg(const Settings &cfg);
  void refreshRadioRuntimeState();
  bool sendRawWithKeys(MessageType type, uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12],
                       const uint8_t encKey[16], const uint8_t macKey[32], const char *logEvent);
};
