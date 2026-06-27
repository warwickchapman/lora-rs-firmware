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
