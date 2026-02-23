#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config_store.h"

class LogBuffer;
class NodeStateMachine;

class AutomationRulesEngine {
 public:
  bool begin(NodeStateMachine *sm, LogBuffer *logs, const Settings &cfg);
  void applyConfig(const Settings &cfg);
  void tick();

  bool load(String *error = nullptr);
  bool saveJson(const String &json, String &error);
  String exportJson() const;
  void appendApiMeta(JsonObject obj) const;

  static const char *rulesPath();

 private:
  static constexpr size_t kMaxRules = 8;
  static constexpr size_t kMaxPredicatesPerRule = 4;
  static constexpr size_t kMaxActionsPerRule = 1;  // v1

  enum class Field : uint8_t {
    Unknown,
    Reachable,
    Input,
    Relay,
    TempC,
    IsStale,
  };

  enum class Op : uint8_t {
    Unknown,
    Eq,
    Ne,
    Gt,
    Ge,
    Lt,
    Le,
  };

  struct Predicate {
    bool in_use = false;
    bool peer_is_self = false;
    uint8_t peer_addr = 0;
    Field field = Field::Unknown;
    Op op = Op::Unknown;
    bool value_is_bool = false;
    bool bool_value = false;
    float num_value = 0.0f;
  };

  struct Action {
    bool in_use = false;
    bool target_is_self = false;
    uint8_t value = 0;
  };

  struct Rule {
    bool in_use = false;
    bool enabled = false;
    String id;
    String name;
    Predicate predicates[kMaxPredicatesPerRule]{};
    size_t predicate_count = 0;
    Action actions[kMaxActionsPerRule]{};
    size_t action_count = 0;
    uint32_t for_ms = 0;
    uint32_t cooldown_ms = 0;
  };

  struct RuleRuntimeState {
    uint32_t condition_true_since_ms = 0;
    uint32_t last_fire_ms = 0;
  };

  struct PeerEvalView {
    bool found = false;
    bool reachable = false;
    bool is_stale = true;
    uint8_t input = 0;
    uint8_t relay = 0;
    bool temp_valid = false;
    float temp_c = NAN;
  };

  NodeStateMachine *sm_ = nullptr;
  LogBuffer *logs_ = nullptr;
  bool role_tx_ = true;
  bool tx_input_lora_control_enabled_ = true;

  bool enabled_ = false;
  bool execution_mode_standalone_ = true;
  String raw_json_;
  String last_load_error_;
  Rule rules_[kMaxRules]{};
  size_t rule_count_ = 0;
  RuleRuntimeState runtime_[kMaxRules]{};

  bool canExecuteNow() const;
  const char *executionGateReason() const;

  bool parseAndNormalize(const String &json, DynamicJsonDocument &normalizedDoc, String &error);
  bool compileFromDoc(const JsonDocument &doc, String &error);
  bool compileRule(const JsonObjectConst &in, Rule &out, String &error, size_t ruleIndex);

  bool parsePredicate(const JsonObjectConst &in, Predicate &out, String &error, size_t ruleIndex, size_t predIndex) const;
  bool parseAction(const JsonObjectConst &in, Action &out, String &error, size_t ruleIndex, size_t actionIndex) const;

  static bool parsePeerToken(const String &token, bool &isSelf, uint8_t &addr);
  static Field parseFieldName(const String &field);
  static Op parseOpName(const String &op);
  static const char *fieldName(Field field);
  static const char *opName(Op op);

  void resetRuntimeState();
  bool evaluatePredicate(const Predicate &p, PeerEvalView &scratch) const;
  bool fillPeerEvalView(bool peerIsSelf, uint8_t peerAddr, PeerEvalView &out) const;
  static bool compareBool(Op op, bool lhs, bool rhs);
  static bool compareNum(Op op, float lhs, float rhs);
  static uint32_t staleAfterMsForPeer(uint32_t pollIntervalMs);
};
