#include "automation_rules_engine.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

#include "automation_rules_store.h"
#include "feature_flags.h"
#include "logger.h"
#include "state_machine.h"

namespace {

#if LRS_ENABLE_AUTOMATIONS
constexpr uint32_t kReloadRetryIntervalMs = 1000;
constexpr uint32_t kMinReachableWindowMs = 5000;
constexpr uint32_t kDefaultReachableWindowMs = 120000;

static bool parseAddressToken(JsonVariantConst v, bool allowSelf, bool &isSelf, uint8_t &addrOut) {
  isSelf = false;
  addrOut = 0;
  if (v.is<int>() || v.is<unsigned int>() || v.is<uint8_t>()) {
    const int n = v.as<int>();
    if (n < 1 || n > 254) return false;
    addrOut = static_cast<uint8_t>(n);
    return true;
  }
  const char *raw = v.as<const char *>();
  if (!raw) return false;
  while (*raw == ' ' || *raw == '\t' || *raw == '\r' || *raw == '\n') ++raw;
  if (*raw == '\0') return false;
  if (allowSelf && strcmp(raw, "self") == 0) {
    isSelf = true;
    return true;
  }
  char *end = nullptr;
  long n = 0;
  if (raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X')) {
    n = strtol(raw, &end, 16);
  } else {
    n = strtol(raw, &end, 10);
  }
  if (end == nullptr || *end != '\0' || n < 1 || n > 254) return false;
  addrOut = static_cast<uint8_t>(n);
  return true;
}

static bool parseOp(const char *s, AutomationRulesEngine::Op &out) {
  if (!s) return false;
  if (strcmp(s, "==") == 0) {
    out = AutomationRulesEngine::Op::Eq;
    return true;
  }
  if (strcmp(s, "!=") == 0) {
    out = AutomationRulesEngine::Op::Ne;
    return true;
  }
  if (strcmp(s, ">") == 0) {
    out = AutomationRulesEngine::Op::Gt;
    return true;
  }
  if (strcmp(s, ">=") == 0) {
    out = AutomationRulesEngine::Op::Ge;
    return true;
  }
  if (strcmp(s, "<") == 0) {
    out = AutomationRulesEngine::Op::Lt;
    return true;
  }
  if (strcmp(s, "<=") == 0) {
    out = AutomationRulesEngine::Op::Le;
    return true;
  }
  return false;
}

static bool parseField(const char *s, AutomationRulesEngine::Field &out) {
  if (!s) return false;
  if (strcmp(s, "temp_c") == 0) {
    out = AutomationRulesEngine::Field::TempC;
    return true;
  }
  if (strcmp(s, "reachable") == 0) {
    out = AutomationRulesEngine::Field::Reachable;
    return true;
  }
  if (strcmp(s, "input") == 0) {
    out = AutomationRulesEngine::Field::Input;
    return true;
  }
  if (strcmp(s, "relay") == 0) {
    out = AutomationRulesEngine::Field::Relay;
    return true;
  }
  return false;
}

static bool jsonBoolLoose(JsonVariantConst v, bool &out) {
  if (v.is<bool>()) {
    out = v.as<bool>();
    return true;
  }
  if (v.is<int>() || v.is<unsigned int>()) {
    out = (v.as<int>() != 0);
    return true;
  }
  const char *s = v.as<const char *>();
  if (!s) return false;
  if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) {
    out = true;
    return true;
  }
  if (strcmp(s, "false") == 0 || strcmp(s, "0") == 0) {
    out = false;
    return true;
  }
  return false;
}
#endif

}  // namespace

void AutomationRulesEngine::begin() {
  clearProgram();
  reload_pending_ = true;
  last_reload_attempt_ms_ = 0;
  last_guard_block_ = GuardBlock::NotLoaded;
  last_match_rule_index_ = -1;
  timing_fields_present_ = false;
}

void AutomationRulesEngine::requestReload() { reload_pending_ = true; }

void AutomationRulesEngine::clearProgram() {
  program_ = Program{};
  for (size_t i = 0; i < 8; ++i) {
    rule_state_[i] = RuleRuntimeState{};
  }
}

