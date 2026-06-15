#include <unity.h>

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
  return UNITY_END();
}
