#include <unity.h>
#include <cstring>

#include "runtime_utils.h"

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
  return UNITY_END();
}