void AutomationRulesEngine::tick(const Settings &cfg, NodeStateMachine &sm) {
#if !LRS_ENABLE_AUTOMATIONS
  (void)cfg;
  (void)sm;
  return;
#else
  maybeReload();
  const GuardBlock guard = guardBlockForRuntime(cfg);
  logGuardTransition(guard);
  if (guard != GuardBlock::None) {
    if (last_match_rule_index_ != -1) {
      logMatchTransition(-1);
    }
    return;
  }

  const uint32_t nowMs = millis();
  int16_t matched = -1;
  for (uint8_t i = 0; i < program_.rule_count; ++i) {
    const Rule &rule = program_.rules[i];
    if (!rule.enabled) continue;
    if (evaluateRule(i, rule, cfg, sm, nowMs)) {
      matched = static_cast<int16_t>(i);
      executeRulePhase3b(matched, rule, cfg, sm, nowMs);
      break;
    }
  }
  logMatchTransition(matched);
#endif
}

void AutomationRulesEngine::maybeReload() {
#if !LRS_ENABLE_AUTOMATIONS
  return;
#else
  if (!reload_pending_) return;
  const uint32_t nowMs = millis();
  if (last_reload_attempt_ms_ != 0U && (nowMs - last_reload_attempt_ms_) < kReloadRetryIntervalMs) return;
  last_reload_attempt_ms_ = nowMs;
  if (loadAndCompile()) {
    reload_pending_ = false;
  }
#endif
}

