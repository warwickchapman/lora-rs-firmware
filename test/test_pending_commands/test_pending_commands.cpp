#include <unity.h>
#include "pending_command_manager.h"
#include "runtime_utils.h"
#include <string.h>

void test_pending_command_wifi_control() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingWifiControl());

  pcm.requestWifiControl(false, 12345, 99);
  TEST_ASSERT_TRUE(pcm.hasPendingWifiControl());

  // Overwrite (last-write-wins)
  pcm.requestWifiControl(true, 54321, 88);
  TEST_ASSERT_TRUE(pcm.hasPendingWifiControl());

  bool enabled = false;
  uint8_t src = 0;
  uint32_t counter = 0;
  TEST_ASSERT_TRUE(pcm.consumeWifiControl(enabled, src, counter));
  TEST_ASSERT_TRUE(enabled);
  TEST_ASSERT_EQUAL_UINT8(88, src);
  TEST_ASSERT_EQUAL_UINT32(54321, counter);

  TEST_ASSERT_FALSE(pcm.hasPendingWifiControl());
  // Verify consumption cleared state
  TEST_ASSERT_FALSE(pcm.consumeWifiControl(enabled, src, counter));
}

void test_pending_command_udp_log_control() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingUdpLogControl());

  IPAddress host(192, 168, 1, 50);
  pcm.requestUdpLogControl(true, host, 514, 300, 77);
  TEST_ASSERT_TRUE(pcm.hasPendingUdpLogControl());

  bool enabled = false;
  IPAddress outHost;
  uint16_t port = 0;
  uint32_t ttlS = 0;
  uint8_t src = 0;

  TEST_ASSERT_TRUE(pcm.consumeUdpLogControl(enabled, outHost, port, ttlS, src));
  TEST_ASSERT_TRUE(enabled);
  TEST_ASSERT_EQUAL_UINT8(host[0], outHost[0]);
  TEST_ASSERT_EQUAL_UINT8(host[1], outHost[1]);
  TEST_ASSERT_EQUAL_UINT8(host[2], outHost[2]);
  TEST_ASSERT_EQUAL_UINT8(host[3], outHost[3]);
  TEST_ASSERT_EQUAL_UINT16(514, port);
  TEST_ASSERT_EQUAL_UINT32(300, ttlS);
  TEST_ASSERT_EQUAL_UINT8(77, src);

  TEST_ASSERT_FALSE(pcm.hasPendingUdpLogControl());
}

void test_pending_command_ota_pull() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingOtaPull());

  IPAddress host(10, 0, 0, 10);
  pcm.requestOtaPull(host, 8080, "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899", 55, 42);
  TEST_ASSERT_TRUE(pcm.hasPendingOtaPull());

  IPAddress outHost;
  uint16_t port = 0;
  char sha256Dest[65]{};
  uint8_t src = 0;
  uint8_t outTransferId = 0;

  TEST_ASSERT_TRUE(pcm.consumeOtaPull(outHost, port, sha256Dest, sizeof(sha256Dest), src, outTransferId));
  TEST_ASSERT_EQUAL_UINT8(42, outTransferId);
  TEST_ASSERT_EQUAL_UINT8(host[0], outHost[0]);
  TEST_ASSERT_EQUAL_UINT8(host[1], outHost[1]);
  TEST_ASSERT_EQUAL_UINT8(host[2], outHost[2]);
  TEST_ASSERT_EQUAL_UINT8(host[3], outHost[3]);
  TEST_ASSERT_EQUAL_UINT16(8080, port);
  TEST_ASSERT_EQUAL_STRING("aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899", sha256Dest);
  TEST_ASSERT_EQUAL_UINT8(55, src);

  TEST_ASSERT_FALSE(pcm.hasPendingOtaPull());
}

