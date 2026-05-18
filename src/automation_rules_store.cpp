#include "automation_rules_store.h"

#include <LittleFS.h>

#include "feature_flags.h"

namespace automation_rules {
namespace {
constexpr char kRulesPath[] = "/automation_rules.json";
}

const char *Store::rulesPath() { return kRulesPath; }

bool Store::exists() {
#if !LRS_ENABLE_AUTOMATIONS
  return false;
#else
  return LittleFS.exists(kRulesPath);
#endif
}

}  // namespace automation_rules
