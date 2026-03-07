#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>

namespace runtime_utils {

bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx);
const char *wifiStatusText(wl_status_t st);

} // namespace runtime_utils
