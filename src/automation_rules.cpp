#include "automation_rules.h"

#include <LittleFS.h>

#include "log_buffer.h"
#include "state_machine.h"

namespace {
constexpr const char *kRulesPath = "/automation_rules.json";
constexpr uint32_t kJsonDocBytes = 8192;
constexpr uint32_t kMinForCooldownMs = 0;

bool parseBoolJson(const JsonVariantConst &value, bool fallback) {
  if (value.isNull()) return fallback;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<int>()) return value.as<int>() != 0;
  const char *raw = value.as<const char *>();
  if (!raw) return fallback;
  String text(raw);
  text.trim();
  text.toLowerCase();
  if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
  if (text == "false" || text == "0" || text == "no" || text == "off") return false;
  return fallback;
}

uint8_t parseAddressTextLocal(const String &text, uint8_t fallback) {
  String t = text;
  t.trim();
  if (t.length() == 0) return fallback;
  long n = -1;
  if (t.startsWith("0x") || t.startsWith("0X")) {
    n = strtol(t.c_str(), nullptr, 16);
  } else {
    n = strtol(t.c_str(), nullptr, 10);
  }
  if (n < 1 || n > 254) return fallback;
  return static_cast<uint8_t>(n);
}

String defaultRulesJson() {
  DynamicJsonDocument doc(1024);
  doc["schema_version"] = 1;
  doc["enabled"] = false;
  doc["execution_mode"] = "standalone";
  JsonArray rules = doc.createNestedArray("rules");
  JsonObject r = rules.createNestedObject();
  r["id"] = "rule_1";
  r["name"] = "New automation";
  r["enabled"] = false;
  JsonObject when = r.createNestedObject("when");
  JsonArray all = when.createNestedArray("all");
  JsonObject p = all.createNestedObject();
  p["peer"] = "self";
  p["field"] = "input";
  p["op"] = "==";
  p["value"] = 1;
  r["for_ms"] = 0;
  r["cooldown_ms"] = 0;
  JsonArray then = r.createNestedArray("then");
  JsonObject a = then.createNestedObject();
  a["action"] = "set_relay";
  a["peer"] = "self";
  a["value"] = 1;
  String out;
  serializeJson(doc, out);
  return out;
}
}  // namespace

bool AutomationRulesEngine::begin(NodeStateMachine *sm, LogBuffer *logs, const Settings &cfg) {
  sm_ = sm;
  logs_ = logs;
  applyConfig(cfg);
  String error;
  if (!load(&error)) {
    last_load_error_ = error;
    if (logs_) logs_->add("auto_rules_load_err", 0, 0, 0);
  }
  return true;
}

void AutomationRulesEngine::applyConfig(const Settings &cfg) {
  role_tx_ = cfg.role_tx;
  tx_input_lora_control_enabled_ = cfg.tx_input_lora_control_enabled;
}

void AutomationRulesEngine::tick() {
  if (!enabled_ || sm_ == nullptr) return;
  if (!canExecuteNow()) return;

  const uint32_t now = millis();
  for (size_t i = 0; i < rule_count_; ++i) {
    Rule &rule = rules_[i];
    RuleRuntimeState &rt = runtime_[i];
    if (!rule.in_use || !rule.enabled) continue;

    bool allTrue = (rule.predicate_count > 0);
    for (size_t p = 0; p < rule.predicate_count; ++p) {
      PeerEvalView scratch{};
      if (!evaluatePredicate(rule.predicates[p], scratch)) {
        allTrue = false;
        break;
      }
    }

    if (!allTrue) {
      rt.condition_true_since_ms = 0;
      continue;
    }

    if (rt.condition_true_since_ms == 0) {
      rt.condition_true_since_ms = now;
    }
    if (rule.for_ms > 0 && (now - rt.condition_true_since_ms) < rule.for_ms) {
      return;  // first-match priority: condition is winning even while timing window matures
    }

    // v1 supports a single self relay action only.
    if (rule.action_count == 0 || !rule.actions[0].in_use) return;
    const Action &action = rule.actions[0];
    const uint8_t desired = action.value ? 1 : 0;

    if (sm_->relayState() == desired) {
      return;  // first-match priority; no-op still blocks lower rules
    }
    if (rule.cooldown_ms > kMinForCooldownMs && rt.last_fire_ms != 0 && (now - rt.last_fire_ms) < rule.cooldown_ms) {
      return;  // still first-match winner
    }

    sm_->automationSetLocalRelay(desired);
    rt.last_fire_ms = now;
    if (logs_) logs_->add("auto_rule_fire", 0, static_cast<uint32_t>(i + 1), desired);
    return;  // first match stops processing
  }
}

