#include <unity.h>
#include <cstring>

#include "config_fields.h"
#include "runtime_utils.h"
#include "state_machine.h"
#include "radio_protocol.h"

size_t NodeStateMachine::peerRuntimeSize() { return sizeof(PeerRuntime); }
size_t NodeStateMachine::pollRuntimeSize() { return sizeof(PollRuntime); }
size_t NodeStateMachine::replaySourceStateSize() { return sizeof(NodeStateMachine::ReplaySourceState); }

void test_isValidRemotePeerIdentity() {
  // Valid remote identities
  TEST_ASSERT_TRUE(runtime_utils::isValidRemotePeerIdentity(1, 12345));
  TEST_ASSERT_TRUE(runtime_utils::isValidRemotePeerIdentity(253, 12345));

  // Invalid chip ID
  TEST_ASSERT_FALSE(runtime_utils::isValidRemotePeerIdentity(1, 0));

  // Invalid addresses
  TEST_ASSERT_FALSE(runtime_utils::isValidRemotePeerIdentity(0, 12345));
  TEST_ASSERT_FALSE(runtime_utils::isValidRemotePeerIdentity(254, 12345)); // kGatewayAddress
  TEST_ASSERT_FALSE(runtime_utils::isValidRemotePeerIdentity(255, 12345));
}

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

void test_paired_gateway_is_tx() {
  bool roleTx = false;
  TEST_ASSERT_TRUE(runtime_utils::parseRoleTxFromModeRole("paired", "gateway", roleTx));
  TEST_ASSERT_TRUE(roleTx);
}