bool AutomationRulesEngine::loadAndCompile() {
#if !LRS_ENABLE_AUTOMATIONS
  return true;
#else
  const uint32_t startMs = millis();
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t blockBefore = lrslog::heapMaxFreeBlock();
  clearProgram();
  program_.loaded = true;
  program_.compile_ok = false;

  size_t docCap = 1536U;
  if (automation_rules::Store::exists()) {
    File fsize = LittleFS.open(automation_rules::Store::rulesPath(), "r");
    if (fsize) {
      const size_t len = static_cast<size_t>(fsize.size());
      docCap = len + 1024U;
      if (docCap < 1536U) docCap = 1536U;
      if (docCap > 8192U) docCap = 8192U;
      fsize.close();
    }
  }

  auto logCompileFail = [&](const char *reason, const char *detail = nullptr) {
    if (detail && detail[0] != '\0') {
      LRS_LOGE(SYS,
               "event=automations_compile_failed reason=%s detail=%s dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu doc_cap=%lu",
               reason,
               detail,
               static_cast<unsigned long>(millis() - startMs),
               static_cast<unsigned long>(heapBefore),
               static_cast<unsigned long>(lrslog::heapFree()),
               static_cast<unsigned long>(blockBefore),
               static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
               static_cast<unsigned long>(docCap));
    } else {
      LRS_LOGE(SYS,
               "event=automations_compile_failed reason=%s dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu doc_cap=%lu",
               reason,
               static_cast<unsigned long>(millis() - startMs),
               static_cast<unsigned long>(heapBefore),
               static_cast<unsigned long>(lrslog::heapFree()),
               static_cast<unsigned long>(blockBefore),
               static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
               static_cast<unsigned long>(docCap));
    }
  };

  DynamicJsonDocument doc(docCap);
  if (!automation_rules::Store::exists()) {
    auto err = deserializeJson(doc, "{\"schema_version\":1,\"enabled\":false,\"execution_mode\":\"standalone\",\"rules\":[]}");
    if (err) {
      logCompileFail("default_json_parse", err.c_str());
      return false;
    }
  } else {
    File f = LittleFS.open(automation_rules::Store::rulesPath(), "r");
    if (!f) {
      logCompileFail("open", automation_rules::Store::rulesPath());
      return false;
    }
    auto err = deserializeJson(doc, f);
    f.close();
    if (err) {
      logCompileFail("parse", err.c_str());
      return false;
    }
  }

  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) {
    logCompileFail("bad_root");
    return false;
  }

  program_.enabled = root["enabled"] | false;
  const char *mode = root["execution_mode"] | "standalone";
  program_.standalone_mode = (strcmp(mode, "paired+rules") != 0);

  if (!parseAddressToken(root["action_target"].isNull() ? root["action_target_v1"] : root["action_target"],
                         true,
                         program_.action_target_is_self,
                         program_.action_target_addr)) {
    program_.action_target_is_self = true;
    program_.action_target_addr = 0;
  }

  JsonArrayConst rules = root["rules"].as<JsonArrayConst>();
  if (rules.isNull()) {
    logCompileFail("bad_rules");
    return false;
  }

  uint8_t outRuleCount = 0;
  timing_fields_present_ = false;
  for (JsonVariantConst ruleVar : rules) {
    if (outRuleCount >= automation_rules::kMaxRules) break;
    JsonObjectConst srcRule = ruleVar.as<JsonObjectConst>();
    if (srcRule.isNull()) {
      char detail[24];
      snprintf(detail, sizeof(detail), "idx=%u", static_cast<unsigned>(outRuleCount));
      logCompileFail("rule_type", detail);
      return false;
    }

    Rule &dstRule = program_.rules[outRuleCount];
    dstRule.enabled = srcRule["enabled"] | true;
    dstRule.for_ms = srcRule["for_ms"] | 0U;
    dstRule.cooldown_ms = srcRule["cooldown_ms"] | 0U;
    if (dstRule.for_ms != 0U || dstRule.cooldown_ms != 0U) timing_fields_present_ = true;
    const char *id = srcRule["id"] | "";
    snprintf(dstRule.id, sizeof(dstRule.id), "%s", id);

    JsonObjectConst when = srcRule["when"].as<JsonObjectConst>();
    JsonArrayConst all = when["all"].as<JsonArrayConst>();
    if (when.isNull() || all.isNull() || all.size() == 0 || all.size() > automation_rules::kMaxPredicatesPerRule) {
      char detail[24];
      snprintf(detail, sizeof(detail), "idx=%u", static_cast<unsigned>(outRuleCount));
      logCompileFail("bad_when", detail);
      return false;
    }

    uint8_t predCount = 0;
    for (JsonVariantConst predVar : all) {
      JsonObjectConst srcPred = predVar.as<JsonObjectConst>();
      if (srcPred.isNull()) return false;
      Predicate &dstPred = dstRule.predicates[predCount];
      if (!parseAddressToken(srcPred["peer"], true, dstPred.peer_is_self, dstPred.peer_addr)) return false;
      if (!parseField(srcPred["field"] | "", dstPred.field)) return false;
      if (!parseOp(srcPred["op"] | "==", dstPred.op)) return false;
      dstPred.for_ms = srcPred["for_ms"] | 0U;
      if (dstPred.for_ms != 0U) timing_fields_present_ = true;

      if (dstPred.field == Field::Reachable) {
        bool b = false;
        if (!jsonBoolLoose(srcPred["value"], b)) return false;
        dstPred.value_kind = ValueKind::Boolean;
        dstPred.bool_value = b;
      } else {
        if (!(srcPred["value"].is<float>() || srcPred["value"].is<double>() || srcPred["value"].is<int>() ||
              srcPred["value"].is<unsigned int>() || srcPred["value"].is<long>() || srcPred["value"].is<const char *>())) {
          return false;
        }
        dstPred.value_kind = ValueKind::Number;
        dstPred.number_value = static_cast<int32_t>(srcPred["value"].as<float>());
      }
      ++predCount;
    }
    dstRule.predicate_count = predCount;

    JsonVariantConst thenVar = srcRule["then"];
    JsonArrayConst actions = thenVar.is<JsonArrayConst>() ? thenVar.as<JsonArrayConst>() : JsonArrayConst();
    if (actions.isNull()) {
      JsonObjectConst one = thenVar.as<JsonObjectConst>();
      if (one.isNull()) return false;
      actions = JsonArrayConst();  // sentinel; parse single below
      Action &dstAction = dstRule.actions[0];
      if (strcmp(one["type"] | "", "set_relay") != 0) return false;
      if (!parseAddressToken(one["peer"], true, dstAction.peer_is_self, dstAction.peer_addr)) return false;
      dstAction.relay_value = (static_cast<int>(one["value"] | 0) == 1) ? 1 : 0;
      dstRule.action_count = 1;
    } else {
      if (actions.size() == 0 || actions.size() > automation_rules::kMaxActionsPerRule) return false;
      uint8_t actionCount = 0;
      for (JsonVariantConst actVar : actions) {
        JsonObjectConst one = actVar.as<JsonObjectConst>();
        if (one.isNull()) return false;
        Action &dstAction = dstRule.actions[actionCount];
        if (strcmp(one["type"] | "", "set_relay") != 0) return false;
        if (!parseAddressToken(one["peer"], true, dstAction.peer_is_self, dstAction.peer_addr)) return false;
        dstAction.relay_value = (static_cast<int>(one["value"] | 0) == 1) ? 1 : 0;
        ++actionCount;
      }
      dstRule.action_count = actionCount;
    }

    ++outRuleCount;
  }
  program_.rule_count = outRuleCount;
  program_.compile_ok = true;

  LRS_LOGI(SYS,
           "event=automations_compiled rules=%u enabled=%u mode=%s action_target=%s dur_ms=%lu heap_before=%lu heap_after=%lu block_before=%lu block_after=%lu doc_cap=%lu",
           static_cast<unsigned>(program_.rule_count),
           static_cast<unsigned>(program_.enabled ? 1 : 0),
           program_.standalone_mode ? "standalone" : "paired+rules",
           program_.action_target_is_self ? "self" : "peer",
           static_cast<unsigned long>(millis() - startMs),
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned long>(blockBefore),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
           static_cast<unsigned long>(docCap));
  if (timing_fields_present_) {
    LRS_LOGI(SYS, "event=automations_timing_enabled phase=3c");
  }
  return true;
