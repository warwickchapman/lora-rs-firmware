#include "mqtt_transaction.h"
#include "radio_protocol.h"

namespace mqtt_transaction_helpers {

OutboundTransactionPlan planOutboundTransaction(
    bool peer_found,
    bool is_pending,
    uint32_t pending_command_id) {
  if (!peer_found) {
    return {false, MqttTransactionOutcome::Untracked, false, 0};
  }
  const bool replaces = is_pending && (pending_command_id != 0);
  return {true, MqttTransactionOutcome::IgnoreObservabilityOnly, replaces, replaces ? pending_command_id : 0};
}

TransactionCommitState commitOutboundTransaction(
    uint32_t command_id,
    uint8_t relay_state,
    uint32_t now_ms) {
  return {
      true, // is_pending
      command_id,
      static_cast<uint8_t>(relay_state ? 1 : 0),
      0, // retry_step
      now_ms + calculateNextRetryDelayMs(0),
      now_ms + kMqttTransactionTimeoutMs,
      PeerAckState::Pending,
  };
}

MqttRetryAttempt prepareRetryAttempt(
    uint32_t command_id,
    uint8_t current_step,
    uint32_t current_counter) {
  uint8_t next_step = current_step + 1;
  uint32_t delay_ms = calculateNextRetryDelayMs(next_step);
  return {command_id, current_step, next_step, current_counter, delay_ms};
}

const char* outcomeToString(MqttTransactionOutcome outcome) {
  switch (outcome) {
    case MqttTransactionOutcome::Confirmed: return "Confirmed";
    case MqttTransactionOutcome::Mismatch: return "Mismatch";
    case MqttTransactionOutcome::Timeout: return "Timeout";
    case MqttTransactionOutcome::Untracked: return "Untracked";
    case MqttTransactionOutcome::Superseded: return "Superseded";
    case MqttTransactionOutcome::IgnoreObservabilityOnly: return "IgnoreObservabilityOnly";
  }
  return "Unknown";
}

MqttTransactionDecision evaluateStatusCorrelation(
    bool pending_active,
    uint32_t pending_command_id,
    uint8_t pending_relay,
    uint8_t msg_flags,
    uint32_t msg_command_id,
    uint8_t msg_relay_state) {
  // If no transaction is pending, status is purely observability.
  if (!pending_active || pending_command_id == 0) {
    return {MqttTransactionOutcome::IgnoreObservabilityOnly, false, false};
  }

  // Require explicit kFlagMqttTransaction flag (0x04)
  const bool isTransactionalStatus = (msg_flags & kFlagMqttTransaction) != 0;
  if (!isTransactionalStatus) {
    return {MqttTransactionOutcome::IgnoreObservabilityOnly, false, false};
  }

  // Require exact matching command_id
  if (msg_command_id != pending_command_id) {
    return {MqttTransactionOutcome::IgnoreObservabilityOnly, false, false};
  }

  // Matching command_id: check requested vs reported relay state
  const uint8_t normPending = pending_relay ? 1 : 0;
  const uint8_t normReported = msg_relay_state ? 1 : 0;

  if (normPending == normReported) {
    return {MqttTransactionOutcome::Confirmed, true, true};
  } else {
    return {MqttTransactionOutcome::Mismatch, true, true};
  }
}

uint32_t calculateNextRetryDelayMs(uint8_t step) {
  if (step < kMaxRetrySteps) {
    return kRetryDelaysMs[step];
  }
  return kRetryDelaysMs[kMaxRetrySteps - 1];
}

} // namespace mqtt_transaction_helpers
