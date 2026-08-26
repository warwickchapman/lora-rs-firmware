#include <unity.h>
#include <cstring>
#include "display_name_store.h"

void test_name_normalization() {
  char out[display_names::kMaxNameChars + 1];
  TEST_ASSERT_TRUE(display_names::normalize(" North Pump ", out));
  TEST_ASSERT_EQUAL_STRING("North Pump", out);
}
void test_name_validation() {
  char out[display_names::kMaxNameChars + 1];
  TEST_ASSERT_FALSE(display_names::normalize("North/Pump", out));
  TEST_ASSERT_FALSE(display_names::normalize("12345678901234567", out));
  TEST_ASSERT_TRUE(display_names::normalize("", out));
  TEST_ASSERT_TRUE(display_names::normalize("1234567890123456", out));
  TEST_ASSERT_EQUAL_STRING("1234567890123456", out);
  TEST_ASSERT_TRUE(display_names::normalize("   ", out));
  TEST_ASSERT_EQUAL_STRING("", out);
}
void test_image_validation_and_corruption() {
  uint8_t image[display_names::kFileBytes]{};
  auto *header = reinterpret_cast<display_names::Header *>(image);
  memcpy(header->magic, "LRNM", 4);
  header->version = display_names::kFormatVersion;
  header->record_count = display_names::kRecordCount;
  auto *records = reinterpret_cast<display_names::Record *>(image + sizeof(*header));
  records[0].chip_id = 0x12345678;
  memcpy(records[0].display_name, "North Pump", 11);
  uint32_t checksum = display_names::checksumBegin();
  checksum = display_names::checksumUpdate(checksum,
      reinterpret_cast<const uint8_t *>(records), sizeof(display_names::Record) * display_names::kRecordCount);
  header->checksum = checksum;
  TEST_ASSERT_TRUE(display_names::validateImage(image, sizeof(image)));
  records[0].display_name[0] = '/';
  TEST_ASSERT_FALSE(display_names::validateImage(image, sizeof(image)));
}
void test_image_set_lookup_clear_and_preserve_by_chip() {
  uint8_t image[display_names::kFileBytes];
  TEST_ASSERT_TRUE(display_names::initializeImage(image));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 0x12345678, "North Pump"));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 0x87654321, "Reservoir"));
  char out[display_names::kMaxNameChars + 1];
  TEST_ASSERT_TRUE(display_names::lookupImage(image, 0x12345678, out));
  TEST_ASSERT_EQUAL_STRING("North Pump", out);
  TEST_ASSERT_TRUE(display_names::lookupImage(image, 0x87654321, out));
  TEST_ASSERT_EQUAL_STRING("Reservoir", out);
  TEST_ASSERT_TRUE(display_names::updateImage(image, 0x12345678, ""));
  TEST_ASSERT_TRUE(display_names::lookupImage(image, 0x12345678, out));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_TRUE(display_names::lookupImage(image, 0x87654321, out));
  TEST_ASSERT_EQUAL_STRING("Reservoir", out);
}

void test_image_rejects_corruption_before_mutation() {
  uint8_t image[display_names::kFileBytes];
  TEST_ASSERT_TRUE(display_names::initializeImage(image));
  image[sizeof(display_names::Header)] ^= 0x80;
  TEST_ASSERT_FALSE(display_names::updateImage(image, 42, "Pump"));
}

void test_existing_chip_is_updated_after_an_earlier_empty_slot() {
  uint8_t image[display_names::kFileBytes];
  TEST_ASSERT_TRUE(display_names::initializeImage(image));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 1, "One"));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 2, "Two"));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 1, ""));
  TEST_ASSERT_TRUE(display_names::updateImage(image, 2, "Updated"));
  char out[display_names::kMaxNameChars + 1];
  TEST_ASSERT_TRUE(display_names::lookupImage(image, 2, out));
  TEST_ASSERT_EQUAL_STRING("Updated", out);
  TEST_ASSERT_TRUE(display_names::validateImage(image, sizeof(image)));
}

void test_fixed_store_rejects_a_fourteenth_name() {
  uint8_t image[display_names::kFileBytes];
  TEST_ASSERT_TRUE(display_names::initializeImage(image));
  for (uint32_t chipId = 1; chipId <= display_names::kRecordCount; ++chipId) {
    TEST_ASSERT_TRUE(display_names::updateImage(image, chipId, "Named"));
  }
  TEST_ASSERT_FALSE(display_names::updateImage(
      image, display_names::kRecordCount + 1, "Overflow"));
}

void test_recovery_prefers_primary_then_new_then_backup() {
  using display_names::RecoverySource;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoverySource::Primary),
      static_cast<int>(display_names::chooseRecoverySource(true, true, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoverySource::NewFile),
      static_cast<int>(display_names::chooseRecoverySource(false, true, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoverySource::Backup),
      static_cast<int>(display_names::chooseRecoverySource(false, false, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoverySource::None),
      static_cast<int>(display_names::chooseRecoverySource(false, false, false)));
}
void test_duplicate_chip_is_corrupt() {
  uint8_t image[display_names::kFileBytes]{};
  auto *header = reinterpret_cast<display_names::Header *>(image);
  memcpy(header->magic, "LRNM", 4); header->version = 1; header->record_count = display_names::kRecordCount;
  auto *records = reinterpret_cast<display_names::Record *>(image + sizeof(*header));
  records[0].chip_id = records[1].chip_id = 42;
  memcpy(records[0].display_name, "One", 4); memcpy(records[1].display_name, "Two", 4);
  uint32_t checksum = display_names::checksumBegin();
  checksum = display_names::checksumUpdate(checksum, reinterpret_cast<const uint8_t *>(records), sizeof(display_names::Record) * display_names::kRecordCount);
  header->checksum = checksum;
  TEST_ASSERT_FALSE(display_names::validateImage(image, sizeof(image)));
}
int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_name_normalization);
  RUN_TEST(test_name_validation);
  RUN_TEST(test_image_validation_and_corruption);
  RUN_TEST(test_image_set_lookup_clear_and_preserve_by_chip);
  RUN_TEST(test_image_rejects_corruption_before_mutation);
  RUN_TEST(test_existing_chip_is_updated_after_an_earlier_empty_slot);
  RUN_TEST(test_fixed_store_rejects_a_fourteenth_name);
  RUN_TEST(test_recovery_prefers_primary_then_new_then_backup);
  RUN_TEST(test_duplicate_chip_is_corrupt);
  return UNITY_END();
}
