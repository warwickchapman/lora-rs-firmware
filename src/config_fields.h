#pragma once

#include <Arduino.h>

enum class ConfigFieldClass {
  RetainedConfig,  // Non-secret config values published as retained topics
  SecretMetadata,  // Secret metadata status only (wifi_sta_password_set, etc.)
  InternalOnly     // Never published / internal-only
};

struct ConfigField {
  const char *name;
  ConfigFieldClass classification;
};

extern const ConfigField kConfigFields[];
extern const size_t kConfigFieldCount;

const ConfigField *findConfigField(const char *name);
