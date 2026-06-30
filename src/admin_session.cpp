#include "admin_session.h"
#include <Arduino.h>

void AdminSession::create(uint32_t current_ms, uint32_t session_id) {
  state_.session_id = session_id;
  state_.created_ms = current_ms;
  state_.last_seq = 0;
  state_.active = true;
}

AdminSession::ValidationResult AdminSession::validate(const ArduinoJson::JsonDocument &doc, uint32_t current_ms) {
  if (doc["session_id"].isNull()) {
    return ValidationResult::SessionRequired;
  }

  const uint32_t req_session_id = doc["session_id"].as<uint32_t>();
  if (!state_.active || state_.session_id != req_session_id) {
    return ValidationResult::SessionInvalid;
  }

  if (current_ms - state_.created_ms >= 300000UL) {
    state_.active = false;
    return ValidationResult::SessionExpired;
  }

  if (doc["seq"].isNull()) {
    return ValidationResult::SequenceReplay;
  }

  const uint32_t seq = doc["seq"].as<uint32_t>();
  if (seq <= state_.last_seq) {
    return ValidationResult::SequenceReplay;
  }

  state_.last_seq = seq;
  return ValidationResult::Ok;
}
