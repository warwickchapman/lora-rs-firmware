#include <unity.h>

#include "identify_transaction_helper.h"

void test_identify_status_requires_exact_correlation() {
  TEST_ASSERT_TRUE(identify_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 6, 6,
      identify_transaction::kResultAccepted));
  TEST_ASSERT_FALSE(identify_transaction::matchesStatus(
      7, 8, 0x12345678UL, 0x12345678UL, 6, 6,
      identify_transaction::kResultAccepted));
  TEST_ASSERT_FALSE(identify_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345679UL, 6, 6,
      identify_transaction::kResultAccepted));
  TEST_ASSERT_FALSE(identify_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 6, 7,
      identify_transaction::kResultAccepted));
  TEST_ASSERT_FALSE(identify_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 6, 6, 99));
}

void test_identify_duration_and_retry_are_bounded() {
  TEST_ASSERT_FALSE(identify_transaction::validDuration(0));
  TEST_ASSERT_TRUE(identify_transaction::validDuration(1));
  TEST_ASSERT_TRUE(identify_transaction::validDuration(30));
  TEST_ASSERT_FALSE(identify_transaction::validDuration(31));
  TEST_ASSERT_TRUE(identify_transaction::shouldRetryAfterTimeout(0));
  TEST_ASSERT_FALSE(identify_transaction::shouldRetryAfterTimeout(1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_identify_status_requires_exact_correlation);
  RUN_TEST(test_identify_duration_and_retry_are_bounded);
  return UNITY_END();
}
