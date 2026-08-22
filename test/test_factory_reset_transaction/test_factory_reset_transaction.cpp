#include <unity.h>

#include "factory_reset_transaction_helper.h"

void test_factory_reset_status_requires_exact_correlation() {
  TEST_ASSERT_TRUE(factory_reset_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 3, 3,
      factory_reset_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(factory_reset_transaction::matchesStatus(
      7, 8, 0x12345678UL, 0x12345678UL, 3, 3,
      factory_reset_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(factory_reset_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345679UL, 3, 3,
      factory_reset_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(factory_reset_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 3, 1,
      factory_reset_transaction::kResultCommitted));
  TEST_ASSERT_FALSE(factory_reset_transaction::matchesStatus(
      7, 7, 0x12345678UL, 0x12345678UL, 3, 3, 99));
}

void test_factory_reset_retry_is_bounded() {
  TEST_ASSERT_TRUE(factory_reset_transaction::shouldRetryAfterTimeout(0));
  TEST_ASSERT_FALSE(factory_reset_transaction::shouldRetryAfterTimeout(1));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_factory_reset_status_requires_exact_correlation);
  RUN_TEST(test_factory_reset_retry_is_bounded);
  return UNITY_END();
}
