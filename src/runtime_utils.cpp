#include "runtime_utils.h"

namespace runtime_utils {

bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx) {
  if (mode == "paired") {
    if (role == "transmitter") {
      roleTx = true;
      return true;
    }
    if (role == "receiver") {
      roleTx = false;
      return true;
    }
    return false;
  }
  if (mode == "standalone" && role == "none") {
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
