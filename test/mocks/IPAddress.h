#pragma once
#include <stdint.h>
#include <string>

class IPAddress {
 public:
  uint8_t ip[4]{};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
  }
  uint8_t operator[](int index) const { return ip[index]; }
  uint8_t& operator[](int index) { return ip[index]; }
  std::string toString() const { return ""; }
};

inline bool operator==(const IPAddress &lhs, const IPAddress &rhs) {
  return lhs.ip[0] == rhs.ip[0] && lhs.ip[1] == rhs.ip[1] &&
         lhs.ip[2] == rhs.ip[2] && lhs.ip[3] == rhs.ip[3];
}

inline bool operator!=(const IPAddress &lhs, const IPAddress &rhs) {
  return !(lhs == rhs);
}
