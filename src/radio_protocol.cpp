#include "radio_protocol.h"

#include <Crypto.h>
#include <AES.h>
#include <CTR.h>
#include <LoRa.h>
#include <SHA256.h>

#include "logger.h"
#include "runtime_utils.h"

namespace {
constexpr uint8_t kNss = 15;
constexpr uint8_t kRst = 16;
constexpr uint8_t kDio0 = 0;
constexpr size_t kPayloadSize = 12;
constexpr size_t kMacSize = 8;

struct __attribute__((packed)) Packet {
  uint8_t dst;
  uint8_t src;
  uint8_t type;
  uint32_t counter;
  uint32_t boot_nonce;
  uint8_t nonce[8];
  uint8_t encrypted[kPayloadSize];
  uint8_t mac[kMacSize];
};

void makeIv(const Packet &p, uint8_t iv[16]) {
  memcpy(iv, p.nonce, 8);
  memcpy(iv + 8, &p.counter, sizeof(p.counter));
  memset(iv + 12, 0, 4);
}

void randomNonce(uint8_t nonce[8]) {
  for (size_t i = 0; i < 8; i++) {
    nonce[i] = static_cast<uint8_t>(random(0, 256));
  }
}

void computeMac(const uint8_t macKey[32], const Packet &p, uint8_t out[32]) {
  SHA256 hash;
  hash.reset();
  hash.update(macKey, 32);
  hash.update(reinterpret_cast<const uint8_t *>(&p), sizeof(Packet) - kMacSize);
  hash.finalize(out, 32);
}
}

bool RadioProtocol::begin(const Settings &cfg) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  if (boot_nonce_ == 0) {
    boot_nonce_ = static_cast<uint32_t>(random(1, 0x7FFFFFFF));
    boot_nonce_ ^= static_cast<uint32_t>(micros());
    if (boot_nonce_ == 0) boot_nonce_ = 1;
  }

  LoRa.setPins(kNss, kRst, kDio0);
  if (!LoRa.begin(runtime_.lora_frequency_hz)) {
    return false;
  }
  LoRa.setTxPower(runtime_.lora_tx_power);
  LoRa.setSpreadingFactor(runtime_.lora_spreading_factor);
  LoRa.setSignalBandwidth(runtime_.lora_bandwidth_hz);
  LoRa.setCodingRate4(runtime_.lora_coding_rate);
  LoRa.enableCrc();

  deriveKeys();
  refreshRadioRuntimeState();
  return true;
}

void RadioProtocol::applyConfig(const Settings &cfg) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  LoRa.idle();
  LoRa.setFrequency(runtime_.lora_frequency_hz);
  LoRa.setTxPower(runtime_.lora_tx_power);
  LoRa.setSpreadingFactor(runtime_.lora_spreading_factor);
  LoRa.setSignalBandwidth(runtime_.lora_bandwidth_hz);
  LoRa.setCodingRate4(runtime_.lora_coding_rate);
  deriveKeys();
  refreshRadioRuntimeState();
}

bool RadioProtocol::send(MessageType type, uint8_t relay, uint8_t input, uint8_t flags, uint32_t counter, uint8_t src, uint8_t dst,
                         uint8_t temp_code, uint8_t sensor_mask, uint8_t sensor_digital0, uint16_t sensor_analog0,
                         uint32_t unix_time_s) {
  uint8_t effectiveMask;
  uint8_t effectiveDigital0;
  radio_protocol_helpers::encodeInputFields(input, sensor_mask, sensor_digital0, effectiveMask, effectiveDigital0);

  if (temp_code != 0xFF) effectiveMask |= 0x02;  // temperature present

  if (!radio_protocol_helpers::validateInputFields(effectiveMask)) {
    return false;
  }

  uint8_t plain[kPayloadSize] = {
      relay,
      input,
      flags,
      temp_code,
      effectiveMask,
      effectiveDigital0,
      static_cast<uint8_t>(sensor_analog0 & 0xFF),
      static_cast<uint8_t>((sensor_analog0 >> 8) & 0xFF),
      static_cast<uint8_t>(unix_time_s & 0xFF),
      static_cast<uint8_t>((unix_time_s >> 8) & 0xFF),
      static_cast<uint8_t>((unix_time_s >> 16) & 0xFF),
      static_cast<uint8_t>((unix_time_s >> 24) & 0xFF),
  };
  return sendRaw(type, counter, src, dst, plain);
}

