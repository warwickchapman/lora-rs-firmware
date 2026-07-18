#pragma once
#include <stdint.h>
#include "IPAddress.h"

class ESP8266WiFiClass {
 public:
  bool isConnected() const { return false; }
  IPAddress localIP() const { return IPAddress(0, 0, 0, 0); }
  int RSSI() const { return 0; }
};

static ESP8266WiFiClass WiFi;