bool AutomationRulesEngine::load(String *error) {
  String raw;
  if (!LittleFS.exists(kRulesPath)) {
    raw = defaultRulesJson();
    String saveErr;
    if (!saveJson(raw, saveErr)) {
      if (error) *error = saveErr;
      return false;
    }
    if (error) *error = "";
    return true;
  }

  File f = LittleFS.open(kRulesPath, "r");
  if (!f) {
    if (error) *error = "open_failed";
    return false;
  }
  raw = f.readString();
  f.close();

  DynamicJsonDocument normalized(kJsonDocBytes);
  String parseError;
  if (!parseAndNormalize(raw, normalized, parseError)) {
    if (error) *error = parseError;
    return false;
  }
  String compileError;
  if (!compileFromDoc(normalized, compileError)) {
    if (error) *error = compileError;
    return false;
  }
  raw_json_ = "";
  serializeJson(normalized, raw_json_);
  last_load_error_ = "";
  if (error) *error = "";
  return true;
}

bool AutomationRulesEngine::saveJson(const String &json, String &error) {
  DynamicJsonDocument normalized(kJsonDocBytes);
  if (!parseAndNormalize(json, normalized, error)) {
    return false;
  }

  String compileError;
  if (!compileFromDoc(normalized, compileError)) {
    error = compileError;
    return false;
  }

  File f = LittleFS.open(kRulesPath, "w");
  if (!f) {
    error = "open_write_failed";
    return false;
  }
  if (serializeJson(normalized, f) == 0) {
    f.close();
    error = "write_failed";
    return false;
  }
  f.close();

  raw_json_ = "";
  serializeJson(normalized, raw_json_);
  last_load_error_ = "";
  resetRuntimeState();
  if (logs_) logs_->add("auto_rules_saved", 0, static_cast<uint32_t>(rule_count_), enabled_ ? 1 : 0);
  error = "";
  return true;
}

String AutomationRulesEngine::exportJson() const {
  if (raw_json_.length() > 0) return raw_json_;
  return defaultRulesJson();
}

void AutomationRulesEngine::appendApiMeta(JsonObject obj) const {
  obj["path"] = kRulesPath;
  obj["v1_standalone_only"] = true;
  obj["v1_action_targets"] = "self_only";
  obj["first_match_priority"] = true;
  obj["and_only_builder"] = true;
  obj["max_rules"] = kMaxRules;
  obj["max_predicates_per_rule"] = kMaxPredicatesPerRule;
  obj["max_actions_per_rule"] = kMaxActionsPerRule;
  JsonObject runtime = obj.createNestedObject("runtime");
  runtime["enabled"] = enabled_;
  runtime["execution_mode"] = execution_mode_standalone_ ? "standalone" : "paired+rules";
  runtime["can_execute_now"] = canExecuteNow();
  runtime["gate_reason"] = executionGateReason();
  runtime["role_tx"] = role_tx_;
  runtime["load_error"] = last_load_error_;
}

const char *AutomationRulesEngine::rulesPath() { return kRulesPath; }

bool AutomationRulesEngine::canExecuteNow() const {
  if (!enabled_) return false;
  if (!execution_mode_standalone_) return false;
  if (role_tx_ && tx_input_lora_control_enabled_) return false;  // v1 safety lane unless TX input->LoRa control is disabled
  return true;
}

const char *AutomationRulesEngine::executionGateReason() const {
  if (!enabled_) return "disabled";
  if (!execution_mode_standalone_) return "paired_rules_not_supported_in_v1";
  if (role_tx_ && tx_input_lora_control_enabled_) return "disable_tx_input_lora_control_to_allow_tx_standalone_rules";
  return "ok";
}