void test_pending_command_wifi_provision() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingWifiProvision());

  pcm.requestWifiProvision("MySSID", "SecretPassword", 44);
  TEST_ASSERT_TRUE(pcm.hasPendingWifiProvision());

  char ssidDest[33]{};
  char passDest[65]{};
  uint8_t src = 0;

  TEST_ASSERT_TRUE(pcm.consumeWifiProvision(ssidDest, sizeof(ssidDest), passDest, sizeof(passDest), src));
  TEST_ASSERT_EQUAL_STRING("MySSID", ssidDest);
  TEST_ASSERT_EQUAL_STRING("SecretPassword", passDest);
  TEST_ASSERT_EQUAL_UINT8(44, src);

  TEST_ASSERT_FALSE(pcm.hasPendingWifiProvision());
}

void test_pending_command_factory_reset() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingFactoryReset());

  pcm.requestFactoryReset(true, false, 33, 0x12345678UL);
  TEST_ASSERT_TRUE(pcm.hasPendingFactoryReset());

  bool keepFleet = false;
  bool keepWifi = true;
  uint8_t src = 0;
  uint32_t transactionId = 0;

  TEST_ASSERT_TRUE(pcm.consumeFactoryReset(keepFleet, keepWifi, src, transactionId));
  TEST_ASSERT_TRUE(keepFleet);
  TEST_ASSERT_FALSE(keepWifi);
  TEST_ASSERT_EQUAL_UINT8(33, src);
  TEST_ASSERT_EQUAL_UINT32(0x12345678UL, transactionId);

  TEST_ASSERT_FALSE(pcm.hasPendingFactoryReset());
}

void test_pending_command_reboot() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingReboot());

  pcm.requestReboot();
  TEST_ASSERT_TRUE(pcm.hasPendingReboot());

  TEST_ASSERT_TRUE(pcm.consumeReboot());
  TEST_ASSERT_FALSE(pcm.hasPendingReboot());
}

void test_pending_command_readdress() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingReaddress());

  pcm.requestReaddress(12, runtime_utils::kGatewayAddress, 0x123456, 0x87654321);
  TEST_ASSERT_TRUE(pcm.hasPendingReaddress());

  uint8_t newAddress = 0;
  uint8_t gwAddr = 0;
  uint32_t chipId = 0;
  uint32_t transactionId = 0;

  TEST_ASSERT_TRUE(pcm.consumeReaddress(newAddress, gwAddr, chipId, transactionId));
  TEST_ASSERT_EQUAL_UINT8(12, newAddress);
  TEST_ASSERT_EQUAL_UINT8(runtime_utils::kGatewayAddress, gwAddr);
  TEST_ASSERT_EQUAL_UINT32(0x123456, chipId);
  TEST_ASSERT_EQUAL_UINT32(0x87654321, transactionId);

  TEST_ASSERT_FALSE(pcm.hasPendingReaddress());
}

void test_pending_command_sensor_config() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingSensorConfig());

  pcm.requestSensorConfig(true, false, true, false);
  TEST_ASSERT_TRUE(pcm.hasPendingSensorConfig());

  bool temp = false;
  bool tank = true;
  bool powerSave = false;
  bool bootGrace = true;

  TEST_ASSERT_TRUE(pcm.consumeSensorConfig(temp, tank, powerSave, bootGrace));
  TEST_ASSERT_TRUE(temp);
  TEST_ASSERT_FALSE(tank);
  TEST_ASSERT_TRUE(powerSave);
  TEST_ASSERT_FALSE(bootGrace);

  TEST_ASSERT_FALSE(pcm.hasPendingSensorConfig());
}

void test_pending_command_fleet_prov_apply() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingFleetProvApply());

  pcm.requestFleetProvApply(999, 10, true, 2, "MyFleetKey");
  TEST_ASSERT_TRUE(pcm.hasPendingFleetProvApply());

  uint16_t sessionNonce = 0;
  uint8_t newAddress = 0;
  bool roleTx = false;
  uint8_t controller = 0;
  char keyDest[65]{};

  TEST_ASSERT_TRUE(pcm.consumeFleetProvApply(sessionNonce, newAddress, roleTx, controller, keyDest, sizeof(keyDest)));
  TEST_ASSERT_EQUAL_UINT16(999, sessionNonce);
  TEST_ASSERT_EQUAL_UINT8(10, newAddress);
  TEST_ASSERT_TRUE(roleTx);
  TEST_ASSERT_EQUAL_UINT8(2, controller);
  TEST_ASSERT_EQUAL_STRING("MyFleetKey", keyDest);

  TEST_ASSERT_FALSE(pcm.hasPendingFleetProvApply());
}