bool RadioProtocol::sendRaw(MessageType type, uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12]) {
  if (default_key_configured_ && type != MessageType::Provisioning) {
    return false;
  }
  return sendRawWithKeys(type, counter, src, dst, payload, enc_key_, mac_key_, "tx_packet");
}

bool RadioProtocol::sendProvisioningRaw(uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12], bool useFactoryKey) {
  if (!lora_enabled_) {
    return false;
  }
  return sendRawWithKeys(MessageType::Provisioning, counter, src, dst, payload,
                         useFactoryKey ? factory_enc_key_ : enc_key_,
                         useFactoryKey ? factory_mac_key_ : mac_key_,
                         "tx_prov");
}

bool RadioProtocol::sendRawWithKeys(MessageType type, uint32_t counter, uint8_t src, uint8_t dst, const uint8_t payload[12],
                                    const uint8_t encKey[16], const uint8_t macKey[32], const char *logEvent) {
  if (!lora_enabled_) {
    return false;
  }

  Packet p{};
  p.dst = dst;
  p.src = src;
  p.type = static_cast<uint8_t>(type);
  p.counter = counter;
  p.boot_nonce = boot_nonce_;
  randomNonce(p.nonce);

  uint8_t plain[kPayloadSize]{};
  memcpy(plain, payload, kPayloadSize);
  uint8_t iv[16];
  makeIv(p, iv);

  CTR<AES128> ctr;
  ctr.setKey(encKey, sizeof(enc_key_));
  ctr.setIV(iv, sizeof(iv));
  ctr.encrypt(p.encrypted, plain, sizeof(plain));

  uint8_t digest[32];
  computeMac(macKey, p, digest);
  memcpy(p.mac, digest, kMacSize);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(reinterpret_cast<const uint8_t *>(&p), sizeof(Packet));
  LoRa.endPacket();
  yield();  // Long LoRa airtime can block; feed ESP8266 watchdog between burst packets.
  LoRa.receive();

  {
    const uint8_t logState = (logEvent != nullptr && strcmp(logEvent, "tx_prov") == 0) ? dst : 0;
    lrslog::event(logEvent, 0, counter, logState);
  }

  return true;
}

