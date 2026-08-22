#ifndef LORA_RS_OTA_HANDOFF_HELPER_H
#define LORA_RS_OTA_HANDOFF_HELPER_H

#include <stdint.h>

namespace ota_handoff {

class OtaHandoffHelper {
public:
  template <typename Tx, typename Status>
  static bool matchesStatusFrame(const Tx &tx, const Status &status, uint8_t src, uint8_t transferId, uint8_t op) {
    if (op == 1) { // manifest_accepted
      // manifest_accepted requires the live ota_pull_tx_ to be active and awaiting_ack
      return tx.active && tx.awaiting_ack && tx.dst == src && tx.transfer_id == transferId;
    } else if (op == 2) { // download_failed
      // download_failed can match the live tx or the persistent accepted status session
      if (tx.active && tx.dst == src && tx.transfer_id == transferId) {
        return true;
      }
      if (status.dst == src && status.transfer_id == transferId && status.stage == 3) {
        // Match the persistent accepted session
        return true;
      }
    }
    return false;
  }

  template <typename Tx, typename Status>
  static bool handleStatusPayload(Tx &tx, Status &status, uint8_t op, uint8_t errorCode, uint32_t now) {
    if (op == 1) { // manifest_accepted
      status.stage = 3; // accepted
      status.timestamp = now;
      tx = Tx{}; // release manifest slot
      return true;
    } else if (op == 2) { // download_failed
      status.stage = 5; // failed
      status.error_code = errorCode;
      status.timestamp = now;
      if (tx.active && tx.dst == status.dst && tx.transfer_id == status.transfer_id) {
        tx = Tx{};
      }
      return true;
    }
    return false;
  }
};

} // namespace ota_handoff

#endif // LORA_RS_OTA_HANDOFF_HELPER_H