#endif
}

bool AutomationRulesEngine::evaluateRule(uint8_t ruleIndex,
                                         const Rule &rule,
                                         const Settings &cfg,
                                         const NodeStateMachine &sm,
                                         uint32_t nowMs) {
  RuleRuntimeState &rt = rule_state_[ruleIndex];
  bool allReady = true;
  for (uint8_t i = 0; i < rule.predicate_count; ++i) {
    const Predicate &pred = rule.predicates[i];
    const bool rawTrue = evaluatePredicateRaw(pred, cfg, sm, nowMs);
    if (!rawTrue) {
      rt.predicate_true_since_ms[i] = 0;
      rt.predicate_hold_logged[i] = false;
      allReady = false;
      continue;
    }

    if (rt.predicate_true_since_ms[i] == 0U) rt.predicate_true_since_ms[i] = nowMs;
    if (pred.for_ms != 0U) {
      const uint32_t heldMs = nowMs - rt.predicate_true_since_ms[i];
      if (heldMs < pred.for_ms) {
        if (!rt.predicate_hold_logged[i]) {
          LRS_LOGI(SYS,
                   "event=automations_hold_pending scope=predicate rule_index=%u predicate_index=%u need_ms=%lu have_ms=%lu",
                   static_cast<unsigned>(ruleIndex),
                   static_cast<unsigned>(i),
                   static_cast<unsigned long>(pred.for_ms),
                   static_cast<unsigned long>(heldMs));
          rt.predicate_hold_logged[i] = true;
        }
        allReady = false;
      } else {
        rt.predicate_hold_logged[i] = false;
      }
    } else {
      rt.predicate_hold_logged[i] = false;
    }
  }

  if (!allReady) {
    rt.rule_true_since_ms = 0U;
    rt.rule_hold_logged = false;
    return false;
  }

  if (rt.rule_true_since_ms == 0U) rt.rule_true_since_ms = nowMs;
  if (rule.for_ms != 0U) {
    const uint32_t heldMs = nowMs - rt.rule_true_since_ms;
    if (heldMs < rule.for_ms) {
      if (!rt.rule_hold_logged) {
        LRS_LOGI(SYS,
                 "event=automations_hold_pending scope=rule rule_index=%u need_ms=%lu have_ms=%lu",
                 static_cast<unsigned>(ruleIndex),
                 static_cast<unsigned long>(rule.for_ms),
                 static_cast<unsigned long>(heldMs));
        rt.rule_hold_logged = true;
      }
      return false;
    }
  }
  rt.rule_hold_logged = false;
  return true;
}

bool AutomationRulesEngine::evaluatePredicateRaw(const Predicate &pred,
                                                 const Settings &cfg,
                                                 const NodeStateMachine &sm,
                                                 uint32_t nowMs) const {
  EvalPeer peer{};
  if (!resolvePeer(pred, cfg, sm, nowMs, peer)) return false;

  switch (pred.field) {
    case Field::Reachable:
      return compareBool(peer.reachable, pred.op, pred.bool_value);
    case Field::Input:
      if (!peer.input_valid) return false;
      return compareNumber(static_cast<float>(peer.input), pred.op, static_cast<float>(pred.number_value));
    case Field::Relay:
      if (!peer.relay_valid) return false;
      return compareNumber(static_cast<float>(peer.relay), pred.op, static_cast<float>(pred.number_value));
    case Field::TempC:
      if (!peer.temp_valid) return false;
      return compareNumber(peer.temp_c, pred.op, static_cast<float>(pred.number_value));
  }
  return false;
}

