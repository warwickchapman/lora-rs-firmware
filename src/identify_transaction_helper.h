#pragma once

#include <stdint.h>

namespace identify_transaction {

constexpr uint8_t kMagic0 = 0x1D;
constexpr uint8_t kMagic1 = 0xE7;
constexpr uint8_t kOpRequest = 1;
constexpr uint8_t kResultAccepted = 2;
constexpr uint8_t kResultPowerSave = 3;
constexpr uint8_t kMinDurationSeconds = 1;
constexpr uint8_t kMaxDurationSeconds = 30;

inline bool validDuration(uint8_t durationSeconds) {
  return durationSeconds >= kMinDurationSeconds &&
         durationSeconds <= kMaxDurationSeconds;
}

inline bool validResult(uint8_t result) {
  return result == kResultAccepted || result == kResultPowerSave;
}

inline bool matchesStatus(uint8_t expectedSrc, uint8_t src,
                          uint32_t expectedTransactionId, uint32_t transactionId,
                          uint8_t expectedDurationSeconds, uint8_t durationSeconds,
                          uint8_t result) {
  return src == expectedSrc && transactionId == expectedTransactionId &&
         durationSeconds == expectedDurationSeconds && validDuration(durationSeconds) &&
         validResult(result);
}

inline bool shouldRetryAfterTimeout(uint8_t retryCount) {
  return retryCount == 0;
}

} // namespace identify_transaction