bool AutomationRulesEngine::parseAndNormalize(const String &json, DynamicJsonDocument &normalizedDoc, String &error) {
  DynamicJsonDocument in(kJsonDocBytes);
  auto err = deserializeJson(in, json);
  if (err) {
    error = "invalid_json";
    return false;
  }
  if (!in.is<JsonObject>()) {
    error = "root_must_be_object";
    return false;
  }

  JsonObjectConst root = in.as<JsonObjectConst>();
  normalizedDoc.clear();
  JsonObject out = normalizedDoc.to<JsonObject>();
  out["schema_version"] = 1;
  out["enabled"] = parseBoolJson(root["enabled"], false);
  String mode = String(static_cast<const char *>(root["execution_mode"] | "standalone"));
  mode.trim();
  mode.toLowerCase();
  if (mode != "standalone" && mode != "paired+rules") {
    error = "execution_mode_invalid";
    return false;
  }
  out["execution_mode"] = mode;

  JsonArrayConst inRules = root["rules"].as<JsonArrayConst>();
  JsonArray outRules = out.createNestedArray("rules");
  if (inRules.isNull()) {
    error = "rules_missing";
    return false;
  }
  if (inRules.size() > kMaxRules) {
    error = "too_many_rules";
    return false;
  }

  for (size_t i = 0; i < inRules.size(); ++i) {
    JsonObjectConst inRule = inRules[i].as<JsonObjectConst>();
    if (inRule.isNull()) {
      error = "rule_not_object";
      return false;
    }
    JsonObject outRule = outRules.createNestedObject();
    String id = String(static_cast<const char *>(inRule["id"] | ""));
    id.trim();
    if (id.length() == 0) id = "rule_" + String(i + 1);
    outRule["id"] = id;
    String name = String(static_cast<const char *>(inRule["name"] | ""));
    name.trim();
    if (name.length() == 0) name = id;
    outRule["name"] = name;
    outRule["enabled"] = parseBoolJson(inRule["enabled"], false);
    outRule["for_ms"] = static_cast<uint32_t>(inRule["for_ms"] | 0U);
    outRule["cooldown_ms"] = static_cast<uint32_t>(inRule["cooldown_ms"] | 0U);

    JsonObjectConst inWhen = inRule["when"].as<JsonObjectConst>();
    if (inWhen.isNull()) {
      error = "rule_when_missing";
      return false;
    }
    JsonArrayConst inAll = inWhen["all"].as<JsonArrayConst>();
    if (inAll.isNull() || inAll.size() == 0) {
      error = "rule_when_all_missing";
      return false;
    }
    if (inAll.size() > kMaxPredicatesPerRule) {
      error = "too_many_predicates";
      return false;
    }
    if (!inWhen["any"].isNull()) {
      error = "when_any_not_supported_in_v1";
      return false;
    }
    JsonObject outWhen = outRule.createNestedObject("when");
    JsonArray outAll = outWhen.createNestedArray("all");
    for (size_t p = 0; p < inAll.size(); ++p) {
      JsonObjectConst inPred = inAll[p].as<JsonObjectConst>();
      if (inPred.isNull()) {
        error = "predicate_not_object";
        return false;
      }
      JsonObject outPred = outAll.createNestedObject();
      String peer = String(static_cast<const char *>(inPred["peer"] | ""));
      peer.trim();
      if (peer.length() == 0) {
        error = "predicate_peer_missing";
        return false;
      }
      bool isSelf = false;
      uint8_t addr = 0;
      if (!parsePeerToken(peer, isSelf, addr)) {
        error = "predicate_peer_invalid";
        return false;
      }
      outPred["peer"] = isSelf ? "self" : peer;

      String field = String(static_cast<const char *>(inPred["field"] | ""));
      field.trim();
      field.toLowerCase();
      if (parseFieldName(field) == Field::Unknown) {
        error = "predicate_field_invalid";
        return false;
      }
      outPred["field"] = field;

      String op = String(static_cast<const char *>(inPred["op"] | ""));
      op.trim();
      if (parseOpName(op) == Op::Unknown) {
        error = "predicate_op_invalid";
        return false;
      }
      outPred["op"] = op;

      if (inPred["value"].isNull()) {
        error = "predicate_value_missing";
        return false;
      }
      if (inPred["value"].is<bool>()) {
        outPred["value"] = inPred["value"].as<bool>();
      } else if (inPred["value"].is<float>() || inPred["value"].is<int>() || inPred["value"].is<long>() || inPred["value"].is<double>()) {
        outPred["value"] = inPred["value"].as<double>();
      } else {
        error = "predicate_value_invalid";
        return false;
      }
    }

    JsonArrayConst inThen = inRule["then"].as<JsonArrayConst>();
    if (inThen.isNull() || inThen.size() == 0) {
      error = "rule_then_missing";
      return false;
    }
    if (inThen.size() > kMaxActionsPerRule) {
      error = "too_many_actions_v1";
      return false;
    }
    JsonArray outThen = outRule.createNestedArray("then");
    for (size_t a = 0; a < inThen.size(); ++a) {
      JsonObjectConst inAct = inThen[a].as<JsonObjectConst>();
      if (inAct.isNull()) {
        error = "action_not_object";
        return false;
      }
      JsonObject outAct = outThen.createNestedObject();
      String action = String(static_cast<const char *>(inAct["action"] | ""));
      action.trim();
      action.toLowerCase();
      if (action != "set_relay") {
        error = "action_type_invalid";
        return false;
      }
      outAct["action"] = "set_relay";
      String peer = String(static_cast<const char *>(inAct["peer"] | ""));
      peer.trim();
      if (peer.length() == 0) {
        error = "action_peer_missing";
        return false;
      }
      bool isSelf = false;
      uint8_t addr = 0;
      if (!parsePeerToken(peer, isSelf, addr)) {
        error = "action_peer_invalid";
        return false;
      }
      if (!isSelf) {
        error = "v1_action_peer_must_be_self";
        return false;
      }
      outAct["peer"] = "self";
      const int v = inAct["value"] | -1;
      if (v != 0 && v != 1) {
        error = "action_value_invalid";
        return false;
      }
      outAct["value"] = v;
    }
  }

  error = "";
  return true;
}