void test_pending_command_fleet_key_change() {
  PendingCommandManager pcm;
  TEST_ASSERT_FALSE(pcm.hasPendingFleetKeyChange());

  pcm.requestFleetKeyChange("NewFleetKey123", 22);
  TEST_ASSERT_TRUE(pcm.hasPendingFleetKeyChange());

  char keyDest[65]{};
  uint8_t src = 0;

  TEST_ASSERT_TRUE(pcm.consumeFleetKeyChange(keyDest, sizeof(keyDest), src));
  TEST_ASSERT_EQUAL_STRING("NewFleetKey123", keyDest);
  TEST_ASSERT_EQUAL_UINT8(22, src);

  TEST_ASSERT_FALSE(pcm.hasPendingFleetKeyChange());
}

void test_pending_command_truncation() {
  PendingCommandManager pcm;

  // SSID limit is 32 chars + null term
  pcm.requestWifiProvision("SSIDSSIDSSIDSSIDSSIDSSIDSSIDSSID12345", "Pass", 1);
  char ssid[33]{};
  char pass[65]{};
  uint8_t src = 0;
  TEST_ASSERT_TRUE(pcm.consumeWifiProvision(ssid, sizeof(ssid), pass, sizeof(pass), src));
  TEST_ASSERT_EQUAL_STRING("SSIDSSIDSSIDSSIDSSIDSSIDSSIDSSID", ssid);
  TEST_ASSERT_EQUAL_INT(32, strlen(ssid));
}

void test_pending_command_reset_all() {
  PendingCommandManager pcm;
  pcm.requestReboot();
  pcm.requestReaddress(1, 2, 3, 4);
  pcm.requestWifiProvision("A", "B", 1);

  TEST_ASSERT_TRUE(pcm.hasPendingReboot());
  TEST_ASSERT_TRUE(pcm.hasPendingReaddress());
  TEST_ASSERT_TRUE(pcm.hasPendingWifiProvision());

  pcm.resetAll();

  TEST_ASSERT_FALSE(pcm.hasPendingReboot());
  TEST_ASSERT_FALSE(pcm.hasPendingReaddress());
  TEST_ASSERT_FALSE(pcm.hasPendingWifiProvision());
}

void test_pending_command_reset_on_config_apply() {
  PendingCommandManager pcm;
  pcm.requestReboot();
  pcm.requestWifiProvision("A", "B", 1);
  pcm.requestUdpLogControl(true, IPAddress(1,1,1,1), 1, 1, 1);
  pcm.requestOtaPull(IPAddress(1,1,1,1), 1, "sha", 1, 99);
  pcm.requestFactoryReset(true, true, 1, 99);
  pcm.requestFleetProvApply(1, 1, true, 1, "key");

  TEST_ASSERT_TRUE(pcm.hasPendingReboot());
  TEST_ASSERT_TRUE(pcm.hasPendingWifiProvision());
  TEST_ASSERT_TRUE(pcm.hasPendingUdpLogControl());
  TEST_ASSERT_TRUE(pcm.hasPendingOtaPull());
  TEST_ASSERT_TRUE(pcm.hasPendingFactoryReset());
  TEST_ASSERT_TRUE(pcm.hasPendingFleetProvApply());

  pcm.resetOnConfigApply();

  // Reboot should remain pending
  TEST_ASSERT_TRUE(pcm.hasPendingReboot());

  // Config subset should be cleared
  TEST_ASSERT_FALSE(pcm.hasPendingWifiProvision());
  TEST_ASSERT_FALSE(pcm.hasPendingUdpLogControl());
  TEST_ASSERT_FALSE(pcm.hasPendingOtaPull());
  TEST_ASSERT_FALSE(pcm.hasPendingFactoryReset());
  TEST_ASSERT_FALSE(pcm.hasPendingFleetProvApply());
}