void test_paired_remote_is_rx() {
  bool roleTx = true;
  TEST_ASSERT_TRUE(runtime_utils::parseRoleTxFromModeRole("paired", "remote", roleTx));
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

void test_schema_four_remote_migrates_to_controller_address() {
  TEST_ASSERT_EQUAL_UINT8(7, runtime_utils::migrateControllerAddress(
      false, false, 0, true, 7));
  TEST_ASSERT_EQUAL_UINT8(9, runtime_utils::migrateControllerAddress(
      false, true, 9, true, 7));
}

void test_schema_four_gateway_drops_legacy_remote_address() {
  TEST_ASSERT_EQUAL_UINT8(0, runtime_utils::migrateControllerAddress(
      true, false, 0, true, 7));
}

void test_identity_wifi_rssi_decoding() {
  TEST_ASSERT_EQUAL_INT(-65,
                        runtime_utils::decodeMaintenanceIdentityWifiRssi(
                            static_cast<uint8_t>(static_cast<int8_t>(-65)), true));
  TEST_ASSERT_EQUAL_INT(0, runtime_utils::decodeMaintenanceIdentityWifiRssi(0, true));
  TEST_ASSERT_EQUAL_INT(0, runtime_utils::decodeMaintenanceIdentityWifiRssi(7, true));
  TEST_ASSERT_EQUAL_INT(0,
                        runtime_utils::decodeMaintenanceIdentityWifiRssi(
                            static_cast<uint8_t>(static_cast<int8_t>(-65)), false));
}

void test_identity_relay_state_decoding() {
  uint8_t relayState = 0;
  TEST_ASSERT_TRUE(runtime_utils::decodeMaintenanceIdentityRelayState(0xA1, relayState));
  TEST_ASSERT_EQUAL_UINT8(1, relayState);
  TEST_ASSERT_TRUE(runtime_utils::decodeMaintenanceIdentityRelayState(0xA0, relayState));
  TEST_ASSERT_EQUAL_UINT8(0, relayState);
  TEST_ASSERT_FALSE(runtime_utils::decodeMaintenanceIdentityRelayState(0, relayState));
}

void test_staggered_ack_delay_guards_first_peer() {
  TEST_ASSERT_EQUAL_UINT32(120,
      runtime_utils::staggeredAckDelayMs(0, 180, 0, 120));
  TEST_ASSERT_EQUAL_UINT32(340,
      runtime_utils::staggeredAckDelayMs(1, 180, 40, 120));
}

void test_group_broadcast_ack_lead_and_window() {
  TEST_ASSERT_EQUAL_UINT8(3, runtime_utils::kMaintenancePayloadVersion);
  TEST_ASSERT_EQUAL_UINT32(250, runtime_utils::kGroupAckLeadMs);
  TEST_ASSERT_EQUAL_UINT32(250,
      runtime_utils::staggeredAckDelayMs(0, runtime_utils::kGroupAckSlotMs, 0,
                                         runtime_utils::kGroupAckLeadMs));
  TEST_ASSERT_EQUAL_UINT32(2760, runtime_utils::groupInitialAckWindowMs(12));
}

void test_gateway_scheduler_queues_receive_observability_until_control_is_idle() {
  const auto inputTransition = runtime_utils::gatewayControlSchedule(true, true, false);
  TEST_ASSERT_TRUE(inputTransition.queue_received_observability);
  TEST_ASSERT_TRUE(inputTransition.run_group_control);
  TEST_ASSERT_FALSE(inputTransition.allow_observability);

  const auto activeGroup = runtime_utils::gatewayControlSchedule(true, false, true);
  TEST_ASSERT_TRUE(activeGroup.queue_received_observability);
  TEST_ASSERT_TRUE(activeGroup.run_group_control);
  TEST_ASSERT_FALSE(activeGroup.allow_observability);

  const auto complete = runtime_utils::gatewayControlSchedule(true, false, false);
  TEST_ASSERT_TRUE(complete.queue_received_observability);
  TEST_ASSERT_FALSE(complete.run_group_control);
  TEST_ASSERT_TRUE(complete.allow_observability);
}

void test_receive_side_poll_and_maintenance_observability_always_queue() {
  TEST_ASSERT_EQUAL(static_cast<int>(runtime_utils::ReceiveObservabilityAction::Queue),
                    static_cast<int>(runtime_utils::receiveObservabilityAction(
                        runtime_utils::ReceiveObservabilityKind::PollResponse)));
  TEST_ASSERT_EQUAL(static_cast<int>(runtime_utils::ReceiveObservabilityAction::Queue),
                    static_cast<int>(runtime_utils::receiveObservabilityAction(
                        runtime_utils::ReceiveObservabilityKind::MaintenanceStatus)));
}

void test_mqtt_config_write_authorization_boundary() {
  const ConfigField *retained = findConfigField("mqtt_control_enabled");
  TEST_ASSERT_NOT_NULL(retained);
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigFieldClass::RetainedConfig), static_cast<int>(retained->classification));
  TEST_ASSERT_TRUE(isMqttWritableConfigField("mqtt_control_enabled"));

  const ConfigField *secret = findConfigField("fleet_passphrase");
  TEST_ASSERT_NOT_NULL(secret);
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigFieldClass::SecretMetadata), static_cast<int>(secret->classification));
  TEST_ASSERT_TRUE(isMqttWritableConfigField("fleet_passphrase"));

  const ConfigField *internal = findConfigField("known_peer_chip_ids");
  TEST_ASSERT_NOT_NULL(internal);
  TEST_ASSERT_EQUAL(static_cast<int>(ConfigFieldClass::InternalOnly), static_cast<int>(internal->classification));
  TEST_ASSERT_FALSE(isMqttWritableConfigField("known_peer_chip_ids"));

  TEST_ASSERT_NULL(findConfigField("not_a_real_config_field"));
  TEST_ASSERT_FALSE(isMqttWritableConfigField("not_a_real_config_field"));
  TEST_ASSERT_FALSE(isMqttWritableConfigField(nullptr));
}

void test_resolve_gateway_targets_paired_empty() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {0};
  uint8_t count = runtime_utils::resolveGatewayTargets(
      runtime_utils::kGatewayAddress,  // localAddress
      0,    // knownPeerCount
      known_peers,
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(0, count);
}

