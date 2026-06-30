#include <unity.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_store.h"

extern Settings g_test_settings;
extern String g_test_lan_hostname;

void setUp() {
  memset(&g_test_settings, 0, sizeof(Settings));
}

void tearDown() {}

void test_serializer_field_count_and_types() {
  g_test_settings.schema_version = 4;
  g_test_settings.commissioned = true;
  g_test_settings.mode = "paired";
  g_test_settings.role_tx = true;
  g_test_settings.local_address = 1;
  g_test_settings.remote_address = 2;
  g_test_settings.paired_target_count = 2;
  g_test_settings.paired_target_addresses[0] = 3;
  g_test_settings.paired_target_addresses[1] = 4;

  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, false);

  // 1. Verify exact 66 fields
  TEST_ASSERT_EQUAL(66, doc.size());

  // 2. Booleans serialize as JSON booleans
  TEST_ASSERT_TRUE(doc["commissioned"].is<bool>());
  TEST_ASSERT_TRUE(doc["commissioned"].as<bool>());
  TEST_ASSERT_TRUE(doc["role_tx"].is<bool>());
  TEST_ASSERT_TRUE(doc["role_tx"].as<bool>());

  // 3. Address lists serialize as JSON arrays
  TEST_ASSERT_TRUE(doc["paired_target_addresses"].is<JsonArray>());
  JsonArray arr = doc["paired_target_addresses"].as<JsonArray>();
  TEST_ASSERT_EQUAL(2, arr.size());
  TEST_ASSERT_EQUAL(3, arr[0].as<int>());
  TEST_ASSERT_EQUAL(4, arr[1].as<int>());
}

void test_serializer_secrets_redaction() {
  g_test_settings.wifi_sta_password = "wifi_secret";
  g_test_settings.mqtt_password = "mqtt_secret";
  g_test_settings.fleet_passphrase = "fleet_secret";
  g_test_settings.admin_password = "admin_secret";

  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, false);

  // includeSecrets = false => secrets are empty
  TEST_ASSERT_EQUAL_STRING("", doc["wifi_sta_password"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["mqtt_password"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["fleet_passphrase"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("", doc["admin_password"].as<const char*>());

  // Set-metadata fields are true
  TEST_ASSERT_TRUE(doc["wifi_sta_password_set"].as<bool>());
  TEST_ASSERT_TRUE(doc["mqtt_password_set"].as<bool>());
  TEST_ASSERT_TRUE(doc["fleet_passphrase_set"].as<bool>());
  TEST_ASSERT_TRUE(doc["admin_password_set"].as<bool>());
}

void test_serializer_secrets_inclusion() {
  g_test_settings.wifi_sta_password = "wifi_secret";
  g_test_settings.mqtt_password = "mqtt_secret";
  g_test_settings.fleet_passphrase = "fleet_secret";
  g_test_settings.admin_password = "admin_secret";

  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, true);

  // includeSecrets = true => secrets are preserved
  TEST_ASSERT_EQUAL_STRING("wifi_secret", doc["wifi_sta_password"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("mqtt_secret", doc["mqtt_password"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("fleet_secret", doc["fleet_passphrase"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("admin_secret", doc["admin_password"].as<const char*>());
}

void test_fleet_passphrase_default() {
  // default key (lora-default-passphrase)
  g_test_settings.fleet_passphrase = "lora-default-passphrase";
  JsonDocument doc1;
  ConfigStore store1;
  writeSettingsJson(doc1, store1, false);
  TEST_ASSERT_TRUE(doc1["fleet_passphrase_default"].as<bool>());

  // custom key
  g_test_settings.fleet_passphrase = "custom_fleet_passphrase_here";
  JsonDocument doc2;
  ConfigStore store2;
  writeSettingsJson(doc2, store2, false);
  TEST_ASSERT_FALSE(doc2["fleet_passphrase_default"].as<bool>());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_serializer_field_count_and_types);
  RUN_TEST(test_serializer_secrets_redaction);
  RUN_TEST(test_serializer_secrets_inclusion);
  RUN_TEST(test_fleet_passphrase_default);
  return UNITY_END();
}