void test_pending_command_reset_on_config_apply_all_survivors() {
  PendingCommandManager pcm;

  // Set all commands that should survive
  pcm.requestWifiControl(true, 100, 10);
  pcm.requestReboot();
  pcm.requestReaddress(5, runtime_utils::kGatewayAddress, 12345, 67890);
  pcm.requestSensorConfig(true, false, true, false);
  pcm.requestFleetKeyChange("fleetkey", 1);

  // Set one command that should be cleared to confirm reset triggers
  pcm.requestOtaPull(IPAddress(1,1,1,1), 80, "sha", 1, 99);

  TEST_ASSERT_TRUE(pcm.hasPendingWifiControl());
  TEST_ASSERT_TRUE(pcm.hasPendingReboot());
  TEST_ASSERT_TRUE(pcm.hasPendingReaddress());
  TEST_ASSERT_TRUE(pcm.hasPendingSensorConfig());
  TEST_ASSERT_TRUE(pcm.hasPendingFleetKeyChange());
  TEST_ASSERT_TRUE(pcm.hasPendingOtaPull());

  pcm.resetOnConfigApply();

  // Survivors must still be pending
  TEST_ASSERT_TRUE(pcm.hasPendingWifiControl());
  TEST_ASSERT_TRUE(pcm.hasPendingReboot());
  TEST_ASSERT_TRUE(pcm.hasPendingReaddress());
  TEST_ASSERT_TRUE(pcm.hasPendingSensorConfig());
  TEST_ASSERT_TRUE(pcm.hasPendingFleetKeyChange());

  // Non-survivor must be cleared
  TEST_ASSERT_FALSE(pcm.hasPendingOtaPull());

  // Consume and verify details of all survivors to ensure exact preservation
  bool wifiEnabled = false;
  uint32_t wifiCounter = 0;
  uint8_t wifiSrc = 0;
  TEST_ASSERT_TRUE(pcm.consumeWifiControl(wifiEnabled, wifiSrc, wifiCounter));
  TEST_ASSERT_TRUE(wifiEnabled);
  TEST_ASSERT_EQUAL_UINT32(100, wifiCounter);
  TEST_ASSERT_EQUAL_UINT8(10, wifiSrc);

  TEST_ASSERT_TRUE(pcm.consumeReboot());

  uint8_t newAddr = 0;
  uint8_t gwAddr = 0;
  uint32_t readdressChipId = 0;
  uint32_t readdressTransactionId = 0;
  TEST_ASSERT_TRUE(pcm.consumeReaddress(newAddr, gwAddr, readdressChipId,
                                        readdressTransactionId));
  TEST_ASSERT_EQUAL_UINT8(5, newAddr);
  TEST_ASSERT_EQUAL_UINT8(runtime_utils::kGatewayAddress, gwAddr);
  TEST_ASSERT_EQUAL_UINT32(12345, readdressChipId);
  TEST_ASSERT_EQUAL_UINT32(67890, readdressTransactionId);

  bool temp = false, tank = true, powerSave = false, bootGrace = true;
  TEST_ASSERT_TRUE(pcm.consumeSensorConfig(temp, tank, powerSave, bootGrace));
  TEST_ASSERT_TRUE(temp);
  TEST_ASSERT_FALSE(tank);
  TEST_ASSERT_TRUE(powerSave);
  TEST_ASSERT_FALSE(bootGrace);

  char key[65]{};
  uint8_t keySrc = 0;
  TEST_ASSERT_TRUE(pcm.consumeFleetKeyChange(key, sizeof(key), keySrc));
  TEST_ASSERT_EQUAL_STRING("fleetkey", key);
  TEST_ASSERT_EQUAL_UINT8(1, keySrc);

  // Assert everything is now consumed
  TEST_ASSERT_FALSE(pcm.hasPendingWifiControl());
  TEST_ASSERT_FALSE(pcm.hasPendingReboot());
  TEST_ASSERT_FALSE(pcm.hasPendingReaddress());
  TEST_ASSERT_FALSE(pcm.hasPendingSensorConfig());
  TEST_ASSERT_FALSE(pcm.hasPendingFleetKeyChange());
}