bool AutomationRulesEngine::resolvePeer(const Predicate &pred,
                                        const Settings &cfg,
                                        const NodeStateMachine &sm,
                                        uint32_t nowMs,
                                        EvalPeer &out) const {
#if !LRS_ENABLE_AUTOMATIONS
  (void)pred;
  (void)cfg;
  (void)sm;
  (void)nowMs;
  out = EvalPeer{};
  return false;
#else
  out = EvalPeer{};
  if (pred.peer_is_self) {
    out.present = true;
    out.reachable = true;
    out.relay_valid = true;
    out.relay = sm.relayState() ? 1 : 0;
    out.input_valid = true;
    out.input = sm.inputState() ? 1 : 0;
    out.temp_valid = sm.localTemperatureValid();
    if (out.temp_valid) out.temp_c = sm.localTemperatureC();
    return true;
  }

  PeerStatusSnapshot peer{};
  const size_t count = sm.peerCount();
  for (size_t i = 0; i < count; ++i) {
    if (!sm.peerByIndex(i, peer)) continue;
    if (peer.address != pred.peer_addr) continue;
    out.present = true;
    out.relay_valid = true;
    out.relay = peer.relay_state ? 1 : 0;
    out.input_valid = true;
    out.input = peer.input_state ? 1 : 0;
    out.temp_valid = peer.temp_valid;
    if (out.temp_valid) out.temp_c = static_cast<float>(peer.temp_c);
    uint32_t staleWindowMs = kDefaultReachableWindowMs;
    if (peer.poll_interval_ms > 0U) {
      staleWindowMs = peer.poll_interval_ms * 2U;
      if (staleWindowMs < kMinReachableWindowMs) staleWindowMs = kMinReachableWindowMs;
    } else if (cfg.heartbeat_ms > 0U) {
      staleWindowMs = cfg.heartbeat_ms * 2U;
      if (staleWindowMs < kMinReachableWindowMs) staleWindowMs = kMinReachableWindowMs;
    }
    out.reachable = (peer.last_seen_ms != 0U) && ((nowMs - peer.last_seen_ms) <= staleWindowMs);
    return true;
  }

  out.present = false;
  out.reachable = false;
  return true;
#endif
}

bool AutomationRulesEngine::compareNumber(float lhs, Op op, float rhs) const {
  switch (op) {
    case Op::Eq:
      return lhs == rhs;
    case Op::Ne:
      return lhs != rhs;
    case Op::Gt:
      return lhs > rhs;
    case Op::Ge:
      return lhs >= rhs;
    case Op::Lt:
      return lhs < rhs;
    case Op::Le:
      return lhs <= rhs;
  }
  return false;
}

bool AutomationRulesEngine::compareBool(bool lhs, Op op, bool rhs) const {
  switch (op) {
    case Op::Eq:
      return lhs == rhs;
    case Op::Ne:
      return lhs != rhs;
    default:
      return false;
  }
}

AutomationRulesEngine::GuardBlock AutomationRulesEngine::guardBlockForRuntime(const Settings &cfg) const {
#if !LRS_ENABLE_AUTOMATIONS
  (void)cfg;
  return GuardBlock::CompileError;
#else
  if (!program_.loaded) return GuardBlock::NotLoaded;
  if (!program_.compile_ok) return GuardBlock::CompileError;
  if (!program_.enabled) return GuardBlock::DisabledConfig;
  if (!program_.standalone_mode) return GuardBlock::NonStandaloneMode;
  if (!program_.action_target_is_self) return GuardBlock::ActionTargetNotSelf;
  if (cfg.role_tx && cfg.input_control_paired_lora_enabled) return GuardBlock::TxInputLoRaControlOwnsRelay;
  return GuardBlock::None;
#endif
}

