#include "peer_manager.h"
#include "runtime_utils.h"
#include "logger.h"
#include <new>

PeerManager::PeerManager() {
  reset();
}

PeerManager::~PeerManager() {
  freePollStorage();
}

void PeerManager::begin(uint8_t localAddress) {
  local_address_ = localAddress;
}

void PeerManager::applyConfig(uint8_t localAddress) {
  local_address_ = localAddress;
  reset();
}

void PeerManager::reset() {
  for (size_t i = 0; i < LRS_MAX_PEERS; ++i) {
    peers_[i] = PeerRuntime{};
  }
  peer_count_ = 0;
  resetPollStorage();
}

PeerRuntime* PeerManager::find(uint8_t address) {
  if (address == 0 || address == 255) return nullptr;
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return &peers_[i];
    }
  }
  return nullptr;
}

const PeerRuntime* PeerManager::find(uint8_t address) const {
  if (address == 0 || address == 255) return nullptr;
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return &peers_[i];
    }
  }
  return nullptr;
}

PeerRuntime* PeerManager::findByIndex(size_t index) {
  if (index >= peer_count_) return nullptr;
  return &peers_[index];
}

const PeerRuntime* PeerManager::findByIndex(size_t index) const {
  if (index >= peer_count_) return nullptr;
  return &peers_[index];
}

bool PeerManager::ensurePollStorage() {
  if (poll_states_ != nullptr && poll_state_capacity_ >= LRS_MAX_PEERS) return true;
  freePollStorage();
  poll_states_ = new (std::nothrow) PollRuntime[LRS_MAX_PEERS];
  if (poll_states_ == nullptr) {
    poll_state_capacity_ = 0;
#if !defined(UNIT_TEST)
    lrslog::event("poll_storage_oom", 0, static_cast<uint32_t>(LRS_MAX_PEERS & 0xFFFFU),
                  static_cast<uint8_t>(sizeof(PollRuntime) & 0xFFU));
#endif
    return false;
  }
  poll_state_capacity_ = LRS_MAX_PEERS;
  resetPollStorage();
  return true;
}

PollRuntime* PeerManager::pollStateForIndex(size_t index) {
  if (poll_states_ == nullptr || index >= poll_state_capacity_) return nullptr;
  return &poll_states_[index];
}

const PollRuntime* PeerManager::pollStateForIndex(size_t index) const {
  if (poll_states_ == nullptr || index >= poll_state_capacity_) return nullptr;
  return &poll_states_[index];
}

PollRuntime* PeerManager::pollStateForPeer(const PeerRuntime *peer) {
  if (peer == nullptr) return nullptr;
  size_t idx = static_cast<size_t>(peer - peers_);
  return pollStateForIndex(idx);
}

const PollRuntime* PeerManager::pollStateForPeer(const PeerRuntime *peer) const {
  if (peer == nullptr) return nullptr;
  size_t idx = static_cast<size_t>(peer - peers_);
  return pollStateForIndex(idx);
}

PeerRuntime* PeerManager::findOrCreate(uint8_t address, uint32_t chipId, uint32_t defaultInterval, bool pollingEnabled, uint32_t now) {
  if (address == 0 || address == 255 || address == local_address_) {
    return nullptr;
  }
  PeerRuntime *node = find(address);
  if (node != nullptr) {
    return node;
  }
  if (peer_count_ >= LRS_MAX_PEERS) {
    return nullptr;
  }
  PeerRuntime &new_node = peers_[peer_count_++];
  new_node = PeerRuntime{};
  new_node.in_use = true;
  new_node.address = address;
  new_node.chip_id = chipId;

  uint32_t interval = defaultInterval;
  new_node.poll_interval_ms = pollingEnabled ? interval : 0;
  if (new_node.poll_interval_ms > 0) {
    if (ensurePollStorage()) {
      PollRuntime *poll = pollStateForIndex(peer_count_ - 1);
      if (poll != nullptr) {
        poll->next_poll_ms = now + new_node.poll_interval_ms;
      }
    }
  }
  return &new_node;
}

bool PeerManager::forget(uint8_t address) {
  if (address == 0 || address == 255) return false;
  size_t idx = LRS_MAX_PEERS;
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      idx = i;
      break;
    }
  }
  if (idx >= peer_count_) return false;
  removePeerAt(idx);
  return true;
}

