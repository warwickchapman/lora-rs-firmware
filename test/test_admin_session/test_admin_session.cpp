#include <unity.h>
#include <ArduinoJson.h>
#include "admin_session.h"

void setUp() {}
void tearDown() {}

void test_session_creation() {
  AdminSession session;
  TEST_ASSERT_FALSE(session.state().active);
  TEST_ASSERT_EQUAL_UINT32(0, session.state().session_id);

  session.create(1000, 42); // current_ms = 1000, session_id = 42
  TEST_ASSERT_TRUE(session.state().active);
  TEST_ASSERT_EQUAL_UINT32(42, session.state().session_id);
  TEST_ASSERT_EQUAL_UINT32(1000, session.state().created_ms);
  TEST_ASSERT_EQUAL_UINT32(0, session.state().last_seq);
}

void test_session_valid_sequence() {
  AdminSession session;
  session.create(1000, 42);

  JsonDocument doc;
  doc["session_id"] = 42;
  doc["seq"] = 1;

  AdminSession::ValidationResult res = session.validate(doc, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::Ok), static_cast<int>(res));
  TEST_ASSERT_EQUAL_UINT32(1, session.state().last_seq);

  // increasing sequence is accepted
  doc["seq"] = 5;
  res = session.validate(doc, 3000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::Ok), static_cast<int>(res));
  TEST_ASSERT_EQUAL_UINT32(5, session.state().last_seq);
}

void test_session_missing_or_invalid() {
  AdminSession session;
  session.create(1000, 42);

  // missing session_id
  JsonDocument doc1;
  doc1["seq"] = 1;
  AdminSession::ValidationResult res = session.validate(doc1, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SessionRequired), static_cast<int>(res));

  // wrong session_id
  JsonDocument doc2;
  doc2["session_id"] = 99;
  doc2["seq"] = 1;
  res = session.validate(doc2, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SessionInvalid), static_cast<int>(res));

  // validating against uninitialized/inactive session
  AdminSession inactiveSession;
  res = inactiveSession.validate(doc2, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SessionInvalid), static_cast<int>(res));
}

void test_session_replay_rejection() {
  AdminSession session;
  session.create(1000, 42);

  JsonDocument doc;
  doc["session_id"] = 42;

  // missing seq
  AdminSession::ValidationResult res = session.validate(doc, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SequenceReplay), static_cast<int>(res));

  // valid sequence first
  doc["seq"] = 10;
  res = session.validate(doc, 2000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::Ok), static_cast<int>(res));

  // repeated sequence
  doc["seq"] = 10;
  res = session.validate(doc, 3000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SequenceReplay), static_cast<int>(res));

  // lower sequence
  doc["seq"] = 9;
  res = session.validate(doc, 4000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SequenceReplay), static_cast<int>(res));
}

void test_session_expiration() {
  AdminSession session;
  session.create(1000, 42);

  JsonDocument doc;
  doc["session_id"] = 42;
  doc["seq"] = 1;

  // exactly under 5 minutes (299,999 ms from created_ms, i.e. current_ms = 300,999 ms)
  AdminSession::ValidationResult res = session.validate(doc, 300999);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::Ok), static_cast<int>(res));

  // exactly at 5 minutes (300,000 ms from created_ms, i.e. current_ms = 301,000 ms)
  doc["seq"] = 2;
  res = session.validate(doc, 301000);
  TEST_ASSERT_EQUAL(static_cast<int>(AdminSession::ValidationResult::SessionExpired), static_cast<int>(res));
  TEST_ASSERT_FALSE(session.state().active);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_session_creation);
  RUN_TEST(test_session_valid_sequence);
  RUN_TEST(test_session_missing_or_invalid);
  RUN_TEST(test_session_replay_rejection);
  RUN_TEST(test_session_expiration);
  return UNITY_END();
}
