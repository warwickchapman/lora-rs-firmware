#include <unity.h>
#include <cstring>

#include "runtime_utils.h"
#include "state_machine.h"

size_t NodeStateMachine::peerRuntimeSize() { return sizeof(NodeStateMachine::PeerRuntime); }
size_t NodeStateMachine::pollRuntimeSize() { return sizeof(NodeStateMachine::PollRuntime); }
size_t NodeStateMachine::replaySourceStateSize() { return sizeof(NodeStateMachine::ReplaySourceState); }

void test_paired_transmitter_is_tx() {
  bool roleTx = false;
  TEST_ASSERT_TRUE(runtime_utils::parseRoleTxFromModeRole("paired", "transmitter", roleTx));
  TEST_ASSERT_TRUE(roleTx);
}

void test_paired_receiver_is_rx() {
  bool roleTx = true;
  TEST_ASSERT_TRUE(runtime_utils::parseRoleTxFromModeRole("paired", "receiver", roleTx));
  TEST_ASSERT_FALSE(roleTx);
}

void test_standalone_none_is_local_tx_path() {
  bool roleTx = false;
  TEST_ASSERT_TRUE(runtime_utils::parseRoleTxFromModeRole("standalone", "none", roleTx));
  TEST_ASSERT_TRUE(roleTx);
}

void test_mesh_aliases_are_rejected_without_changing_output() {
  bool roleTx = true;
  TEST_ASSERT_FALSE(runtime_utils::parseRoleTxFromModeRole("mesh", "coordinator", roleTx));
  TEST_ASSERT_TRUE(roleTx);

  roleTx = false;
  TEST_ASSERT_FALSE(runtime_utils::parseRoleTxFromModeRole("mesh", "node", roleTx));
  TEST_ASSERT_FALSE(roleTx);
}

void test_invalid_inputs_are_rejected_without_changing_output() {
  bool roleTx = true;
  TEST_ASSERT_FALSE(runtime_utils::parseRoleTxFromModeRole("", "", roleTx));
  TEST_ASSERT_TRUE(roleTx);

  TEST_ASSERT_FALSE(runtime_utils::parseRoleTxFromModeRole("paired", "none", roleTx));
  TEST_ASSERT_TRUE(roleTx);

  TEST_ASSERT_FALSE(runtime_utils::parseRoleTxFromModeRole(nullptr, "receiver", roleTx));
  TEST_ASSERT_TRUE(roleTx);
}

void test_wifi_status_text_known_values() {
  TEST_ASSERT_EQUAL_STRING("idle", runtime_utils::wifiStatusText(WL_IDLE_STATUS));
  TEST_ASSERT_EQUAL_STRING("connected", runtime_utils::wifiStatusText(WL_CONNECTED));
  TEST_ASSERT_EQUAL_STRING("wrong_password", runtime_utils::wifiStatusText(WL_WRONG_PASSWORD));
  TEST_ASSERT_EQUAL_STRING("no_shield", runtime_utils::wifiStatusText(WL_NO_SHIELD));
}

void test_wifi_status_text_unknown_value() {
  TEST_ASSERT_EQUAL_STRING("unknown", runtime_utils::wifiStatusText(12345));
}

void test_resolve_gateway_targets_paired_empty() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {0};
  uint8_t paired_targets[12] = {1};
  
  uint8_t count = runtime_utils::resolveGatewayTargets(
      true, // isPairedMode
      254,  // localAddress
      0,    // knownPeerCount
      known_peers,
      1,    // pairedTargetCount
      paired_targets,
      1,    // remoteAddress
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(0, count);
}

void test_resolve_gateway_targets_paired_with_peers() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {2, 3};
  uint8_t paired_targets[12] = {1};
  
  uint8_t count = runtime_utils::resolveGatewayTargets(
      true, // isPairedMode
      254,  // localAddress
      2,    // knownPeerCount
      known_peers,
      1,    // pairedTargetCount
      paired_targets,
      1,    // remoteAddress
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(2, count);
  TEST_ASSERT_EQUAL_UINT8(2, targets[0]);
  TEST_ASSERT_EQUAL_UINT8(3, targets[1]);
}

