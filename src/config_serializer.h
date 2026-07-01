#pragma once

#include <ArduinoJson.h>
#include "config_store.h"

void writeAddressArray(ArduinoJson::JsonDocument &doc, const char *key, const uint8_t *values,
                       uint8_t count, uint8_t cap);

void writeSettingsJsonObject(ArduinoJson::JsonObject config, ConfigStore &store,
                             bool includeSecrets);

void writeSettingsJson(ArduinoJson::JsonDocument &doc, ConfigStore &config,
                       bool includeSecrets);
