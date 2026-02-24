#include "state_machine.h"

#include "logger.h"

namespace {
constexpr uint8_t kLedPin = 2;
constexpr uint32_t kRssiGoodIntervalMs = 5000;
constexpr uint32_t kRssiMediumIntervalMs = 3000;
constexpr uint32_t kRssiLowIntervalMs = 1000;
constexpr uint32_t kNoLinkFastIntervalMs = 150;
constexpr uint32_t kMinValidUnixTimeS = 1704067200UL;  // 2024-01-01 UTC
constexpr uint8_t kFlagTimeAuthoritative = 0x01;
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