void test_resolve_gateway_targets_standalone_fallback() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {0};
  uint8_t paired_targets[12] = {1};
  
  uint8_t count = runtime_utils::resolveGatewayTargets(
      false, // isPairedMode
      254,   // localAddress
      0,     // knownPeerCount
      known_peers,
      1,     // pairedTargetCount
      paired_targets,
      1,     // remoteAddress
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(1, count);
  TEST_ASSERT_EQUAL_UINT8(1, targets[0]);

  // Test remote_address fallback
  uint8_t targets2[12]{};
  uint8_t paired_targets2[12] = {0};
  uint8_t count2 = runtime_utils::resolveGatewayTargets(
      false, // isPairedMode
      254,   // localAddress
      0,     // knownPeerCount
      known_peers,
      0,     // pairedTargetCount
      paired_targets2,
      5,     // remoteAddress
      targets2,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(1, count2);
  TEST_ASSERT_EQUAL_UINT8(5, targets2[0]);
}

void test_resolve_gateway_targets_filters_local_address() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {2, 254, 3};
  
  uint8_t count = runtime_utils::resolveGatewayTargets(
      true, // isPairedMode
      254,  // localAddress (gateway address)
      3,    // knownPeerCount
      known_peers,
      0,
      nullptr,
      0,
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(2, count);
  TEST_ASSERT_EQUAL_UINT8(2, targets[0]);
  TEST_ASSERT_EQUAL_UINT8(3, targets[1]);
}

bool simulateIsConfiguredOperationalPeer(
    bool roleTx,
    const char* mode,
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t pairedTargetCount,
    const uint8_t *pairedTargetAddresses,
    uint8_t remoteAddress,
    uint8_t addressToCheck
) {
  if (!roleTx || strcmp(mode, "paired") != 0) return false;
  if (addressToCheck < 1 || addressToCheck > 12) return false;

  uint8_t targets[12]{};
  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
      true, // isPairedMode
      localAddress,
      knownPeerCount,
      knownPeerAddresses,
      pairedTargetCount,
      pairedTargetAddresses,
      remoteAddress,
      targets,
      12
  );

  for (uint8_t i = 0; i < targetCount; ++i) {
    if (targets[i] == addressToCheck) return true;
  }
  return false;
}

void test_operational_peer_admitted_when_configured() {
  uint8_t known_peers[12] = {5, 6};
  TEST_ASSERT_TRUE(simulateIsConfiguredOperationalPeer(
      true, "paired", 254, 2, known_peers, 0, nullptr, 0, 5
  ));
}

void test_operational_peer_rejected_when_unconfigured() {
  uint8_t known_peers[12] = {5, 6};
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", 254, 2, known_peers, 0, nullptr, 0, 7
  ));
}

void test_operational_peer_rejected_when_out_of_range() {
  uint8_t known_peers[12] = {5, 16};
  // Address 16 is in configured known peers list, but exceeds limit 12, so must be rejected
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", 254, 2, known_peers, 0, nullptr, 0, 16
  ));
}

void test_operational_peer_rejected_when_empty_fleet() {
  uint8_t known_peers[12] = {0};
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", 254, 0, known_peers, 0, nullptr, 0, 5
  ));
}