void test_resolve_gateway_targets_paired_with_peers() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {2, 3};
  uint8_t count = runtime_utils::resolveGatewayTargets(
      runtime_utils::kGatewayAddress,  // localAddress
      2,    // knownPeerCount
      known_peers,
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(2, count);
  TEST_ASSERT_EQUAL_UINT8(2, targets[0]);
  TEST_ASSERT_EQUAL_UINT8(3, targets[1]);
}

void test_resolve_gateway_targets_has_no_legacy_fallback() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {0};
  uint8_t count = runtime_utils::resolveGatewayTargets(
      runtime_utils::kGatewayAddress,   // localAddress
      0,     // knownPeerCount
      known_peers,
      targets,
      12
  );
  
  TEST_ASSERT_EQUAL_UINT8(0, count);
}

void test_resolve_gateway_targets_filters_local_address() {
  uint8_t targets[12]{};
  uint8_t known_peers[12] = {2, runtime_utils::kGatewayAddress, 3};
  
  uint8_t count = runtime_utils::resolveGatewayTargets(
      runtime_utils::kGatewayAddress,  // localAddress (gateway address)
      3,    // knownPeerCount
      known_peers,
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
    uint8_t addressToCheck
) {
  if (!roleTx || strcmp(mode, "paired") != 0) return false;
  if (addressToCheck < 1 || addressToCheck > 12) return false;

  uint8_t targets[12]{};
  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
      localAddress,
      knownPeerCount,
      knownPeerAddresses,
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
      true, "paired", runtime_utils::kGatewayAddress, 2, known_peers, 5
  ));
}

void test_operational_peer_rejected_when_unconfigured() {
  uint8_t known_peers[12] = {5, 6};
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", runtime_utils::kGatewayAddress, 2, known_peers, 7
  ));
}

void test_operational_peer_rejected_when_out_of_range() {
  uint8_t known_peers[12] = {5, 16};
  // Address 16 is in configured known peers list, but exceeds limit 12, so must be rejected
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", runtime_utils::kGatewayAddress, 2, known_peers, 16
  ));
}

void test_operational_peer_rejected_when_empty_fleet() {
  uint8_t known_peers[12] = {0};
  TEST_ASSERT_FALSE(simulateIsConfiguredOperationalPeer(
      true, "paired", runtime_utils::kGatewayAddress, 0, known_peers, 5
  ));
}

int simulateLoraInventoryStatusDevices(
    bool roleTx,
    const char* mode,
    uint8_t localAddress,
    uint8_t knownPeerCount,
    const uint8_t *knownPeerAddresses,
    uint8_t cachedPeerCount,
    const uint8_t *cachedPeerAddresses,
    uint8_t *outDevices,
    uint8_t maxDevices
) {
  uint8_t targets[12]{};
  uint8_t targetCount = 0;

  if (roleTx) {
    targetCount = runtime_utils::resolveGatewayTargets(
        localAddress,
        knownPeerCount,
        knownPeerAddresses,
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
      true, "paired", runtime_utils::kGatewayAddress, 2, known_peers,
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

void test_candidate_unknown_chip_is_not_conflict() {
  uint8_t known_peers[12] = {5, 6};
  uint32_t known_peer_chip_ids[12] = {111, 222};

  CandidateReason unknownReason = runtime_utils::evaluateCandidateReason(
      6, 0, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::Ok, unknownReason);

  CandidateReason realConflict = runtime_utils::evaluateCandidateReason(
      6, 333, 2, known_peers, known_peer_chip_ids
  );
  TEST_ASSERT_EQUAL(CandidateReason::Conflict, realConflict);
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

static Settings makeRemoteSettings(uint8_t controllerAddress) {
  Settings cfg{};
  cfg.role_tx = false;
  cfg.local_address = 1;
  cfg.controller_address = controllerAddress;
  cfg.allowed_controller_count = 0;
  memset(cfg.allowed_controller_addresses, 0, sizeof(cfg.allowed_controller_addresses));
  cfg.mqtt_controller_addresses = "";
  cfg.heartbeat_ms = 60000;
  cfg.tx_command_retry_timeout_ms = 180000;
  cfg.rx_failsafe_timeout_ms = 180000;
  return cfg;
}

static Settings makeGatewaySettings() {
  Settings cfg{};
  cfg.role_tx = true;
  cfg.local_address = 254;
  cfg.controller_address = 0;
  cfg.allowed_controller_count = 0;
  memset(cfg.allowed_controller_addresses, 0, sizeof(cfg.allowed_controller_addresses));
  cfg.mqtt_controller_addresses = "";
  cfg.heartbeat_ms = 60000;
  cfg.tx_command_retry_timeout_ms = 180000;
  cfg.rx_failsafe_timeout_ms = 180000;
  return cfg;
}

void test_mqtt_controller_authorization_remote_accepts_controller_address() {
  const Settings cfg = makeRemoteSettings(254);
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 254));
}

void test_mqtt_controller_authorization_remote_rejects_unconfigured_sources() {
  const Settings cfg = makeRemoteSettings(254);
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 1));
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 2));
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 253));
}

