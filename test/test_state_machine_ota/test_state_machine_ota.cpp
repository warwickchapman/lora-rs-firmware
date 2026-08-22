#include <unity.h>
#include <stdint.h>
#include "ota_handoff_helper.h"

// Define mock structs matching NodeStateMachine's inner state variables
struct DummyTxTransfer {
  bool active = false;
  uint8_t dst = 0;
  uint8_t transfer_id = 0;
  bool awaiting_ack = false;
};

struct DummyStatusRecord {
  uint8_t dst = 0;
  uint8_t transfer_id = 0;
  uint8_t stage = 0;
  uint8_t error_code = 0;
  uint32_t timestamp = 0;
};

void test_ota_handoff_status_matching() {
  DummyTxTransfer tx;
  DummyStatusRecord status;

  // 1. Initially inactive: no match for accepted (op 1) or failed (op 2)
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 1));
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 2));

  // 2. Active session but NOT awaiting_ack (manifest chunks sending, commit not sent yet)
  tx.active = true;
  tx.dst = 12;
  tx.transfer_id = 42;
  tx.awaiting_ack = false;

  // Early manifest_accepted (op = 1) MUST be rejected before commit/awaiting_ack
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 1));
  // download_failed (op = 2) can still match an active transfer
  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 2));

  // 3. Active session AND awaiting_ack (commit frame sent)
  tx.awaiting_ack = true;
  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 1));
  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 2));

  // 4. Active session: rejects wrong address or wrong transfer ID
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 13, 42, 1));
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 43, 1));
}

void test_ota_handoff_terminal_state_rejection() {
  DummyTxTransfer tx;
  DummyStatusRecord status;

  // Gateway has triggered remote OTA: active tx slot and status record are occupied
  tx.active = true;
  tx.dst = 12;
  tx.transfer_id = 42;
  tx.awaiting_ack = true; // commit sent
  status.dst = 12;
  status.transfer_id = 42;
  status.stage = 2; // awaiting_ack

  // Remote sends manifest_accepted (op = 1) -> session accepted and active tx slot is released
  bool handled = ota_handoff::OtaHandoffHelper::handleStatusPayload(tx, status, 1, 0, 1000);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL_UINT8(3, status.stage); // accepted
  TEST_ASSERT_FALSE(tx.active);

  // Replayed/delayed manifest_accepted (op = 1) arrives on persistent session -> MUST be rejected (revival guard)
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 1));

  // Late download_failed (op = 2) arrives on persistent session -> allowed to match and transition
  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 2));
  handled = ota_handoff::OtaHandoffHelper::handleStatusPayload(tx, status, 2, 9, 2000);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL_UINT8(5, status.stage); // failed
  TEST_ASSERT_EQUAL_UINT8(9, status.error_code);

  // Once status is failed (stage 5), replayed accepted (op = 1) or failed (op = 2) frames are rejected
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 1));
  TEST_ASSERT_FALSE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, status, 12, 42, 2));
}

void test_late_failure_does_not_cancel_another_manifest() {
  DummyTxTransfer tx;
  tx.active = true;
  tx.awaiting_ack = true;
  tx.dst = 7;
  tx.transfer_id = 77;

  DummyStatusRecord previous;
  previous.dst = 6;
  previous.transfer_id = 66;
  previous.stage = 3;

  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::matchesStatusFrame(tx, previous, 6, 66, 2));
  TEST_ASSERT_TRUE(ota_handoff::OtaHandoffHelper::handleStatusPayload(tx, previous, 2, 9, 3000));
  TEST_ASSERT_TRUE(tx.active);
  TEST_ASSERT_EQUAL_UINT8(7, tx.dst);
  TEST_ASSERT_EQUAL_UINT8(77, tx.transfer_id);
  TEST_ASSERT_EQUAL_UINT8(5, previous.stage);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_ota_handoff_status_matching);
  RUN_TEST(test_ota_handoff_terminal_state_rejection);
  RUN_TEST(test_late_failure_does_not_cancel_another_manifest);
  return UNITY_END();
}
