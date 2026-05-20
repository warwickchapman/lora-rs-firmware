#include "runtime_utils.h"

#include <cstring>

namespace runtime_utils {

#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx) {
  return parseRoleTxFromModeRole(mode.c_str(), role.c_str(), roleTx);
}
#endif

bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx) {
  if (mode == nullptr || role == nullptr) return false;
  if (strcmp(mode, "paired") == 0) {
    if (strcmp(role, "transmitter") == 0) {
      roleTx = true;
      return true;
    }
    if (strcmp(role, "receiver") == 0) {
      roleTx = false;
      return true;
    }
    return false;
  }
  if (strcmp(mode, "standalone") == 0 && strcmp(role, "none") == 0) {
    roleTx = true;
    return true;
  }
  return false;
}

const char *wifiStatusText(wl_status_t st) {
  switch (st) {
  case WL_IDLE_STATUS:
    return "idle";
  case WL_NO_SSID_AVAIL:
    return "ssid_not_found";
  case WL_SCAN_COMPLETED:
    return "scan_completed";
  case WL_CONNECTED:
    return "connected";
  case WL_CONNECT_FAILED:
    return "connect_failed";
  case WL_CONNECTION_LOST:
    return "connection_lost";
  case WL_DISCONNECTED:
    return "disconnected";
#ifdef WL_WRONG_PASSWORD
  case WL_WRONG_PASSWORD:
    return "wrong_password";
#endif
#ifdef WL_NO_SHIELD
  case WL_NO_SHIELD:
    return "no_shield";
#endif
  default:
    return "unknown";
  }
}

} // namespace runtime_utils
