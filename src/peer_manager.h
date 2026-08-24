#pragma once

#include "peer_types.h"
#include "config_store.h"

class PeerManager {
public:
  PeerManager();

  void begin(uint8_t localAddress);
  void applyConfig(uint8_t localAddress);
  void reset();

  size_t count() const { return peer_count_; }
  PeerRuntime* find(uint8_t address);
  const PeerRuntime* find(uint8_t address) const;
  PeerRuntime* findByIndex(size_t index);
  const PeerRuntime* findByIndex(size_t index) const;

  PeerRuntime* findOrCreate(uint8_t address, uint32_t chipId);
  bool forget(uint8_t address);
  void clearAllPending();

  bool buildStatusSnapshot(size_t index, PeerStatusSnapshot &out) const;

private:
  PeerRuntime peers_[LRS_MAX_PEERS];
  size_t peer_count_ = 0;

  uint8_t local_address_ = 0;

  void removePeerAt(size_t idx);
};