void AutomationRulesEngine::logGuardTransition(GuardBlock block) {
  if (block == last_guard_block_) return;
  last_guard_block_ = block;
  switch (block) {
    case GuardBlock::None:
      LRS_LOGI(SYS, "event=automations_runtime_ready phase=3c");
      break;
    case GuardBlock::DisabledConfig:
      LRS_LOGI(SYS, "event=automations_runtime_blocked reason=config_disabled");
      break;
    case GuardBlock::NonStandaloneMode:
      LRS_LOGI(SYS, "event=automations_runtime_blocked reason=mode_not_standalone");
      break;
    case GuardBlock::TxInputLoRaControlOwnsRelay:
      LRS_LOGI(SYS, "event=automations_runtime_blocked reason=input_control_paired_lora_enabled");
      break;
    case GuardBlock::ActionTargetNotSelf:
      LRS_LOGI(SYS, "event=automations_runtime_blocked reason=action_target_not_self");
      break;
    case GuardBlock::CompileError:
      LRS_LOGW(SYS, "event=automations_runtime_blocked reason=compile_error");
      break;
    case GuardBlock::NotLoaded:
      LRS_LOGI(SYS, "event=automations_runtime_blocked reason=not_loaded");
      break;
  }
}

void AutomationRulesEngine::logMatchTransition(int16_t ruleIndex) {
  if (ruleIndex == last_match_rule_index_) return;
  if (ruleIndex < 0) {
    if (last_match_rule_index_ >= 0) {
      LRS_LOGI(SYS, "event=automations_match_clear prev_rule_index=%d", last_match_rule_index_);
    }
    last_match_rule_index_ = -1;
    return;
  }
  const Rule &rule = program_.rules[static_cast<uint8_t>(ruleIndex)];
  LRS_LOGI(SYS,
           "event=automations_match phase=3c rule_index=%d rule_id=%s",
           ruleIndex,
           (rule.id[0] != '\0') ? rule.id : "rule");
  last_match_rule_index_ = ruleIndex;
}

void AutomationRulesEngine::executeRulePhase3b(int16_t ruleIndex,
                                               const Rule &rule,
                                               const Settings &cfg,
                                               NodeStateMachine &sm,
                                               uint32_t nowMs) {
  (void)cfg;
  const bool sameMatchedRule = (last_match_rule_index_ == ruleIndex);
  RuleRuntimeState &rt = rule_state_[static_cast<uint8_t>(ruleIndex)];
  if (rule.cooldown_ms != 0U && rt.cooldown_until_ms != 0U && static_cast<int32_t>(nowMs - rt.cooldown_until_ms) < 0) {
    if (!rt.cooldown_logged) {
      LRS_LOGI(SYS,
               "event=automations_cooldown_active rule_id=%s remaining_ms=%lu",
               (rule.id[0] != '\0') ? rule.id : "rule",
               static_cast<unsigned long>(rt.cooldown_until_ms - nowMs));
      rt.cooldown_logged = true;
    }
    return;
  }
  rt.cooldown_logged = false;
  bool sawUnsupportedAction = false;
  bool wroteRelay = false;
  for (uint8_t i = 0; i < rule.action_count; ++i) {
    const Action &a = rule.actions[i];
    if (!a.peer_is_self) {
      sawUnsupportedAction = true;
      continue;
    }
    const uint8_t desired = a.relay_value ? 1U : 0U;
    if (sm.relayState() == desired) {
      if (!sameMatchedRule) {
        LRS_LOGI(SYS, "event=automations_action_skip reason=already_desired rule_id=%s relay=%u",
                 (rule.id[0] != '\0') ? rule.id : "rule",
                 static_cast<unsigned>(desired));
      }
      continue;
    }
    sm.automationSetLocalRelay(desired);
    wroteRelay = true;
    LRS_LOGI(SYS, "event=automations_action_fired phase=3c rule_id=%s relay=%u",
             (rule.id[0] != '\0') ? rule.id : "rule",
             static_cast<unsigned>(desired));
  }
  if (wroteRelay && rule.cooldown_ms != 0U) {
    rt.cooldown_until_ms = nowMs + rule.cooldown_ms;
  }
  if (!wroteRelay && sawUnsupportedAction && !sameMatchedRule) {
    LRS_LOGI(SYS, "event=automations_action_skip reason=non_self_target rule_id=%s",
             (rule.id[0] != '\0') ? rule.id : "rule");
  }
}
