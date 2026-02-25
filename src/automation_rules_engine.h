#pragma once

#include <Arduino.h>

#include "config_store.h"

class NodeStateMachine;

class AutomationRulesEngine {
 public:
  enum class Field : uint8_t { TempC, Reachable, Input, Relay };
  enum class Op : uint8_t { Eq, Ne, Gt, Ge, Lt, Le };

  void begin();
  void requestReload();
  void tick(const Settings &cfg, NodeStateMachine &sm);

 private:
  enum class ValueKind : uint8_t { Number, Boolean };
  enum class GuardBlock : uint8_t {
    None = 0,
    DisabledConfig,
    NonStandaloneMode,
    MqttControlOwnsRelay,
    TxInputLoRaControlOwnsRelay,
    ActionTargetNotSelf,
    CompileError,
    NotLoaded,
  };

  struct Predicate {
    bool peer_is_self = true;
    uint8_t peer_addr = 0;
    Field field = Field::Input;
    Op op = Op::Eq;
    ValueKind value_kind = ValueKind::Number;
    int32_t number_value = 0;
    bool bool_value = false;
    uint32_t for_ms = 0;
  };

  struct Action {
    bool peer_is_self = true;
    uint8_t peer_addr = 0;
    uint8_t relay_value = 0;
  };

  struct Rule {
    bool enabled = true;
    char id[33] = {0};
    uint8_t predicate_count = 0;
    uint8_t action_count = 0;
    uint32_t for_ms = 0;
    uint32_t cooldown_ms = 0;
    Predicate predicates[4];
    Action actions[4];
  };

  struct Program {
    bool loaded = false;
    bool compile_ok = false;
    bool enabled = false;
    bool standalone_mode = true;
    bool action_target_is_self = true;
    uint8_t action_target_addr = 0;
    uint8_t rule_count = 0;
    Rule rules[8];
  };

  struct EvalPeer {
    bool present = false;
    bool reachable = false;
    bool relay_valid = false;
    uint8_t relay = 0;
    bool input_valid = false;
    uint8_t input = 0;
    bool temp_valid = false;
    float temp_c = 0.0f;
  };

  struct RuleRuntimeState {
    uint32_t predicate_true_since_ms[4] = {0, 0, 0, 0};
    uint32_t rule_true_since_ms = 0;
    uint32_t cooldown_until_ms = 0;
    bool predicate_hold_logged[4] = {false, false, false, false};
    bool rule_hold_logged = false;
    bool cooldown_logged = false;
  };

  Program program_{};
  RuleRuntimeState rule_state_[8]{};
  bool reload_pending_ = false;
  uint32_t last_reload_attempt_ms_ = 0;
  GuardBlock last_guard_block_ = GuardBlock::NotLoaded;
  int16_t last_match_rule_index_ = -1;
  bool timing_fields_present_ = false;

  void clearProgram();
  void maybeReload();
  bool loadAndCompile();
  bool evaluateRule(uint8_t ruleIndex, const Rule &rule, const Settings &cfg, const NodeStateMachine &sm, uint32_t nowMs);
  void executeRulePhase3b(int16_t ruleIndex, const Rule &rule, const Settings &cfg, NodeStateMachine &sm, uint32_t nowMs);
  bool evaluatePredicateRaw(const Predicate &pred, const Settings &cfg, const NodeStateMachine &sm, uint32_t nowMs) const;
  bool resolvePeer(const Predicate &pred, const Settings &cfg, const NodeStateMachine &sm, uint32_t nowMs, EvalPeer &out) const;
  bool compareNumber(float lhs, Op op, float rhs) const;
  bool compareBool(bool lhs, Op op, bool rhs) const;
  GuardBlock guardBlockForRuntime(const Settings &cfg) const;
  void logGuardTransition(GuardBlock block);
  void logMatchTransition(int16_t ruleIndex);
};