bool AutomationRulesEngine::compileFromDoc(const JsonDocument &doc, String &error) {
  JsonObjectConst root = doc.as<JsonObjectConst>();
  enabled_ = parseBoolJson(root["enabled"], false);
  const String mode = String(static_cast<const char *>(root["execution_mode"] | "standalone"));
  execution_mode_standalone_ = (mode == "standalone");
  rule_count_ = 0;
  for (size_t i = 0; i < kMaxRules; ++i) {
    rules_[i] = Rule{};
  }

  JsonArrayConst rules = root["rules"].as<JsonArrayConst>();
  for (size_t i = 0; i < rules.size() && i < kMaxRules; ++i) {
    Rule rule{};
    if (!compileRule(rules[i].as<JsonObjectConst>(), rule, error, i)) {
      return false;
    }
    rule.in_use = true;
    rules_[rule_count_++] = rule;
  }
  resetRuntimeState();
  error = "";
  return true;
}

bool AutomationRulesEngine::compileRule(const JsonObjectConst &in, Rule &out, String &error, size_t ruleIndex) {
  out.enabled = parseBoolJson(in["enabled"], false);
  out.id = String(static_cast<const char *>(in["id"] | ""));
  out.name = String(static_cast<const char *>(in["name"] | ""));
  out.for_ms = static_cast<uint32_t>(in["for_ms"] | 0U);
  out.cooldown_ms = static_cast<uint32_t>(in["cooldown_ms"] | 0U);

  JsonArrayConst all = in["when"]["all"].as<JsonArrayConst>();
  if (all.isNull() || all.size() == 0 || all.size() > kMaxPredicatesPerRule) {
    error = "compile_predicates_invalid@" + String(ruleIndex + 1);
    return false;
  }
  for (size_t i = 0; i < all.size(); ++i) {
    if (!parsePredicate(all[i].as<JsonObjectConst>(), out.predicates[i], error, ruleIndex, i)) {
      return false;
    }
    out.predicates[i].in_use = true;
    out.predicate_count++;
  }

  JsonArrayConst then = in["then"].as<JsonArrayConst>();
  if (then.isNull() || then.size() == 0 || then.size() > kMaxActionsPerRule) {
    error = "compile_actions_invalid@" + String(ruleIndex + 1);
    return false;
  }
  for (size_t i = 0; i < then.size(); ++i) {
    if (!parseAction(then[i].as<JsonObjectConst>(), out.actions[i], error, ruleIndex, i)) {
      return false;
    }
    out.actions[i].in_use = true;
    out.action_count++;
  }

  return true;
}

