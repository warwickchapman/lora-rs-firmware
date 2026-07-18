#include <unity.h>
#include <cstring>

#include "state_machine.h"

class NodeStateMachineTestHelper {
 public:
  static bool isAuthorizedMqttController(const NodeStateMachine &sm, uint8_t src) {
    return sm.isAuthorizedMqttController(src);
  }
};

static Settings makeRemoteSettings(uint8_t controllerAddress) {
  Settings cfg{};
  cfg.role_tx = false;
  cfg.local_address = 1;
  cfg.controller_address = controllerAddress;
  cfg.allowed_controller_count = 0;
  memset(cfg.allowed_controller_addresses, 0, sizeof(cfg.allowed_controller_addresses));
  cfg.mqtt_controller_addresses = "";
  cfg.heartbeat_ms = 60000;
  cfg.tx_command_retry_timeout_ms = 180000;
  cfg.rx_failsafe_timeout_ms = 180000;
  return cfg;
}

static Settings makeGatewaySettings() {
  Settings cfg{};
  cfg.role_tx = true;
  cfg.local_address = 254;
  cfg.controller_address = 0;
  cfg.allowed_controller_count = 0;
  memset(cfg.allowed_controller_addresses, 0, sizeof(cfg.allowed_controller_addresses));
  cfg.mqtt_controller_addresses = "";
  cfg.heartbeat_ms = 60000;
  cfg.tx_command_retry_timeout_ms = 180000;
  cfg.rx_failsafe_timeout_ms = 180000;
  return cfg;
}

void test_remote_accepts_mqtt_from_configured_controller_address() {
  const Settings cfg = makeRemoteSettings(254);
  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 254));
}

void test_remote_rejects_mqtt_from_non_controller_sources() {
  const Settings cfg = makeRemoteSettings(254);
  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 1));
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 2));
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 253));
}

void test_remote_allows_extra_controller_via_allowed_list() {
  Settings cfg = makeRemoteSettings(254);
  cfg.allowed_controller_count = 1;
  cfg.allowed_controller_addresses[0] = 3;

  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 254));  // configured controller
  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 3));    // explicit extra controller
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 2));   // not authorized
}

void test_remote_allows_extra_controller_via_mqtt_csv() {
  Settings cfg = makeRemoteSettings(254);
  cfg.mqtt_controller_addresses = "5,6";

  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 254));  // configured controller
  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 5));    // CSV extra controller
  TEST_ASSERT_TRUE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 6));    // CSV extra controller
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 4));   // not authorized
}

void test_gateway_does_not_gain_remote_controller_authorization() {
  const Settings cfg = makeGatewaySettings();
  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 254));
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 1));
}

void test_reserved_and_zero_addresses_are_rejected() {
  const Settings cfg = makeRemoteSettings(254);
  NodeStateMachine sm;
  sm.applyConfig(cfg);

  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 0));
  TEST_ASSERT_FALSE(NodeStateMachineTestHelper::isAuthorizedMqttController(sm, 255));
}
int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_remote_accepts_mqtt_from_configured_controller_address);
  RUN_TEST(test_remote_rejects_mqtt_from_non_controller_sources);
  RUN_TEST(test_remote_allows_extra_controller_via_allowed_list);
  RUN_TEST(test_remote_allows_extra_controller_via_mqtt_csv);
  RUN_TEST(test_gateway_does_not_gain_remote_controller_authorization);
  RUN_TEST(test_reserved_and_zero_addresses_are_rejected);
  return UNITY_END();
}
