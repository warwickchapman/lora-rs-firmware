// Shared stubs for native tests that link src/state_machine*.cpp.
// These provide the minimal symbol surface required to link; they are not
// exercised by the focused authorization tests.
#include <stdint.h>
#include <string>

#include "radio_protocol.h"
#include "admin_config_utils.h"
#include "logger.h"

namespace admin_config_utils {
const size_t kMinDeploymentKeyLen = 8;
}

bool RadioProtocol::begin(const Settings &) { return true; }
void RadioProtocol::applyConfig(const Settings &) {}
bool RadioProtocol::send(MessageType, uint8_t, uint8_t, uint8_t, uint32_t, uint8_t, uint8_t,
                         uint8_t, uint8_t, uint8_t, uint16_t, uint32_t) { return true; }
bool RadioProtocol::sendRaw(MessageType, uint32_t, uint8_t, uint8_t, const uint8_t *) { return true; }
bool RadioProtocol::sendProvisioningRaw(uint32_t, uint8_t, uint8_t, const uint8_t *, bool) { return true; }
bool RadioProtocol::receive(ProtocolMessage &) { return false; }
void RadioProtocol::deriveKeys() {}

namespace lrslog {
bool enabled(Level) { return false; }
void logf(Level, Category, const char *, ...) {}
void event(const char *, int, uint32_t, uint8_t) {}
void event(const String &, int, uint32_t, uint8_t) {}
uint32_t heapFree() { return 0; }
uint8_t heapFragPercent() { return 0; }
uint32_t heapMaxFreeBlock() { return 0; }
}  // namespace lrslog
