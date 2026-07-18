#include "state_machine.h"

#include "logger.h"

namespace {
constexpr uint32_t kReplayEntryStaleMs = 15UL * 60UL * 1000UL;
}

bool NodeStateMachine::isTrustedReplaySource(uint8_t src, bool commissioningTraffic) const {
  if (src == 0 || src == 255) return false;
  if (!runtime_.role_tx && src == runtime_.controller_address) return true;
  if (commissioningTraffic) return true;
  if (peer_manager_.find(src) != nullptr) return true;
  return false;
}

bool NodeStateMachine::shouldAcceptReplayAndUpdate(const ProtocolMessage &msg, bool trustedSourceHint) {
  const uint32_t now = millis();
  size_t used = 0;
  int matchIdx = -1;
  int emptyIdx = -1;
  int oldestIdx = -1;
  uint32_t oldestSeenMs = 0;
  int staleIdx = -1;
  uint32_t staleSeenMs = 0;

  for (size_t i = 0; i < kReplayTrackedSources; ++i) {
    ReplaySourceState &s = replay_sources_[i];
    if (!s.in_use) {
      if (emptyIdx < 0) emptyIdx = static_cast<int>(i);
      continue;
    }
    used++;
    if (s.src == msg.src) {
      matchIdx = static_cast<int>(i);
      break;
    }
    if (oldestIdx < 0 || static_cast<int32_t>(s.last_seen_ms - oldestSeenMs) < 0) {
      oldestIdx = static_cast<int>(i);
      oldestSeenMs = s.last_seen_ms;
    }
    if (static_cast<uint32_t>(now - s.last_seen_ms) >= kReplayEntryStaleMs) {
      if (staleIdx < 0 || static_cast<int32_t>(s.last_seen_ms - staleSeenMs) < 0) {
        staleIdx = static_cast<int>(i);
        staleSeenMs = s.last_seen_ms;
      }
    }
  }

  if (used > replay_table_peak_used_) replay_table_peak_used_ = static_cast<uint8_t>(used);

  if (matchIdx >= 0) {
    ReplaySourceState &s = replay_sources_[matchIdx];
    if (s.boot_nonce == msg.boot_nonce) {
      if (msg.counter <= s.counter) {
        lrslog::event("rx_replay_drop", msg.rssi, msg.counter, msg.relay_state);
        return false;
      }
    } else if (s.boot_nonce != 0) {
      lrslog::event("rx_peer_reboot", msg.rssi, msg.counter, msg.relay_state);
    }
    s.boot_nonce = msg.boot_nonce;
    s.counter = msg.counter;
    s.last_seen_ms = now;
    return true;
  }

  int insertIdx = emptyIdx;
  bool staleEvict = false;
  if (insertIdx < 0 && staleIdx >= 0) {
    insertIdx = staleIdx;
    staleEvict = true;
  }

  if (insertIdx < 0) {
    if (!trustedSourceHint) {
      replay_table_full_drops_++;
      lrslog::event("rx_replay_table_full_drop", msg.rssi, msg.counter, msg.relay_state);
      return false;
    }
    insertIdx = oldestIdx;
    replay_table_evictions_++;
    lrslog::event("rx_replay_table_evict", msg.rssi, msg.counter, msg.relay_state);
  } else if (staleEvict) {
    replay_table_evictions_++;
    replay_table_stale_evictions_++;
    lrslog::event("rx_replay_table_stale_evict", msg.rssi, msg.counter, msg.relay_state);
  }

  if (insertIdx < 0) {
    replay_table_full_drops_++;
    lrslog::event("rx_replay_table_full_drop", msg.rssi, msg.counter, msg.relay_state);
    return false;
  }

  ReplaySourceState &s = replay_sources_[insertIdx];
  s.in_use = true;
  s.src = msg.src;
  s.boot_nonce = msg.boot_nonce;
  s.counter = msg.counter;
  s.last_seen_ms = now;
  used++;
  if (used > replay_table_peak_used_) replay_table_peak_used_ = static_cast<uint8_t>(used);
  return true;
}
