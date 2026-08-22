#pragma once

#include <stdint.h>

namespace readdress_transaction {

constexpr uint8_t kResultCommitted = 1;
constexpr uint8_t kResultSaveFailed = 2;

inline bool validResult(uint8_t result) {
  return result == kResultCommitted || result == kResultSaveFailed;
}

inline bool matchesStatus(uint8_t oldAddress, uint8_t assignedAddress, uint8_t src,
                          uint32_t expectedChipId, uint32_t chipId,
                          uint32_t expectedTransactionId, uint32_t transactionId,
                          uint8_t result) {
  if (!validResult(result) || chipId != expectedChipId ||
      transactionId != expectedTransactionId) {
    return false;
  }
  const uint8_t expectedSource =
      result == kResultCommitted ? assignedAddress : oldAddress;
  return src == expectedSource;
}

inline bool shouldRetryAfterTimeout(uint8_t retryCount) {
  return retryCount == 0;
}

} // namespace readdress_transaction
