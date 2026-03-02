#include "automation_rules_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Print.h>
#include <cstdio>
#include <cstring>

#include "feature_flags.h"
#include "logger.h"

namespace automation_rules {
#if !LRS_ENABLE_AUTOMATIONS

const char *Store::rulesPath() { return "/automation_rules.json"; }
bool Store::exists() { return false; }
size_t Store::defaultJsonLength() { return 74; }
void Store::writeDefaultJson(Print &out) {
  out.print("{\"schema_version\":1,\"enabled\":false,\"execution_mode\":"
            "\"standalone\",\"rules\":[]}\n");
}
SaveResult Store::validateAndSave(const String &) {
  SaveResult res{};
  res.ok = false;
  res.http_status = 404;
  res.error_code = "disabled";
  return res;
}

#else
namespace {

constexpr char kRulesPath[] = "/automation_rules.json";
constexpr char kTempPath[] = "/automation_rules.tmp";
constexpr char kDefaultJson[] =
    "{\"schema_version\":1,\"enabled\":false,\"execution_mode\":\"standalone\","
    "\"rules\":[]}\n";
constexpr uint32_t kSchemaVersion = 1;
void setError(SaveResult &res, uint16_t status, const char *code,
              const char *detail = nullptr) {
  res.ok = false;
  res.http_status = status;
  res.error_code = code ? code : "invalid";
  if (detail && detail[0] != '\0') {
    snprintf(res.detail, sizeof(res.detail), "%s", detail);
  } else {
    res.detail[0] = '\0';
  }
}

bool isToken(const char *s, size_t maxLen = 32) {
  if (s == nullptr || s[0] == '\0')
    return false;
  size_t n = 0;
  while (s[n] != '\0') {
    const char c = s[n];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok)
      return false;
    ++n;
    if (n > maxLen)
      return false;
  }
  return true;
}

bool isAddressToken(JsonVariantConst v, bool allowSelf) {
  if (v.is<int>() || v.is<unsigned int>() || v.is<uint8_t>()) {
    const int n = v.as<int>();
    return n >= 1 && n <= 254;
  }
  const char *raw = v.as<const char *>();
  if (!raw)
    return false;
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n')
    ++raw;
  if (*raw == '\0')
    return false;
  if (allowSelf && strcmp(raw, "self") == 0)
    return true;
  char *end = nullptr;
  long n = 0;
  if ((raw[0] == '0') && (raw[1] == 'x' || raw[1] == 'X')) {
    n = strtol(raw, &end, 16);
  } else {
    n = strtol(raw, &end, 10);
  }
  if (end == nullptr || *end != '\0')
    return false;
  return n >= 1 && n <= 254;
}

bool isNonNegativeInt(JsonVariantConst v) {
  if (v.isNull())
    return true;
  if (v.is<int>() || v.is<unsigned int>() || v.is<uint32_t>() ||
      v.is<unsigned long>()) {
    return v.as<long>() >= 0;
  }
  const char *raw = v.as<const char *>();
  if (!raw || *raw == '\0')
    return false;
  char *end = nullptr;
  strtoul(raw, &end, 10);
  return end != nullptr && *end == '\0';
}

bool validatePredicate(JsonObjectConst pred) {
  if (pred.isNull())
    return false;
  if (!isAddressToken(pred["peer"], true))
    return false;
  const char *field = pred["field"] | "";
  const char *op = pred["op"] | "";
  if (field[0] == '\0' || op[0] == '\0')
    return false;
  if (pred["value"].isNull())
    return false;
  if (!pred["for_ms"].isNull() && !isNonNegativeInt(pred["for_ms"]))
    return false;
  return true;
}

bool validateAction(JsonVariantConst actionVar) {
  JsonObjectConst action = actionVar.as<JsonObjectConst>();
  if (action.isNull())
    return false;
  const char *type = action["type"] | "";
  if (strcmp(type, "set_relay") != 0)
    return false;
  if (!isAddressToken(action["peer"], true))
    return false;
  if (action["value"].isNull())
    return false;
  return true;
}

bool validateRule(JsonVariantConst ruleVar) {
  JsonObjectConst rule = ruleVar.as<JsonObjectConst>();
  if (rule.isNull())
    return false;
  if (!isToken(rule["id"] | "", 32))
    return false;

  const char *name = rule["name"] | "";
  if (name[0] != '\0' && strlen(name) > 64)
    return false;
  if (!rule["for_ms"].isNull() && !isNonNegativeInt(rule["for_ms"]))
    return false;
  if (!rule["cooldown_ms"].isNull() && !isNonNegativeInt(rule["cooldown_ms"]))
    return false;

  JsonObjectConst when = rule["when"].as<JsonObjectConst>();
  if (when.isNull())
    return false;
  JsonArrayConst all = when["all"].as<JsonArrayConst>();
  if (all.isNull())
    return false;
  const size_t predCount = all.size();
  if (predCount == 0 || predCount > kMaxPredicatesPerRule)
    return false;
  for (JsonVariantConst pred : all) {
    if (!validatePredicate(pred.as<JsonObjectConst>()))
      return false;
  }

  JsonVariantConst thenVar = rule["then"];
  if (thenVar.isNull())
    return false;
  if (thenVar.is<JsonArrayConst>()) {
    JsonArrayConst arr = thenVar.as<JsonArrayConst>();
    const size_t n = arr.size();
    if (n == 0 || n > kMaxActionsPerRule)
      return false;
    for (JsonVariantConst action : arr) {
      if (!validateAction(action))
        return false;
    }
    return true;
  }

  return validateAction(thenVar);
}

bool validateRoot(JsonDocument &doc, SaveResult &res) {
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) {
    setError(res, 400, "bad_root");
    return false;
  }