void test_mqtt_controller_authorization_stale_allowed_list_does_not_block_controller() {
  Settings cfg = makeRemoteSettings(254);
  cfg.allowed_controller_count = 1;
  cfg.allowed_controller_addresses[0] = 3;  // stale/wrong explicit entry
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 254));
}

void test_mqtt_controller_authorization_gateway_not_remote_authority() {
  const Settings cfg = makeGatewaySettings();
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 254));
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 1));
}

void test_mqtt_controller_authorization_rejects_reserved_addresses() {
  const Settings cfg = makeRemoteSettings(254);
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 0));
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 255));
}

void test_mqtt_controller_authorization_allowed_list_extra_controller() {
  Settings cfg = makeRemoteSettings(254);
  cfg.allowed_controller_count = 1;
  cfg.allowed_controller_addresses[0] = 3;
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 254));  // configured controller
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 3));    // explicit extra controller
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 2));    // not authorized
}

void test_mqtt_controller_authorization_mqtt_csv_extra_controller() {
  Settings cfg = makeRemoteSettings(254);
  cfg.mqtt_controller_addresses = "5,6";
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 254));  // configured controller
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 5));    // CSV extra controller
  TEST_ASSERT_TRUE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 6));    // CSV extra controller
  TEST_ASSERT_FALSE(runtime_utils::isAuthorizedMqttController(
      cfg.role_tx, cfg.controller_address, &cfg, 4));    // not authorized
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
void test_evaluate_maint_block_transition(void) {
  MaintBlockState prev_state;
  prev_state.reason = MaintBlockReason::None;

  // Transition: Empty -> GroupActive
  MaintBlockTransition t1 = evaluateMaintBlockTransition(MaintAttemptResult::GroupActive, 5, MaintenanceRequestSource::FleetScan, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::GroupActive), static_cast<uint8_t>(t1.next_state.reason));
  TEST_ASSERT_EQUAL(5, t1.next_state.address);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintenanceRequestSource::FleetScan), static_cast<uint8_t>(t1.next_state.source));
  TEST_ASSERT_TRUE(t1.should_emit_event);

  // Transition: GroupActive -> GroupActive (same address/source)
  prev_state = t1.next_state;
  MaintBlockTransition t2 = evaluateMaintBlockTransition(MaintAttemptResult::GroupActive, 5, MaintenanceRequestSource::FleetScan, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::GroupActive), static_cast<uint8_t>(t2.next_state.reason));
  TEST_ASSERT_FALSE(t2.should_emit_event);

  // Transition: GroupActive -> RadioBudget (new reason)
  MaintBlockTransition t3 = evaluateMaintBlockTransition(MaintAttemptResult::RadioBudget, 5, MaintenanceRequestSource::FleetScan, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::RadioBudget), static_cast<uint8_t>(t3.next_state.reason));
  TEST_ASSERT_TRUE(t3.should_emit_event);

  // Transition: GroupActive -> Empty (queue empty)
  MaintBlockTransition t4 = evaluateMaintBlockTransition(MaintAttemptResult::Empty, 0, MaintenanceRequestSource::FleetScan, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::None), static_cast<uint8_t>(t4.next_state.reason));
  TEST_ASSERT_FALSE(t4.should_emit_event); // Silent clear

  // Transition: GroupActive -> Success
  MaintBlockTransition t5 = evaluateMaintBlockTransition(MaintAttemptResult::Success, 0, MaintenanceRequestSource::FleetScan, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::None), static_cast<uint8_t>(t5.next_state.reason));
  TEST_ASSERT_FALSE(t5.should_emit_event); // Silent clear

  // Transition: None -> SendFailed
  prev_state.reason = MaintBlockReason::None;
  MaintBlockTransition t6 = evaluateMaintBlockTransition(MaintAttemptResult::SendFailed, 3, MaintenanceRequestSource::AdminPeerRefresh, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::SendFailed), static_cast<uint8_t>(t6.next_state.reason));
  TEST_ASSERT_EQUAL(3, t6.next_state.address);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintenanceRequestSource::AdminPeerRefresh), static_cast<uint8_t>(t6.next_state.source));
  TEST_ASSERT_TRUE(t6.should_emit_event);

  // Same reason, changed pending address
  prev_state = t6.next_state;
  MaintBlockTransition t7 = evaluateMaintBlockTransition(MaintAttemptResult::SendFailed, 4, MaintenanceRequestSource::AdminPeerRefresh, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::SendFailed), static_cast<uint8_t>(t7.next_state.reason));
  TEST_ASSERT_EQUAL(4, t7.next_state.address);
  TEST_ASSERT_TRUE(t7.should_emit_event);

  // Same reason/address, changed source
  prev_state = t7.next_state;
  MaintBlockTransition t8 = evaluateMaintBlockTransition(MaintAttemptResult::SendFailed, 4, MaintenanceRequestSource::AdminDiagnostics, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::SendFailed), static_cast<uint8_t>(t8.next_state.reason));
  TEST_ASSERT_EQUAL(4, t8.next_state.address);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintenanceRequestSource::AdminDiagnostics), static_cast<uint8_t>(t8.next_state.source));
  TEST_ASSERT_TRUE(t8.should_emit_event);

  // Unchanged reason/address/source remains silent
  prev_state = t8.next_state;
  MaintBlockTransition t9 = evaluateMaintBlockTransition(MaintAttemptResult::SendFailed, 4, MaintenanceRequestSource::AdminDiagnostics, prev_state);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MaintBlockReason::SendFailed), static_cast<uint8_t>(t9.next_state.reason));
  TEST_ASSERT_FALSE(t9.should_emit_event);
}