bool RadioProtocol::receive(ProtocolMessage &msg) {
  if (!lora_enabled_) {
    return false;
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return false;
  }

  if (packetSize != static_cast<int>(sizeof(Packet))) {
    while (LoRa.available()) {
      LoRa.read();
    }
    {
      lrslog::event("rx_invalid_size", LoRa.packetRssi(), 0, 0);
    }
    return false;
  }

  Packet p{};
  size_t read = LoRa.readBytes(reinterpret_cast<uint8_t *>(&p), sizeof(Packet));
  if (read != sizeof(Packet)) {
    return false;
  }

  const bool isProvisioning = (p.type == static_cast<uint8_t>(MessageType::Provisioning));
  if (default_key_configured_ && !isProvisioning) {
    lrslog::event("rx_default_key_block", LoRa.packetRssi(), p.counter, p.type);
    return false;
  }

  uint8_t digest[32];
  bool viaFactoryKey = false;
  computeMac(mac_key_, p, digest);
  bool macOk = (memcmp(digest, p.mac, kMacSize) == 0);
  if (!macOk && isProvisioning) {
    computeMac(factory_mac_key_, p, digest);
    macOk = (memcmp(digest, p.mac, kMacSize) == 0);
    viaFactoryKey = macOk;
  }
  if (macOk && isProvisioning && default_key_configured_) {
    viaFactoryKey = true;
  }
  if (!macOk) {
    {
      lrslog::event("rx_bad_mac", LoRa.packetRssi(), p.counter, 0);
    }
    return false;
  }

  uint8_t iv[16];
  makeIv(p, iv);
  uint8_t plain[kPayloadSize]{};

  CTR<AES128> ctr;
  ctr.setKey(viaFactoryKey ? factory_enc_key_ : enc_key_, sizeof(enc_key_));
  ctr.setIV(iv, sizeof(iv));
  ctr.decrypt(plain, p.encrypted, sizeof(plain));

  msg.type = static_cast<MessageType>(p.type);
  if (radio_protocol_helpers::usesOperationalInputFields(msg.type) &&
      !radio_protocol_helpers::validateInputFields(plain[4])) {
    lrslog::event("rx_invalid_mask", LoRa.packetRssi(), p.counter, plain[4]);
    return false;
  }

  msg.relay_state = plain[0];
  msg.input_state = plain[1];
  msg.flags = plain[2];
  msg.temp_code = plain[3];
  msg.sensor_mask = plain[4];
  msg.sensor_digital0 = plain[5];
  msg.sensor_analog0 = static_cast<uint16_t>(plain[6]) | (static_cast<uint16_t>(plain[7]) << 8);
  msg.unix_time_s = static_cast<uint32_t>(plain[8]) | (static_cast<uint32_t>(plain[9]) << 8) |
                    (static_cast<uint32_t>(plain[10]) << 16) | (static_cast<uint32_t>(plain[11]) << 24);
  memcpy(msg.raw_payload, plain, sizeof(msg.raw_payload));
  msg.counter = p.counter;
  msg.boot_nonce = p.boot_nonce;
  msg.src = p.src;
  msg.dst = p.dst;
  msg.rssi = LoRa.packetRssi();
  msg.via_factory_key = viaFactoryKey;

  {
    lrslog::event("rx_packet", msg.rssi, msg.counter, msg.relay_state);
  }

  return true;
}

void RadioProtocol::deriveKeys() {
  if (!settings_) {
    return;
  }
  SHA256 hash;
  uint8_t digest[32];
  const char *fleet = settings_->fleet_passphrase.c_str();
  const size_t fleetLen = settings_->fleet_passphrase.length();
  const char *factory = runtime_utils::kDefaultDeploymentKey;
  const size_t factoryLen = strlen(runtime_utils::kDefaultDeploymentKey);
  const uint8_t sep = ':';

  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(fleet), fleetLen);
  hash.update(&sep, 1);
  hash.update(reinterpret_cast<const uint8_t *>("enc"), 3);
  hash.finalize(digest, sizeof(digest));
  memcpy(enc_key_, digest, sizeof(enc_key_));

  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(fleet), fleetLen);
  hash.update(&sep, 1);
  hash.update(reinterpret_cast<const uint8_t *>("mac"), 3);
  hash.finalize(digest, sizeof(digest));
  memcpy(mac_key_, digest, sizeof(mac_key_));

  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(factory), factoryLen);
  hash.update(&sep, 1);
  hash.update(reinterpret_cast<const uint8_t *>("enc"), 3);
  hash.finalize(digest, sizeof(digest));
  memcpy(factory_enc_key_, digest, sizeof(factory_enc_key_));

  hash.reset();
  hash.update(reinterpret_cast<const uint8_t *>(factory), factoryLen);
  hash.update(&sep, 1);
  hash.update(reinterpret_cast<const uint8_t *>("mac"), 3);
  hash.finalize(digest, sizeof(digest));
  memcpy(factory_mac_key_, digest, sizeof(factory_mac_key_));
}

void RadioProtocol::refreshRuntimeCfg(const Settings &cfg) {
  runtime_.lora_frequency_hz = cfg.lora_frequency_hz;
  runtime_.lora_tx_power = cfg.lora_tx_power;
  runtime_.lora_spreading_factor = cfg.lora_spreading_factor;
  runtime_.lora_bandwidth_hz = cfg.lora_bandwidth_hz;
  runtime_.lora_coding_rate = cfg.lora_coding_rate;
}

void RadioProtocol::refreshRadioRuntimeState() {
  default_key_configured_ =
      settings_ && runtime_utils::isDefaultDeploymentKey(settings_->fleet_passphrase.c_str());
  lora_enabled_ = true;
  LoRa.idle();
  LoRa.receive();
}