  const uint32_t schemaVersion = root["schema_version"] | kSchemaVersion;
  if (schemaVersion != kSchemaVersion) {
    setError(res, 400, "bad_schema");
    return false;
  }

  const char *mode = root["execution_mode"] | "standalone";
  if (strcmp(mode, "standalone") != 0 && strcmp(mode, "paired+rules") != 0) {
    setError(res, 400, "bad_mode");
    return false;
  }

  if (!root["peer_display"].isNull()) {
    const char *peerDisplay = root["peer_display"] | "";
    if (strcmp(peerDisplay, "addresses") != 0 &&
        strcmp(peerDisplay, "names") != 0) {
      setError(res, 400, "bad_peer_display");
      return false;
    }
  }

  if (!root["action_target"].isNull() &&
      !isAddressToken(root["action_target"], true)) {
    setError(res, 400, "bad_action_target");
    return false;
  }
  if (!root["action_target_v1"].isNull() &&
      !isAddressToken(root["action_target_v1"], true)) {
    setError(res, 400, "bad_action_target");
    return false;
  }

  JsonArrayConst rules = root["rules"].as<JsonArrayConst>();
  if (rules.isNull()) {
    setError(res, 400, "bad_rules");
    return false;
  }
  if (rules.size() > kMaxRules) {
    snprintf(res.detail, sizeof(res.detail), "max=%u",
             static_cast<unsigned>(kMaxRules));
    setError(res, 400, "too_many_rules", res.detail);
    return false;
  }
  size_t ruleIndex = 0;
  for (JsonVariantConst rule : rules) {
    if (!validateRule(rule)) {
      char detail[32];
      snprintf(detail, sizeof(detail), "rule[%u]",
               static_cast<unsigned>(ruleIndex));
      setError(res, 400, "bad_rule", detail);
      return false;
    }
    ++ruleIndex;
  }

  return true;
}

bool writeFileAtomic(const String &body, SaveResult &res) {
  File tmp = LittleFS.open(kTempPath, "w");
  if (!tmp) {
    setError(res, 500, "open_tmp");
    LRS_LOGE(FS, "event=automation_rules_open_failed path=%s mode=w",
             kTempPath);
    return false;
  }

  const size_t bytes = tmp.print(body);
  tmp.flush();
  tmp.close();
  if (bytes != static_cast<size_t>(body.length())) {
    LittleFS.remove(kTempPath);
    setError(res, 500, "write_failed");
    LRS_LOGE(FS, "event=automation_rules_write_failed path=%s want=%lu got=%lu",
             kTempPath, static_cast<unsigned long>(body.length()),
             static_cast<unsigned long>(bytes));
    return false;
  }

  LittleFS.remove(kRulesPath);
  if (!LittleFS.rename(kTempPath, kRulesPath)) {
    LittleFS.remove(kTempPath);
    setError(res, 500, "rename_failed");
    LRS_LOGE(FS, "event=automation_rules_rename_failed tmp=%s dst=%s",
             kTempPath, kRulesPath);
    return false;
  }

  res.ok = true;
  res.http_status = 200;
  res.error_code = "ok";
  res.saved_bytes = bytes;
  res.detail[0] = '\0';
  LRS_LOGI(FS, "event=automation_rules_saved path=%s bytes=%lu", kRulesPath,
           static_cast<unsigned long>(bytes));
  return true;
}

} // namespace

const char *Store::rulesPath() { return kRulesPath; }

bool Store::exists() { return LittleFS.exists(kRulesPath); }

size_t Store::defaultJsonLength() { return strlen(kDefaultJson); }

void Store::writeDefaultJson(Print &out) { out.print(kDefaultJson); }

SaveResult Store::validateAndSave(const String &body) {
  SaveResult res{};
  if (body.length() == 0) {
    setError(res, 400, "empty_body");
    return res;
  }
  if (body.length() > kMaxPayloadBytes) {
    char detail[48];
    snprintf(detail, sizeof(detail), "max_bytes=%lu",
             static_cast<unsigned long>(kMaxPayloadBytes));
    setError(res, 413, "payload_too_large", detail);
    return res;
  }

  size_t docCap = body.length() + 768U;
  if (docCap < 1536U)
    docCap = 1536U;
  if (docCap > 8192U)
    docCap = 8192U;
  JsonDocument doc;
  auto err = deserializeJson(doc, body);
  if (err) {
    setError(res, 400, "invalid_json", err.c_str());
    return res;
  }
  if (!validateRoot(doc, res))
    return res;
  writeFileAtomic(body, res);
  return res;
}

#endif
} // namespace automation_rules
