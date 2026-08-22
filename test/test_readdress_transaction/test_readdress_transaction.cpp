#include <unity.h>

#include "readdress_transaction_helper.h"

void test_readdress_status_requires_exact_correlation() {
  TEST_ASSERT_TRUE(readdress_transaction::matchesStatus(
      7, 3, 3, 0x123456, 0x123456, 0x87654321, 0x87654321,
      readdress_transaction::kResultCommitted));
  TEST_ASSERT_TRUE(readdress_transaction::matchesStatus(
      7, 3, 7, 0x123456, 0x123456, 0x87654321, 0x87654321,
      readdress_transaction::kResultSaveFailed));
  TEST_ASSERT_FALSE(readdress_transaction::matchesStatus(
      7, 3, 7, 0x123456, 0x123456, 0x87654321, 0x87654321,
      readdress_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(readdress_transaction::matchesStatus(
      7, 3, 3, 0x123456, 0x123457, 0x87654321, 0x87654321,
      readdress_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(readdress_transaction::matchesStatus(
      7, 3, 3, 0x123456, 0x123456, 0x87654321, 0x87654322,
      readdress_transaction::kResultCommitted));
}

void test_readdress_retry_is_bounded() {
  TEST_ASSERT_TRUE(readdress_transaction::shouldRetryAfterTimeout(0));
  TEST_ASSERT_FALSE(readdress_transaction::shouldRetryAfterTimeout(1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_readdress_status_requires_exact_correlation);
  RUN_TEST(test_readdress_retry_is_bounded);
  return UNITY_END();
}
