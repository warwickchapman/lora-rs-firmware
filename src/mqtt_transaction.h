#pragma once

#include <Arduino.h>
#include <cstdint>
#include "peer_types.h"

namespace mqtt_transaction_helpers {

enum class MqttTransactionOutcome : uint8_t {
  Confirmed,
  Mismatch,
  Timeout,
  Untracked,
  Superseded,
  IgnoreObservabilityOnly,
};

const char* outcomeToString(MqttTransactionOutcome outcome);

constexpr uint32_t kMqttTransactionTimeoutMs = 5500;
constexpr uint32_t kRetryDelaysMs[] = {500, 1500, 3000};
constexpr size_t kMaxRetrySteps = sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]);

struct MqttTransactionDecision {
  MqttTransactionOutcome outcome;
  bool settle_pending;
  bool is_terminal;
};

// Evaluates outbound transaction decision before sending airtime
struct OutboundTransactionPlan {
  bool may_queue;
  MqttTransactionOutcome immediate_outcome;
  bool replaces_pending;
  uint32_t superseded_command_id;
};

OutboundTransactionPlan planOutboundTransaction(
    bool peer_found,
    bool is_pending,
    uint32_t pending_command_id);

// Creates scheduler-owned transaction state before the first RF attempt.
struct TransactionCommitState {
  bool is_pending;
  uint32_t pending_command_id;
  uint8_t pending_relay;
  uint8_t retry_step;
  uint32_t next_retry_ms;
  uint32_t pending_deadline_ms;
  PeerAckState ack_state;
};

TransactionCommitState createOutboundTransaction(
    uint32_t command_id,
    uint8_t relay_state,
    uint32_t now_ms);

// Prepares the next scheduler attempt while preserving the logical command ID.
struct MqttRetryAttempt {
  uint32_t command_id;
  uint8_t current_step;
  uint8_t next_step;
  uint32_t next_delay_ms;
};

MqttRetryAttempt prepareRetryAttempt(
    uint32_t command_id,
    uint8_t current_step);

// Evaluates whether an incoming MqttStatus frame correlates to a pending MQTT transaction.
// Status correlates ONLY if kFlagMqttTransaction (0x04) is present in msg_flags and msg_command_id matches pending_command_id.
MqttTransactionDecision evaluateStatusCorrelation(
    bool pending_active,
    uint32_t pending_command_id,
    uint8_t pending_relay,
    uint8_t msg_flags,
    uint32_t msg_command_id,
    uint8_t msg_relay_state);

// Returns the retry delay for the given step index (0 -> 500ms, 1 -> 1500ms, 2 -> 3000ms).
uint32_t calculateNextRetryDelayMs(uint8_t step);

// Checks if a transaction deadline has expired.
inline bool isTransactionTimedOut(uint32_t now, uint32_t deadline_ms) {
  return static_cast<int32_t>(now - deadline_ms) >= 0;
}

} // namespace mqtt_transaction_helpers