void PeerManager::removePeerAt(size_t idx) {
  if (idx >= peer_count_) return;
  for (size_t i = idx; i + 1 < peer_count_; ++i) {
    peers_[i] = peers_[i + 1];
    if (poll_states_ != nullptr && i + 1 < poll_state_capacity_) {
      poll_states_[i] = poll_states_[i + 1];
    }
  }
  if (peer_count_ > 0) {
    peer_count_--;
    peers_[peer_count_] = PeerRuntime{};
    if (poll_states_ != nullptr && peer_count_ < poll_state_capacity_) {
      poll_states_[peer_count_] = PollRuntime{};
    }
  }
}

void PeerManager::clearAllPending() {
  for (size_t i = 0; i < peer_count_; ++i) {
    peers_[i].pending = false;
    peers_[i].retry_step = 0;
    peers_[i].next_retry_ms = 0;
    peers_[i].pending_counter = 0;
    peers_[i].pending_deadline_ms = 0;
    peers_[i].wifi_pending = false;
  }
  if (poll_states_ != nullptr) {
    for (size_t i = 0; i < poll_state_capacity_; ++i) {
      poll_states_[i].poll_pending = false;
      poll_states_[i].poll_retry_step = 0;
      poll_states_[i].poll_next_retry_ms = 0;
      poll_states_[i].poll_counter = 0;
      poll_states_[i].poll_deadline_ms = 0;
    }
  }
}



bool PeerManager::buildStatusSnapshot(size_t index, PeerStatusSnapshot &out) const {
  if (index >= peer_count_) return false;
  const PeerRuntime &p = peers_[index];
  out = PeerStatusSnapshot{};
  out.address = p.address;
  out.relay_state = p.relay_state;
  out.input_state = p.input_state;
  out.input_state_known = p.input_state_known;
  out.sensors = p.sensors;
  out.uplink_rssi = p.uplink_rssi;
  out.downlink_rssi_valid = p.downlink_rssi_valid;
  out.downlink_rssi = p.downlink_rssi;
  out.last_seen_ms = p.last_seen_ms;
  out.last_cmd_counter = p.last_cmd_counter;
  out.ack_state = p.ack_state;
  out.poll_interval_ms = p.poll_interval_ms;
  
  const PollRuntime *poll = pollStateForIndex(index);
  if (poll != nullptr) {
    out.poll_pending = poll->poll_pending;
    out.last_poll_tx_ms = poll->last_poll_tx_ms;
  }
  
  out.wifi_state_known = p.wifi_state_known;
  out.wifi_enabled = p.wifi_enabled;
  out.wifi_connected_known = p.wifi_connected_known;
  out.wifi_connected = p.wifi_connected;
  for (int i = 0; i < 4; ++i) out.ip[i] = p.ip[i];
  out.mqtt_state_known = p.mqtt_state_known;
  out.mqtt_enabled = p.mqtt_enabled;
  out.mqtt_connected = p.mqtt_connected;
  out.chip_id = p.chip_id;
  out.fw_major = p.fw_major;
  out.fw_minor = p.fw_minor;
  out.fw_patch = p.fw_patch;
  out.fw_build = p.fw_build;
  out.uptime_ms = p.uptime_ms;
  out.maintenance_debug_known = p.maintenance_debug_known;
  out.heap_free = p.heap_free;
  out.heap_max_block = p.heap_max_block;
  out.heap_frag_pct = p.heap_frag_pct;
  out.debug_uptime_ms = p.debug_uptime_ms;
  out.wifi_last_confirm_ms = p.wifi_last_confirm_ms;
  out.power_save_listen_only = p.power_save_listen_only;
  out.power_save_active = p.power_save_active;
  return true;
}

void PeerManager::freePollStorage() {
  if (poll_states_ != nullptr) {
    delete[] poll_states_;
    poll_states_ = nullptr;
  }
  poll_state_capacity_ = 0;
}

void PeerManager::resetPollStorage() {
  if (poll_states_ == nullptr) return;
  for (size_t i = 0; i < poll_state_capacity_; ++i) {
    poll_states_[i] = PollRuntime{};
  }
}