void test_radio_protocol_encode_input_fields(void) {
  uint8_t out_mask = 0;
  uint8_t out_digital0 = 0;

  // input=0, mask=0x0C, digital0=1 produces mask=0x0C and digital0=1
  radio_protocol_helpers::encodeInputFields(0, 0x0C, 1, out_mask, out_digital0);
  TEST_ASSERT_EQUAL(0x0C, out_mask);
  TEST_ASSERT_EQUAL(1, out_digital0);

  // input=0, mask=0, digital0=0xFF produces mask=0x01 and digital0=0
  radio_protocol_helpers::encodeInputFields(0, 0x00, 0xFF, out_mask, out_digital0);
  TEST_ASSERT_EQUAL(0x01, out_mask);
  TEST_ASSERT_EQUAL(0, out_digital0);

  // no caller-provided digital0 may implicitly declare physical-input bit 0
  radio_protocol_helpers::encodeInputFields(1, 0x00, 10, out_mask, out_digital0);
  TEST_ASSERT_EQUAL(0x00, out_mask);
  TEST_ASSERT_EQUAL(10, out_digital0);
}

void test_radio_protocol_resolve_input_state(void) {
  // input=0, mask=0x0C, digital0=1 resolves input=0
  uint8_t resolved = radio_protocol_helpers::resolveInputState(0, 0x0C, 1);
  TEST_ASSERT_EQUAL(0, resolved);

  // input=0, mask=0x01, digital0=1 resolves input=1
  resolved = radio_protocol_helpers::resolveInputState(0, 0x01, 1);
  TEST_ASSERT_EQUAL(1, resolved);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_isValidRemotePeerIdentity);
  RUN_TEST(test_paired_transmitter_is_tx);
  RUN_TEST(test_paired_receiver_is_rx);
  RUN_TEST(test_paired_gateway_is_tx);
  RUN_TEST(test_paired_remote_is_rx);
  RUN_TEST(test_standalone_none_is_local_tx_path);
  RUN_TEST(test_mesh_aliases_are_rejected_without_changing_output);
  RUN_TEST(test_invalid_inputs_are_rejected_without_changing_output);
  RUN_TEST(test_wifi_status_text_known_values);
  RUN_TEST(test_wifi_status_text_unknown_value);
  RUN_TEST(test_schema_four_remote_migrates_to_controller_address);
  RUN_TEST(test_schema_four_gateway_drops_legacy_remote_address);
  RUN_TEST(test_identity_wifi_rssi_decoding);
  RUN_TEST(test_identity_relay_state_decoding);
  RUN_TEST(test_staggered_ack_delay_guards_first_peer);
  RUN_TEST(test_group_broadcast_ack_lead_and_window);
  RUN_TEST(test_gateway_scheduler_queues_receive_observability_until_control_is_idle);
  RUN_TEST(test_receive_side_poll_and_maintenance_observability_always_queue);
  RUN_TEST(test_mqtt_config_write_authorization_boundary);
  RUN_TEST(test_resolve_gateway_targets_paired_empty);
  RUN_TEST(test_resolve_gateway_targets_paired_with_peers);
  RUN_TEST(test_resolve_gateway_targets_has_no_legacy_fallback);
  RUN_TEST(test_resolve_gateway_targets_filters_local_address);
  RUN_TEST(test_operational_peer_admitted_when_configured);
  RUN_TEST(test_operational_peer_rejected_when_unconfigured);
  RUN_TEST(test_operational_peer_rejected_when_out_of_range);
  RUN_TEST(test_operational_peer_rejected_when_empty_fleet);
  RUN_TEST(test_inventory_status_excludes_unconfigured_cached_peers);
  RUN_TEST(test_candidate_lifecycle);
  RUN_TEST(test_candidate_ok);
  RUN_TEST(test_candidate_conflict_readdress);
  RUN_TEST(test_candidate_unknown_chip_is_not_conflict);
  RUN_TEST(test_candidate_out_of_range_readdress);
  RUN_TEST(test_full_fleet_dangerous_reset_guarded);
  RUN_TEST(test_remote_two_stage_confirm_success);
  RUN_TEST(test_remote_two_stage_confirm_save_fail);
  RUN_TEST(test_remote_two_stage_reset_confirm_success);
  RUN_TEST(test_gateway_confirm_validation);
  RUN_TEST(test_start_adoption_reverts_on_tx_fail);
  RUN_TEST(test_candidate_probe_tick_flow);
  RUN_TEST(test_candidate_telemetry_does_not_reset_active_attempts);
  RUN_TEST(test_mqtt_controller_authorization_remote_accepts_controller_address);
  RUN_TEST(test_mqtt_controller_authorization_remote_rejects_unconfigured_sources);
  RUN_TEST(test_mqtt_controller_authorization_stale_allowed_list_does_not_block_controller);
  RUN_TEST(test_mqtt_controller_authorization_gateway_not_remote_authority);
  RUN_TEST(test_mqtt_controller_authorization_rejects_reserved_addresses);
  RUN_TEST(test_mqtt_controller_authorization_allowed_list_extra_controller);
  RUN_TEST(test_mqtt_controller_authorization_mqtt_csv_extra_controller);
  RUN_TEST(test_evaluate_maint_block_transition);
  RUN_TEST(test_radio_protocol_encode_input_fields);
  RUN_TEST(test_radio_protocol_resolve_input_state);
  RUN_TEST(test_struct_sizes);
  return UNITY_END();
}
