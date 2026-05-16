#pragma once

#include <Arduino.h>

bool otaPullFromUrl(const char *url, const char *sha256Hex, String &error);
