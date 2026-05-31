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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_paired_transmitter_is_tx);
  RUN_TEST(test_paired_receiver_is_rx);
  RUN_TEST(test_standalone_none_is_local_tx_path);
  RUN_TEST(test_mesh_aliases_are_rejected_without_changing_output);
  RUN_TEST(test_invalid_inputs_are_rejected_without_changing_output);
  RUN_TEST(test_wifi_status_text_known_values);
  RUN_TEST(test_wifi_status_text_unknown_value);
  return UNITY_END();
}
