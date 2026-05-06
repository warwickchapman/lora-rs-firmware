#include "state_machine.h"

#include "logger.h"

namespace {
constexpr uint32_t kRssiGoodIntervalMs = 5000;
constexpr uint32_t kRssiMediumIntervalMs = 3000;
constexpr uint32_t kRssiLowIntervalMs = 1000;
constexpr uint32_t kNoLinkFastIntervalMs = 150;
constexpr uint32_t kMinValidUnixTimeS = 1704067200UL;  // 2024-01-01 UTC
constexpr uint8_t kFlagTimeAuthoritative = 0x01;
constexpr uint8_t kFlagPairedInputSlave = 0x02;
}

void NodeStateMachine::setAuthoritativeUnixTime(uint32_t unixTimeS) {
  if (unixTimeS < kMinValidUnixTimeS) return;
  shared_time_valid_ = true;
  shared_time_authoritative_ = true;
  shared_time_sync_unix_s_ = unixTimeS;
  shared_time_sync_ms_ = millis();
  lrslog::event("time_sync_ntp", 0, unixTimeS, 0);
}

void NodeStateMachine::captureRemoteTemp(uint8_t tempCode) {
  if (tempCode == 0xFF) return;
  remote_temp_valid_ = true;
  remote_temp_c_ = static_cast<int8_t>(tempCode);
  remote_temp_ms_ = millis();
}

uint8_t NodeStateMachine::txFlags() const {
  uint8_t flags = shared_time_authoritative_ ? kFlagTimeAuthoritative : 0U;
  if (runtime_.role_tx && runtime_.input_control_paired_lora_enabled) {
    flags |= kFlagPairedInputSlave;
  }
  return flags;
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
    lrslog::event("time_sync_peer", 0, unixTimeS, 0);
    return;
  }
  if (shared_time_authoritative_) {
    return;
  }
  const uint32_t current = currentUnixTimeS(now);
  if (unixTimeS > current || (current > unixTimeS && (current - unixTimeS) > 30U)) {
    shared_time_sync_unix_s_ = unixTimeS;
    shared_time_sync_ms_ = now;
    lrslog::event("time_sync_peer", 0, unixTimeS, 0);
  }
}

uint32_t NodeStateMachine::currentUnixTimeS(uint32_t nowMs) const {
  if (!shared_time_valid_) return 0;
  return shared_time_sync_unix_s_ + ((nowMs - shared_time_sync_ms_) / 1000U);
}

void NodeStateMachine::tickLed() {
  const uint32_t now = millis();
  if (tickIdentifyLed(now)) return;

  uint32_t blinkInterval = kNoLinkFastIntervalMs;

  if (last_packet_ms_ != 0 && (now - last_packet_ms_) <= (runtime_.heartbeat_ms * 2U)) {
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

void NodeStateMachine::triggerIdentify(uint32_t durationMs) {
  if (durationMs == 0) durationMs = kIdentifyLedDurationMs;
  const uint32_t now = millis();
  identify_led_started_ms_ = now;
  identify_led_until_ms_ = now + durationMs;
  led_on_ = false;
  last_led_toggle_ms_ = now;
  tickIdentifyLed(now);
}

bool NodeStateMachine::tickIdentifyLed(uint32_t now) {
  if (identify_led_until_ms_ == 0) return false;
  if (static_cast<int32_t>(now - identify_led_until_ms_) >= 0) {
    identify_led_until_ms_ = 0;
    identify_led_started_ms_ = 0;
    led_on_ = false;
    digitalWrite(kLedPin, HIGH);
    return false;
  }

  const uint32_t t = (now - identify_led_started_ms_) % 2000U;
  const bool on =
      (t < 120U) || (t >= 240U && t < 360U) || (t >= 480U && t < 600U) ||
      (t >= 980U && t < 1100U) || (t >= 1220U && t < 1340U) ||
      (t >= 1460U && t < 1580U);
  led_on_ = on;
  digitalWrite(kLedPin, on ? LOW : HIGH);
  return true;
}
