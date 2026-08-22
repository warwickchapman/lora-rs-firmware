#pragma once

#include <stdint.h>

namespace factory_reset_transaction {

constexpr uint8_t kResultCommitted = 1;
constexpr uint8_t kResultSaveFailed = 2;

inline bool validResult(uint8_t result) {
  return result == kResultCommitted || result == kResultSaveFailed;
}

inline bool matchesStatus(uint8_t expectedSrc, uint8_t src,
                          uint32_t expectedTransactionId, uint32_t transactionId,
                          uint8_t expectedFlags, uint8_t flags, uint8_t result) {
  return src == expectedSrc && transactionId == expectedTransactionId &&
         flags == expectedFlags &&
         validResult(result);
}

inline bool shouldRetryAfterTimeout(uint8_t retryCount) {
  return retryCount == 0;
}

} // namespace factory_reset_transaction
