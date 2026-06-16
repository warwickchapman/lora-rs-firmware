#pragma once

#include <Arduino.h>

using OtaStatusCallback = void (*)(const char *status, void *ctx);
bool otaPullFromUrl(const char *url, const char *sha256Hex, String &error,
                    OtaStatusCallback cb = nullptr, void *ctx = nullptr);
