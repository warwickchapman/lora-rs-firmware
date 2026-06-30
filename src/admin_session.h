#pragma once
#include <ArduinoJson.h>

class AdminSession {
public:
  struct SessionState {
    uint32_t session_id = 0;
    uint32_t created_ms = 0;
    uint32_t last_seq = 0;
    bool active = false;
  };

  enum class ValidationResult {
    Ok,
    SessionRequired,
    SessionInvalid,
    SessionExpired,
    SequenceReplay
  };

  AdminSession() = default;

  const SessionState& state() const { return state_; }
  SessionState& state() { return state_; }

  void create(uint32_t current_ms, uint32_t session_id);
  ValidationResult validate(const ArduinoJson::JsonDocument &doc, uint32_t current_ms);

private:
  SessionState state_;
};
