#pragma once

#if defined(UNIT_TEST)
using wl_status_t = int;
constexpr wl_status_t WL_IDLE_STATUS = 0;
constexpr wl_status_t WL_NO_SSID_AVAIL = 1;
constexpr wl_status_t WL_SCAN_COMPLETED = 2;
constexpr wl_status_t WL_CONNECTED = 3;
constexpr wl_status_t WL_CONNECT_FAILED = 4;
constexpr wl_status_t WL_CONNECTION_LOST = 5;
constexpr wl_status_t WL_DISCONNECTED = 6;
constexpr wl_status_t WL_WRONG_PASSWORD = 7;
constexpr wl_status_t WL_NO_SHIELD = 8;
#else
#include <Arduino.h>
#include <ESP8266WiFi.h>
#endif

namespace runtime_utils {

#if !defined(UNIT_TEST)
bool parseRoleTxFromModeRole(const String &mode, const String &role, bool &roleTx);
#endif
bool parseRoleTxFromModeRole(const char *mode, const char *role, bool &roleTx);
const char *wifiStatusText(wl_status_t st);

} // namespace runtime_utils
