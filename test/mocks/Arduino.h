#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

#define F(x) x
#define PROGMEM

#define LOW 0
#define HIGH 1

typedef std::string String;

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

inline long random(long min_val, long max_val) {
  if (min_val >= max_val) return min_val;
  return min_val + (rand() % (max_val - min_val));
}
