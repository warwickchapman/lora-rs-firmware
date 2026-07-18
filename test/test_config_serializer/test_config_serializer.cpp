#include <unity.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_store.h"
#include "runtime_utils.h"

extern Settings g_test_settings;
extern String g_test_lan_hostname;

void setUp() {
  memset(&g_test_settings, 0, sizeof(Settings));
}

void tearDown() {}

void test_serializer_field_count_and_types() {
  g_test_settings.schema_version = 5;
  g_test_settings.commissioned = true;
  g_test_settings.mode = "paired";
  g_test_settings.role_tx = false;
  g_test_settings.local_address = 1;
  g_test_settings.controller_address = 2;

  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, false);

  // The schema clean break is explicit: remote-only controller, no legacy keys.
  TEST_ASSERT_EQUAL(5, doc["schema_version"].as<int>());
  TEST_ASSERT_EQUAL(2, doc["controller_address"].as<int>());
  TEST_ASSERT_FALSE(doc.containsKey("remote_address"));
  TEST_ASSERT_FALSE(doc.containsKey("paired_target_addresses"));

  // 2. Booleans serialize as JSON booleans
  TEST_ASSERT_TRUE(doc["commissioned"].is<bool>());
  TEST_ASSERT_TRUE(doc["commissioned"].as<bool>());
  TEST_ASSERT_TRUE(doc["role_tx"].is<bool>());
  TEST_ASSERT_FALSE(doc["role_tx"].as<bool>());

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

void test_write_settings_json_object_nested() {
  g_test_settings.schema_version = 5;
  g_test_settings.commissioned = true;
  g_test_settings.mode = "paired";
  g_test_settings.role_tx = false;
  g_test_settings.local_address = 1;
  g_test_settings.controller_address = 2;

  JsonDocument doc;
  doc["cmd"] = "get_config";
  JsonObject config = doc["config"].to<JsonObject>();
  ConfigStore store;
  writeSettingsJsonObject(config, store, false);

  // Fields land under out["config"], not at root
  TEST_ASSERT_TRUE(doc.containsKey("cmd"));
  TEST_ASSERT_EQUAL_STRING("get_config", doc["cmd"].as<const char*>());
  TEST_ASSERT_FALSE(doc.containsKey("schema_version"));
  TEST_ASSERT_TRUE(doc["config"].containsKey("schema_version"));
  TEST_ASSERT_EQUAL(5, doc["config"]["schema_version"].as<int>());
  TEST_ASSERT_TRUE(doc["config"].containsKey("commissioned"));
  TEST_ASSERT_TRUE(doc["config"]["commissioned"].as<bool>());
  TEST_ASSERT_TRUE(doc["config"].containsKey("mode"));
  TEST_ASSERT_EQUAL_STRING("paired", doc["config"]["mode"].as<const char*>());
  TEST_ASSERT_TRUE(doc["config"].containsKey("local_address"));
  TEST_ASSERT_EQUAL(1, doc["config"]["local_address"].as<int>());
  TEST_ASSERT_EQUAL(2, doc["config"]["controller_address"].as<int>());
  TEST_ASSERT_FALSE(doc["config"].containsKey("remote_address"));
  TEST_ASSERT_FALSE(doc["config"].containsKey("paired_target_addresses"));
}

void test_gateway_serializer_omits_remote_only_and_legacy_fields() {
  g_test_settings.schema_version = 5;
  g_test_settings.role_tx = true;
  g_test_settings.local_address = 254;
  g_test_settings.controller_address = 0;
  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, false);

  TEST_ASSERT_EQUAL(5, doc["schema_version"].as<int>());
  TEST_ASSERT_FALSE(doc.containsKey("controller_address"));
  TEST_ASSERT_FALSE(doc.containsKey("remote_address"));
  TEST_ASSERT_FALSE(doc.containsKey("paired_target_addresses"));
}

void test_schema_four_migration_serializes_clean_schema_five() {
  g_test_settings.schema_version = 5;
  g_test_settings.role_tx = false;
  g_test_settings.local_address = 1;
  g_test_settings.controller_address = runtime_utils::migrateControllerAddress(
      false, false, 0, true, 254);
  JsonDocument doc;
  ConfigStore store;
  writeSettingsJson(doc, store, false);

  TEST_ASSERT_EQUAL(5, doc["schema_version"].as<int>());
  TEST_ASSERT_EQUAL(254, doc["controller_address"].as<int>());
  TEST_ASSERT_FALSE(doc.containsKey("remote_address"));
  TEST_ASSERT_FALSE(doc.containsKey("paired_target_addresses"));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_serializer_field_count_and_types);
  RUN_TEST(test_serializer_secrets_redaction);
  RUN_TEST(test_serializer_secrets_inclusion);
  RUN_TEST(test_fleet_passphrase_default);
  RUN_TEST(test_write_settings_json_object_nested);
  RUN_TEST(test_gateway_serializer_omits_remote_only_and_legacy_fields);
  RUN_TEST(test_schema_four_migration_serializes_clean_schema_five);
  return UNITY_END();
}