bool AutomationRulesEngine::parsePredicate(const JsonObjectConst &in,
                                           Predicate &out,
                                           String &error,
                                           size_t ruleIndex,
                                           size_t predIndex) const {
  String peer = String(static_cast<const char *>(in["peer"] | ""));
  if (!parsePeerToken(peer, out.peer_is_self, out.peer_addr)) {
    error = "predicate_peer_invalid@" + String(ruleIndex + 1) + ":" + String(predIndex + 1);
    return false;
  }
  String field = String(static_cast<const char *>(in["field"] | ""));
  field.trim();
  field.toLowerCase();
  out.field = parseFieldName(field);
  if (out.field == Field::Unknown) {
    error = "predicate_field_invalid@" + String(ruleIndex + 1) + ":" + String(predIndex + 1);
    return false;
  }
  String op = String(static_cast<const char *>(in["op"] | ""));
  out.op = parseOpName(op);
  if (out.op == Op::Unknown) {
    error = "predicate_op_invalid@" + String(ruleIndex + 1) + ":" + String(predIndex + 1);
    return false;
  }

  if (in["value"].is<bool>()) {
    out.value_is_bool = true;
    out.bool_value = in["value"].as<bool>();
    out.num_value = out.bool_value ? 1.0f : 0.0f;
  } else {
    out.value_is_bool = false;
    out.num_value = static_cast<float>(in["value"].as<double>());
    out.bool_value = (out.num_value != 0.0f);
  }
  return true;
}

bool AutomationRulesEngine::parseAction(const JsonObjectConst &in,
                                        Action &out,
                                        String &error,
                                        size_t ruleIndex,
                                        size_t actionIndex) const {
  String action = String(static_cast<const char *>(in["action"] | ""));
  action.trim();
  action.toLowerCase();
  if (action != "set_relay") {
    error = "action_type_invalid@" + String(ruleIndex + 1) + ":" + String(actionIndex + 1);
    return false;
  }
  String peer = String(static_cast<const char *>(in["peer"] | ""));
  bool isSelf = false;
  uint8_t addr = 0;
  if (!parsePeerToken(peer, isSelf, addr) || !isSelf) {
    error = "v1_action_peer_must_be_self@" + String(ruleIndex + 1) + ":" + String(actionIndex + 1);
    return false;
  }
  out.target_is_self = true;
  const int v = in["value"] | -1;
  if (v != 0 && v != 1) {
    error = "action_value_invalid@" + String(ruleIndex + 1) + ":" + String(actionIndex + 1);
    return false;
  }
  out.value = static_cast<uint8_t>(v);
  return true;
}

bool AutomationRulesEngine::parsePeerToken(const String &token, bool &isSelf, uint8_t &addr) {
  String t = token;
  t.trim();
  t.toLowerCase();
  if (t == "self") {
    isSelf = true;
    addr = 0;
    return true;
  }
  const uint8_t parsed = parseAddressTextLocal(t, 0);
  if (parsed == 0) return false;
  isSelf = false;
  addr = parsed;
  return true;
}

AutomationRulesEngine::Field AutomationRulesEngine::parseFieldName(const String &field) {
  if (field == "reachable") return Field::Reachable;
  if (field == "input") return Field::Input;
  if (field == "relay") return Field::Relay;
  if (field == "temp_c") return Field::TempC;
  if (field == "is_stale") return Field::IsStale;
  return Field::Unknown;
}

AutomationRulesEngine::Op AutomationRulesEngine::parseOpName(const String &op) {
  if (op == "==") return Op::Eq;
  if (op == "!=") return Op::Ne;
  if (op == ">") return Op::Gt;
  if (op == ">=") return Op::Ge;
  if (op == "<") return Op::Lt;
  if (op == "<=") return Op::Le;
  return Op::Unknown;
}