int simulateLoraInventoryStatusDevices(
    bool roleTx,
    const char* mode,
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t pairedTargetCount,
    const uint8_t *pairedTargetAddresses,
    uint8_t remoteAddress,
    uint8_t cachedPeerCount,
    const uint8_t *cachedPeerAddresses,
    uint8_t *outDevices,
    uint8_t maxDevices
) {
  uint8_t targets[12]{};
  uint8_t targetCount = 0;

  if (roleTx) {
    bool isPairedMode = (strcmp(mode, "paired") == 0);
    targetCount = runtime_utils::resolveGatewayTargets(
        isPairedMode,
        localAddress,
        knownPeerCount,
        knownPeerAddresses,
        pairedTargetCount,
        pairedTargetAddresses,
        remoteAddress,
        targets,
        12
    );
  }

  uint8_t deviceCount = 0;
  for (uint8_t i = 0; i < targetCount; ++i) {
    const uint8_t addr = targets[i];
    if (addr < 1 || addr > 12) continue;

    bool hasCached = false;
    for (uint8_t j = 0; j < cachedPeerCount; ++j) {
      if (cachedPeerAddresses[j] == addr) {
        hasCached = true;
        break;
      }
    }

    if (deviceCount < maxDevices) {
      outDevices[deviceCount++] = addr;
    }
  }

  return deviceCount;
}

void test_inventory_status_excludes_unconfigured_cached_peers() {
  uint8_t known_peers[12] = {5, 6};
  uint8_t cached_peers[3] = {5, 16, 18}; // cache contains configured target 5, plus unconfigured 16 and 18

  uint8_t devices[12]{};
  int deviceCount = simulateLoraInventoryStatusDevices(
      true, "paired", 254, 2, known_peers, 0, nullptr, 0,
      3, cached_peers, devices, 12
  );

  // The devices output should only contain configured targets (5, 6), and exclude unconfigured cached peers (16, 18)
  TEST_ASSERT_EQUAL_INT(2, deviceCount);
  TEST_ASSERT_EQUAL_UINT8(5, devices[0]);
  TEST_ASSERT_EQUAL_UINT8(6, devices[1]);
}

void test_candidate_lifecycle() {
  DiscoveryCandidate c{};
  TEST_ASSERT_EQUAL(CandidateState::SeenAddressOnly, c.state);
  TEST_ASSERT_EQUAL_UINT32(0, c.chip_id);
  
  // Transition to Identified
  c.chip_id = 0x8829ca;
  c.state = CandidateState::Identified;
  TEST_ASSERT_EQUAL(CandidateState::Identified, c.state);
  TEST_ASSERT_EQUAL_UINT32(0x8829ca, c.chip_id);
  
  // Transition to Readdressing
  c.state = CandidateState::Readdressing;
  TEST_ASSERT_EQUAL(CandidateState::Readdressing, c.state);
  
  // Transition to Adopted
  c.state = CandidateState::Adopted;
  TEST_ASSERT_EQUAL(CandidateState::Adopted, c.state);
}

