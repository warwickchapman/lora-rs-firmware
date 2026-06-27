#pragma once

#include "peer_types.h"
#include "config_store.h"

class PeerManager {
public:
  PeerManager();
  ~PeerManager();

  void begin(uint8_t localAddress);
  void applyConfig(uint8_t localAddress);
  void reset();

  size_t count() const { return peer_count_; }
  PeerRuntime* find(uint8_t address);
  const PeerRuntime* find(uint8_t address) const;
  PeerRuntime* findByIndex(size_t index);
  const PeerRuntime* findByIndex(size_t index) const;

  // Lazy allocation & index alignment matching original behavior
  bool ensurePollStorage();
  PollRuntime* pollStateForIndex(size_t index);
  const PollRuntime* pollStateForIndex(size_t index) const;
  PollRuntime* pollStateForPeer(const PeerRuntime *peer);
  const PollRuntime* pollStateForPeer(const PeerRuntime *peer) const;

  // Slot mutations (preserves returning nullptr when full)
  PeerRuntime* findOrCreate(uint8_t address, uint32_t chipId, uint32_t defaultInterval, bool pollingEnabled, uint32_t now);
  bool forget(uint8_t address);
  void clearAllPending();

  bool buildStatusSnapshot(size_t index, PeerStatusSnapshot &out) const;
  void freePollStorage();

private:
  PeerRuntime peers_[LRS_MAX_PEERS];
  size_t peer_count_ = 0;

  // Existing lazy allocation behavior preserved inside manager
  PollRuntime *poll_states_ = nullptr;
  size_t poll_state_capacity_ = 0;

  uint8_t local_address_ = 0;

  void resetPollStorage();
  void removePeerAt(size_t idx);
};