const char *AutomationRulesEngine::fieldName(Field field) {
  switch (field) {
    case Field::Reachable:
      return "reachable";
    case Field::Input:
      return "input";
    case Field::Relay:
      return "relay";
    case Field::TempC:
      return "temp_c";
    case Field::IsStale:
      return "is_stale";
    default:
      return "unknown";
  }
}

const char *AutomationRulesEngine::opName(Op op) {
  switch (op) {
    case Op::Eq:
      return "==";
    case Op::Ne:
      return "!=";
    case Op::Gt:
      return ">";
    case Op::Ge:
      return ">=";
    case Op::Lt:
      return "<";
    case Op::Le:
      return "<=";
    default:
      return "?";
  }
}

void AutomationRulesEngine::resetRuntimeState() {
  for (size_t i = 0; i < kMaxRules; ++i) {
    runtime_[i] = RuleRuntimeState{};
  }
}

bool AutomationRulesEngine::evaluatePredicate(const Predicate &p, PeerEvalView &scratch) const {
  if (!fillPeerEvalView(p.peer_is_self, p.peer_addr, scratch)) {
    return false;
  }
  switch (p.field) {
    case Field::Reachable:
      return compareBool(p.op, scratch.reachable, p.bool_value);
    case Field::Input:
      return compareNum(p.op, static_cast<float>(scratch.input), p.num_value);
    case Field::Relay:
      return compareNum(p.op, static_cast<float>(scratch.relay), p.num_value);
    case Field::TempC:
      if (!scratch.temp_valid) return false;
      return compareNum(p.op, scratch.temp_c, p.num_value);
    case Field::IsStale:
      return compareBool(p.op, scratch.is_stale, p.bool_value);
    case Field::Unknown:
    default:
      return false;
  }
}

bool AutomationRulesEngine::fillPeerEvalView(bool peerIsSelf, uint8_t peerAddr, PeerEvalView &out) const {
  out = PeerEvalView{};
  if (sm_ == nullptr) return false;
  if (peerIsSelf) {
    out.found = true;
    out.reachable = true;
    out.is_stale = false;
    out.input = sm_->inputState();
    out.relay = sm_->relayState();
    out.temp_valid = sm_->localTemperatureValid();
    out.temp_c = sm_->localTemperatureC();
    return true;
  }

  const size_t n = sm_->peerCount();
  const uint32_t now = millis();
  for (size_t i = 0; i < n; ++i) {
    PeerStatusSnapshot node{};
    if (!sm_->peerByIndex(i, node)) continue;
    if (node.address != peerAddr) continue;
    const uint32_t seenAgeMs = (node.last_seen_ms > 0 && now >= node.last_seen_ms) ? (now - node.last_seen_ms) : 0;
    const uint32_t staleAfter = staleAfterMsForPeer(node.poll_interval_ms);
    out.found = true;
    out.is_stale = (node.last_seen_ms == 0) || (seenAgeMs > staleAfter);
    out.reachable = !out.is_stale;
    out.input = node.input_state ? 1 : 0;
    out.relay = node.relay_state ? 1 : 0;
    out.temp_valid = node.temp_valid;
    out.temp_c = static_cast<float>(node.temp_c);
    return true;
  }

  // Unknown peer is treated as not reachable / stale.
  out.found = false;
  out.is_stale = true;
  out.reachable = false;
  out.temp_valid = false;
  return true;
}

bool AutomationRulesEngine::compareBool(Op op, bool lhs, bool rhs) {
  switch (op) {
    case Op::Eq:
      return lhs == rhs;
    case Op::Ne:
      return lhs != rhs;
    default:
      return false;
  }
}

bool AutomationRulesEngine::compareNum(Op op, float lhs, float rhs) {
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
    default:
      return false;
  }
}

uint32_t AutomationRulesEngine::staleAfterMsForPeer(uint32_t pollIntervalMs) {
  uint32_t staleThresholdMs = (pollIntervalMs > 0) ? (pollIntervalMs * 3U) : 300000U;
  if (staleThresholdMs < 180000U) staleThresholdMs = 180000U;
  return staleThresholdMs;
}