void test_pending_command_overwrite_string_backed() {
  PendingCommandManager pcm;

  // 1. Wifi provision overwrite
  pcm.requestWifiProvision("SSID1", "PASS1", 1);
  TEST_ASSERT_TRUE(pcm.hasPendingWifiProvision());
  pcm.requestWifiProvision("SSID2", "PASS2", 2);
  char ssid[33]{};
  char pass[65]{};
  uint8_t src = 0;
  TEST_ASSERT_TRUE(pcm.consumeWifiProvision(ssid, sizeof(ssid), pass, sizeof(pass), src));
  TEST_ASSERT_EQUAL_STRING("SSID2", ssid);
  TEST_ASSERT_EQUAL_STRING("PASS2", pass);
  TEST_ASSERT_EQUAL_UINT8(2, src);

  // 2. OTA pull overwrite
  pcm.requestOtaPull(IPAddress(1,1,1,1), 80, "sha1", 10, 88);
  TEST_ASSERT_TRUE(pcm.hasPendingOtaPull());
  pcm.requestOtaPull(IPAddress(2,2,2,2), 443, "sha2", 20, 89);
  IPAddress host;
  uint16_t port = 0;
  char sha[65]{};
  uint8_t outTransferId = 0;
  TEST_ASSERT_TRUE(pcm.consumeOtaPull(host, port, sha, sizeof(sha), src, outTransferId));
  TEST_ASSERT_EQUAL_UINT8(89, outTransferId);
  TEST_ASSERT_EQUAL_UINT8(2, host[0]);
  TEST_ASSERT_EQUAL_UINT8(2, host[1]);
  TEST_ASSERT_EQUAL_UINT8(2, host[2]);
  TEST_ASSERT_EQUAL_UINT8(2, host[3]);
  TEST_ASSERT_EQUAL_UINT16(443, port);
  TEST_ASSERT_EQUAL_STRING("sha2", sha);
  TEST_ASSERT_EQUAL_UINT8(20, src);

  // 3. Fleet provision apply overwrite
  pcm.requestFleetProvApply(100, 10, true, 2, "key1");
  TEST_ASSERT_TRUE(pcm.hasPendingFleetProvApply());
  pcm.requestFleetProvApply(200, 20, false, 4, "key2");
  uint16_t nonce = 0;
  uint8_t addr = 0;
  bool roleTx = true;
  uint8_t ctrl = 0;
  char key[65]{};
  TEST_ASSERT_TRUE(pcm.consumeFleetProvApply(nonce, addr, roleTx, ctrl, key, sizeof(key)));
  TEST_ASSERT_EQUAL_UINT16(200, nonce);
  TEST_ASSERT_EQUAL_UINT8(20, addr);
  TEST_ASSERT_FALSE(roleTx);
  TEST_ASSERT_EQUAL_UINT8(4, ctrl);
  TEST_ASSERT_EQUAL_STRING("key2", key);

  // 4. Fleet key change overwrite
  pcm.requestFleetKeyChange("keyA", 5);
  TEST_ASSERT_TRUE(pcm.hasPendingFleetKeyChange());
  pcm.requestFleetKeyChange("keyB", 6);
  TEST_ASSERT_TRUE(pcm.consumeFleetKeyChange(key, sizeof(key), src));
  TEST_ASSERT_EQUAL_STRING("keyB", key);
  TEST_ASSERT_EQUAL_UINT8(6, src);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_pending_command_wifi_control);
  RUN_TEST(test_pending_command_udp_log_control);
  RUN_TEST(test_pending_command_ota_pull);
  RUN_TEST(test_pending_command_wifi_provision);
  RUN_TEST(test_pending_command_factory_reset);
  RUN_TEST(test_pending_command_reboot);
  RUN_TEST(test_pending_command_readdress);
  RUN_TEST(test_pending_command_sensor_config);
  RUN_TEST(test_pending_command_fleet_prov_apply);
  RUN_TEST(test_pending_command_fleet_key_change);
  RUN_TEST(test_pending_command_truncation);
  RUN_TEST(test_pending_command_reset_all);
  RUN_TEST(test_pending_command_reset_on_config_apply);
  RUN_TEST(test_pending_command_reset_on_config_apply_all_survivors);
  RUN_TEST(test_pending_command_overwrite_string_backed);
  return UNITY_END();
}
