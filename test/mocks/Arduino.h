#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <algorithm>
#include <cctype>

#define F(x) x
#define PROGMEM

#define LOW 0
#define HIGH 1

#define INPUT 0
#define OUTPUT 1

class String : public std::string {
 public:
  String() = default;
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}

  void trim() {
    size_t start = 0;
    while (start < length() && std::isspace(static_cast<unsigned char>((*this)[start]))) ++start;
    size_t end = length();
    while (end > start && std::isspace(static_cast<unsigned char>((*this)[end - 1]))) --end;
    *this = substr(start, end - start);
  }

  void toLowerCase() {
    for (char &c : *this) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
};

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

inline long random(long min_val, long max_val) {
  if (min_val >= max_val) return min_val;
  return min_val + (rand() % (max_val - min_val));
}