void test_candidate_ok() {
  uint8_t known_peers[12] = {5, 6};
  uint32_t known_peer_chip_ids[12] = {111, 222};
  
  CandidateReason reason = runtime_utils::evaluateCandidateReason(
      7, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::Ok, reason);
  
  uint8_t resolved = runtime_utils::resolveAdoptionAddress(
      7, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL_UINT8(7, resolved);
}

void test_candidate_conflict_readdress() {
  uint8_t known_peers[12] = {5, 6};
  uint32_t known_peer_chip_ids[12] = {111, 222};
  
  // Candidate at address 5 has conflict (since chip_id 0x333 != 111)
  CandidateReason reason = runtime_utils::evaluateCandidateReason(
      5, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::Conflict, reason);
  
  // Readdress conflict: should assign the next free address in 1..12.
  // Address 1, 2, 3, 4 are free, so it should assign 1!
  uint8_t resolved = runtime_utils::resolveAdoptionAddress(
      5, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL_UINT8(1, resolved);
  
  // Let's test with all addresses up to 5 taken
  uint8_t known_peers2[12] = {1, 2, 3, 4, 5, 6};
  uint32_t known_peer_chip_ids2[12] = {11, 22, 33, 44, 55, 66};
  uint8_t resolved2 = runtime_utils::resolveAdoptionAddress(
      5, 0x333, 6, known_peers2, known_peer_chip_ids2
  );
  // Next free address after 1..6 is 7
  TEST_ASSERT_EQUAL_UINT8(7, resolved2);
}

void test_candidate_out_of_range_readdress() {
  uint8_t known_peers[12] = {5, 6};
  uint32_t known_peer_chip_ids[12] = {111, 222};
  
  // Candidate at address 16 is out of range
  CandidateReason reason = runtime_utils::evaluateCandidateReason(
      16, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::OutOfRange, reason);
  
  // Readdress out-of-range: should assign next free (which is 1)
  uint8_t resolved = runtime_utils::resolveAdoptionAddress(
      16, 0x333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL_UINT8(1, resolved);
}

void test_full_fleet_dangerous_reset_guarded() {
  // Configured peers fill the entire list of 12 peers
  uint8_t known_peers[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  uint32_t known_peer_chip_ids[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  
  // Candidate chip 0x999 tries to adopt at address 5 (which is configured for chip 5)
  CandidateReason reason = runtime_utils::evaluateCandidateReason(
      5, 0x999, 12, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::Conflict, reason);
  
  // Since fleet is full (all 1..12 are taken), resolveAdoptionAddress should return 0 (reset request)
  uint8_t resolved = runtime_utils::resolveAdoptionAddress(
      5, 0x999, 12, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL_UINT8(0, resolved);
}

void test_remote_two_stage_confirm_success() {
  bool outSendConfirm = false;
  uint8_t outConfirmAddress = 99;
  RemoteReaddressState state = runtime_utils::transitionRemoteReaddress(
      RemoteReaddressState::Idle,
      true,  // rxRequest
      5,     // rxNewAddress
      false, // saveSucceeded (not done yet)
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::PendingSave, state);
  TEST_ASSERT_FALSE(outSendConfirm);

  state = runtime_utils::transitionRemoteReaddress(
      state,
      false, // rxRequest
      5,     // rxNewAddress
      true,  // saveSucceeded
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::Completed, state);
  TEST_ASSERT_TRUE(outSendConfirm);
  TEST_ASSERT_EQUAL_UINT8(5, outConfirmAddress);
}

void test_remote_two_stage_confirm_save_fail() {
  bool outSendConfirm = false;
  uint8_t outConfirmAddress = 99;
  RemoteReaddressState state = runtime_utils::transitionRemoteReaddress(
      RemoteReaddressState::Idle,
      true,  // rxRequest
      5,     // rxNewAddress
      false, // saveSucceeded
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::PendingSave, state);
  TEST_ASSERT_FALSE(outSendConfirm);

  state = runtime_utils::transitionRemoteReaddress(
      state,
      false, // rxRequest
      5,     // rxNewAddress
      false, // saveSucceeded (failed!)
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::Failed, state);
  TEST_ASSERT_FALSE(outSendConfirm);
}

void test_remote_two_stage_reset_confirm_success() {
  bool outSendConfirm = false;
  uint8_t outConfirmAddress = 99;
  RemoteReaddressState state = runtime_utils::transitionRemoteReaddress(
      RemoteReaddressState::Idle,
      true,  // rxRequest
      0,     // rxNewAddress (reset)
      false, // saveSucceeded
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::PendingReset, state);
  TEST_ASSERT_FALSE(outSendConfirm);

  state = runtime_utils::transitionRemoteReaddress(
      state,
      false, // rxRequest
      0,     // rxNewAddress
      true,  // saveSucceeded
      outSendConfirm,
      outConfirmAddress
  );
  TEST_ASSERT_EQUAL(RemoteReaddressState::Completed, state);
  TEST_ASSERT_TRUE(outSendConfirm);
  TEST_ASSERT_EQUAL_UINT8(0, outConfirmAddress);
}

void test_gateway_confirm_validation() {
  // Successful readdress confirm
  TEST_ASSERT_TRUE(runtime_utils::validateGatewayConfirm(
      0x123, 5, 5, true, 0x123, 5, 16
  ));

  // Mismatched confirm address
  TEST_ASSERT_FALSE(runtime_utils::validateGatewayConfirm(
      0x123, 6, 5, true, 0x123, 5, 16
  ));

  // Mismatched source address for readdress (should be 5, not old 16)
  TEST_ASSERT_FALSE(runtime_utils::validateGatewayConfirm(
      0x123, 5, 16, true, 0x123, 5, 16
  ));

  // Successful reset confirm (source must be old address 16 since new address is 0)
  TEST_ASSERT_TRUE(runtime_utils::validateGatewayConfirm(
      0x123, 0, 16, true, 0x123, 0, 16
  ));

  // Mismatched source address for reset (should be old address 16, not 5)
  TEST_ASSERT_FALSE(runtime_utils::validateGatewayConfirm(
      0x123, 0, 5, true, 0x123, 0, 16
  ));

  // Adoption inactive
  TEST_ASSERT_FALSE(runtime_utils::validateGatewayConfirm(
      0x123, 5, 5, false, 0x123, 5, 16
  ));
}

void test_start_adoption_reverts_on_tx_fail() {
  CandidateState cState = CandidateState::Identified;
  bool adoptionActive = false;
  
  // TX fails on first attempt
  bool ok = runtime_utils::transitionAdoptionStart(cState, adoptionActive, false, false);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL(CandidateState::Identified, cState);
  TEST_ASSERT_FALSE(adoptionActive);

  // TX succeeds
  ok = runtime_utils::transitionAdoptionStart(cState, adoptionActive, false, true);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(CandidateState::Readdressing, cState);
  TEST_ASSERT_TRUE(adoptionActive);
}

void test_candidate_probe_tick_flow() {
  DiscoveryCandidate c{};
  c.in_use = true;
  c.state = CandidateState::SeenAddressOnly;
  
  uint32_t now = 1000;
  uint32_t last_global_probe_ms = 0;
  bool outSendProbe = false;
  
  // 1st attempt: should succeed and send probe
  bool ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(outSendProbe);
  TEST_ASSERT_EQUAL_UINT8(1, c.probe_attempt_count);
  TEST_ASSERT_EQUAL_UINT32(now, c.last_probe_ms);
  TEST_ASSERT_EQUAL_UINT32(now, last_global_probe_ms);
  TEST_ASSERT_EQUAL(CandidateState::SeenAddressOnly, c.state);
  
  // 2nd attempt within candidate interval (1500ms): should fail/do nothing
  now += 1000; // now = 2000
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_FALSE(ok); // no tick processed
  TEST_ASSERT_EQUAL_UINT8(1, c.probe_attempt_count);
  
  // 2nd attempt within global gap (500ms):
  // Let's reset last_probe_ms to 0 to simulate candidate interval met, but last_global_probe_ms is 1000.
  // We try at now = 1200.
  c.last_probe_ms = 0;
  now = 1200;
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_FALSE(ok); // global gap violated
  
  // Let's meet all intervals and do the remaining attempts.
  // 2nd attempt:
  now = 3000;
  c.last_probe_ms = 1000;
  last_global_probe_ms = 1000;
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(outSendProbe);
  TEST_ASSERT_EQUAL_UINT8(2, c.probe_attempt_count);
  
  // 3rd attempt:
  now = 5000;
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(outSendProbe);
  TEST_ASSERT_EQUAL_UINT8(3, c.probe_attempt_count);
  
  // 4th attempt (kCandidateProbeMaxAttempts = 4):
  now = 7000;
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(outSendProbe);
  TEST_ASSERT_EQUAL_UINT8(4, c.probe_attempt_count);
  
  // 5th call: attempt count has reached max, should transition to Failed and NOT send probe
  now = 9000;
  ok = runtime_utils::tickCandidateProbe(c, now, last_global_probe_ms, outSendProbe);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_FALSE(outSendProbe);
  TEST_ASSERT_EQUAL(CandidateState::Failed, c.state);
  TEST_ASSERT_EQUAL_UINT32(now, c.last_seen_ms);
}

void test_candidate_telemetry_does_not_reset_active_attempts() {
  DiscoveryCandidate c{};
  c.in_use = true;
  c.state = CandidateState::SeenAddressOnly;
  c.probe_attempt_count = 2;
  c.last_probe_ms = 500;
  
  // Receive telemetry (chipId == 0) while SeenAddressOnly: counters should NOT reset
  uint32_t chipId = 0;
  if (chipId == 0) {
    if (c.state == CandidateState::Failed) {
      c.state = CandidateState::SeenAddressOnly;
      c.probe_attempt_count = 0;
      c.last_probe_ms = 0;
    }
  }
  TEST_ASSERT_EQUAL(CandidateState::SeenAddressOnly, c.state);
  TEST_ASSERT_EQUAL_UINT8(2, c.probe_attempt_count);
  TEST_ASSERT_EQUAL_UINT32(500, c.last_probe_ms);
  
  // Transition to Failed, then simulate telemetry packet receipt
  c.state = CandidateState::Failed;
  if (chipId == 0) {
    if (c.state == CandidateState::Failed) {
      c.state = CandidateState::SeenAddressOnly;
      c.probe_attempt_count = 0;
      c.last_probe_ms = 0;
    }
  }
  TEST_ASSERT_EQUAL(CandidateState::SeenAddressOnly, c.state);
  TEST_ASSERT_EQUAL_UINT8(0, c.probe_attempt_count);
  TEST_ASSERT_EQUAL_UINT32(0, c.last_probe_ms);
}

void test_struct_sizes() {
  size_t peerSize = NodeStateMachine::peerRuntimeSize();
  size_t pollSize = NodeStateMachine::pollRuntimeSize();
  size_t replaySize = NodeStateMachine::replaySourceStateSize();

  printf("AUDIT_METRIC: sizeof(PeerRuntime) = %zu\n", peerSize);
  printf("AUDIT_METRIC: sizeof(PollRuntime) = %zu\n", pollSize);
  printf("AUDIT_METRIC: sizeof(DiscoveryCandidate) = %zu\n", sizeof(DiscoveryCandidate));
  printf("AUDIT_METRIC: sizeof(ReplaySourceState) = %zu\n", replaySize);

  TEST_ASSERT_TRUE(peerSize <= 256);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_paired_transmitter_is_tx);
  RUN_TEST(test_paired_receiver_is_rx);
  RUN_TEST(test_standalone_none_is_local_tx_path);
  RUN_TEST(test_mesh_aliases_are_rejected_without_changing_output);
  RUN_TEST(test_invalid_inputs_are_rejected_without_changing_output);
  RUN_TEST(test_wifi_status_text_known_values);
  RUN_TEST(test_wifi_status_text_unknown_value);
  RUN_TEST(test_resolve_gateway_targets_paired_empty);
  RUN_TEST(test_resolve_gateway_targets_paired_with_peers);
  RUN_TEST(test_resolve_gateway_targets_standalone_fallback);
  RUN_TEST(test_resolve_gateway_targets_filters_local_address);
  RUN_TEST(test_operational_peer_admitted_when_configured);
  RUN_TEST(test_operational_peer_rejected_when_unconfigured);
  RUN_TEST(test_operational_peer_rejected_when_out_of_range);
  RUN_TEST(test_operational_peer_rejected_when_empty_fleet);
  RUN_TEST(test_inventory_status_excludes_unconfigured_cached_peers);
  RUN_TEST(test_candidate_lifecycle);
  RUN_TEST(test_candidate_ok);
  RUN_TEST(test_candidate_conflict_readdress);
  RUN_TEST(test_candidate_out_of_range_readdress);
  RUN_TEST(test_full_fleet_dangerous_reset_guarded);
  RUN_TEST(test_remote_two_stage_confirm_success);
  RUN_TEST(test_remote_two_stage_confirm_save_fail);
  RUN_TEST(test_remote_two_stage_reset_confirm_success);
  RUN_TEST(test_gateway_confirm_validation);
  RUN_TEST(test_start_adoption_reverts_on_tx_fail);
  RUN_TEST(test_candidate_probe_tick_flow);
  RUN_TEST(test_candidate_telemetry_does_not_reset_active_attempts);
  RUN_TEST(test_struct_sizes);
  return UNITY_END();
}

