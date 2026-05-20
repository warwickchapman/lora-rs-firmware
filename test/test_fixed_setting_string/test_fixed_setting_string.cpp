#include <unity.h>

#include "fixed_setting_string.h"

void test_null_assignment_becomes_empty() {
  FixedSettingString<8> value("abc");
  value = static_cast<const char *>(nullptr);
  TEST_ASSERT_EQUAL_STRING("", value.c_str());
  TEST_ASSERT_TRUE(value.isEmpty());
}

void test_truncates_to_buffer_size_minus_one() {
  FixedSettingString<5> value("abcdef");
  TEST_ASSERT_EQUAL_STRING("abcd", value.c_str());
  TEST_ASSERT_EQUAL_UINT(4, value.length());
}

void test_exact_boundary_length_fits() {
  FixedSettingString<5> value("abcd");
  TEST_ASSERT_EQUAL_STRING("abcd", value.c_str());
  TEST_ASSERT_EQUAL_UINT(4, value.length());
}

void test_trim_removes_outer_whitespace_only() {
  FixedSettingString<16> value(" \tAb C\r\n");
  value.trim();
  TEST_ASSERT_EQUAL_STRING("Ab C", value.c_str());
}

void test_to_lower_case_only_changes_ascii_uppercase() {
  FixedSettingString<16> value("AbC-123_Z");
  value.toLowerCase();
  TEST_ASSERT_EQUAL_STRING("abc-123_z", value.c_str());
}

void test_equality_and_copy_assignment() {
  FixedSettingString<12> source("gateway");
  FixedSettingString<12> copy;
  copy = source;
  TEST_ASSERT_TRUE(copy == "gateway");
  TEST_ASSERT_FALSE(copy != "gateway");
  TEST_ASSERT_TRUE(copy.equals("gateway"));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_null_assignment_becomes_empty);
  RUN_TEST(test_truncates_to_buffer_size_minus_one);
  RUN_TEST(test_exact_boundary_length_fits);
  RUN_TEST(test_trim_removes_outer_whitespace_only);
  RUN_TEST(test_to_lower_case_only_changes_ascii_uppercase);
  RUN_TEST(test_equality_and_copy_assignment);
  return UNITY_END();
}
