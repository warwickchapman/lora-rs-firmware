#include "state_machine.h"

#include <ESP8266WiFi.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdlib.h>

#include "build_info.h"
#include "logger.h"
#include "admin_config_utils.h"
#include "runtime_utils.h"

namespace {
constexpr uint8_t kInputPin = 4;
constexpr uint8_t kRelayPin = 5;
constexpr uint32_t kDebounceMs = 50;
constexpr uint32_t kTxRelayEchoDelayMs = 500;
// Paired dry-contact control is latency-sensitive; retry quickly first, then
// back off to limit sustained airtime when a peer is unavailable.
constexpr uint32_t kAckRetryScheduleMs[] = {350, 650, 1000, 1500, 2500, 4000, 6500, 10000, 16000, 25000, 40000, 55000};
constexpr uint8_t kAckRetryJitterPct = 15;
constexpr uint32_t kAckSlotBaseMs = 180;
constexpr uint32_t kAckSlotJitterMaxMs = 40;
constexpr uint32_t kAckWindowGuardMs = 120;
constexpr uint32_t kAckRetryOneShotTimeoutMs = 350;
constexpr uint32_t kAckRetryInterNodeGapMs = 20;
constexpr uint32_t kMqttRetryScheduleMs[] = {1000, 2000, 3000, 5000, 8000, 13000, 21000, 34000, 55000};
constexpr uint32_t kDefaultRemotePollIntervalMs = 60000;
constexpr uint32_t kMinRemotePollIntervalMs = 60000;
constexpr uint32_t kMaxRemotePollIntervalMs = 3600000;
constexpr uint8_t kFlagTimeAuthoritative = 0x01;
constexpr uint8_t kFlagPairedInputSlave = 0x02;
constexpr uint8_t kFlagGroupPollCorrelation = 0x04;
constexpr uint32_t kMinRetryTimeoutMs = 5000U;
constexpr uint32_t kMaxRetryTimeoutMs = 3600000U;
constexpr uint32_t kMinRxFailsafeTimeoutMs = 5000U;
constexpr uint32_t kMaxRxFailsafeTimeoutMs = 3600000U;
constexpr uint8_t kWifiProvisionOpStart = 1;
constexpr uint8_t kWifiProvisionOpData = 2;
constexpr uint8_t kWifiProvisionOpCommit = 3;
constexpr uint8_t kWifiProvisionChunkDataBytes = 7;
constexpr uint8_t kWifiProvisionBroadcastAddress = 255;
constexpr uint8_t kWifiControlBroadcastAddress = 255;
constexpr uint8_t kWifiControlOpSet = 1;
constexpr uint8_t kWifiControlOpStatus = 2;
constexpr uint8_t kUdpLogControlOpSet = 1;
constexpr uint8_t kMaintenancePayloadVersion = 2;
constexpr uint8_t kMaintenancePageIdentity = 0;
constexpr uint8_t kMaintenancePageDebug = 1;
constexpr uint8_t kMaintenancePageSensors = 2;
constexpr uint8_t kMaintenancePageVersion = 3;
constexpr uint8_t kOtaPullControlOpStart = 1;
constexpr uint8_t kOtaPullControlOpHash = 2;
constexpr uint8_t kOtaPullControlOpCommit = 3;
constexpr uint8_t kOtaPullControlHashChunkBytes = 8;
constexpr uint8_t kOtaPullControlHashChunks = 4;
constexpr uint32_t kOtaPullControlFrameSpacingMs = 250;
constexpr uint8_t kFactoryResetMagic0 = 0xA5;
constexpr uint8_t kFactoryResetMagic1 = 0x5A;
constexpr uint8_t kFactoryResetKeepFleetFlag = 0x01;
constexpr uint8_t kFactoryResetKeepWifiFlag = 0x02;
constexpr uint8_t kRebootMagic0 = 0xB5;
constexpr uint8_t kRebootMagic1 = 0x5B;
constexpr uint8_t kSensorConfigMagic0 = 0xC5;
constexpr uint8_t kSensorConfigMagic1 = 0x5C;
constexpr uint32_t kWifiProvisionCooldownMs = 60000;
constexpr uint8_t kFleetKeyControlOpStart = 1;
constexpr uint8_t kFleetKeyControlOpData = 2;
constexpr uint8_t kFleetKeyControlOpCommit = 3;
constexpr uint8_t kProvOpDiscoverStart = 1;
constexpr uint8_t kProvOpAnnounce = 2;
constexpr uint8_t kProvOpAssign = 3;
constexpr uint8_t kProvOpKeyStart = 4;
constexpr uint8_t kProvOpKeyData = 5;
constexpr uint8_t kProvOpKeyCommit = 6;
constexpr uint8_t kProvOpApplyCommit = 7;
constexpr uint8_t kProvOpVerify = 8;
constexpr uint8_t kProvHwModelLrs = 1;
constexpr uint8_t kProvHwRevA1 = 0xA1;
constexpr uint8_t kProvRoleTxFlag = 0x01;
constexpr uint32_t kProvVerifyTimeoutMs = 4000;
constexpr uint32_t kProvLateVerifyProbeTimeoutMs = 1500;
constexpr uint32_t kProvDiscoverReplyBaseMs = 2000;
constexpr uint32_t kProvDiscoverReplyPerDeviceMs = 2200;
constexpr uint32_t kProvDiscoverReplyWindowMaxMs = 30000;
constexpr uint8_t kProvDiscoverBroadcastBurstCount = 2;
constexpr uint32_t kProvDiscoverBroadcastGapMs = 150;
constexpr uint8_t kProvMaxRetriesPerNode = 3;
constexpr uint8_t kProvCoordinatorBurstPacketsPerTick = 6;
constexpr uint8_t kProvAnnounceRepeatCount = 2;
constexpr uint16_t kProvAnnounceRetryBackoffMs = 120;
constexpr uint8_t kProvKeyChunkBytes = 3;
constexpr uint8_t kProvBroadcastAddress = 255;
constexpr size_t kProvChunkBitmapMax = 31;
constexpr uint8_t kProvAddressMin = 1;
constexpr uint8_t kProvAddressMax = Settings::kAddressListCap;
constexpr uint32_t kStateMachineLivenessLogIntervalMs = 60000;
constexpr uint32_t kProvWatchdogLogIntervalMs = 2000;
constexpr uint32_t kStartupPhaseTraceWindowMs = 15000;

inline void startupTxPhaseTrace(const char *phase) {
  const uint32_t now = millis();
  if (now > kStartupPhaseTraceWindowMs) return;
  static const char *lastPhase = nullptr;
  static uint32_t lastPhaseLogMs = 0;
  // Startup DEBUG traces are diagnostic-only; suppress repeated phase spam.
  if (lastPhase == phase && static_cast<uint32_t>(now - lastPhaseLogMs) < 250U) return;
  lastPhase = phase;
  lastPhaseLogMs = now;
  LRS_LOGD(LORA, "event=startup_tx_phase phase=%s ms=%lu", phase, static_cast<unsigned long>(now));
}

inline void startupTxSendTimingTrace(const char *phase, uint32_t startMs) {
  if (startMs > kStartupPhaseTraceWindowMs) return;
  const uint32_t endMs = millis();
  LRS_LOGD(LORA, "event=startup_tx_phase phase=%s ms=%lu dur_ms=%lu", phase, static_cast<unsigned long>(endMs),
           static_cast<unsigned long>(endMs - startMs));
}

bool isDefaultDeploymentKey(const char *v) {
  if (v == nullptr) return false;
  while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') ++v;
  size_t len = strlen(v);
  while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t' || v[len - 1] == '\r' || v[len - 1] == '\n')) --len;
  return strlen("lora-default-passphrase") == len && strncmp(v, "lora-default-passphrase", len) == 0;
}

bool csvContainsAddress(const String &raw, uint8_t src) {
  const char *cursor = raw.c_str();
  while (*cursor != '\0') {
    while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (*cursor == '\0') break;

    char *tail = nullptr;
    const long parsed = strtol(cursor, &tail, 0);
    if (tail != cursor) {
      while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') {
        ++tail;
      }
      if ((*tail == ',' || *tail == '\0') && parsed > 0 && parsed < 255 && static_cast<uint8_t>(parsed) == src) {
        return true;
      }
    }

    while (*cursor != '\0' && *cursor != ',') {
      ++cursor;
    }
    if (*cursor == ',') ++cursor;
  }
  return false;
}

bool fixedListContainsAddress(const uint8_t *values, uint8_t count, uint8_t src) {
  if (values == nullptr || src == 0 || src == 255) return false;
  if (count > Settings::kAddressListCap) count = Settings::kAddressListCap;
  for (uint8_t i = 0; i < count; ++i) {
    if (values[i] == src) return true;
  }
  return false;
}

uint16_t crc16Ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFFU;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t b = 0; b < 8; ++b) {
      crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void encodeU16LE(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
}

uint16_t decodeU16LE(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void encodeU32LE(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFFU);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFFU);
}

uint32_t decodeU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void parseFwVersionPacked(uint8_t &major, uint8_t &minor, uint8_t &patch) {
  major = static_cast<uint8_t>(LRS_FW_MAJOR);
  minor = static_cast<uint8_t>(LRS_FW_MINOR);
  patch = static_cast<uint8_t>(LRS_FW_PATCH);
}

uint16_t fwDevBuild() {
  return static_cast<uint16_t>(LRS_FW_DEV_BUILD);
}

uint32_t fnv1a32(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= 16777619UL;
  }
  return h;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool sha256HexToBytes(const char *hex, uint8_t out[32]) {
  if (hex == nullptr || strlen(hex) != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    const int hi = hexNibble(hex[i * 2]);
    const int lo = hexNibble(hex[(i * 2) + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

String sha256BytesToHex(const uint8_t digest[32]) {
  char out[65];
  for (size_t i = 0; i < 32; ++i) {
    snprintf(out + (i * 2), 3, "%02x", digest[i]);
  }
  out[64] = '\0';
  return String(out);
}

void fillProvisionPayloadBytes(const ProtocolMessage &msg, uint8_t out[7]) {
  out[0] = msg.sensor_digital0;
  out[1] = static_cast<uint8_t>(msg.sensor_analog0 & 0xFFU);
  out[2] = static_cast<uint8_t>((msg.sensor_analog0 >> 8) & 0xFFU);
  out[3] = static_cast<uint8_t>(msg.unix_time_s & 0xFFU);
  out[4] = static_cast<uint8_t>((msg.unix_time_s >> 8) & 0xFFU);
  out[5] = static_cast<uint8_t>((msg.unix_time_s >> 16) & 0xFFU);
  out[6] = static_cast<uint8_t>((msg.unix_time_s >> 24) & 0xFFU);
}

uint8_t encodeTempCode(bool valid, float celsius) {
  if (!valid || !isfinite(celsius)) return 0xFF;
  int t = static_cast<int>(roundf(celsius));
  if (t < -127) t = -127;
  if (t > 126) t = 126;
  return static_cast<uint8_t>(static_cast<int8_t>(t));
}

TankSensorState decodeTankState(uint8_t raw) {
  if (raw <= static_cast<uint8_t>(TankSensorState::Overrange)) {
    return static_cast<TankSensorState>(raw);
  }
  return TankSensorState::Disabled;
}

RxFailsafeMode parseRxFailsafeMode(const String &rawMode) {
  String mode = rawMode;
  mode.trim();
  mode.toLowerCase();
  if (mode == "force_off") return RxFailsafeMode::ForceOff;
  if (mode == "force_on") return RxFailsafeMode::ForceOn;
  return RxFailsafeMode::HoldLast;
}

uint32_t jitteredDelayMs(uint32_t baseMs, uint8_t pct) {
  if (baseMs == 0 || pct == 0) return baseMs;
  const uint32_t span = (baseMs * static_cast<uint32_t>(pct)) / 100U;
  if (span == 0) return baseMs;
  const long jitter = random(-static_cast<long>(span), static_cast<long>(span) + 1L);
  const int64_t adjusted = static_cast<int64_t>(baseMs) + static_cast<int64_t>(jitter);
  return (adjusted < 1) ? 1U : static_cast<uint32_t>(adjusted);
}
}

bool NodeStateMachine::begin(const Settings &cfg, RadioProtocol *radio) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  radio_ = radio;

  pinMode(kLedPin, OUTPUT);
  pinMode(kRelayPin, OUTPUT);
  pinMode(kInputPin, INPUT);

  digitalWrite(kRelayPin, LOW);
  digitalWrite(kLedPin, HIGH);

  input_state_ = localDryContactState();
  last_input_raw_ = input_state_;
  relay_state_ = digitalRead(kRelayPin);

  link_state_ = LinkState::Idle;
  last_heartbeat_ms_ = millis();
  wait_ack_since_ms_ = millis();
  last_led_toggle_ms_ = millis();
  last_packet_ms_ = 0;
  last_packet_rssi_ = -127;
  last_wifi_prov_tx_ms_ = 0;
  tx_state_sync_pending_ = runtime_.role_tx && runtime_.input_control_paired_lora_enabled;
  tx_command_pending_ = false;
  tx_pending_command_counter_ = 0;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  tx_command_retry_deadline_ms_ = 0;
  resetTxGroupState();
  rx_deferred_ack_pending_ = false;
  rx_deferred_ack_due_ms_ = 0;
  rx_deferred_ack_command_id_ = 0;
  rx_deferred_ack_dst_ = 0;
  rx_deferred_ack_relay_ = 0;
  rx_deferred_ack_input_ = 0;
  paired_input_slave_mode_ = false;
  rx_push_pending_ = false;
  rx_last_push_ms_ = 0;
  last_rx_control_ms_ = 0;
  maintenance_debug_pending_ = false;
  maintenance_debug_dst_ = 0;
  maintenance_sensor_pending_ = false;
  maintenance_sensor_dst_ = 0;
  maintenance_version_pending_ = false;
  maintenance_version_dst_ = 0;
  last_maint_page_tx_ms_ = 0;
  last_wifi_prov_tx_ms_ = 0;
  peer_count_ = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    peers_[i] = PeerRuntime{};
  }
  resetPollStorage();
  fleet_scan_active_ = false;
  fleet_scan_start_address_ = 1;
  fleet_scan_end_address_ = 32;
  fleet_scan_next_address_ = 1;
  fleet_scan_interval_ms_ = 1500;
  fleet_scan_next_ms_ = 0;
  fleet_scan_started_ms_ = 0;
  fleet_scan_last_tx_ms_ = 0;
  fleet_scan_sent_ = 0;
  wifi_prov_rx_ = WifiProvisionRxTransfer{};
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  udp_log_control_pending_ = false;
  udp_log_control_pending_enabled_ = false;
  udp_log_control_pending_host_ = IPAddress();
  udp_log_control_pending_port_ = 0;
  udp_log_control_pending_ttl_s_ = 0;
  udp_log_control_pending_src_ = 0;
  ota_pull_tx_ = OtaPullTxTransfer{};
  ota_pull_rx_ = OtaPullRxTransfer{};
  ota_pull_pending_ = false;
  ota_pull_pending_host_ = IPAddress();
  ota_pull_pending_port_ = 0;
  ota_pull_pending_sha256_ = "";
  ota_pull_pending_src_ = 0;
  ota_silence_until_ms_ = 0;
  ota_pull_active_ = false;
  ota_pull_start_ms_ = 0;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_pending_src_ = 0;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_controller_address_ = 0;
  fleet_prov_apply_key_ = "";
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  tick_watchdog_last_log_ms_ = millis();
  freeProvisioningStorage();
  LRS_LOGI(SYS,
           "event=prov_capacity max_devices=%u bytes=%u mode=lazy",
           static_cast<unsigned>(kMaxProvisioningDevices),
           static_cast<unsigned>(sizeof(ProvisioningDevice) * kMaxProvisioningDevices));
  for (size_t i = 0; i < kReplayTrackedSources; ++i) replay_sources_[i] = ReplaySourceState{};
  replay_table_evictions_ = 0;
  replay_table_stale_evictions_ = 0;
  replay_table_full_drops_ = 0;
  replay_table_peak_used_ = 0;
  LRS_LOGI(SYS,
           "event=memory_policy max_peers=%u replay_sources=%u max_prov_devices=%u",
           static_cast<unsigned>(kMaxPeers),
           static_cast<unsigned>(kReplayTrackedSources),
           static_cast<unsigned>(kMaxProvisioningDevices));

  prePopulateGatewayPeerCache();

  return true;
}

void NodeStateMachine::applyConfig(const Settings &cfg) {
  settings_ = &cfg;
  refreshRuntimeCfg(cfg);
  link_state_ = LinkState::Idle;
  wait_ack_since_ms_ = millis();
  last_heartbeat_ms_ = millis();
  tx_ack_pending_ = false;
  tx_state_sync_pending_ = runtime_.role_tx && runtime_.input_control_paired_lora_enabled;
  tx_command_pending_ = false;
  tx_pending_command_counter_ = 0;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  tx_command_retry_deadline_ms_ = 0;
  resetTxGroupState();
  rx_deferred_ack_pending_ = false;
  rx_deferred_ack_due_ms_ = 0;
  rx_deferred_ack_command_id_ = 0;
  rx_deferred_ack_dst_ = 0;
  rx_deferred_ack_relay_ = 0;
  rx_deferred_ack_input_ = 0;
  paired_input_slave_mode_ = false;
  rx_push_pending_ = false;
  rx_last_push_ms_ = 0;
  last_rx_control_ms_ = 0;
  maintenance_debug_pending_ = false;
  maintenance_debug_dst_ = 0;
  maintenance_sensor_pending_ = false;
  maintenance_sensor_dst_ = 0;
  maintenance_version_pending_ = false;
  maintenance_version_dst_ = 0;
  peer_count_ = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    peers_[i] = PeerRuntime{};
  }
  resetPollStorage();
  fleet_scan_active_ = false;
  fleet_scan_next_ms_ = 0;
  fleet_scan_started_ms_ = 0;
  fleet_scan_last_tx_ms_ = 0;
  fleet_scan_sent_ = 0;
  wifi_prov_rx_ = WifiProvisionRxTransfer{};
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  udp_log_control_pending_ = false;
  udp_log_control_pending_enabled_ = false;
  udp_log_control_pending_host_ = IPAddress();
  udp_log_control_pending_port_ = 0;
  udp_log_control_pending_ttl_s_ = 0;
  udp_log_control_pending_src_ = 0;
  ota_pull_tx_ = OtaPullTxTransfer{};
  ota_pull_rx_ = OtaPullRxTransfer{};
  ota_pull_pending_ = false;
  ota_pull_pending_host_ = IPAddress();
  ota_pull_pending_port_ = 0;
  ota_pull_pending_sha256_ = "";
  ota_pull_pending_src_ = 0;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_pending_src_ = 0;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_controller_address_ = 0;
  fleet_prov_apply_key_ = "";
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  tick_watchdog_last_log_ms_ = millis();
  freeProvisioningStorage();
  for (size_t i = 0; i < kReplayTrackedSources; ++i) replay_sources_[i] = ReplaySourceState{};
  replay_table_evictions_ = 0;
  replay_table_stale_evictions_ = 0;
  replay_table_full_drops_ = 0;
  replay_table_peak_used_ = 0;
  prePopulateGatewayPeerCache();
}

void NodeStateMachine::refreshRuntimeCfg(const Settings &cfg) {
  runtime_.role_tx = cfg.role_tx;
  runtime_.local_address = cfg.local_address;
  runtime_.remote_address = cfg.remote_address;
  runtime_.heartbeat_ms = cfg.heartbeat_ms;
  runtime_.heartbeat_enabled = cfg.heartbeat_enabled;
  runtime_.ack_timeout_ms = cfg.ack_timeout_ms;
  runtime_.mqtt_remote_retry_timeout_ms = cfg.mqtt_remote_retry_timeout_ms;
  runtime_.tx_mqtt_remote_polling_enabled = cfg.tx_mqtt_remote_polling_enabled;
  runtime_.tx_mqtt_remote_default_poll_interval_ms = cfg.tx_mqtt_remote_default_poll_interval_ms;
  runtime_.maintenance_debug_telemetry_enabled = cfg.maintenance_debug_telemetry_enabled;
  runtime_.rx_push_on_change_enabled = cfg.rx_push_on_change_enabled;
  runtime_.rx_push_min_interval_ms = cfg.rx_push_min_interval_ms;
  runtime_.input_control_paired_lora_enabled = cfg.input_control_paired_lora_enabled;
  runtime_.mqtt_control_enabled = cfg.mqtt_control_enabled;
  runtime_.rx_failsafe_mode = parseRxFailsafeMode(String(cfg.rx_failsafe_mode.c_str()));
  runtime_.tx_command_retry_timeout_ms = cfg.tx_command_retry_timeout_ms;
  runtime_.rx_failsafe_timeout_ms = cfg.rx_failsafe_timeout_ms;
  if (runtime_.tx_command_retry_timeout_ms < kMinRetryTimeoutMs) runtime_.tx_command_retry_timeout_ms = kMinRetryTimeoutMs;
  if (runtime_.tx_command_retry_timeout_ms > kMaxRetryTimeoutMs) runtime_.tx_command_retry_timeout_ms = kMaxRetryTimeoutMs;
  if (runtime_.rx_failsafe_timeout_ms < kMinRxFailsafeTimeoutMs) runtime_.rx_failsafe_timeout_ms = kMinRxFailsafeTimeoutMs;
  if (runtime_.rx_failsafe_timeout_ms > kMaxRxFailsafeTimeoutMs) runtime_.rx_failsafe_timeout_ms = kMaxRxFailsafeTimeoutMs;
  if (!runtime_.tx_mqtt_remote_polling_enabled) {
    freePollStorage();
  }
}

bool NodeStateMachine::ensureProvisioningStorage() {
  if (prov_devices_ != nullptr && prov_device_capacity_ >= kMaxProvisioningDevices) {
    return true;
  }
  freeProvisioningStorage();
  prov_devices_ = new (std::nothrow) ProvisioningDevice[kMaxProvisioningDevices];
  if (prov_devices_ == nullptr) {
    prov_device_capacity_ = 0;
    prov_device_count_ = 0;
    lrslog::event("prov_storage_oom", 0, static_cast<uint32_t>(kMaxProvisioningDevices & 0xFFFFU),
                  static_cast<uint8_t>(sizeof(ProvisioningDevice) & 0xFFU));
    return false;
  }
  prov_device_capacity_ = kMaxProvisioningDevices;
  prov_device_count_ = 0;
  return true;
}

void NodeStateMachine::resetProvisioningStorage() {
  prov_device_count_ = 0;
  if (prov_devices_ == nullptr) return;
  for (size_t i = 0; i < prov_device_capacity_; ++i) prov_devices_[i] = ProvisioningDevice{};
}

void NodeStateMachine::freeProvisioningStorage() {
  if (prov_devices_ != nullptr) {
    delete[] prov_devices_;
    prov_devices_ = nullptr;
  }
  prov_device_capacity_ = 0;
  prov_device_count_ = 0;
}

NodeStateMachine::PollRuntime *NodeStateMachine::pollStateForIndex(size_t index) {
  if (poll_states_ == nullptr || index >= poll_state_capacity_) return nullptr;
  return &poll_states_[index];
}

const NodeStateMachine::PollRuntime *NodeStateMachine::pollStateForIndex(size_t index) const {
  if (poll_states_ == nullptr || index >= poll_state_capacity_) return nullptr;
  return &poll_states_[index];
}

bool NodeStateMachine::ensurePollStorage() {
  if (poll_states_ != nullptr && poll_state_capacity_ >= kMaxPeers) return true;
  freePollStorage();
  poll_states_ = new (std::nothrow) PollRuntime[kMaxPeers];
  if (poll_states_ == nullptr) {
    poll_state_capacity_ = 0;
    lrslog::event("poll_storage_oom", 0, static_cast<uint32_t>(kMaxPeers & 0xFFFFU),
                  static_cast<uint8_t>(sizeof(PollRuntime) & 0xFFU));
    return false;
  }
  poll_state_capacity_ = kMaxPeers;
  resetPollStorage();
  return true;
}

void NodeStateMachine::resetPollStorage() {
  if (poll_states_ == nullptr) return;
  for (size_t i = 0; i < poll_state_capacity_; ++i) poll_states_[i] = PollRuntime{};
}

void NodeStateMachine::freePollStorage() {
  if (poll_states_ != nullptr) {
    delete[] poll_states_;
    poll_states_ = nullptr;
  }
  poll_state_capacity_ = 0;
}

void NodeStateMachine::tick(bool powerSaveActive) {
  power_save_active_ = powerSaveActive;
  const uint32_t now = millis();
  if (ota_pull_active_ && !ota_pull_pending_ && (now - ota_pull_start_ms_ > 60000UL)) {
    ota_pull_active_ = false;
    ota_pull_rx_ = OtaPullRxTransfer{};
    lrslog::event("ota_pull_control_timeout", 0, 0, 0);
  }
  if (static_cast<uint32_t>(now - tick_watchdog_last_log_ms_) >= kStateMachineLivenessLogIntervalMs) {
    LRS_LOGI(SYS,
             "event=sm_tick_alive role_tx=%u link_state=%u prov_active=%u prov_state=%u fleet_scan=%u heap_free=%lu max_free_block=%lu",
             runtime_.role_tx ? 1U : 0U,
             static_cast<unsigned>(link_state_),
             prov_.active ? 1U : 0U,
             static_cast<unsigned>(prov_.state),
             fleet_scan_active_ ? 1U : 0U,
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    tick_watchdog_last_log_ms_ = now;
  }

  resetRadioTxBudgetForTick();
  tickReceive();
  tickPendingMaintenancePages();

  if (runtime_.role_tx) {
    tickTransmitter();
  } else {
    tickReceiver();
  }
  tickProvisioningTarget(now);

  tickLed(powerSaveActive);
  finishRadioTxBudgetForTick();
}

LinkState NodeStateMachine::linkState() const { return link_state_; }
uint8_t NodeStateMachine::relayState() const { return relay_state_; }
uint8_t NodeStateMachine::relayFeedbackState() const { return digitalRead(kRelayPin) ? 1 : 0; }
uint8_t NodeStateMachine::inputState() const { return input_state_; }
uint8_t NodeStateMachine::localDryContactState() const { return digitalRead(kInputPin) ? 1 : 0; }
int NodeStateMachine::lastPacketRssi() const { return last_packet_rssi_; }
uint32_t NodeStateMachine::lastPacketMs() const { return last_packet_ms_; }
uint32_t NodeStateMachine::lastTxMs() const { return last_tx_ms_; }
void NodeStateMachine::setLocalSensors(const SensorRegistry &registry, uint16_t tankCurrentCentiMa, uint16_t tankVoltageMv) {
  local_sensors_ = registry;
  local_tank_current_centi_ma_ = tankCurrentCentiMa;
  local_tank_voltage_mv_ = tankVoltageMv;
  SensorReading dc{};
  dc.kind = SensorKind::DryContact;
  dc.state = SensorState::Ok;
  dc.instance = 0;
  dc.value = input_state_;
  dc.scale = 0;
  local_sensors_.upsert(dc);
}

const SensorRegistry &NodeStateMachine::localSensors() const {
  return local_sensors_;
}
uint16_t NodeStateMachine::localTankCurrentCentiMa() const {
  return local_tank_current_centi_ma_;
}
uint16_t NodeStateMachine::localTankVoltageMv() const {
  return local_tank_voltage_mv_;
}
uint8_t NodeStateMachine::localTempCodeToSend() const {
  SensorReading r{};
  if (local_sensors_.find(SensorKind::TemperatureC, 0, r)) {
    if (r.state == SensorState::Ok) {
      int32_t val = r.value;
      if (r.scale == 1) {
        val /= 10;
      }
      if (val >= -128 && val <= 127) {
        return static_cast<uint8_t>(static_cast<int8_t>(val));
      }
    }
  }
  return 0xFFU;
}
void NodeStateMachine::setMqttConnected(bool connected) { mqtt_connected_ = connected; }
bool NodeStateMachine::sharedUnixTimeValid() const { return shared_time_valid_; }
uint32_t NodeStateMachine::sharedUnixTime() const { return currentUnixTimeS(millis()); }
RxControlSource NodeStateMachine::lastRxControlSource() const { return last_rx_control_source_; }

size_t NodeStateMachine::peerCount() const { return peer_count_; }

bool NodeStateMachine::peerByIndex(size_t index, PeerStatusSnapshot &out) const {
  if (index >= peer_count_) return false;
  const PeerRuntime &node = peers_[index];
  if (!node.in_use) return false;
  out.address = node.address;
  out.relay_state = node.relay_state;
  out.input_state = node.input_state;
  out.input_state_known = node.input_state_known;
  out.sensors = node.sensors;
  out.uplink_rssi = node.uplink_rssi;
  out.downlink_rssi_valid = node.downlink_rssi_valid;
  out.downlink_rssi = node.downlink_rssi;
  out.last_seen_ms = node.last_seen_ms;
  out.last_cmd_counter = node.last_cmd_counter;
  out.ack_state = node.ack_state;
  out.wifi_state_known = node.wifi_state_known;
  out.wifi_enabled = node.wifi_enabled;
  out.wifi_connected_known = node.wifi_connected_known;
  out.wifi_connected = node.wifi_connected;
  memcpy(out.ip, node.ip, sizeof(out.ip));
  out.mqtt_state_known = node.mqtt_state_known;
  out.mqtt_enabled = node.mqtt_enabled;
  out.mqtt_connected = node.mqtt_connected;
  out.chip_id = node.chip_id;
  out.fw_major = node.fw_major;
  out.fw_minor = node.fw_minor;
  out.fw_patch = node.fw_patch;
  out.fw_build = node.fw_build;
  out.uptime_ms = node.uptime_ms;
  out.maintenance_debug_known = node.maintenance_debug_known;
  out.heap_free = node.heap_free;
  out.heap_max_block = node.heap_max_block;
  out.heap_frag_pct = node.heap_frag_pct;
  out.relay_feedback = node.relay_feedback;
  out.input_feedback = node.input_feedback;
  out.debug_uptime_ms = node.debug_uptime_ms;
  out.wifi_last_confirm_ms = node.wifi_last_confirm_ms;
  out.power_save_listen_only = node.power_save_listen_only;
  out.power_save_active = node.power_save_active;
  out.poll_interval_ms = node.poll_interval_ms;
  const PollRuntime *poll = pollStateForIndex(index);
  out.last_poll_tx_ms = poll ? poll->last_poll_tx_ms : 0U;
  out.poll_pending = poll ? poll->poll_pending : false;
  return true;
}

bool NodeStateMachine::peerByAddress(uint8_t address, PeerStatusSnapshot &out) const {
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return peerByIndex(i, out);
    }
  }
  return false;
}

bool NodeStateMachine::isConfiguredOperationalPeer(uint8_t address) const {
  if (settings_ == nullptr) return false;
  if (!runtime_.role_tx || settings_->mode != "paired") return false;
  if (address < 1 || address > 12) return false;

  uint8_t targets[Settings::kAddressListCap]{};
  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
    true,
    runtime_.local_address,
    settings_->known_peer_count,
    settings_->known_peer_addresses,
    settings_->paired_target_count,
    settings_->paired_target_addresses,
    settings_->remote_address,
    targets,
    Settings::kAddressListCap
  );

  for (uint8_t i = 0; i < targetCount; ++i) {
    if (targets[i] == address) {
      return true;
    }
  }
  return false;
}

void NodeStateMachine::mqttSetLocalRelay(uint8_t relayState) {
  if ((runtime_.role_tx && runtime_.input_control_paired_lora_enabled) || (!runtime_.role_tx && paired_input_slave_mode_)) {
    lrslog::event("relay_local_mqtt_blocked", 0, last_counter_, relayState ? 1 : 0);
    return;
  }
  relay_state_ = relayState ? 1 : 0;
  digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
  {
    lrslog::event("mqtt_local_relay", 0, last_counter_, relay_state_);
  }
}

void NodeStateMachine::sendTxState(MessageType type, uint8_t relayState, uint8_t inputState, const char *logEvent, bool resetRetryWindow) {
  const uint32_t now = millis();
  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(now);
  yield();  // Feed ESP8266 watchdog before retry/start-sync LoRa sends during startup loops.
  if (!radio_->send(type, relayState, inputState, txFlags(), last_counter_, runtime_.local_address, runtime_.remote_address, localTempCodeToSend(),
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    link_state_ = LinkState::Idle;
    tx_command_pending_ = false;
    tx_pending_command_counter_ = 0;
    tx_retry_step_ = 0;
    tx_next_retry_ms_ = 0;
    tx_command_retry_deadline_ms_ = 0;
    return;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  wait_ack_since_ms_ = now;
  link_state_ = LinkState::WaitAck;

  tx_command_pending_ = true;
  tx_pending_command_counter_ = last_counter_;
  if (resetRetryWindow || tx_command_retry_deadline_ms_ == 0) {
    tx_command_retry_deadline_ms_ = now + runtime_.tx_command_retry_timeout_ms;
  }
  tx_pending_relay_state_ = relayState ? 1 : 0;
  tx_pending_input_state_ = inputState ? 1 : 0;
  const uint8_t idx = tx_retry_step_ < (sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0]))
                          ? tx_retry_step_
                          : (sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0])) - 1;
  tx_next_retry_ms_ = now + jitteredDelayMs(kAckRetryScheduleMs[idx], kAckRetryJitterPct);
  if (tx_retry_step_ < ((sizeof(kAckRetryScheduleMs) / sizeof(kAckRetryScheduleMs[0])) - 1)) {
    tx_retry_step_++;
  }

  if (logEvent != nullptr) {
    lrslog::event(logEvent, 0, last_counter_, relayState ? 1 : 0);
  }
  yield();  // LoRa send path yields too, but yield again after scheduling/logging to avoid tight retry loops.
}

bool NodeStateMachine::isPairedTargetAddress(uint8_t addr) const {
  if (addr == 0 || addr == 255 || settings_ == nullptr) return false;

  uint8_t targets[Settings::kAddressListCap]{};
  const bool isPairedMode = (settings_->mode == "paired");

  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
      isPairedMode,
      runtime_.local_address,
      settings_->known_peer_count,
      settings_->known_peer_addresses,
      settings_->paired_target_count,
      settings_->paired_target_addresses,
      settings_->remote_address,
      targets,
      Settings::kAddressListCap
  );

  for (uint8_t i = 0; i < targetCount; ++i) {
    if (targets[i] == addr) return true;
  }
  return false;
}

bool NodeStateMachine::buildTxGroupTargets() {
  tx_group_target_count_ = 0;
  memset(tx_group_targets, 0, sizeof(tx_group_targets));
  tx_group_expected_bitmap_ = 0;
  tx_group_acked_bitmap_ = 0;
  tx_group_retry_bitmap_ = 0;

  if (settings_ == nullptr) return false;

  const bool isPairedMode = (settings_->mode == "paired");

  tx_group_target_count_ = runtime_utils::resolveGatewayTargets(
      isPairedMode,
      runtime_.local_address,
      settings_->known_peer_count,
      settings_->known_peer_addresses,
      settings_->paired_target_count,
      settings_->paired_target_addresses,
      settings_->remote_address,
      tx_group_targets,
      Settings::kAddressListCap
  );

  // Re-establish expectation bitmap based on count
  for (uint8_t i = 0; i < tx_group_target_count_; ++i) {
    tx_group_expected_bitmap_ |= (1UL << i);
  }

  return tx_group_target_count_ > 0;
}

uint8_t NodeStateMachine::txGroupTargetIndexForAddress(uint8_t addr) const {
  for (uint8_t i = 0; i < tx_group_target_count_; ++i) {
    if (tx_group_targets[i] == addr) return i;
  }
  return 0xFF;
}

uint32_t NodeStateMachine::txGroupMissingBitmap() const { return tx_group_expected_bitmap_ & ~tx_group_acked_bitmap_; }

bool NodeStateMachine::txGroupHasMissingTargets() const { return txGroupMissingBitmap() != 0; }

bool NodeStateMachine::isGroupActive() const {
  return runtime_.input_control_paired_lora_enabled &&
         (tx_group_phase_ == PairedGroupPhase::AwaitInitialAcks ||
          tx_group_phase_ == PairedGroupPhase::PollMissingSequential);
}

void NodeStateMachine::resetTxGroupState() {
  tx_command_pending_ = false;
  tx_pending_command_counter_ = 0;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  tx_command_retry_deadline_ms_ = 0;
  tx_group_phase_ = PairedGroupPhase::Idle;
  tx_group_command_id_ = 0;
  tx_group_expected_bitmap_ = 0;
  tx_group_acked_bitmap_ = 0;
  tx_group_retry_bitmap_ = 0;
  tx_group_target_count_ = 0;
  memset(tx_group_targets, 0, sizeof(tx_group_targets));
  tx_group_window_deadline_ms_ = 0;
  tx_group_initial_send_cursor_ = 0;
  tx_group_retry_cursor_ = 0;
  tx_group_retry_addr_ = 0;
  tx_group_retry_deadline_ms_ = 0;
  tx_group_desired_relay_state_ = 0;
  tx_group_desired_input_state_ = 0;
}

bool NodeStateMachine::sendTxGroupChangeToAddress(uint8_t addr, const char *eventName, const char *phase) {
  if (!radioTxBudgetAvailable() || radio_ == nullptr) return false;
  const uint32_t now = millis();
  last_counter_++;
  if (!radio_->send(MessageType::Change, tx_group_desired_relay_state_, tx_group_desired_input_state_, txFlags(), last_counter_,
                    runtime_.local_address, addr, localTempCodeToSend(), 0, 0xFF, 0xFFFF, tx_group_command_id_)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  wait_ack_since_ms_ = now;
  link_state_ = LinkState::WaitAck;
  tx_command_pending_ = true;
  tx_pending_command_counter_ = tx_group_command_id_;
  tx_command_retry_deadline_ms_ = now + runtime_.tx_command_retry_timeout_ms;
  tx_pending_relay_state_ = tx_group_desired_relay_state_;
  tx_pending_input_state_ = tx_group_desired_input_state_;
  if (eventName != nullptr) {
    lrslog::event(eventName, 0, tx_group_command_id_, addr);
  }
  LRS_LOGI(LORA,
           "event=%s phase=%s command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=%u",
           eventName ? eventName : "tx_alln_send",
           phase ? phase : "unknown",
           static_cast<unsigned long>(tx_group_command_id_),
           static_cast<unsigned>(tx_group_target_count_),
           static_cast<unsigned long>(tx_group_expected_bitmap_),
           static_cast<unsigned long>(tx_group_acked_bitmap_),
           static_cast<unsigned long>(txGroupMissingBitmap()),
           static_cast<unsigned>(addr));
  return true;
}

bool NodeStateMachine::sendTxGroupPollToAddress(uint8_t addr, const char *eventName, const char *phase) {
  if (!radioTxBudgetAvailable() || radio_ == nullptr) return false;
  const uint32_t now = millis();
  last_counter_++;
  if (!radio_->send(MessageType::PollRequest, 0, input_state_, txFlags() | kFlagGroupPollCorrelation, last_counter_,
                    runtime_.local_address, addr, localTempCodeToSend(), 0, 0xFF, 0xFFFF, tx_group_command_id_)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  // Don't modify link_state_ or tx_command_pending_ because this is a non-actuating poll.
  tx_group_retry_addr_ = addr;
  tx_group_retry_deadline_ms_ = now + kAckRetryOneShotTimeoutMs;
  if (eventName != nullptr) {
    lrslog::event(eventName, 0, tx_group_command_id_, addr);
  }
  LRS_LOGI(LORA,
           "event=%s phase=%s command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=%u",
           eventName ? eventName : "tx_alln_poll_send",
           phase ? phase : "unknown",
           static_cast<unsigned long>(tx_group_command_id_),
           static_cast<unsigned>(tx_group_target_count_),
           static_cast<unsigned long>(tx_group_expected_bitmap_),
           static_cast<unsigned long>(tx_group_acked_bitmap_),
           static_cast<unsigned long>(txGroupMissingBitmap()),
           static_cast<unsigned>(addr));
  return true;
}

void NodeStateMachine::startTxGroupCommand(uint8_t relayState, uint8_t inputState) {
  if (!buildTxGroupTargets()) {
    resetTxGroupState();
    return;
  }
  tx_group_desired_relay_state_ = relayState ? 1 : 0;
  tx_group_desired_input_state_ = inputState ? 1 : 0;
  tx_group_command_id_ =
      static_cast<uint32_t>((millis() ^ last_counter_ ^ (static_cast<uint32_t>(runtime_.local_address) << 24) ^ random(1, 0x7FFFFFFF)) &
                            0xFFFFFFFFUL);
  if (tx_group_command_id_ == 0) tx_group_command_id_ = 1;
  tx_group_phase_ = PairedGroupPhase::AwaitInitialAcks;
  tx_group_initial_send_cursor_ = 0;
  tx_group_window_deadline_ms_ = 0;
  tx_group_retry_cursor_ = 0;
  tx_group_retry_addr_ = 0;
  tx_group_retry_deadline_ms_ = 0;
  tx_command_pending_ = true;
  tx_pending_command_counter_ = tx_group_command_id_;
  tx_command_retry_deadline_ms_ = millis() + runtime_.tx_command_retry_timeout_ms;
  link_state_ = LinkState::WaitAck;
  lrslog::event("tx_alln_start", 0, tx_group_command_id_, tx_group_target_count_);
  LRS_LOGI(LORA,
           "event=tx_alln_start phase=initial_window command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=0",
           static_cast<unsigned long>(tx_group_command_id_),
           static_cast<unsigned>(tx_group_target_count_),
           static_cast<unsigned long>(tx_group_expected_bitmap_),
           static_cast<unsigned long>(tx_group_acked_bitmap_),
           static_cast<unsigned long>(txGroupMissingBitmap()));
}

void NodeStateMachine::finishTxGroupSuccess() {
  const uint32_t now = millis();
  const uint32_t commandId = tx_group_command_id_;
  tx_ack_pending_ = true;
  tx_ack_apply_ms_ = now + kTxRelayEchoDelayMs;
  tx_ack_relay_state_ = tx_group_desired_relay_state_;
  tx_command_pending_ = false;
  tx_pending_command_counter_ = 0;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  tx_command_retry_deadline_ms_ = 0;
  link_state_ = LinkState::Idle;
  lrslog::event("tx_alln_complete_ok", 0, commandId, tx_group_desired_relay_state_);
  LRS_LOGI(LORA,
           "event=tx_alln_complete_ok phase=complete command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=0",
           static_cast<unsigned long>(commandId),
           static_cast<unsigned>(tx_group_target_count_),
           static_cast<unsigned long>(tx_group_expected_bitmap_),
           static_cast<unsigned long>(tx_group_acked_bitmap_),
           static_cast<unsigned long>(txGroupMissingBitmap()));
  resetTxGroupState();
}

void NodeStateMachine::finishTxGroupPartial() {
  const uint32_t commandId = tx_group_command_id_;
  const uint32_t missing = txGroupMissingBitmap();
  for (uint8_t i = 0; i < tx_group_target_count_; ++i) {
    if ((missing & (1UL << i)) == 0) continue;
    updatePeerAckStatus(tx_group_targets[i], tx_group_desired_relay_state_, tx_group_desired_input_state_, PeerAckState::Timeout);
  }
  tx_command_pending_ = false;
  tx_pending_command_counter_ = 0;
  tx_retry_step_ = 0;
  tx_next_retry_ms_ = 0;
  tx_command_retry_deadline_ms_ = 0;
  link_state_ = LinkState::Timeout;
  lrslog::event("tx_alln_complete_partial", 0, commandId, static_cast<uint8_t>(missing & 0xFFU));
  LRS_LOGI(LORA,
           "event=tx_alln_complete_partial phase=complete command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=0",
           static_cast<unsigned long>(commandId),
           static_cast<unsigned>(tx_group_target_count_),
           static_cast<unsigned long>(tx_group_expected_bitmap_),
           static_cast<unsigned long>(tx_group_acked_bitmap_),
           static_cast<unsigned long>(missing));
  resetTxGroupState();
}

void NodeStateMachine::updatePeerAckStatus(uint8_t src, uint8_t relayState, uint8_t inputState, PeerAckState ackState, int rssi) {
  if (runtime_.role_tx && settings_ != nullptr && settings_->mode == "paired") {
    if (!isConfiguredOperationalPeer(src)) return;
  }
  PeerRuntime *node = findOrCreatePeer(src);
  if (node == nullptr) return;
  (void)inputState;
  if (ackState == PeerAckState::Ok) {
    node->relay_state = relayState ? 1 : 0;
  }
  node->last_seen_ms = millis();
  node->last_cmd_counter = tx_group_command_id_;
  node->ack_state = ackState;
  node->uptime_ms = 0;
  node->debug_uptime_ms = 0;
  if (rssi != -127) {
    node->uplink_rssi = rssi;
  }
}

uint8_t NodeStateMachine::pairedAckRankForLocalAddress() const {
  if (settings_ != nullptr) {
    uint8_t count = settings_->paired_target_count;
    if (count > Settings::kAddressListCap) count = Settings::kAddressListCap;
    for (uint8_t i = 0; i < count; ++i) {
      if (settings_->paired_target_addresses[i] == runtime_.local_address) return i;
    }
  }
  if (runtime_.local_address > 0) return static_cast<uint8_t>(runtime_.local_address - 1);
  return 0;
}

void NodeStateMachine::scheduleDeferredAck(uint8_t dst, uint8_t relayState, uint8_t inputState, uint32_t commandId) {
  const uint8_t rank = pairedAckRankForLocalAddress();
  const uint32_t span = kAckSlotJitterMaxMs + 1U;
  const uint32_t seed = fnv1a32(reinterpret_cast<const uint8_t *>(&commandId), sizeof(commandId)) ^
                        static_cast<uint32_t>(runtime_.local_address);
  const uint32_t jitter = (span == 0) ? 0U : (seed % span);
  const uint32_t delayMs = static_cast<uint32_t>(rank) * kAckSlotBaseMs + jitter;
  rx_deferred_ack_pending_ = true;
  rx_deferred_ack_due_ms_ = millis() + delayMs;
  rx_deferred_ack_command_id_ = commandId;
  rx_deferred_ack_dst_ = dst;
  rx_deferred_ack_relay_ = relayState ? 1 : 0;
  rx_deferred_ack_input_ = inputState ? 1 : 0;
  lrslog::event("rx_alln_ack_sched", 0, commandId, rank);
  LRS_LOGI(LORA,
           "event=rx_alln_ack_sched phase=initial_window command_id=%lu target_count=0 expected_bitmap=0x00000000 acked_bitmap=0x00000000 missing_bitmap=0x00000000 addr=%u",
           static_cast<unsigned long>(commandId), static_cast<unsigned>(dst));
}

void NodeStateMachine::tickDeferredAck(uint32_t now) {
  if (!rx_deferred_ack_pending_) return;
  if (static_cast<int32_t>(now - rx_deferred_ack_due_ms_) < 0) return;
  if (!radioTxBudgetAvailable()) return;
  last_counter_++;
  if (!radio_->send(MessageType::Ack, rx_deferred_ack_relay_, rx_deferred_ack_input_, txFlags(), last_counter_, runtime_.local_address,
                    rx_deferred_ack_dst_, localTempCodeToSend(), 0, 0xFF, 0xFFFF, rx_deferred_ack_command_id_)) {
    return;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  lrslog::event("rx_alln_ack_tx", 0, rx_deferred_ack_command_id_, rx_deferred_ack_dst_);
  LRS_LOGI(LORA,
           "event=rx_alln_ack_tx phase=initial_window command_id=%lu target_count=0 expected_bitmap=0x00000000 acked_bitmap=0x00000000 missing_bitmap=0x00000000 addr=%u",
           static_cast<unsigned long>(rx_deferred_ack_command_id_),
           static_cast<unsigned>(rx_deferred_ack_dst_));
  rx_deferred_ack_pending_ = false;
}

void NodeStateMachine::tickTxGroupCommand(uint32_t now) {
  if (tx_group_phase_ == PairedGroupPhase::Idle || tx_group_phase_ == PairedGroupPhase::Complete) return;
  if (tx_command_retry_deadline_ms_ != 0 && static_cast<int32_t>(now - tx_command_retry_deadline_ms_) >= 0) {
    finishTxGroupPartial();
    return;
  }

  if (tx_group_phase_ == PairedGroupPhase::AwaitInitialAcks) {
    if (tx_group_initial_send_cursor_ < tx_group_target_count_) {
      if (tx_group_initial_send_cursor_ == 0) {
        if (!sendTxGroupChangeToAddress(255, "tx_alln_bcast", "initial_window")) {
          return;
        }
      }
      tx_group_initial_send_cursor_ = tx_group_target_count_;
      return;
    }

    if (tx_group_window_deadline_ms_ == 0) {
      const uint32_t windowMs =
          static_cast<uint32_t>(tx_group_target_count_) * (kAckSlotBaseMs + kAckSlotJitterMaxMs) + kAckWindowGuardMs;
      tx_group_window_deadline_ms_ = now + windowMs;
    }

    if (!txGroupHasMissingTargets()) {
      finishTxGroupSuccess();
      return;
    }

    if (static_cast<int32_t>(now - tx_group_window_deadline_ms_) < 0) return;

    tx_group_retry_bitmap_ = txGroupMissingBitmap();
    lrslog::event("tx_alln_window_close", 0, tx_group_command_id_, static_cast<uint8_t>(tx_group_retry_bitmap_ & 0xFFU));
    LRS_LOGI(LORA,
             "event=tx_alln_window_close phase=initial_window command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=0",
             static_cast<unsigned long>(tx_group_command_id_),
             static_cast<unsigned>(tx_group_target_count_),
             static_cast<unsigned long>(tx_group_expected_bitmap_),
             static_cast<unsigned long>(tx_group_acked_bitmap_),
             static_cast<unsigned long>(txGroupMissingBitmap()));
    tx_group_phase_ = PairedGroupPhase::PollMissingSequential;
    tx_group_retry_cursor_ = 0;
    tx_group_retry_addr_ = 0;
    tx_group_retry_deadline_ms_ = now;
    return;
  }

  if (tx_group_phase_ != PairedGroupPhase::PollMissingSequential) return;

  if (!txGroupHasMissingTargets()) {
    finishTxGroupSuccess();
    return;
  }

  if (tx_group_retry_addr_ != 0) {
    const uint8_t idx = txGroupTargetIndexForAddress(tx_group_retry_addr_);
    if (idx != 0xFF && (tx_group_acked_bitmap_ & (1UL << idx)) != 0U) {
      tx_group_retry_addr_ = 0;
      tx_group_retry_deadline_ms_ = now + kAckRetryInterNodeGapMs;
      return;
    }
    if (static_cast<int32_t>(now - tx_group_retry_deadline_ms_) < 0) return;
    lrslog::event("tx_alln_poll_timeout", 0, tx_group_command_id_, tx_group_retry_addr_);
    LRS_LOGI(LORA,
             "event=tx_alln_poll_timeout phase=poll_once command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=%u",
             static_cast<unsigned long>(tx_group_command_id_),
             static_cast<unsigned>(tx_group_target_count_),
             static_cast<unsigned long>(tx_group_expected_bitmap_),
             static_cast<unsigned long>(tx_group_acked_bitmap_),
             static_cast<unsigned long>(txGroupMissingBitmap()),
             static_cast<unsigned>(tx_group_retry_addr_));
    tx_group_retry_addr_ = 0;
    tx_group_retry_deadline_ms_ = now + kAckRetryInterNodeGapMs;
    return;
  }

  if (static_cast<int32_t>(now - tx_group_retry_deadline_ms_) < 0) return;

  while (tx_group_retry_cursor_ < tx_group_target_count_) {
    const uint8_t idx = tx_group_retry_cursor_++;
    const uint32_t bit = (1UL << idx);
    if ((tx_group_retry_bitmap_ & bit) == 0U) continue;
    if ((tx_group_acked_bitmap_ & bit) != 0U) continue;
    const uint8_t addr = tx_group_targets[idx];
    if (!sendTxGroupPollToAddress(addr, "tx_alln_poll_send", "poll_once")) return;
    // sendTxGroupPollToAddress sets tx_group_retry_addr_ and tx_group_retry_deadline_ms_
    return;
  }

  if (!txGroupHasMissingTargets()) {
    finishTxGroupSuccess();
  } else {
    finishTxGroupPartial();
  }
}

bool NodeStateMachine::sendPeerMqttCommand(uint8_t dstAddress, uint8_t relayState, uint32_t *sentCounter) {
  if (!radioTxBudgetAvailable()) return false;
  const uint8_t targetRelay = relayState ? 1 : 0;
  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::Mqtt, targetRelay, input_state_, txFlags(), last_counter_, runtime_.local_address, dstAddress,
                    localTempCodeToSend(),
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  {
    lrslog::event("mqtt_remote_relay_tx", 0, last_counter_, targetRelay);
  }
  return true;
}

bool NodeStateMachine::mqttSendPeerRelay(uint8_t dstAddress, uint8_t relayState) {
  if (!runtime_.role_tx) return false;
  if (runtime_.input_control_paired_lora_enabled) {
    lrslog::event("mqtt_remote_relay_blocked", 0, dstAddress, relayState ? 1 : 0);
    return false;
  }
  if (dstAddress == 0 || dstAddress == 255) return false;

  uint32_t sentCounter = 0;
  if (!sendPeerMqttCommand(dstAddress, relayState, &sentCounter)) {
    return false;
  }

  // Decouple send-path from managed peer slots: if cache is full, keep command send
  // working as transient fire-and-forget (no retry/poll/ack tracking for this peer).
  PeerRuntime *node = findOrCreatePeer(dstAddress);
  if (node == nullptr) {
    lrslog::event("mqtt_remote_transient", 0, sentCounter, relayState ? 1 : 0);
    return true;
  }

  const uint32_t now = millis();
  if (runtime_.tx_mqtt_remote_polling_enabled && node->poll_interval_ms == 0) {
    uint32_t interval = runtime_.tx_mqtt_remote_default_poll_interval_ms;
    if (interval < kMinRemotePollIntervalMs) interval = kDefaultRemotePollIntervalMs;
    if (interval > kMaxRemotePollIntervalMs) interval = kMaxRemotePollIntervalMs;
    node->poll_interval_ms = interval;
    if (ensurePollStorage()) {
      PollRuntime *poll = pollStateForIndex(static_cast<size_t>(node - peers_));
      if (poll != nullptr) {
        poll->next_poll_ms = now + interval;
      }
    }
  }
  node->pending = true;
  node->pending_relay = relayState ? 1 : 0;
  node->retry_step = 0;
  node->next_retry_ms = now + kMqttRetryScheduleMs[0];
  node->pending_counter = sentCounter;
  node->pending_deadline_ms = now + runtime_.mqtt_remote_retry_timeout_ms;
  node->ack_state = PeerAckState::Pending;
  node->last_cmd_counter = sentCounter;
  return true;
}

bool NodeStateMachine::mqttSetPeerPollIntervalMs(uint8_t dstAddress, uint32_t pollIntervalMs) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
  if (node == nullptr) return false;

  if (pollIntervalMs > 0 && pollIntervalMs < kMinRemotePollIntervalMs) pollIntervalMs = kMinRemotePollIntervalMs;
  if (pollIntervalMs > kMaxRemotePollIntervalMs) pollIntervalMs = kMaxRemotePollIntervalMs;
  if (pollIntervalMs > 0) {
    if (!ensurePollStorage()) return false;
  }
  node->poll_interval_ms = pollIntervalMs;
  PollRuntime *poll = pollStateForIndex(static_cast<size_t>(node - peers_));
  if (pollIntervalMs == 0) {
    if (poll != nullptr) {
      *poll = PollRuntime{};
    }
  } else if (poll != nullptr) {
    poll->next_poll_ms = millis() + 1000;
  }
  return true;
}

bool NodeStateMachine::mqttPollPeerNow(uint8_t dstAddress) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
  if (node == nullptr) return false;
  if (!ensurePollStorage()) return false;
  PollRuntime *poll = pollStateForIndex(static_cast<size_t>(node - peers_));
  if (poll == nullptr) return false;

  uint32_t sentCounter = 0;
  if (!sendPollRequest(dstAddress, &sentCounter)) return false;
  const uint32_t now = millis();
  const uint32_t pollResponseDeadlineMs = (runtime_.ack_timeout_ms >= 2000U) ? runtime_.ack_timeout_ms : 2000U;
  poll->poll_pending = true;
  poll->poll_counter = sentCounter;
  poll->poll_deadline_ms = now + pollResponseDeadlineMs;
  poll->last_poll_tx_ms = now;
  if (node->poll_interval_ms > 0) {
    poll->next_poll_ms = now + node->poll_interval_ms;
  }
  return true;
}

bool NodeStateMachine::mqttSetPeerWifi(uint8_t dstAddress, bool enabled) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;
  if (!radioTxBudgetAvailable()) return false;

  PeerRuntime *node = findOrCreatePeer(dstAddress);
  if (node == nullptr) return false;

  last_counter_++;
  const uint32_t sentCounter = last_counter_;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::WifiControl, kWifiControlOpSet, enabled ? 1 : 0,
                    txFlags(), sentCounter, runtime_.local_address, dstAddress,
                    localTempCodeToSend(), 0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  const uint32_t now = millis();
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  node->wifi_pending = true;
  node->wifi_pending_enabled = enabled;
  node->wifi_pending_counter = sentCounter;
  node->wifi_pending_deadline_ms = now + runtime_.mqtt_remote_retry_timeout_ms;
  node->wifi_state_known = false;
  lrslog::event(enabled ? "wifi_control_enable_tx" : "wifi_control_disable_tx", 0, sentCounter, dstAddress);
  return true;
}

bool NodeStateMachine::sendBroadcastWifiDisable() {
  if (!runtime_.role_tx) return false;
  if (!radioTxBudgetAvailable()) return false;
  last_counter_++;
  const uint32_t sentCounter = last_counter_;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::WifiControl, kWifiControlOpSet, 0, txFlags(),
                    sentCounter, runtime_.local_address, kWifiControlBroadcastAddress,
                    localTempCodeToSend(), 0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event("wifi_control_disable_broadcast_tx", 0, sentCounter, kWifiControlBroadcastAddress);
  return true;
}

bool NodeStateMachine::mqttSetPeerUdpLogControl(uint8_t dstAddress, bool enabled, IPAddress host, uint16_t port, uint32_t ttlS) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;
  if (enabled && (port == 0 || host == IPAddress())) return false;
  if (radio_ == nullptr) return false;
  if (!radioTxBudgetAvailable()) return false;

  uint8_t payload[12]{};
  payload[0] = kUdpLogControlOpSet;
  payload[1] = enabled ? 1U : 0U;
  payload[2] = static_cast<uint8_t>(port & 0xFFU);
  payload[3] = static_cast<uint8_t>((port >> 8) & 0xFFU);
  payload[4] = static_cast<uint8_t>(ttlS & 0xFFU);
  payload[5] = static_cast<uint8_t>((ttlS >> 8) & 0xFFU);
  payload[6] = static_cast<uint8_t>((ttlS >> 16) & 0xFFU);
  payload[7] = static_cast<uint8_t>((ttlS >> 24) & 0xFFU);
  payload[8] = host[0];
  payload[9] = host[1];
  payload[10] = host[2];
  payload[11] = host[3];

  last_counter_++;
  if (!radio_->sendRaw(MessageType::UdpLogControl, last_counter_, runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event(enabled ? "udp_log_control_enable_tx" : "udp_log_control_disable_tx", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::sendPeerOtaPullControl(uint8_t dstAddress, IPAddress host, uint16_t port,
                                              const char *sha256Hex) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;
  if (port == 0 || host == IPAddress()) return false;
  if (radio_ == nullptr) return false;
  if (ota_pull_tx_.active) return false;
  uint8_t sha256[32]{};
  if (!sha256HexToBytes(sha256Hex, sha256)) return false;

  uint8_t transferId = static_cast<uint8_t>((millis() ^ last_counter_ ^ dstAddress) & 0xFFU);
  if (transferId == 0) transferId = 1;

  ota_pull_tx_ = OtaPullTxTransfer{};
  ota_pull_tx_.active = true;
  ota_pull_tx_.dst = dstAddress;
  ota_pull_tx_.transfer_id = transferId;
  ota_pull_tx_.host = host;
  ota_pull_tx_.port = port;
  memcpy(ota_pull_tx_.sha256, sha256, sizeof(ota_pull_tx_.sha256));
  ota_pull_tx_.next_tx_ms = millis();
  lrslog::event("ota_pull_control_queued", 0, transferId, dstAddress);
  return true;
}

bool NodeStateMachine::sendQueuedOtaPullControlFrame() {
  if (!ota_pull_tx_.active || radio_ == nullptr) return false;
  if (!radioTxBudgetAvailable()) return false;

  uint8_t payload[12]{};
  if (ota_pull_tx_.frame_index == 0) {
    payload[0] = kOtaPullControlOpStart;
    payload[1] = ota_pull_tx_.transfer_id;
    payload[2] = static_cast<uint8_t>(ota_pull_tx_.port & 0xFFU);
    payload[3] = static_cast<uint8_t>((ota_pull_tx_.port >> 8) & 0xFFU);
    payload[4] = ota_pull_tx_.host[0];
    payload[5] = ota_pull_tx_.host[1];
    payload[6] = ota_pull_tx_.host[2];
    payload[7] = ota_pull_tx_.host[3];
    payload[8] = kOtaPullControlHashChunks;
  } else if (ota_pull_tx_.frame_index <= kOtaPullControlHashChunks) {
    const uint8_t idx = ota_pull_tx_.frame_index - 1;
    payload[0] = kOtaPullControlOpHash;
    payload[1] = ota_pull_tx_.transfer_id;
    payload[2] = idx;
    payload[3] = kOtaPullControlHashChunkBytes;
    memcpy(payload + 4, ota_pull_tx_.sha256 + (idx * kOtaPullControlHashChunkBytes),
           kOtaPullControlHashChunkBytes);
  } else {
    payload[0] = kOtaPullControlOpCommit;
    payload[1] = ota_pull_tx_.transfer_id;
    payload[2] = kOtaPullControlHashChunks;
  }

  last_counter_++;
  if (!radio_->sendRaw(MessageType::OtaPullControl, last_counter_,
                       runtime_.local_address, ota_pull_tx_.dst, payload)) {
    return false;
  }

  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event("ota_pull_control_tx", 0, last_counter_, ota_pull_tx_.dst);
  ota_pull_tx_.frame_index++;
  if (ota_pull_tx_.frame_index > kOtaPullControlHashChunks + 1U) {
    ota_pull_tx_ = OtaPullTxTransfer{};
  } else {
    ota_pull_tx_.next_tx_ms = millis() + kOtaPullControlFrameSpacingMs;
  }
  return true;
}

void NodeStateMachine::tickPendingOtaPullControl(uint32_t now) {
  if (!ota_pull_tx_.active) return;
  if (isGroupActive()) return;
  if (static_cast<int32_t>(now - ota_pull_tx_.next_tx_ms) < 0) return;
  if (!sendQueuedOtaPullControlFrame()) {
    ota_pull_tx_.next_tx_ms = now + kOtaPullControlFrameSpacingMs;
  }
}

bool NodeStateMachine::hasPendingWifiControl() const { return wifi_control_pending_; }

bool NodeStateMachine::consumePendingWifiControl(bool &enabled, uint8_t &src, uint32_t &commandCounter) {
  if (!wifi_control_pending_) return false;
  enabled = wifi_control_pending_enabled_;
  src = wifi_control_pending_src_;
  commandCounter = wifi_control_pending_counter_;
  wifi_control_pending_ = false;
  wifi_control_pending_src_ = 0;
  wifi_control_pending_counter_ = 0;
  return true;
}

bool NodeStateMachine::sendWifiControlStatus(uint8_t dstAddress, bool enabled, uint32_t commandCounter) {
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255) return false;
  if (!radioTxBudgetAvailable()) return false;
  last_counter_++;
  if (!radio_->send(MessageType::WifiControl, kWifiControlOpStatus, enabled ? 1 : 0,
                    txFlags(), last_counter_, runtime_.local_address, dstAddress,
                    localTempCodeToSend(), 0, 0xFF, 0xFFFF, commandCounter)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event(enabled ? "wifi_control_status_on_tx" : "wifi_control_status_off_tx", 0, commandCounter, dstAddress);
  return true;
}

bool NodeStateMachine::hasPendingUdpLogControl() const { return udp_log_control_pending_; }

bool NodeStateMachine::consumePendingUdpLogControl(bool &enabled, IPAddress &host, uint16_t &port, uint32_t &ttlS, uint8_t &src) {
  if (!udp_log_control_pending_) return false;
  enabled = udp_log_control_pending_enabled_;
  host = udp_log_control_pending_host_;
  port = udp_log_control_pending_port_;
  ttlS = udp_log_control_pending_ttl_s_;
  src = udp_log_control_pending_src_;
  udp_log_control_pending_ = false;
  udp_log_control_pending_enabled_ = false;
  udp_log_control_pending_host_ = IPAddress();
  udp_log_control_pending_port_ = 0;
  udp_log_control_pending_ttl_s_ = 0;
  udp_log_control_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::consumePendingOtaPull(IPAddress &host, uint16_t &port, String &sha256Hex,
                                             uint8_t &src) {
  if (!ota_pull_pending_) return false;
  host = ota_pull_pending_host_;
  port = ota_pull_pending_port_;
  sha256Hex = ota_pull_pending_sha256_;
  src = ota_pull_pending_src_;
  ota_pull_pending_ = false;
  ota_pull_pending_host_ = IPAddress();
  ota_pull_pending_port_ = 0;
  ota_pull_pending_sha256_ = "";
  ota_pull_pending_src_ = 0;
  return true;
}

void NodeStateMachine::removePeerAt(size_t idx) {
  if (idx >= peer_count_) return;
  for (size_t i = idx; i + 1 < peer_count_; ++i) {
    peers_[i] = peers_[i + 1];
    if (poll_states_ != nullptr && i + 1 < poll_state_capacity_) {
      poll_states_[i] = poll_states_[i + 1];
    }
  }
  if (peer_count_ > 0) {
    peer_count_--;
    peers_[peer_count_] = PeerRuntime{};
    if (poll_states_ != nullptr && peer_count_ < poll_state_capacity_) {
      poll_states_[peer_count_] = PollRuntime{};
    }
  }
}

bool NodeStateMachine::mqttForgetPeer(uint8_t dstAddress) {
  if (!runtime_.role_tx) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  size_t idx = kMaxPeers;
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == dstAddress) {
      idx = i;
      break;
    }
  }
  if (idx >= peer_count_) return false;
  removePeerAt(idx);
  {
    lrslog::event("mqtt_remote_forget", 0, 0, dstAddress);
  }
  return true;
}

uint32_t NodeStateMachine::resolveChipIdForAddress(uint8_t address) const {
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const ProvisionedAddressEntry &e = provisioned_addrs_[i];
    if (e.in_use && e.assigned_address == address) {
      return e.chip_id;
    }
  }
  return 0;
}

uint32_t NodeStateMachine::activePeerChipIdForAddress(uint8_t address) const {
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return peers_[i].chip_id;
    }
  }
  return 0;
}

bool NodeStateMachine::fleetScanStart(uint8_t startAddress, uint8_t endAddress, uint16_t intervalMs) {
  if (!runtime_.role_tx) return false;
  if (startAddress == 0 || startAddress == 255 || endAddress == 0 || endAddress == 255) return false;
  if (startAddress > endAddress) return false;
  if (intervalMs < 80U) intervalMs = 80U;
  if (intervalMs > 2000U) intervalMs = 2000U;

  fleet_scan_start_address_ = startAddress;
  fleet_scan_end_address_ = endAddress;
  fleet_scan_next_address_ = startAddress;
  fleet_scan_interval_ms_ = intervalMs;
  fleet_scan_started_ms_ = millis();
  fleet_scan_last_tx_ms_ = 0;
  fleet_scan_sent_ = 0;
  fleet_scan_next_ms_ = fleet_scan_started_ms_;
  fleet_scan_active_ = true;
  lrslog::event("fleet_scan_start", 0, startAddress, endAddress);
  return true;
}

void NodeStateMachine::fleetScanCancel() {
  if (!fleet_scan_active_) return;
  fleet_scan_active_ = false;
  fleet_scan_next_ms_ = 0;
  lrslog::event("fleet_scan_cancel", 0, fleet_scan_sent_, fleet_scan_next_address_);
}

bool NodeStateMachine::fleetScanSnapshot(FleetScanSnapshot &out) const {
  out.active = fleet_scan_active_;
  out.start_address = fleet_scan_start_address_;
  out.end_address = fleet_scan_end_address_;
  out.next_address = fleet_scan_next_address_;
  out.interval_ms = fleet_scan_interval_ms_;
  out.started_ms = fleet_scan_started_ms_;
  out.last_tx_ms = fleet_scan_last_tx_ms_;
  out.sent = fleet_scan_sent_;
  return true;
}

bool NodeStateMachine::sendFleetWifiProvision(const String &ssid, const String &password, uint8_t targetAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  if (fleetWifiProvisionCooldownRemainingMs() > 0) return false;
  if (targetAddress == 0) return false;
  if (ssid.length() == 0 || ssid.length() > 32) return false;
  if (password.length() > 64) return false;

  const size_t totalLen = static_cast<size_t>(ssid.length() + password.length());
  if (totalLen == 0 || totalLen > sizeof(wifi_prov_rx_.data)) return false;
  const uint8_t totalChunks =
      static_cast<uint8_t>((totalLen + (kWifiProvisionChunkDataBytes - 1U)) / kWifiProvisionChunkDataBytes);
  if (totalChunks == 0 || totalChunks > 31U) return false;

  uint8_t data[96]{};
  size_t pos = 0;
  for (size_t i = 0; i < static_cast<size_t>(ssid.length()); ++i) data[pos++] = static_cast<uint8_t>(ssid[i]);
  for (size_t i = 0; i < static_cast<size_t>(password.length()); ++i) data[pos++] = static_cast<uint8_t>(password[i]);
  const uint32_t hash = fnv1a32(data, totalLen);

  uint8_t transferId = static_cast<uint8_t>((millis() ^ last_counter_ ^ runtime_.local_address) & 0xFFU);
  if (transferId == 0) transferId = 1;

  uint8_t payload[12]{};
  payload[0] = kWifiProvisionOpStart;
  payload[1] = transferId;
  payload[3] = totalChunks;
  payload[4] = static_cast<uint8_t>(ssid.length());
  payload[5] = static_cast<uint8_t>(password.length());
  payload[6] = static_cast<uint8_t>(hash & 0xFFU);
  payload[7] = static_cast<uint8_t>((hash >> 8) & 0xFFU);
  payload[8] = static_cast<uint8_t>((hash >> 16) & 0xFFU);
  payload[9] = static_cast<uint8_t>((hash >> 24) & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, runtime_.local_address, targetAddress, payload)) {
    return false;
  }

  for (uint8_t idx = 0; idx < totalChunks; ++idx) {
    memset(payload, 0, sizeof(payload));
    payload[0] = kWifiProvisionOpData;
    payload[1] = transferId;
    payload[2] = idx;
    payload[3] = totalChunks;
    const size_t offset = static_cast<size_t>(idx) * kWifiProvisionChunkDataBytes;
    size_t chunkLen = totalLen - offset;
    if (chunkLen > kWifiProvisionChunkDataBytes) chunkLen = kWifiProvisionChunkDataBytes;
    payload[4] = static_cast<uint8_t>(chunkLen);
    memcpy(payload + 5, data + offset, chunkLen);
    last_counter_++;
    if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, runtime_.local_address, targetAddress, payload)) {
      return false;
    }
  }

  memset(payload, 0, sizeof(payload));
  payload[0] = kWifiProvisionOpCommit;
  payload[1] = transferId;
  payload[3] = totalChunks;
  last_counter_++;
  if (!radio_->sendRaw(MessageType::WifiProvision, last_counter_, runtime_.local_address, targetAddress, payload)) {
    return false;
  }

  const uint32_t sentAt = millis();
  last_tx_ms_ = sentAt;
  markRadioTxSentThisTick();
  last_wifi_prov_tx_ms_ = sentAt;
  lrslog::event("wifi_prov_tx", 0, last_counter_, totalChunks);
  return true;
}

bool NodeStateMachine::consumePendingWifiProvision(String &ssid, String &password, uint8_t &src) {
  if (!wifi_prov_pending_) return false;
  ssid = wifi_prov_pending_ssid_;
  password = wifi_prov_pending_password_;
  src = wifi_prov_pending_src_;
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::hasPendingWifiProvision() const { return wifi_prov_pending_; }

uint32_t NodeStateMachine::fleetWifiProvisionCooldownRemainingMs() const {
  if (last_wifi_prov_tx_ms_ == 0) return 0;
  const uint32_t now = millis();
  const uint32_t elapsed = now - last_wifi_prov_tx_ms_;
  if (elapsed >= kWifiProvisionCooldownMs) return 0;
  return kWifiProvisionCooldownMs - elapsed;
}

bool NodeStateMachine::sendPeerFleetKeyChange(uint8_t targetAddress, const String &newFleetKey) {
  const size_t totalLen = static_cast<size_t>(newFleetKey.length());
  if (totalLen < admin_config_utils::kMinDeploymentKeyLen || totalLen > 64) return false;

  constexpr uint8_t kFleetKeyChunkDataBytes = 7;
  const uint8_t totalChunks =
      static_cast<uint8_t>((totalLen + (kFleetKeyChunkDataBytes - 1U)) / kFleetKeyChunkDataBytes);
  if (totalChunks == 0 || totalChunks > 31U) return false;

  uint8_t data[64]{};
  for (size_t i = 0; i < totalLen; ++i) data[i] = static_cast<uint8_t>(newFleetKey[i]);
  const uint32_t hash = fnv1a32(data, totalLen);

  uint8_t transferId = static_cast<uint8_t>((millis() ^ last_counter_ ^ runtime_.local_address) & 0xFFU);
  if (transferId == 0) transferId = 1;

  uint8_t payload[12]{};
  payload[0] = kFleetKeyControlOpStart;
  payload[1] = transferId;
  payload[3] = totalChunks;
  payload[4] = static_cast<uint8_t>(totalLen);
  payload[6] = static_cast<uint8_t>(hash & 0xFFU);
  payload[7] = static_cast<uint8_t>((hash >> 8) & 0xFFU);
  payload[8] = static_cast<uint8_t>((hash >> 16) & 0xFFU);
  payload[9] = static_cast<uint8_t>((hash >> 24) & 0xFFU);

  last_counter_++;
  if (!radio_->sendRaw(MessageType::FleetKeyControl, last_counter_, runtime_.local_address, targetAddress, payload)) {
    return false;
  }

  for (uint8_t idx = 0; idx < totalChunks; ++idx) {
    memset(payload, 0, sizeof(payload));
    payload[0] = kFleetKeyControlOpData;
    payload[1] = transferId;
    payload[2] = idx;
    payload[3] = totalChunks;
    const size_t offset = static_cast<size_t>(idx) * kFleetKeyChunkDataBytes;
    size_t chunkLen = totalLen - offset;
    if (chunkLen > kFleetKeyChunkDataBytes) chunkLen = kFleetKeyChunkDataBytes;
    payload[4] = static_cast<uint8_t>(chunkLen);
    memcpy(payload + 5, data + offset, chunkLen);

    last_counter_++;
    if (!radio_->sendRaw(MessageType::FleetKeyControl, last_counter_, runtime_.local_address, targetAddress, payload)) {
      return false;
    }
  }

  memset(payload, 0, sizeof(payload));
  payload[0] = kFleetKeyControlOpCommit;
  payload[1] = transferId;
  payload[3] = totalChunks;

  last_counter_++;
  if (!radio_->sendRaw(MessageType::FleetKeyControl, last_counter_, runtime_.local_address, targetAddress, payload)) {
    return false;
  }

  const uint32_t sentAt = millis();
  last_tx_ms_ = sentAt;
  markRadioTxSentThisTick();
  lrslog::event("fleet_key_tx", 0, last_counter_, totalChunks);
  return true;
}

bool NodeStateMachine::hasPendingFleetKeyChange() const { return fleet_key_pending_; }

bool NodeStateMachine::consumePendingFleetKeyChange(String &newKey, uint8_t &src) {
  if (!fleet_key_pending_) return false;
  newKey = fleet_key_pending_key_;
  src = fleet_key_pending_src_;
  fleet_key_pending_ = false;
  fleet_key_pending_key_ = "";
  fleet_key_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::sendPeerReboot(uint8_t dstAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (!runtime_.role_tx) return false;
  if (radio_ == nullptr) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  uint8_t payload[12]{};
  payload[0] = kRebootMagic0;
  payload[1] = kRebootMagic1;
  payload[2] = 0;
  payload[3] = static_cast<uint8_t>(runtime_.local_address);
  payload[4] = static_cast<uint8_t>(millis() & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::Reboot, last_counter_, runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event("reboot_peer_tx", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::consumePendingReboot() {
  if (!reboot_pending_) return false;
  reboot_pending_ = false;
  return true;
}

bool NodeStateMachine::sendPeerSensorConfig(uint8_t dstAddress, bool tempEnabled, bool tankEnabled, bool powerSaveEnabled, bool powerSaveBootGrace) {
  if (!radioTxBudgetAvailable()) return false;
  if (!runtime_.role_tx) return false;
  if (radio_ == nullptr) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  uint8_t payload[12]{};
  payload[0] = kSensorConfigMagic0;
  payload[1] = kSensorConfigMagic1;
  payload[2] = (tempEnabled ? 0x01 : 0) | (powerSaveEnabled ? 0x02 : 0) | (!powerSaveBootGrace ? 0x04 : 0);
  payload[3] = tankEnabled ? 1 : 0;
  payload[4] = static_cast<uint8_t>(runtime_.local_address);
  payload[5] = static_cast<uint8_t>(millis() & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::SensorConfig, last_counter_, runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  lrslog::event("sensor_config_peer_tx", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::consumePendingSensorConfig(bool &tempEnabled, bool &tankEnabled, bool &powerSaveEnabled, bool &powerSaveBootGrace) {
  if (!sensor_config_pending_) return false;
  tempEnabled = sensor_config_temp_enabled_;
  tankEnabled = sensor_config_tank_enabled_;
  powerSaveEnabled = sensor_config_power_save_enabled_;
  powerSaveBootGrace = sensor_config_power_save_boot_grace_;
  sensor_config_pending_ = false;
  return true;
}

bool NodeStateMachine::sendPeerFactoryReset(uint8_t dstAddress, bool keepSharedFleetKey, bool keepWifiCredentials) {
  if (!radioTxBudgetAvailable()) return false;
  if (!runtime_.role_tx) return false;
  if (radio_ == nullptr) return false;
  if (dstAddress == 0 || dstAddress == 255) return false;

  uint8_t payload[12]{};
  payload[0] = kFactoryResetMagic0;
  payload[1] = kFactoryResetMagic1;
  payload[2] = (keepSharedFleetKey ? kFactoryResetKeepFleetFlag : 0) |
               (keepWifiCredentials ? kFactoryResetKeepWifiFlag : 0);
  payload[3] = static_cast<uint8_t>(runtime_.local_address);
  payload[4] = static_cast<uint8_t>(millis() & 0xFFU);
  last_counter_++;
  if (!radio_->sendRaw(MessageType::FactoryReset, last_counter_, runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (!keepSharedFleetKey) {
    for (size_t i = 0; i < kMaxPeers; ++i) {
      if (provisioned_addrs_[i].in_use && provisioned_addrs_[i].assigned_address == dstAddress) {
        provisioned_addrs_[i] = ProvisionedAddressEntry{};
      }
    }
  }
  lrslog::event(keepSharedFleetKey ? "factory_reset_peer_tx_keep" : "factory_reset_peer_tx_full", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::consumePendingFactoryReset(bool &keepSharedFleetKey, bool &keepWifiCredentials, uint8_t &src) {
  if (!factory_reset_pending_) return false;
  keepSharedFleetKey = factory_reset_keep_fleet_pending_;
  keepWifiCredentials = factory_reset_keep_wifi_pending_;
  src = factory_reset_pending_src_;
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_keep_wifi_pending_ = false;
  factory_reset_pending_src_ = 0;
  return true;
}

bool NodeStateMachine::isAuthorizedMqttController(uint8_t src) const {
  if (settings_ == nullptr) return false;
  if (fixedListContainsAddress(settings_->allowed_controller_addresses, settings_->allowed_controller_count, src)) {
    return true;
  }
  const String raw(settings_->mqtt_controller_addresses.c_str());
  if (raw.length() == 0) return false;
  return csvContainsAddress(raw, src);
}

bool NodeStateMachine::isAuthorizedPairedSource(uint8_t src) const {
  if (settings_ == nullptr) return false;
  if (fixedListContainsAddress(settings_->allowed_controller_addresses, settings_->allowed_controller_count, src)) {
    return true;
  }
  return src == runtime_.remote_address;
}

bool NodeStateMachine::isDefaultFleetKey() const {
  return settings_ != nullptr &&
         isDefaultDeploymentKey(settings_->fleet_passphrase.c_str());
}

static const char *localProvisioningSessionStateText(ProvisioningSessionState s) {
  switch (s) {
    case ProvisioningSessionState::Discovering: return "discovering";
    case ProvisioningSessionState::Ready: return "ready";
    case ProvisioningSessionState::Provisioning: return "provisioning";
    case ProvisioningSessionState::Complete: return "complete";
    case ProvisioningSessionState::Error: return "error";
    default: return "idle";
  }
}

void NodeStateMachine::enterProvisioningQuietMode() {
  uint32_t scans = fleet_scan_active_ ? 1 : 0;
  uint32_t maint = (maintenance_debug_pending_ ? 1 : 0) + 
                   (maintenance_sensor_pending_ ? 1 : 0) + 
                   (maintenance_version_pending_ ? 1 : 0);
  uint32_t groups = (tx_group_phase_ != PairedGroupPhase::Idle) ? 1 : 0;
  
  uint32_t peerCmds = 0;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    if (peers_[i].in_use && peers_[i].pending) {
      peerCmds++;
    }
  }
  
  uint32_t polls = 0;
  if (poll_states_ != nullptr) {
    for (size_t i = 0; i < poll_state_capacity_; ++i) {
      if (poll_states_[i].poll_pending) {
        polls++;
      }
    }
  }

  // 1. Cancel active fleet scan
  fleet_scan_active_ = false;

  // 2. Cancel any pending maintenance pages
  maintenance_debug_pending_ = false;
  maintenance_debug_dst_ = 0;
  maintenance_sensor_pending_ = false;
  maintenance_sensor_dst_ = 0;
  maintenance_version_pending_ = false;
  maintenance_version_dst_ = 0;

  // 3. Reset group command state
  resetTxGroupState();

  // 4. Clear other transient/pending TX flags in state machine
  tx_ack_pending_ = false;
  tx_state_sync_pending_ = false;
  rx_deferred_ack_pending_ = false;
  rx_push_pending_ = false;

  // 5. Clear pending peer command and poll states (preserving identity/cache)
  for (size_t i = 0; i < kMaxPeers; ++i) {
    if (peers_[i].in_use) {
      peers_[i].pending = false;
      peers_[i].retry_step = 0;
      peers_[i].next_retry_ms = 0;
      peers_[i].pending_counter = 0;
      peers_[i].pending_deadline_ms = 0;
      peers_[i].wifi_pending = false;
    }
  }
  if (poll_states_ != nullptr) {
    for (size_t i = 0; i < poll_state_capacity_; ++i) {
      poll_states_[i].poll_pending = false;
      poll_states_[i].poll_retry_step = 0;
      poll_states_[i].poll_next_retry_ms = 0;
      poll_states_[i].poll_counter = 0;
      poll_states_[i].poll_deadline_ms = 0;
    }
  }

  addProvLog("Provisioning quiet mode entered");
  if (scans || maint || groups || peerCmds || polls) {
    addProvLog("Cancelled: scan=%u maint=%u grp=%u cmd=%u poll=%u",
               scans, maint, groups, peerCmds, polls);
  }
}

void NodeStateMachine::exitProvisioningCoordinatorMode(ProvisioningSessionState endState) {
  if (prov_.active || prov_.pause_normal_tx) {
    prov_.active = false;
    prov_.pause_normal_tx = false;
    prov_.state = endState;
    prov_.phase_deadline_ms = 0;
    prov_.watchdog_last_log_ms = 0;

    LRS_LOGI(API, "event=provisioning_coordinator_exit state=%s", localProvisioningSessionStateText(endState));
    addProvLog("Provisioning quiet mode exited (state=%s)", localProvisioningSessionStateText(endState));
  }
}

bool NodeStateMachine::provisioningStartDiscovery(uint16_t estimatedCount) {
  if (!runtime_.role_tx || radio_ == nullptr) {
    LRS_LOGW(API,
             "event=provisioning_start_reject reason=%s role_tx=%u radio=%u",
             !runtime_.role_tx ? "not_tx" : "radio_unavailable",
             runtime_.role_tx ? 1U : 0U,
             (radio_ != nullptr) ? 1U : 0U);
    return false;
  }
  if (estimatedCount == 0) estimatedCount = 1;
  if (estimatedCount > static_cast<uint16_t>(kMaxProvisioningDevices)) {
    estimatedCount = static_cast<uint16_t>(kMaxProvisioningDevices);
  }
  if (isDefaultFleetKey()) {
    LRS_LOGW(API, "event=provisioning_start_reject reason=default_fleet_key");
    return false;  // gateway must have a production key
  }

  prov_ = ProvisioningSessionRuntime{};
  prov_.active = true;
  prov_.state = ProvisioningSessionState::Discovering;
  prov_.session_nonce = static_cast<uint16_t>((millis() ^ last_counter_ ^ runtime_.local_address ^ random(1, 65535)) & 0xFFFFU);
  if (prov_.session_nonce == 0) prov_.session_nonce = 1;
  prov_.estimated_count = estimatedCount;
  prov_.started_ms = millis();
  prov_.pause_normal_tx = true;
  prov_.discover_broadcast_window_ms =
      (kProvDiscoverBroadcastBurstCount > 1) ? ((kProvDiscoverBroadcastBurstCount - 1U) * kProvDiscoverBroadcastGapMs) : 0U;
  prov_.discover_reply_window_ms = kProvDiscoverReplyBaseMs + (static_cast<uint32_t>(estimatedCount) * kProvDiscoverReplyPerDeviceMs);
  if (prov_.discover_reply_window_ms > kProvDiscoverReplyWindowMaxMs) {
    prov_.discover_reply_window_ms = kProvDiscoverReplyWindowMaxMs;
  }
  prov_.phase_deadline_ms = prov_.started_ms + prov_.discover_broadcast_window_ms + prov_.discover_reply_window_ms;
  const uint32_t cap = prov_.started_ms + 120000UL;
  if (prov_.phase_deadline_ms > cap) prov_.phase_deadline_ms = cap;
  if (prov_.phase_deadline_ms <= prov_.started_ms) prov_.phase_deadline_ms = prov_.started_ms + 1000UL;
  uint32_t totalWindowMs = prov_.phase_deadline_ms - prov_.started_ms;
  if (totalWindowMs <= prov_.discover_broadcast_window_ms) {
    prov_.discover_broadcast_window_ms = 0;
    totalWindowMs = prov_.phase_deadline_ms - prov_.started_ms;
  }
  prov_.discover_reply_window_ms = totalWindowMs - prov_.discover_broadcast_window_ms;
  prov_.provision_all_requested = false;
  prov_.current_index = 0;
  prov_.watchdog_last_log_ms = 0;
  // Queue discovery broadcast(s) for the gateway tick instead of requiring
  // immediate radio TX in the API request path.
  prov_.discover_broadcast_remaining = (kProvDiscoverBroadcastBurstCount > 0) ? kProvDiscoverBroadcastBurstCount : 1U;
  prov_.next_discover_broadcast_ms = prov_.started_ms;
  if (!ensureProvisioningStorage()) {
    LRS_LOGW(API,
             "event=provisioning_start_reject reason=oom_provisioning_storage estimated=%u heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned>(estimatedCount),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    prov_ = ProvisioningSessionRuntime{};
    return false;
  }
  resetProvisioningStorage();

  // Clear provisioning log buffer first so quiet mode logs are kept
  prov_log_head_ = 0;
  prov_log_count_ = 0;
  memset(prov_logs_, 0, sizeof(prov_logs_));

  enterProvisioningQuietMode();

  for (size_t i = 0; i < kMaxPeers; ++i) {
    provisioned_addrs_[i] = ProvisionedAddressEntry{};
  }
  addProvLog("Discovery started: nonce=%u count=%u", prov_.session_nonce, estimatedCount);

  lrslog::event("prov_discover_start", 0, prov_.session_nonce, estimatedCount);
  return true;
}

bool NodeStateMachine::provisioningStartProvisionAll() {
  if (!(prov_.state == ProvisioningSessionState::Ready || prov_.state == ProvisioningSessionState::Complete ||
        prov_.state == ProvisioningSessionState::Error)) {
    return false;
  }
  recomputeProvisioningConflictsAndAssignments();
  prov_.active = true;
  prov_.provision_all_requested = true;
  prov_.state = ProvisioningSessionState::Provisioning;
  prov_.current_index = 0;
  prov_.phase_deadline_ms = millis();
  prov_.pause_normal_tx = true;
  prov_.watchdog_last_log_ms = 0;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (!prov_devices_[i].in_use) continue;
    prov_devices_[i].selected = true;
    if (prov_devices_[i].state == ProvisioningDeviceState::Verified) continue;
    prov_devices_[i].state = ProvisioningDeviceState::Discovered;
    prov_devices_[i].retries = 0;
    prov_devices_[i].key_total_chunks = 0;
    prov_devices_[i].key_len = 0;
    prov_devices_[i].key_crc16 = 0;
    prov_devices_[i].key_next_chunk = 0;
    prov_devices_[i].key_start_sent = false;
    prov_devices_[i].key_commit_sent = false;
  }
  return true;
}

void NodeStateMachine::provisioningCancel() {
  if (prov_.state == ProvisioningSessionState::Discovering &&
      prov_device_count_ > 0) {
    recomputeProvisioningConflictsAndAssignments();
    LRS_LOGI(API,
             "event=prov_discover_ready reason=operator_stop found=%u estimated=%u elapsed_ms=%lu",
             static_cast<unsigned>(prov_device_count_),
             static_cast<unsigned>(prov_.estimated_count),
             static_cast<unsigned long>(millis() - prov_.started_ms));
    exitProvisioningCoordinatorMode(ProvisioningSessionState::Ready);
    return;
  }
  exitProvisioningCoordinatorMode(ProvisioningSessionState::Idle);
  prov_ = ProvisioningSessionRuntime{};
  prov_rx_ = ProvTargetRxState{};
  freeProvisioningStorage();
}

bool NodeStateMachine::provisioningSession(ProvisioningSessionSnapshot &out) const {
  out = ProvisioningSessionSnapshot{};
  out.active = prov_.active;
  out.state = prov_.state;
  out.session_nonce = prov_.session_nonce;
  out.estimated_count = prov_.estimated_count;
  out.started_ms = prov_.started_ms;
  out.phase_deadline_ms = prov_.phase_deadline_ms;
  out.paused_normal_tx = prov_.pause_normal_tx;
  out.discovered_count = prov_device_count_;
  size_t conflicts = 0, selected = 0, verified = 0, failed = 0;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    const ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (d.selected) selected++;
    if (d.address_conflict) conflicts++;
    if (d.state == ProvisioningDeviceState::Verified) verified++;
    if (d.state == ProvisioningDeviceState::Failed) failed++;
  }
  out.selected_count = selected;
  out.conflict_count = conflicts;
  out.verified_count = verified;
  out.failed_count = failed;
  return true;
}

size_t NodeStateMachine::provisioningDeviceCount() const { return prov_device_count_; }

bool NodeStateMachine::provisioningDeviceByIndex(size_t index, ProvisioningDeviceSnapshot &out) const {
  if (index >= prov_device_count_) return false;
  const ProvisioningDevice &d = prov_devices_[index];
  if (!d.in_use) return false;
  out.chip_id = d.chip_id;
  out.current_address = d.current_address;
  out.assigned_address = d.assigned_address;
  out.role_tx = d.role_tx;
  out.hw_model = d.hw_model;
  out.hw_rev = d.hw_rev;
  out.fw_major = d.fw_major;
  out.fw_minor = d.fw_minor;
  out.fw_patch = d.fw_patch;
  out.fw_build = d.fw_build;
  out.rssi = d.rssi;
  out.first_seen_ms = d.first_seen_ms;
  out.last_seen_ms = d.last_seen_ms;
  out.selected = d.selected;
  out.address_conflict = d.address_conflict;
  out.state = d.state;
  return true;
}

size_t NodeStateMachine::provisioningLogCount() const {
  return prov_log_count_;
}

bool NodeStateMachine::provisioningLogByIndex(size_t index, uint32_t &timestampMs, char outMsg[56]) const {
  if (index >= prov_log_count_) return false;
  const size_t start = (prov_log_count_ < kMaxProvLogs) ? 0 : prov_log_head_;
  const size_t idx = (start + index) % kMaxProvLogs;
  timestampMs = prov_logs_[idx].timestamp_ms;
  memcpy(outMsg, prov_logs_[idx].message, 56);
  return true;
}

void NodeStateMachine::addProvLog(const char *fmt, ...) {
  const uint32_t now = millis();
  const size_t idx = (prov_log_head_ + prov_log_count_) % kMaxProvLogs;
  
  va_list args;
  va_start(args, fmt);
  vsnprintf(prov_logs_[idx].message, sizeof(prov_logs_[idx].message), fmt, args);
  va_end(args);
  
  prov_logs_[idx].timestamp_ms = now;
  
  if (prov_log_count_ < kMaxProvLogs) {
    prov_log_count_++;
  } else {
    prov_log_head_ = (prov_log_head_ + 1) % kMaxProvLogs;
  }
}

bool NodeStateMachine::consumePendingFleetProvisionApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx,
                                                         uint8_t &controllerAddress, String &fleetKey) {
  if (!fleet_prov_apply_pending_) return false;
  sessionNonce = fleet_prov_apply_session_nonce_;
  newAddress = fleet_prov_apply_address_;
  roleTx = fleet_prov_apply_role_tx_;
  controllerAddress = fleet_prov_apply_controller_address_;
  fleetKey = fleet_prov_apply_key_;
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_controller_address_ = 0;
  fleet_prov_apply_key_ = "";
  return true;
}

bool NodeStateMachine::hasPendingFleetProvisionApply() const { return fleet_prov_apply_pending_; }

bool NodeStateMachine::sendProvisioningVerify(uint16_t sessionNonce, uint8_t assignedAddress) {
  return sendProvisioningVerifyPacket(sessionNonce, assignedAddress);
}

NodeStateMachine::ProvisioningDevice *NodeStateMachine::findProvisioningDeviceByChip(uint32_t chipId) {
  if (prov_devices_ == nullptr) return nullptr;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (prov_devices_[i].in_use && prov_devices_[i].chip_id == chipId) return &prov_devices_[i];
  }
  return nullptr;
}

NodeStateMachine::ProvisioningDevice *NodeStateMachine::upsertProvisioningDevice(uint32_t chipId) {
  if (chipId == 0) return nullptr;
  if (prov_devices_ == nullptr) return nullptr;
  if (ProvisioningDevice *d = findProvisioningDeviceByChip(chipId)) return d;
  if (prov_device_count_ >= prov_device_capacity_) return nullptr;
  ProvisioningDevice &d = prov_devices_[prov_device_count_++];
  d = ProvisioningDevice{};
  d.in_use = true;
  d.chip_id = chipId;
  d.selected = true;
  d.state = ProvisioningDeviceState::Discovered;
  return &d;
}

uint8_t NodeStateMachine::preferredProvisionedAddressForChip(uint32_t chipId) const {
  if (chipId == 0) return 0;
  // Check volatile in-memory cache first (populated during current session)
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const ProvisionedAddressEntry &e = provisioned_addrs_[i];
    if (!e.in_use || e.chip_id != chipId) continue;
    if (e.assigned_address < kProvAddressMin || e.assigned_address > kProvAddressMax) return 0;
    return e.assigned_address;
  }
  // Fall back to persistent chip→address mapping from Settings
  if (settings_ != nullptr) {
    for (size_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
      if (settings_->known_peer_chip_ids[i] == chipId) {
        const uint8_t addr = settings_->known_peer_addresses[i];
        if (addr >= kProvAddressMin && addr <= kProvAddressMax) return addr;
      }
    }
  }
  return 0;
}

bool NodeStateMachine::hasDiscoveredProvisioningChip(uint32_t chipId) const {
  if (chipId == 0 || prov_devices_ == nullptr) return false;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    const ProvisioningDevice &d = prov_devices_[i];
    if (d.in_use && d.chip_id == chipId) return true;
  }
  return false;
}

void NodeStateMachine::rememberProvisionedAddress(uint32_t chipId, uint8_t assignedAddress) {
  if (chipId == 0) return;
  if (assignedAddress < kProvAddressMin || assignedAddress > kProvAddressMax) return;
  const uint32_t now = millis();
  size_t emptyIndex = kMaxPeers;
  size_t oldestIndex = 0;
  uint32_t oldestMs = 0xFFFFFFFFUL;
  for (size_t i = 0; i < kMaxPeers; ++i) {
    ProvisionedAddressEntry &e = provisioned_addrs_[i];
    if (e.in_use && e.chip_id == chipId) {
      e.assigned_address = assignedAddress;
      e.updated_ms = now;
      return;
    }
    if (!e.in_use && emptyIndex == kMaxPeers) {
      emptyIndex = i;
      continue;
    }
    if (e.in_use && e.updated_ms < oldestMs) {
      oldestMs = e.updated_ms;
      oldestIndex = i;
    }
  }
  const size_t idx = (emptyIndex < kMaxPeers) ? emptyIndex : oldestIndex;
  provisioned_addrs_[idx].in_use = true;
  provisioned_addrs_[idx].chip_id = chipId;
  provisioned_addrs_[idx].assigned_address = assignedAddress;
  provisioned_addrs_[idx].updated_ms = now;
}

void NodeStateMachine::prePopulateGatewayPeerCache() {
  if (!runtime_.role_tx || settings_ == nullptr) return;

  uint8_t targets[Settings::kAddressListCap]{};
  const bool isPairedMode = (settings_->mode == "paired");

  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
    isPairedMode,
    runtime_.local_address,
    settings_->known_peer_count,
    settings_->known_peer_addresses,
    settings_->paired_target_count,
    settings_->paired_target_addresses,
    settings_->remote_address,
    targets,
    Settings::kAddressListCap
  );

  for (uint8_t i = 0; i < targetCount; ++i) {
    findOrCreatePeer(targets[i]);
  }
}

void NodeStateMachine::recomputeProvisioningConflictsAndAssignments() {
  uint8_t prev_assigned[kMaxProvisioningDevices]{};
  bool prev_conflict[kMaxProvisioningDevices]{};
  for (size_t i = 0; i < prov_device_count_ && i < kMaxProvisioningDevices; ++i) {
    prev_assigned[i] = prov_devices_[i].assigned_address;
    prev_conflict[i] = prov_devices_[i].address_conflict;
  }

  bool used[256]{};
  used[0] = true;
  used[255] = true;
  auto addressBelongsToCurrentProvisioningDevice = [this](uint8_t address) {
    if (address < kProvAddressMin || address > kProvAddressMax) return false;
    for (size_t i = 0; i < prov_device_count_; ++i) {
      const ProvisioningDevice &d = prov_devices_[i];
      if (!d.in_use) continue;
      if (d.current_address == address) return true;
      if (d.assigned_address == address) return true;
    }
    // Check persistent chip→address mapping: if this address's stored
    // chip_id matches a discovered provisioning device, the address
    // belongs to that device (e.g. factory-reset device reclaiming its
    // old slot).
    if (settings_ != nullptr) {
      for (size_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
        if (settings_->known_peer_addresses[i] != address) continue;
        const uint32_t chipId = settings_->known_peer_chip_ids[i];
        if (chipId == 0) continue;
        for (size_t j = 0; j < prov_device_count_; ++j) {
          if (prov_devices_[j].in_use && prov_devices_[j].chip_id == chipId) return true;
        }
      }
    }
    return false;
  };
  auto getSettingsChipIdForAddress = [this](uint8_t address) -> uint32_t {
    if (settings_ == nullptr) return 0;
    for (size_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
      if (settings_->known_peer_addresses[i] == address) {
        return settings_->known_peer_chip_ids[i];
      }
    }
    return 0;
  };
  auto hasUnassignedProvisioningDevices = [this]() -> bool {
    if (prov_devices_ == nullptr) return false;
    for (size_t i = 0; i < prov_device_count_; ++i) {
      if (prov_devices_[i].in_use && prov_devices_[i].assigned_address == 0) {
        return true;
      }
    }
    return false;
  };
  if (runtime_.local_address >= kProvAddressMin && runtime_.local_address <= kProvAddressMax) {
    used[runtime_.local_address] = true;
  }
  if (settings_ != nullptr) {
    for (size_t i = 0; i < settings_->paired_target_count && i < Settings::kAddressListCap; ++i) {
      const uint8_t addr = settings_->paired_target_addresses[i];
      if (addr < kProvAddressMin || addr > kProvAddressMax) continue;
      if (addressBelongsToCurrentProvisioningDevice(addr)) continue;
      if (getSettingsChipIdForAddress(addr) == 0 && hasUnassignedProvisioningDevices()) {
        continue;
      }
      used[addr] = true;
    }
    for (size_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
      const uint8_t addr = settings_->known_peer_addresses[i];
      if (addr < kProvAddressMin || addr > kProvAddressMax) continue;
      if (addressBelongsToCurrentProvisioningDevice(addr)) continue;
      if (getSettingsChipIdForAddress(addr) == 0 && hasUnassignedProvisioningDevices()) {
        continue;
      }
      used[addr] = true;
    }
  }
  for (size_t i = 0; i < peer_count_; ++i) {
    const PeerRuntime &peer = peers_[i];
    if (!peer.in_use) continue;
    if (peer.address >= kProvAddressMin && peer.address <= kProvAddressMax) {
      if (addressBelongsToCurrentProvisioningDevice(peer.address)) continue;
      used[peer.address] = true;
    }
  }
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const ProvisionedAddressEntry &e = provisioned_addrs_[i];
    if (!e.in_use) continue;
    if (e.assigned_address < kProvAddressMin || e.assigned_address > kProvAddressMax) continue;
    if (hasDiscoveredProvisioningChip(e.chip_id)) continue;
    if (e.chip_id == 0 && hasUnassignedProvisioningDevices()) {
      continue;
    }
    used[e.assigned_address] = true;
  }
  for (size_t i = 0; i < prov_device_count_; ++i) {
    prov_devices_[i].address_conflict = false;
    prov_devices_[i].assigned_address = 0;
  }
  for (size_t i = 0; i < prov_device_count_; ++i) {
    if (!prov_devices_[i].in_use) continue;
    for (size_t j = i + 1; j < prov_device_count_; ++j) {
      if (!prov_devices_[j].in_use) continue;
      const uint8_t current = prov_devices_[i].current_address;
      if (current >= kProvAddressMin && current <= kProvAddressMax &&
          current == prov_devices_[j].current_address) {
        prov_devices_[i].address_conflict = true;
        prov_devices_[j].address_conflict = true;
      }
    }
  }
  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    uint8_t preferred = preferredProvisionedAddressForChip(d.chip_id);
    if (preferred >= kProvAddressMin && preferred <= kProvAddressMax && !used[preferred]) {
      d.assigned_address = preferred;
      used[d.assigned_address] = true;
      LRS_LOGI(API,
               "event=prov_address_alloc chip_id=0x%08lx current=%u assigned=%u source=preferred",
               static_cast<unsigned long>(d.chip_id),
               static_cast<unsigned>(d.current_address),
               static_cast<unsigned>(d.assigned_address));
    }
  }
  uint8_t next = kProvAddressMin;
  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (!d.address_conflict && d.assigned_address >= kProvAddressMin && d.assigned_address <= kProvAddressMax) continue;
    while (next <= kProvAddressMax && used[next]) next++;
    if (next > kProvAddressMax) {
      d.assigned_address = 0;
      d.state = ProvisioningDeviceState::Failed;
      continue;
    }
    d.assigned_address = next;
    used[next] = true;
    LRS_LOGI(API,
             "event=prov_address_alloc chip_id=0x%08lx current=%u assigned=%u source=next_free",
             static_cast<unsigned long>(d.chip_id),
             static_cast<unsigned>(d.current_address),
             static_cast<unsigned>(d.assigned_address));
    next++;
  }

  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use) continue;
    if (d.address_conflict && !prev_conflict[i]) {
      addProvLog("Conflict detected for chip=0x%08lx address=%u",
                 static_cast<unsigned long>(d.chip_id), d.current_address);
    }
    if (d.assigned_address != prev_assigned[i] && d.assigned_address != 0) {
      addProvLog("Assigned address %u to chip=0x%08lx",
                 d.assigned_address, static_cast<unsigned long>(d.chip_id));
    }
  }
}

bool NodeStateMachine::sendProvisioningCoordinatorPacketFactory(const uint8_t payload[12], uint8_t dst) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, runtime_.local_address, dst, payload, true)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::sendProvisioningDiscoverStart(uint16_t sessionNonce, uint32_t replyWindowMs, uint32_t broadcastWindowMs) {
  uint8_t payload[12]{};
  payload[0] = kProvOpDiscoverStart;
  encodeU16LE(payload + 1, sessionNonce);
  uint16_t replyTicks = static_cast<uint16_t>(replyWindowMs / 100U);
  if (replyTicks == 0) replyTicks = 1;
  encodeU16LE(payload + 3, replyTicks);
  uint32_t broadcastTicks = broadcastWindowMs / 100U;
  if (broadcastTicks > 255U) broadcastTicks = 255U;
  payload[5] = static_cast<uint8_t>(broadcastTicks);
  return sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress);
}

bool NodeStateMachine::sendProvisioningAnnounce(uint16_t sessionNonce) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  uint8_t payload[12]{};
  payload[0] = kProvOpAnnounce;
  encodeU16LE(payload + 1, sessionNonce);
  encodeU32LE(payload + 3, ESP.getChipId());
  payload[7] = runtime_.local_address;
  payload[8] = (runtime_.role_tx ? kProvRoleTxFlag : 0) | ((kProvHwModelLrs & 0x0F) << 4);
  payload[9] = static_cast<uint8_t>(fwDevBuild() & 0xFFU);
  uint8_t maj = 0, min = 0, patch = 0;
  parseFwVersionPacked(maj, min, patch);
  payload[10] = static_cast<uint8_t>(((maj & 0x0F) << 4) | (min & 0x0F));
  payload[11] = patch;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, runtime_.local_address, kProvBroadcastAddress, payload, true)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::sendProvisioningVerifyPacket(uint16_t sessionNonce, uint8_t assignedAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr) return false;
  if (isDefaultFleetKey()) return false;
  uint8_t payload[12]{};
  payload[0] = kProvOpVerify;
  encodeU16LE(payload + 1, sessionNonce);
  encodeU32LE(payload + 3, ESP.getChipId());
  payload[7] = assignedAddress;
  payload[8] = (runtime_.role_tx ? kProvRoleTxFlag : 0) | ((kProvHwModelLrs & 0x0F) << 4);
  payload[9] = static_cast<uint8_t>(fwDevBuild() & 0xFFU);
  uint8_t maj = 0, min = 0, patch = 0;
  parseFwVersionPacked(maj, min, patch);
  payload[10] = static_cast<uint8_t>(((maj & 0x0F) << 4) | (min & 0x0F));
  payload[11] = patch;
  last_counter_++;
  const uint32_t now = millis();
  if (!radio_->sendProvisioningRaw(last_counter_, runtime_.local_address, kProvBroadcastAddress, payload, false)) {
    return false;
  }
  last_tx_ms_ = now;
  markRadioTxSentThisTick();
  return true;
}

bool NodeStateMachine::confirmProvisioningByFleetResponse(const ProtocolMessage &msg) {
  if (!runtime_.role_tx || !prov_.active || prov_.state != ProvisioningSessionState::Provisioning) return false;
  const uint32_t now = millis();
  for (size_t i = 0; i < prov_device_count_; ++i) {
    ProvisioningDevice &d = prov_devices_[i];
    if (!d.in_use || !d.selected || d.state != ProvisioningDeviceState::AwaitVerify) continue;
    if (d.assigned_address == 0 || d.assigned_address != msg.src) continue;
    d.current_address = msg.src;
    d.rssi = msg.rssi;
    d.last_seen_ms = now;
    d.state = ProvisioningDeviceState::Verified;
    rememberProvisionedAddress(d.chip_id, d.assigned_address);
    if (prov_.current_index == i) {
      prov_.current_index++;
    }
    recomputeProvisioningConflictsAndAssignments();
    lrslog::event("prov_verify_late", msg.rssi, static_cast<uint32_t>(d.chip_id & 0xFFFFU), msg.src);
    return true;
  }
  return false;
}

NodeStateMachine::PeerRuntime *NodeStateMachine::findOrCreatePeer(uint8_t address) {
  if (address == 0 || address == 255 || address == runtime_.local_address) {
    return nullptr;
  }
  for (size_t i = 0; i < peer_count_; ++i) {
    if (peers_[i].in_use && peers_[i].address == address) {
      return &peers_[i];
    }
  }
  if (peer_count_ >= kMaxPeers) {
    return nullptr;
  }
  PeerRuntime &node = peers_[peer_count_++];
  node = PeerRuntime{};
  node.in_use = true;
  node.address = address;

  // Resolve chip ID from recently provisioned address cache if available
  for (size_t i = 0; i < kMaxPeers; ++i) {
    const ProvisionedAddressEntry &e = provisioned_addrs_[i];
    if (e.in_use && e.assigned_address == address && e.chip_id != 0) {
      node.chip_id = e.chip_id;
      break;
    }
  }

  // Also check if there's a chip ID persisted in Settings!
  if (node.chip_id == 0 && settings_ != nullptr) {
    for (size_t i = 0; i < settings_->known_peer_count && i < Settings::kAddressListCap; ++i) {
      if (settings_->known_peer_addresses[i] == address) {
        node.chip_id = settings_->known_peer_chip_ids[i];
        break;
      }
    }
  }

  uint32_t interval = runtime_.tx_mqtt_remote_default_poll_interval_ms;
  if (interval < kMinRemotePollIntervalMs) interval = kDefaultRemotePollIntervalMs;
  if (interval > kMaxRemotePollIntervalMs) interval = kMaxRemotePollIntervalMs;
  if (!runtime_.tx_mqtt_remote_polling_enabled) {
    interval = 0;
  }
  node.poll_interval_ms = interval;
  if (interval > 0) {
    if (ensurePollStorage()) {
      PollRuntime *poll = pollStateForIndex(static_cast<size_t>(&node - peers_));
      if (poll != nullptr) {
        poll->next_poll_ms = millis() + interval;
      }
    }
  }
  return &node;
}

bool NodeStateMachine::sendPollRequest(uint8_t dstAddress, uint32_t *sentCounter) {
  if (!radioTxBudgetAvailable()) return false;
  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(millis());
  if (!radio_->send(MessageType::PollRequest, 0, input_state_, txFlags(), last_counter_, runtime_.local_address, dstAddress,
                    localTempCodeToSend(),
                    0, 0xFF, 0xFFFF, unixTimeS)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (sentCounter != nullptr) {
    *sentCounter = last_counter_;
  }
  {
    lrslog::event("tx_poll_request", 0, last_counter_, 0);
  }
  return true;
}

bool NodeStateMachine::sendMaintenanceRequest(uint8_t dstAddress, uint32_t *sentCounter) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255) return false;
  uint8_t payload[12]{};
  payload[0] = 1;  // request version
  last_counter_++;
  if (!radio_->sendRaw(MessageType::MaintenanceRequest, last_counter_,
                       runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  markRadioTxSentThisTick();
  if (sentCounter != nullptr) *sentCounter = last_counter_;
  lrslog::event("maint_request_tx", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::sendMaintenanceStatus(uint8_t dstAddress) {
  if (ota_pull_active_ || (ota_silence_until_ms_ != 0 && millis() < ota_silence_until_ms_)) return false;
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255 || settings_ == nullptr) return false;
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
  parseFwVersionPacked(major, minor, patch);
  const IPAddress ip = WiFi.localIP();
  uint8_t flags = 0;
  if (settings_->wifi_admin_enabled) flags |= 0x01;
  if (WiFi.isConnected()) flags |= 0x02;
  if (settings_->mqtt_client_enabled) flags |= 0x04;
  if (mqtt_connected_) flags |= 0x08;
  if (runtime_.role_tx) flags |= 0x10;
  if (settings_->power_save_listen_only) flags |= 0x20;
  if (power_save_active_) flags |= 0x40;
  uint8_t payload[12]{};
  const uint32_t chipId = ESP.getChipId() & 0xFFFFFFUL;
  payload[0] = kMaintenancePayloadVersion;
  payload[1] = kMaintenancePageIdentity;
  payload[2] = flags;
  payload[3] = static_cast<uint8_t>(chipId & 0xFFU);
  payload[4] = static_cast<uint8_t>((chipId >> 8) & 0xFFU);
  payload[5] = static_cast<uint8_t>((chipId >> 16) & 0xFFU);
  payload[6] = static_cast<uint8_t>(((major & 0x0FU) << 4) | (minor & 0x0FU));
  payload[7] = patch;
  payload[8] = ip[0];
  payload[9] = ip[1];
  payload[10] = ip[2];
  payload[11] = ip[3];
  last_counter_++;
  if (!radio_->sendRaw(MessageType::MaintenanceStatus, last_counter_,
                       runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  last_maint_page_tx_ms_ = last_tx_ms_;
  markRadioTxSentThisTick();
  lrslog::event("maint_status_tx", 0, last_counter_, dstAddress);
  maintenance_version_pending_ = true;
  maintenance_version_dst_ = dstAddress;
  maintenance_sensor_pending_ = true;
  maintenance_sensor_dst_ = dstAddress;
  maintenance_sensor_page_index_ = 0;
  if (runtime_.maintenance_debug_telemetry_enabled) {
    maintenance_debug_pending_ = true;
    maintenance_debug_dst_ = dstAddress;
  }
  return true;
}

bool NodeStateMachine::sendMaintenanceVersionStatus(uint8_t dstAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255) return false;
  const uint16_t build = fwDevBuild();
  uint8_t major = 0;
  uint8_t minor = 0;
  uint8_t patch = 0;
  parseFwVersionPacked(major, minor, patch);
  uint8_t payload[12]{};
  payload[0] = kMaintenancePayloadVersion;
  payload[1] = kMaintenancePageVersion;
  payload[2] = major;
  payload[3] = minor;
  payload[4] = patch;
  encodeU16LE(payload + 5, build);
  encodeU32LE(payload + 7, millis());

  last_counter_++;
  if (!radio_->sendRaw(MessageType::MaintenanceStatus, last_counter_,
                       runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  last_maint_page_tx_ms_ = last_tx_ms_;
  markRadioTxSentThisTick();
  lrslog::event("maint_version_tx", 0, last_counter_, dstAddress);
  return true;
}

bool NodeStateMachine::sendMaintenanceSensorStatus(uint8_t dstAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255) return false;
  uint8_t payload[12]{};
  payload[0] = kMaintenancePayloadVersion;
  payload[1] = kMaintenancePageSensors;
  payload[2] = maintenance_sensor_page_index_;
  payload[3] = local_sensors_.count();

  auto packReading = [this](uint8_t index, uint8_t *dest) {
    SensorReading r{};
    if (local_sensors_.byIndex(index, r)) {
      int32_t val = r.value;
      SensorState st = r.state;
      if (val < -32768) {
        val = -32768;
      } else if (val > 32767) {
        val = 32767;
        st = SensorState::Overrange;
      }
      int16_t val16 = static_cast<int16_t>(val);
      dest[0] = (static_cast<uint8_t>(r.kind) << 4) | (r.instance & 0x0F);
      dest[1] = (static_cast<uint8_t>(st) << 4) | (r.scale & 0x0F);
      dest[2] = static_cast<uint8_t>(val16 & 0xFFU);
      dest[3] = static_cast<uint8_t>((val16 >> 8) & 0xFFU);
    } else {
      memset(dest, 0, 4);
    }
  };

  packReading(maintenance_sensor_page_index_ * 2, payload + 4);
  packReading(maintenance_sensor_page_index_ * 2 + 1, payload + 8);

  last_counter_++;
  if (!radio_->sendRaw(MessageType::MaintenanceStatus, last_counter_,
                       runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  last_maint_page_tx_ms_ = last_tx_ms_;
  markRadioTxSentThisTick();
  lrslog::event("maint_sensor_status_tx", maintenance_sensor_page_index_, last_counter_, dstAddress);

  if ((maintenance_sensor_page_index_ + 1) * 2 >= local_sensors_.count()) {
    maintenance_sensor_pending_ = false;
    maintenance_sensor_dst_ = 0;
    maintenance_sensor_page_index_ = 0;
  } else {
    maintenance_sensor_page_index_++;
  }

  return true;
}

bool NodeStateMachine::sendMaintenanceDebugStatus(uint8_t dstAddress) {
  if (!radioTxBudgetAvailable()) return false;
  if (radio_ == nullptr || dstAddress == 0 || dstAddress == 255) return false;
  uint8_t payload[12]{};
  payload[0] = kMaintenancePayloadVersion;
  payload[1] = kMaintenancePageDebug;
  uint32_t heapFree = lrslog::heapFree();
  uint32_t heapMaxBlock = lrslog::heapMaxFreeBlock();
  if (heapFree > 0xFFFFUL) heapFree = 0xFFFFUL;
  if (heapMaxBlock > 0xFFFFUL) heapMaxBlock = 0xFFFFUL;
  payload[2] = static_cast<uint8_t>(heapFree & 0xFFU);
  payload[3] = static_cast<uint8_t>((heapFree >> 8) & 0xFFU);
  payload[4] = static_cast<uint8_t>(heapMaxBlock & 0xFFU);
  payload[5] = static_cast<uint8_t>((heapMaxBlock >> 8) & 0xFFU);
  payload[6] = lrslog::heapFragPercent();
  payload[7] = relayFeedbackState();
  payload[8] = localDryContactState();
  uint32_t uptimeMinutes = millis() / 60000UL;
  if (uptimeMinutes > 0xFFFFUL) uptimeMinutes = 0xFFFFUL;
  payload[9] = static_cast<uint8_t>(uptimeMinutes & 0xFFU);
  payload[10] = static_cast<uint8_t>((uptimeMinutes >> 8) & 0xFFU);
  payload[11] = 0;

  last_counter_++;
  if (!radio_->sendRaw(MessageType::MaintenanceStatus, last_counter_,
                       runtime_.local_address, dstAddress, payload)) {
    return false;
  }
  last_tx_ms_ = millis();
  last_maint_page_tx_ms_ = last_tx_ms_;
  markRadioTxSentThisTick();
  lrslog::event("maint_debug_status_tx", 0, last_counter_, dstAddress);
  return true;
}

void NodeStateMachine::tickPendingMaintenancePages() {
  if (prov_.active || prov_.pause_normal_tx) return;
  if (ota_pull_active_ || (ota_silence_until_ms_ != 0 && millis() < ota_silence_until_ms_)) return;
  if (maintenance_version_pending_) {
    if (millis() - last_maint_page_tx_ms_ >= kMaintenancePageGapMs) {
      if (sendMaintenanceVersionStatus(maintenance_version_dst_)) {
        maintenance_version_pending_ = false;
        maintenance_version_dst_ = 0;
      }
    }
    return;
  }

  if (maintenance_sensor_pending_) {
    if (millis() - last_maint_page_tx_ms_ >= kMaintenancePageGapMs) {
      if (sendMaintenanceSensorStatus(maintenance_sensor_dst_)) {
        maintenance_sensor_pending_ = false;
        maintenance_sensor_dst_ = 0;
      }
    }
    return;
  }

  if (!maintenance_debug_pending_) return;
  if (!runtime_.maintenance_debug_telemetry_enabled) {
    maintenance_debug_pending_ = false;
    maintenance_debug_dst_ = 0;
    return;
  }
  if (millis() - last_maint_page_tx_ms_ >= kMaintenancePageGapMs) {
    if (sendMaintenanceDebugStatus(maintenance_debug_dst_)) {
      maintenance_debug_pending_ = false;
      maintenance_debug_dst_ = 0;
    }
  }
}

bool NodeStateMachine::handleMaintenanceStatus(const ProtocolMessage &msg) {
  if (!runtime_.role_tx) return false;
  if (settings_ != nullptr && settings_->mode == "paired" && !isConfiguredOperationalPeer(msg.src)) {
    return false;
  }
  PeerRuntime *node = findOrCreatePeer(msg.src);
  if (node == nullptr) return false;
  if (node->uptime_ms > 0 && millis() - node->uptime_received_ms >= 5000) {
    node->uptime_ms = 0;
  }
  const uint8_t *p = msg.raw_payload;
  if (p[0] != kMaintenancePayloadVersion) {
    lrslog::event("maint_status_unsupported", msg.rssi, msg.counter, p[0]);
    return false;
  }
  if (p[1] == kMaintenancePageIdentity) {
    const uint8_t flags = p[2];
    const uint32_t reportedChipId = static_cast<uint32_t>(p[3]) |
                                    (static_cast<uint32_t>(p[4]) << 8) |
                                    (static_cast<uint32_t>(p[5]) << 16);
    if (reportedChipId != 0) {
      for (size_t i = 0; i < peer_count_; ++i) {
        if (!peers_[i].in_use || peers_[i].address == msg.src ||
            peers_[i].chip_id != reportedChipId) {
          continue;
        }
        lrslog::event("peer_identity_moved", msg.rssi, peers_[i].address, msg.src);
        removePeerAt(i);
        node = findOrCreatePeer(msg.src);
        if (node == nullptr) return false;
        break;
      }
      rememberProvisionedAddress(reportedChipId, msg.src);
    }
    node->chip_id = reportedChipId;
    const uint8_t nextMajor = static_cast<uint8_t>((p[6] >> 4) & 0x0FU);
    const uint8_t nextMinor = static_cast<uint8_t>(p[6] & 0x0FU);
    const uint8_t nextPatch = p[7];
    if (node->fw_major != nextMajor || node->fw_minor != nextMinor || node->fw_patch != nextPatch) {
      node->fw_build = 0;
    }
    node->fw_major = nextMajor;
    node->fw_minor = nextMinor;
    node->fw_patch = nextPatch;
    node->wifi_state_known = true;
    node->wifi_enabled = (flags & 0x01U) != 0U;
    node->wifi_connected_known = true;
    node->wifi_connected = (flags & 0x02U) != 0U;
    node->mqtt_state_known = true;
    node->mqtt_enabled = (flags & 0x04U) != 0U;
    node->mqtt_connected = (flags & 0x08U) != 0U;
    node->power_save_listen_only = (flags & 0x20U) != 0U;
    node->power_save_active = (flags & 0x40U) != 0U;
    memcpy(node->ip, p + 8, sizeof(node->ip));
  } else if (p[1] == kMaintenancePageVersion) {
    node->fw_major = p[2];
    node->fw_minor = p[3];
    node->fw_patch = p[4];
    node->fw_build = decodeU16LE(p + 5);
    node->uptime_ms = decodeU32LE(p + 7);
    node->uptime_received_ms = millis();
    lrslog::event("maint_version_rx", msg.rssi, msg.counter, node->address);
  } else if (p[1] == kMaintenancePageSensors) {
    const uint8_t pageIdx = p[2];
    if (pageIdx == 0) {
      node->sensors.clear();
    }

    auto unpackReading = [](const uint8_t *src, SensorReading &out) -> bool {
      uint8_t kindVal = src[0] >> 4;
      uint8_t instVal = src[0] & 0x0F;
      if (kindVal == 0) return false;
      out.kind = static_cast<SensorKind>(kindVal);
      out.instance = instVal;
      out.state = static_cast<SensorState>(src[1] >> 4);
      out.scale = src[1] & 0x0F;
      int16_t val16 = static_cast<int16_t>(src[2] | (static_cast<uint16_t>(src[3]) << 8));
      out.value = val16;
      return true;
    };

    SensorReading rA{}, rB{};
    if (unpackReading(p + 4, rA)) {
      node->sensors.upsert(rA);
    }
    if (unpackReading(p + 8, rB)) {
      node->sensors.upsert(rB);
    }
    node->sensors_updated_ms = millis();

    SensorReading dc{};
    if (node->sensors.find(SensorKind::DryContact, 0, dc)) {
      if (dc.state == SensorState::Ok) {
        node->input_state = dc.value ? 1 : 0;
        node->input_state_known = true;
        node->input_feedback = node->input_state;
      }
    }
  } else if (p[1] == kMaintenancePageDebug) {
    node->maintenance_debug_known = true;
    node->heap_free = static_cast<uint32_t>(p[2]) |
                      (static_cast<uint32_t>(p[3]) << 8);
    node->heap_max_block = static_cast<uint32_t>(p[4]) |
                           (static_cast<uint32_t>(p[5]) << 8);
    node->heap_frag_pct = p[6];
    node->relay_feedback = p[7] ? 1 : 0;
    node->input_feedback = p[8] ? 1 : 0;
    node->debug_uptime_ms = (static_cast<uint32_t>(p[9]) |
                             (static_cast<uint32_t>(p[10]) << 8)) * 60000UL;
  } else {
    lrslog::event("maint_page_unsupported", msg.rssi, msg.counter, p[1]);
    return false;
  }
  node->uplink_rssi = msg.rssi;
  node->last_seen_ms = millis();
  node->last_cmd_counter = msg.counter;
  lrslog::event("maint_status_rx", msg.rssi, msg.counter, msg.src);
  return true;
}

void NodeStateMachine::tickPeerMqttCommands(uint32_t now) {
  if (!runtime_.role_tx) return;
  for (size_t i = 0; i < peer_count_; ++i) {
    PeerRuntime &node = peers_[i];
    if (!node.in_use || !node.pending) continue;

    if (static_cast<int32_t>(now - node.pending_deadline_ms) >= 0) {
      node.pending = false;
      node.ack_state = PeerAckState::Timeout;
      {
        lrslog::event("mqtt_remote_ack_timeout", 0, node.pending_counter, node.pending_relay);
      }
      continue;
    }

    if (static_cast<int32_t>(now - node.next_retry_ms) < 0) {
      continue;
    }
    if (isGroupActive()) {
      continue;
    }
    if (!radioTxBudgetAvailable()) {
      return;
    }

    uint32_t sentCounter = 0;
    if (sendPeerMqttCommand(node.address, node.pending_relay, &sentCounter)) {
      const uint8_t idx = node.retry_step < (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0]))
                              ? node.retry_step
                              : (sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1;
      node.next_retry_ms = now + kMqttRetryScheduleMs[idx];
      if (node.retry_step < ((sizeof(kMqttRetryScheduleMs) / sizeof(kMqttRetryScheduleMs[0])) - 1)) {
        node.retry_step++;
      }
      node.pending_counter = sentCounter;
      node.last_cmd_counter = sentCounter;
      node.ack_state = PeerAckState::Pending;
    } else {
      lrslog::event("mqtt_remote_retry_send_fail", 0, node.pending_counter, node.pending_relay);
    }
  }
}

void NodeStateMachine::tickPeerPolling(uint32_t now) {
  if (!runtime_.role_tx) return;
  if (!runtime_.tx_mqtt_remote_polling_enabled) return;
  if (poll_states_ == nullptr) {
    if (!ensurePollStorage()) return;
  }
  const uint32_t pollResponseDeadlineMs = (runtime_.ack_timeout_ms >= 2000U) ? runtime_.ack_timeout_ms : 2000U;
  for (size_t i = 0; i < peer_count_; ++i) {
    PeerRuntime &node = peers_[i];
    PollRuntime &poll = poll_states_[i];
    if (!node.in_use || node.poll_interval_ms == 0) continue;

    if (poll.poll_pending && static_cast<int32_t>(now - poll.poll_deadline_ms) >= 0) {
      poll.poll_pending = false;
      lrslog::event("tx_poll_timeout", 0, poll.poll_counter, 0);
      poll.next_poll_ms = now + node.poll_interval_ms;
    }

    // Keep exactly one in-flight poll per node to avoid overlap ambiguity.
    if (poll.poll_pending) {
      continue;
    }

    if (static_cast<int32_t>(now - poll.next_poll_ms) < 0) {
      continue;
    }
    if (isGroupActive()) {
      continue;
    }
    if (!radioTxBudgetAvailable()) {
      return;
    }

    uint32_t sentCounter = 0;
    if (sendPollRequest(node.address, &sentCounter)) {
      poll.poll_pending = true;
      poll.poll_counter = sentCounter;
      poll.poll_deadline_ms = now + pollResponseDeadlineMs;
      poll.last_poll_tx_ms = now;
      poll.next_poll_ms = now + node.poll_interval_ms;
    } else {
      // Retry soon if radio send fails.
      poll.next_poll_ms = now + 1000U;
    }
  }
}

void NodeStateMachine::tickPeerMaintenance(uint32_t now) {
  if (!runtime_.role_tx || settings_ == nullptr || fleet_scan_active_) return;
  if (static_cast<int32_t>(now - next_peer_maintenance_ms_) < 0) return;
  if (isGroupActive()) return;
  if (!radioTxBudgetAvailable()) return;

  uint8_t targets[Settings::kAddressListCap]{};
  const bool isPairedMode = (settings_->mode == "paired");

  uint8_t targetCount = runtime_utils::resolveGatewayTargets(
    isPairedMode,
    runtime_.local_address,
    settings_->known_peer_count,
    settings_->known_peer_addresses,
    settings_->paired_target_count,
    settings_->paired_target_addresses,
    settings_->remote_address,
    targets,
    Settings::kAddressListCap
  );

  if (targetCount == 0) return;

  // 2. Dynamically calculate staggering spacing over the full configured cycle
  uint32_t spacingMs = runtime_.heartbeat_ms / targetCount;
  if (spacingMs < 2000UL) {
    spacingMs = 2000UL; // Safe lower bound to prevent radio flooding
  }

  // 3. Query the next peer in round-robin fashion
  if (peer_maintenance_cursor_ >= targetCount) peer_maintenance_cursor_ = 0;
  const uint8_t dst = targets[peer_maintenance_cursor_++];
  uint32_t sentCounter = 0;
  if (sendMaintenanceRequest(dst, &sentCounter)) {
    lrslog::event("peer_maint_probe", 0, sentCounter, dst);
    next_peer_maintenance_ms_ = now + spacingMs;
  } else {
    // If radio fails, retry in 2 seconds
    next_peer_maintenance_ms_ = now + 2000UL;
  }
}

void NodeStateMachine::tickFleetScan(uint32_t now) {
  if (!runtime_.role_tx) {
    fleet_scan_active_ = false;
    fleet_scan_next_ms_ = 0;
    return;
  }
  if (!fleet_scan_active_) return;
  if (static_cast<int32_t>(now - fleet_scan_next_ms_) < 0) return;
  if (isGroupActive()) return;
  if (!radioTxBudgetAvailable()) return;
  if (fleet_scan_next_address_ < fleet_scan_start_address_ || fleet_scan_next_address_ > fleet_scan_end_address_) {
    fleet_scan_active_ = false;
    fleet_scan_next_ms_ = 0;
    lrslog::event("fleet_scan_done", 0, fleet_scan_sent_, 0);
    return;
  }

  uint32_t sentCounter = 0;
  if (!sendMaintenanceRequest(fleet_scan_next_address_, &sentCounter)) {
    fleet_scan_next_ms_ = now + 250U;
    return;
  }

  fleet_scan_sent_++;
  fleet_scan_last_tx_ms_ = now;
  lrslog::event("fleet_scan_probe", 0, sentCounter, fleet_scan_next_address_);
  fleet_scan_next_address_++;
  if (fleet_scan_next_address_ > fleet_scan_end_address_) {
    fleet_scan_active_ = false;
    fleet_scan_next_ms_ = 0;
    lrslog::event("fleet_scan_done", 0, fleet_scan_sent_, 0);
    return;
  }
  fleet_scan_next_ms_ = now + fleet_scan_interval_ms_;
}

void NodeStateMachine::tickTransmitter() {
  const uint32_t now = millis();

  if (prov_.active) {
    startupTxPhaseTrace("prov_coord_before");
    tickProvisioningCoordinator(now);
    return;
  }

  if (tx_ack_pending_ && static_cast<int32_t>(now - tx_ack_apply_ms_) >= 0) {
    relay_state_ = tx_ack_relay_state_;
    digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
    tx_ack_pending_ = false;
  }

  int inputLogical = static_cast<int>(localDryContactState());
  if (inputLogical != last_input_raw_) {
    last_debounce_ms_ = now;
    last_input_raw_ = inputLogical;
  }

  if ((now - last_debounce_ms_) > kDebounceMs && inputLogical != input_state_) {
    input_state_ = static_cast<uint8_t>(inputLogical);
    if (runtime_.input_control_paired_lora_enabled) {
      tx_state_sync_pending_ = false;
      resetTxGroupState();
      startTxGroupCommand(input_state_, input_state_);
    }
  }

  if (runtime_.input_control_paired_lora_enabled && tx_state_sync_pending_ &&
      (tx_group_phase_ == PairedGroupPhase::Idle || tx_group_phase_ == PairedGroupPhase::Complete)) {
    tx_state_sync_pending_ = false;
    startTxGroupCommand(input_state_, input_state_);
  }

  const bool groupActive = isGroupActive();

  if (groupActive) {
    tickTxGroupCommand(now);
  }

  if (!groupActive && runtime_.heartbeat_enabled && (now - last_heartbeat_ms_) >= runtime_.heartbeat_ms) {
    last_heartbeat_ms_ = now;
    if (runtime_.input_control_paired_lora_enabled) {
      resetTxGroupState();
      startTxGroupCommand(input_state_, input_state_);
      tickTxGroupCommand(now);
    } else {
      if (radioTxBudgetAvailable()) {
        last_counter_++;
        const uint32_t unixTimeS = currentUnixTimeS(now);
        if (radio_->send(MessageType::Heartbeat, input_state_, input_state_, txFlags(), last_counter_, runtime_.local_address,
                         runtime_.remote_address,
                         localTempCodeToSend(), 0, 0xFF, 0xFFFF, unixTimeS)) {
          last_tx_ms_ = now;
          markRadioTxSentThisTick();
          {
            lrslog::event("tx_heartbeat", 0, last_counter_, input_state_);
          }
        }
      }
    }
  }

  tickPendingOtaPullControl(now);
  tickPeerMqttCommands(now);
  tickPeerPolling(now);
  tickPeerMaintenance(now);
  tickFleetScan(now);
  startupTxPhaseTrace("after_peer_polling");

  if (link_state_ == LinkState::WaitAck && (now - wait_ack_since_ms_) >= runtime_.ack_timeout_ms) {
    relay_state_ = 0;
    digitalWrite(kRelayPin, LOW);
    link_state_ = LinkState::Timeout;
    {
      lrslog::event("tx_ack_timeout", 0, last_counter_, relay_state_);
    }
  }
}

void NodeStateMachine::tickReceiver() {
  const uint32_t now = millis();
  applyReceiverFailsafe(now);
  tickDeferredAck(now);
  int inputLogical = static_cast<int>(localDryContactState());
  if (inputLogical != last_input_raw_) {
    last_debounce_ms_ = now;
    last_input_raw_ = inputLogical;
  }

  if ((now - last_debounce_ms_) > kDebounceMs && inputLogical != input_state_) {
    input_state_ = static_cast<uint8_t>(inputLogical);
    rx_push_pending_ = true;
  }

  if (!runtime_.rx_push_on_change_enabled || !rx_push_pending_) {
    return;
  }
  if (runtime_.remote_address == 0 || runtime_.remote_address == 255) {
    return;
  }

  const uint32_t minIntervalMs = runtime_.rx_push_min_interval_ms < 60000U ? 60000U : runtime_.rx_push_min_interval_ms;
  const bool firstPush = (rx_last_push_ms_ == 0);
  if (!firstPush && (now - rx_last_push_ms_) < minIntervalMs) {
    return;
  }
  if (!radioTxBudgetAvailable()) {
    return;
  }

  last_counter_++;
  const uint32_t unixTimeS = currentUnixTimeS(now);
  if (radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), txFlags(), last_counter_, runtime_.local_address,
                   runtime_.remote_address, localTempCodeToSend(), 0, 0xFF, 0xFFFF, unixTimeS)) {
    last_tx_ms_ = now;
    markRadioTxSentThisTick();
    rx_last_push_ms_ = now;
    rx_push_pending_ = false;
    lrslog::event("rx_push_on_change", 0, last_counter_, input_state_);
  }
}

void NodeStateMachine::tickReceive() {
  ProtocolMessage msg{};
  if (!radio_->receive(msg)) return;

  const bool isProvisioning = (msg.type == MessageType::Provisioning);
  if (msg.src == 0 || msg.src == 255 || (!isProvisioning && msg.src == runtime_.local_address)) {
    lrslog::event("rx_invalid_source", msg.rssi, msg.counter, msg.src);
    return;
  }

  if (isProvisioning && msg.src == runtime_.local_address) {
    addProvLog("Bypassed self-source check: src=%u type=%u", msg.src, static_cast<unsigned>(msg.type));
  }

  const bool isWifiProvision = (msg.type == MessageType::WifiProvision);
  const bool isWifiControl = (msg.type == MessageType::WifiControl);
  const bool isUdpLogControl = (msg.type == MessageType::UdpLogControl);
  const bool isOtaPullControl = (msg.type == MessageType::OtaPullControl);
  const bool isFactoryReset = (msg.type == MessageType::FactoryReset);
  const bool isMaintenance = (msg.type == MessageType::MaintenanceRequest ||
                              msg.type == MessageType::MaintenanceStatus);
  if (isProvisioning) {
    handleProvisioningFrame(msg);
    return;
  }
  const bool isChange = (msg.type == MessageType::Change);
  if (!isWifiProvision && !isWifiControl && !isUdpLogControl && !isOtaPullControl &&
      !isMaintenance && !isChange && msg.dst != runtime_.local_address) {
    lrslog::event("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  if (isChange && msg.dst != runtime_.local_address && msg.dst != 255) {
    lrslog::event("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  if (isWifiProvision && msg.dst != runtime_.local_address && msg.dst != kWifiProvisionBroadcastAddress) {
    lrslog::event("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  if (isWifiControl && msg.dst != runtime_.local_address && msg.dst != kWifiControlBroadcastAddress) {
    lrslog::event("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }
  const bool isReboot = (msg.type == MessageType::Reboot);
  const bool isSensorConfig = (msg.type == MessageType::SensorConfig);
  const bool isFleetKeyControl = (msg.type == MessageType::FleetKeyControl);
  if ((isUdpLogControl || isOtaPullControl || isFactoryReset || isReboot || isSensorConfig || isFleetKeyControl) && msg.dst != runtime_.local_address) {
    lrslog::event("rx_wrong_address", msg.rssi, msg.counter, msg.relay_state);
    return;
  }

  if (!isWifiProvision && !isUdpLogControl && !isOtaPullControl && !isFleetKeyControl && runtime_.role_tx) {
    const bool fromPaired = isPairedTargetAddress(msg.src);
    const bool mqttStatus = (msg.type == MessageType::MqttStatus);
    const bool pollResponse = (msg.type == MessageType::PollResponse);
    const bool wifiStatus = (msg.type == MessageType::WifiControl && msg.relay_state == kWifiControlOpStatus);
    const bool heartbeat = (msg.type == MessageType::Heartbeat);
    const bool change = (msg.type == MessageType::Change);
    const bool isAck = (msg.type == MessageType::Ack);
    if (!mqttStatus && !pollResponse && !wifiStatus && !isMaintenance && !heartbeat && !change && !isAck && !fromPaired) {
      lrslog::event("rx_wrong_source", msg.rssi, msg.counter, msg.relay_state);
      return;
    }
  } else if (!isWifiProvision) {
    if (msg.type == MessageType::Mqtt) {
      if (!runtime_.mqtt_control_enabled) {
        lrslog::event("rx_mqtt_control_disabled", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      if (!isAuthorizedMqttController(msg.src)) {
        lrslog::event("rx_mqtt_unauthorized_source", msg.rssi, msg.counter, msg.src);
        return;
      }
    } else if (msg.type == MessageType::PollRequest) {
      // Allow fleet scans from any same-key TX even when this RX is paired to a different remote source.
    } else if (msg.type == MessageType::MaintenanceRequest) {
      // Allow bounded same-key maintenance inventory without exposing secrets.
    } else if (msg.type == MessageType::WifiControl) {
      // Same-key broadcast/targeted WiFi control is accepted so a TX can recover
      // or disable managed remotes even when pairing is being reworked.
    } else if (msg.type == MessageType::UdpLogControl) {
      // Targeted same-key diagnostics are accepted only to toggle UDP mirroring
      // on remotes that already have WiFi. It is not a general remote shell.
    } else if (msg.type == MessageType::OtaPullControl) {
      // Targeted same-key OTA pull only carries the temporary firmware server
      // endpoint; the device still downloads the binary over WiFi.
    } else if (msg.type == MessageType::FactoryReset) {
      // Same-key factory reset is allowed.
    } else if (msg.type == MessageType::Reboot) {
      // Same-key remote reboot.
    } else if (msg.type == MessageType::SensorConfig) {
      // Same-key remote sensor config.
    } else if (msg.type == MessageType::FleetKeyControl) {
      // Same-key targeted Fleet Key Control.
    } else if ((msg.type == MessageType::Change || msg.type == MessageType::Heartbeat) && msg.src == 254) {
      // Same-key gateway fleet control remains valid even if an older or
      // manually recovered receiver has stale controller-pairing metadata.
    } else if (!isAuthorizedPairedSource(msg.src)) {
      lrslog::event("rx_filtered_source", msg.rssi, msg.counter, msg.relay_state);
      return;
    }
  }

  const bool trustedReplaySource = isTrustedReplaySource(msg.src, isWifiProvision || isWifiControl || isUdpLogControl ||
                                                                  isOtaPullControl || isFactoryReset || isMaintenance ||
                                                                  isReboot || isSensorConfig || isFleetKeyControl);
  if (!shouldAcceptReplayAndUpdate(msg, trustedReplaySource)) {
    return;
  }
  last_packet_ms_ = millis();
  last_packet_rssi_ = msg.rssi;
  if (isWifiProvision) {
    handleWifiProvisionFrame(msg);
    return;
  }
  if (isWifiControl) {
    handleWifiControlFrame(msg);
    return;
  }
  if (isUdpLogControl) {
    handleUdpLogControlFrame(msg);
    return;
  }
  if (isOtaPullControl) {
    handleOtaPullControlFrame(msg);
    return;
  }
  if (isFactoryReset) {
    handleFactoryResetFrame(msg);
    return;
  }
  if (isReboot) {
    handleRebootFrame(msg);
    return;
  }
  if (isSensorConfig) {
    handleSensorConfigFrame(msg);
    return;
  }
  if (isFleetKeyControl) {
    handleFleetKeyControlFrame(msg);
    return;
  }
  if (msg.type == MessageType::MaintenanceStatus) {
    handleMaintenanceStatus(msg);
    return;
  }
  if (msg.type == MessageType::Heartbeat || msg.type == MessageType::PollResponse || msg.type == MessageType::MqttStatus) {
    updateSharedTimeFromPeer(msg.unix_time_s, (msg.flags & kFlagTimeAuthoritative) != 0U);
  }

  if (runtime_.role_tx) {
    PeerRuntime *node = nullptr;
    if (settings_ == nullptr || settings_->mode != "paired" || isConfiguredOperationalPeer(msg.src)) {
      node = findOrCreatePeer(msg.src);
    }
    if (node != nullptr) {
      node->uplink_rssi = msg.rssi;
      node->last_seen_ms = millis();

      const bool statusCarriesState = (msg.type == MessageType::Heartbeat ||
                                       msg.type == MessageType::PollResponse ||
                                       msg.type == MessageType::MqttStatus);
      if (statusCarriesState) {
        node->relay_state = msg.relay_state ? 1 : 0;
        node->uptime_ms = 0;
        node->debug_uptime_ms = 0;
        // Prefer explicit digital sensor payload when present; fallback to legacy input byte.
        const bool digitalPresent = (msg.sensor_mask & 0x01U) != 0U;
        if (digitalPresent && msg.sensor_digital0 != 0xFFU) {
          node->input_state = (msg.sensor_digital0 != 0U) ? 1 : 0;
        } else {
          node->input_state = msg.input_state ? 1 : 0;
        }
        node->input_state_known = true;

        if (msg.temp_code != 0xFF) {
          SensorReading tempReading{};
          tempReading.kind = SensorKind::TemperatureC;
          tempReading.instance = 0;
          tempReading.state = SensorState::Ok;
          tempReading.value = static_cast<int8_t>(msg.temp_code);
          tempReading.scale = 0;
          node->sensors.upsert(tempReading);
        }

        if ((msg.sensor_mask & 0x04U) != 0U) {
          node->downlink_rssi_valid = true;
          node->downlink_rssi = static_cast<int>(static_cast<int16_t>(msg.sensor_analog0));
        }

        if ((msg.sensor_mask & 0x08U) != 0U && msg.sensor_digital0 != 0xFFU) {
          node->wifi_state_known = true;
          node->wifi_enabled = (msg.sensor_digital0 != 0U);
          node->wifi_last_confirm_ms = millis();
        }
      }
    }

    if (msg.type == MessageType::Ack) {
      if (!ackMatchesPendingCommand(msg)) {
        if (!tx_command_pending_) {
          // Heartbeat/state-sync ACKs from the paired RX are still proof that
          // the link is alive, even when no command-confirmation ACK is
          // currently outstanding.
          link_state_ = LinkState::Idle;
          lrslog::event("tx_ack_alive", msg.rssi, msg.counter, msg.relay_state);
        } else {
          LRS_LOGW(LORA,
                   "event=tx_ack_counter_mismatch acked=%lu pending=%lu rx_counter=%lu",
                   static_cast<unsigned long>(msg.unix_time_s),
                   static_cast<unsigned long>(tx_pending_command_counter_),
                   static_cast<unsigned long>(msg.counter));
        }
        return;
      }
      updatePeerAckStatus(msg.src, msg.relay_state, msg.input_state, PeerAckState::Ok, msg.rssi);
      const uint8_t idx = txGroupTargetIndexForAddress(msg.src);
      if (idx != 0xFF) {
        tx_group_acked_bitmap_ |= (1UL << idx);
        lrslog::event("tx_alln_ack_rx", msg.rssi, tx_group_command_id_, msg.src);
        LRS_LOGI(LORA,
                 "event=tx_alln_ack_rx phase=%s command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=%u",
                 (tx_group_phase_ == PairedGroupPhase::PollMissingSequential) ? "poll_once" : "initial_window",
                 static_cast<unsigned long>(tx_group_command_id_),
                 static_cast<unsigned>(tx_group_target_count_),
                 static_cast<unsigned long>(tx_group_expected_bitmap_),
                 static_cast<unsigned long>(tx_group_acked_bitmap_),
                 static_cast<unsigned long>(txGroupMissingBitmap()),
                 static_cast<unsigned>(msg.src));
      }
      if (tx_group_phase_ == PairedGroupPhase::AwaitInitialAcks || tx_group_phase_ == PairedGroupPhase::PollMissingSequential) {
        if (!txGroupHasMissingTargets()) {
          finishTxGroupSuccess();
        }
      } else {
        tx_ack_pending_ = true;
        tx_ack_apply_ms_ = millis() + kTxRelayEchoDelayMs;
        tx_ack_relay_state_ = msg.relay_state;
        tx_command_pending_ = false;
        tx_pending_command_counter_ = 0;
        tx_retry_step_ = 0;
        tx_next_retry_ms_ = 0;
        tx_command_retry_deadline_ms_ = 0;
        link_state_ = LinkState::Idle;
        lrslog::event("tx_ack", msg.rssi, msg.counter, msg.relay_state);
      }
      return;
    }

    if (msg.type == MessageType::MqttStatus || msg.type == MessageType::PollResponse) {
      if (confirmProvisioningByFleetResponse(msg)) {
        return;
      }
      PeerRuntime *node = nullptr;
      if (settings_ == nullptr || settings_->mode != "paired" || isConfiguredOperationalPeer(msg.src)) {
        node = findOrCreatePeer(msg.src);
      }
      if (node == nullptr) {
        lrslog::event("mqtt_remote_node_limit", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      node->relay_state = msg.relay_state ? 1 : 0;
      node->uptime_ms = 0;
      node->debug_uptime_ms = 0;
      // Prefer explicit digital sensor payload when present; fallback to legacy input byte.
      const bool digitalPresent = (msg.sensor_mask & 0x01U) != 0U;
      if (digitalPresent && msg.sensor_digital0 != 0xFFU) {
        node->input_state = (msg.sensor_digital0 != 0U) ? 1 : 0;
      } else {
        node->input_state = msg.input_state ? 1 : 0;
      }
      node->input_state_known = true;
      node->uplink_rssi = msg.rssi;
      node->last_seen_ms = millis();
      node->last_cmd_counter = msg.counter;
      if (msg.temp_code != 0xFF) {
        SensorReading tempReading{};
        tempReading.kind = SensorKind::TemperatureC;
        tempReading.instance = 0;
        tempReading.state = SensorState::Ok;
        tempReading.value = static_cast<int8_t>(msg.temp_code);
        tempReading.scale = 0;
        node->sensors.upsert(tempReading);
      }
      if ((msg.sensor_mask & 0x04) != 0) {
        node->downlink_rssi_valid = true;
        node->downlink_rssi = static_cast<int>(static_cast<int16_t>(msg.sensor_analog0));
      }
      if ((msg.sensor_mask & 0x08) != 0 && msg.sensor_digital0 != 0xFFU) {
        node->wifi_state_known = true;
        node->wifi_enabled = (msg.sensor_digital0 != 0U);
        node->wifi_last_confirm_ms = millis();
      }
      if (msg.type == MessageType::MqttStatus) {
        // Retries can overlap and responses may arrive out of order.
        // Any valid status from this node confirms link health and should unblock polling.
        if (node->pending) {
          node->pending = false;
          lrslog::event("mqtt_remote_ack_ok", msg.rssi, msg.counter, msg.relay_state);
        }
        node->ack_state = PeerAckState::Ok;
        lrslog::event("mqtt_remote_status_rx", msg.rssi, msg.counter, msg.relay_state);
      } else {
        // Clear pending on any valid response from this node; retries can overlap counters.
        PollRuntime *poll = pollStateForIndex(static_cast<size_t>(node - peers_));
        if (poll != nullptr) {
          poll->poll_pending = false;
          poll->next_poll_ms = millis() + node->poll_interval_ms;
        }
        lrslog::event("tx_poll_response", msg.rssi, msg.counter, msg.relay_state);
        
        // Non-actuating confirmation for PollMissingSequential phase
        if (tx_group_phase_ == PairedGroupPhase::PollMissingSequential && msg.unix_time_s == tx_group_command_id_) {
          if (msg.relay_state == tx_group_desired_relay_state_) {
            const uint8_t idx = txGroupTargetIndexForAddress(msg.src);
            if (idx != 0xFF) {
              tx_group_acked_bitmap_ |= (1UL << idx);
              lrslog::event("tx_alln_poll_ok", msg.rssi, tx_group_command_id_, msg.src);
              LRS_LOGI(LORA,
                       "event=tx_alln_poll_ok phase=poll_once command_id=%lu target_count=%u expected_bitmap=0x%08lx acked_bitmap=0x%08lx missing_bitmap=0x%08lx addr=%u",
                       static_cast<unsigned long>(tx_group_command_id_),
                       static_cast<unsigned>(tx_group_target_count_),
                       static_cast<unsigned long>(tx_group_expected_bitmap_),
                       static_cast<unsigned long>(tx_group_acked_bitmap_),
                       static_cast<unsigned long>(txGroupMissingBitmap()),
                       static_cast<unsigned>(msg.src));
              if (!txGroupHasMissingTargets()) {
                finishTxGroupSuccess();
              }
            }
          }
        }
      }
    }
    return;
  }

  if (msg.type == MessageType::PollRequest) {
    if (!radioTxBudgetAvailable()) {
      return;
    }
    const uint8_t sensorMask = 0x0C;  // downlink RSSI + WiFi enabled state
    const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
    const uint8_t wifiState = (settings_ == nullptr || settings_->wifi_admin_enabled) ? 1U : 0U;
    
    const bool isGroupPoll = (msg.flags & kFlagGroupPollCorrelation) != 0U;
    uint32_t replyUnixTime;
    uint8_t replyFlags = txFlags();
    if (isGroupPoll) {
      // Correlated echo path for group poll: echo command ID and clear authoritative flag to prevent feeding random IDs to time sync
      replyUnixTime = msg.unix_time_s;
      replyFlags &= ~kFlagTimeAuthoritative;
    } else {
      replyUnixTime = currentUnixTimeS(millis());
    }

    last_counter_++;
    if (radio_->send(MessageType::PollResponse, relay_state_, localDryContactState(), replyFlags, last_counter_, runtime_.local_address,
                     msg.src,
                     localTempCodeToSend(), sensorMask, wifiState, downlinkRssiEnc, replyUnixTime)) {
      last_tx_ms_ = millis();
      markRadioTxSentThisTick();
      {
        lrslog::event("rx_poll_response_tx", msg.rssi, last_counter_, relay_state_);
      }
    }
    return;
  }
  if (msg.type == MessageType::MaintenanceRequest) {
    if (msg.dst == runtime_.local_address) {
      sendMaintenanceStatus(msg.src);
    }
    return;
  }

  if (msg.type == MessageType::Change || msg.type == MessageType::Heartbeat || msg.type == MessageType::Mqtt) {
    if (!runtime_.role_tx && msg.type == MessageType::Mqtt && paired_input_slave_mode_) {
      lrslog::event("rx_slave_block_mqtt", msg.rssi, msg.counter, msg.relay_state);
      return;
    }
    if (!runtime_.role_tx && msg.type != MessageType::Mqtt) {
      const bool nextSlaveMode = (msg.flags & kFlagPairedInputSlave) != 0U;
      if (nextSlaveMode != paired_input_slave_mode_) {
        paired_input_slave_mode_ = nextSlaveMode;
        lrslog::event(paired_input_slave_mode_ ? "rx_slave_mode_on" : "rx_slave_mode_off", msg.rssi, msg.counter,
                      msg.relay_state);
      }
    }
    relay_state_ = msg.relay_state;
    input_state_ = msg.input_state;
    last_rx_control_ms_ = millis();
    last_rx_control_source_ = (msg.type == MessageType::Mqtt) ? RxControlSource::Mqtt : RxControlSource::LoRa;
    digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
    if (msg.type != MessageType::Mqtt) {
      const uint32_t commandId = (msg.type == MessageType::Change) ? msg.unix_time_s : msg.counter;
      scheduleDeferredAck(msg.src, relay_state_, input_state_, commandId);
    } else {
      if (!radioTxBudgetAvailable()) {
        lrslog::event("rx_apply_no_status_budget", msg.rssi, msg.counter, msg.relay_state);
        return;
      }
      const uint8_t sensorMask = 0x0C;  // downlink RSSI + WiFi enabled state
      const uint16_t downlinkRssiEnc = static_cast<uint16_t>(static_cast<int16_t>(msg.rssi));
      const uint8_t wifiState = (settings_ == nullptr || settings_->wifi_admin_enabled) ? 1U : 0U;
      const uint32_t unixTimeS = currentUnixTimeS(millis());
      last_counter_++;
      if (radio_->send(MessageType::MqttStatus, relay_state_, localDryContactState(), txFlags(), last_counter_, runtime_.local_address,
                       msg.src, localTempCodeToSend(), sensorMask, wifiState, downlinkRssiEnc, unixTimeS)) {
        last_tx_ms_ = millis();
        markRadioTxSentThisTick();
      }
    }

    {
      lrslog::event(msg.type == MessageType::Mqtt ? "rx_apply_mqtt" : "rx_apply_and_ack", msg.rssi, msg.counter, msg.relay_state);
    }
  }
}

bool NodeStateMachine::handleWifiProvisionFrame(const ProtocolMessage &msg) {
  const uint8_t op = msg.relay_state;
  const uint8_t transferId = msg.input_state;
  const uint8_t chunkIndex = msg.flags;
  const uint8_t totalChunks = msg.temp_code;
  const uint8_t valueLen = msg.sensor_mask;
  uint8_t bytes[7]{};
  fillProvisionPayloadBytes(msg, bytes);

  if (op == kWifiProvisionOpStart) {
    const uint8_t ssidLen = valueLen;
    const uint8_t passLen = bytes[0];
    const size_t totalLen = static_cast<size_t>(ssidLen) + static_cast<size_t>(passLen);
    const uint32_t expectedHash = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                                  (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24);
    const uint8_t expectedChunks =
        static_cast<uint8_t>((totalLen + (kWifiProvisionChunkDataBytes - 1U)) / kWifiProvisionChunkDataBytes);
    if (transferId == 0 || ssidLen == 0 || ssidLen > 32 || passLen > 64 || totalLen == 0 || totalLen > sizeof(wifi_prov_rx_.data) ||
        totalChunks == 0 || totalChunks > 31 || totalChunks != expectedChunks) {
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      lrslog::event("wifi_prov_rx_bad_start", msg.rssi, msg.counter, op);
      return false;
    }
    wifi_prov_rx_ = WifiProvisionRxTransfer{};
    wifi_prov_rx_.active = true;
    wifi_prov_rx_.src = msg.src;
    wifi_prov_rx_.transfer_id = transferId;
    wifi_prov_rx_.total_chunks = totalChunks;
    wifi_prov_rx_.ssid_len = ssidLen;
    wifi_prov_rx_.pass_len = passLen;
    wifi_prov_rx_.expected_hash = expectedHash;
    lrslog::event("wifi_prov_rx_start", msg.rssi, msg.counter, totalChunks);
    return true;
  }

  if (!wifi_prov_rx_.active || wifi_prov_rx_.src != msg.src || wifi_prov_rx_.transfer_id != transferId ||
      wifi_prov_rx_.total_chunks != totalChunks) {
    lrslog::event("wifi_prov_rx_orphan", msg.rssi, msg.counter, op);
    return false;
  }

  const size_t totalLen = static_cast<size_t>(wifi_prov_rx_.ssid_len) + static_cast<size_t>(wifi_prov_rx_.pass_len);
  if (op == kWifiProvisionOpData) {
    if (chunkIndex >= wifi_prov_rx_.total_chunks || valueLen > kWifiProvisionChunkDataBytes) {
      lrslog::event("wifi_prov_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    const size_t offset = static_cast<size_t>(chunkIndex) * kWifiProvisionChunkDataBytes;
    if (offset >= totalLen || (offset + valueLen) > totalLen) {
      lrslog::event("wifi_prov_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    memcpy(wifi_prov_rx_.data + offset, bytes, valueLen);
    wifi_prov_rx_.received_bitmap |= (1UL << chunkIndex);
    return true;
  }

  if (op == kWifiProvisionOpCommit) {
    const uint32_t wantBitmap = (1UL << wifi_prov_rx_.total_chunks) - 1UL;
    if ((wifi_prov_rx_.received_bitmap & wantBitmap) != wantBitmap) {
      lrslog::event("wifi_prov_rx_incomplete", msg.rssi, msg.counter, wifi_prov_rx_.total_chunks);
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      return false;
    }
    const uint32_t gotHash = fnv1a32(wifi_prov_rx_.data, totalLen);
    if (gotHash != wifi_prov_rx_.expected_hash) {
      lrslog::event("wifi_prov_rx_hash_fail", msg.rssi, msg.counter, 0);
      wifi_prov_rx_ = WifiProvisionRxTransfer{};
      return false;
    }
    char ssidBuf[33]{};
    char passBuf[65]{};
    memcpy(ssidBuf, wifi_prov_rx_.data, wifi_prov_rx_.ssid_len);
    memcpy(passBuf, wifi_prov_rx_.data + wifi_prov_rx_.ssid_len, wifi_prov_rx_.pass_len);
    wifi_prov_pending_ssid_ = String(ssidBuf);
    wifi_prov_pending_password_ = String(passBuf);
    wifi_prov_pending_src_ = wifi_prov_rx_.src;
    wifi_prov_pending_ = (wifi_prov_pending_ssid_.length() > 0);
    lrslog::event("wifi_prov_rx_ready", msg.rssi, msg.counter, wifi_prov_rx_.total_chunks);
    wifi_prov_rx_ = WifiProvisionRxTransfer{};
    return wifi_prov_pending_;
  }

  lrslog::event("wifi_prov_rx_unknown", msg.rssi, msg.counter, op);
  return false;
}

bool NodeStateMachine::handleFleetKeyControlFrame(const ProtocolMessage &msg) {
  const uint8_t op = msg.relay_state;
  const uint8_t transferId = msg.input_state;
  const uint8_t chunkIndex = msg.flags;
  const uint8_t totalChunks = msg.temp_code;
  const uint8_t valueLen = msg.sensor_mask;
  uint8_t bytes[7]{};
  fillProvisionPayloadBytes(msg, bytes);

  if (op == kFleetKeyControlOpStart) {
    const uint8_t keyLen = valueLen;
    const uint32_t expectedHash = static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                                  (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24);
    constexpr uint8_t kFleetKeyChunkDataBytes = 7;
    const uint8_t expectedChunks =
        static_cast<uint8_t>((keyLen + (kFleetKeyChunkDataBytes - 1U)) / kFleetKeyChunkDataBytes);
    if (transferId == 0 || keyLen < admin_config_utils::kMinDeploymentKeyLen || keyLen > 64 ||
        totalChunks == 0 || totalChunks > 31 || totalChunks != expectedChunks) {
      fleet_key_rx_ = FleetKeyControlRxTransfer{};
      lrslog::event("fleet_key_rx_bad_start", msg.rssi, msg.counter, op);
      return false;
    }
    fleet_key_rx_ = FleetKeyControlRxTransfer{};
    fleet_key_rx_.active = true;
    fleet_key_rx_.src = msg.src;
    fleet_key_rx_.transfer_id = transferId;
    fleet_key_rx_.total_chunks = totalChunks;
    fleet_key_rx_.key_len = keyLen;
    fleet_key_rx_.expected_hash = expectedHash;
    lrslog::event("fleet_key_rx_start", msg.rssi, msg.counter, totalChunks);
    return true;
  }

  if (!fleet_key_rx_.active || fleet_key_rx_.src != msg.src || fleet_key_rx_.transfer_id != transferId ||
      fleet_key_rx_.total_chunks != totalChunks) {
    lrslog::event("fleet_key_rx_orphan", msg.rssi, msg.counter, op);
    return false;
  }

  if (op == kFleetKeyControlOpData) {
    constexpr uint8_t kFleetKeyChunkDataBytes = 7;
    if (chunkIndex >= fleet_key_rx_.total_chunks || valueLen > kFleetKeyChunkDataBytes) {
      lrslog::event("fleet_key_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    const size_t offset = static_cast<size_t>(chunkIndex) * kFleetKeyChunkDataBytes;
    if (offset >= fleet_key_rx_.key_len || (offset + valueLen) > fleet_key_rx_.key_len) {
      lrslog::event("fleet_key_rx_bad_chunk", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    memcpy(fleet_key_rx_.data + offset, bytes, valueLen);
    fleet_key_rx_.received_bitmap |= (1UL << chunkIndex);
    return true;
  }

  if (op == kFleetKeyControlOpCommit) {
    const uint32_t wantBitmap = (1UL << fleet_key_rx_.total_chunks) - 1UL;
    if ((fleet_key_rx_.received_bitmap & wantBitmap) != wantBitmap) {
      lrslog::event("fleet_key_rx_incomplete", msg.rssi, msg.counter, fleet_key_rx_.total_chunks);
      fleet_key_rx_ = FleetKeyControlRxTransfer{};
      return false;
    }
    const uint32_t gotHash = fnv1a32(fleet_key_rx_.data, fleet_key_rx_.key_len);
    if (gotHash != fleet_key_rx_.expected_hash) {
      lrslog::event("fleet_key_rx_hash_fail", msg.rssi, msg.counter, 0);
      fleet_key_rx_ = FleetKeyControlRxTransfer{};
      return false;
    }
    char keyBuf[65]{};
    memcpy(keyBuf, fleet_key_rx_.data, fleet_key_rx_.key_len);
    fleet_key_pending_key_ = String(keyBuf);
    fleet_key_pending_src_ = fleet_key_rx_.src;
    fleet_key_pending_ = (fleet_key_pending_key_.length() > 0);
    lrslog::event("fleet_key_rx_ready", msg.rssi, msg.counter, fleet_key_rx_.total_chunks);
    fleet_key_rx_ = FleetKeyControlRxTransfer{};
    return fleet_key_pending_;
  }

  lrslog::event("fleet_key_rx_unknown", msg.rssi, msg.counter, op);
  return false;
}

bool NodeStateMachine::handleWifiControlFrame(const ProtocolMessage &msg) {
  const uint8_t op = msg.relay_state;
  const bool enabled = msg.input_state != 0;
  if (runtime_.role_tx) {
    if (op != kWifiControlOpStatus) {
      lrslog::event("wifi_control_tx_bad_op", msg.rssi, msg.counter, op);
      return false;
    }
    PeerRuntime *node = nullptr;
    if (settings_ == nullptr || settings_->mode != "paired" || isConfiguredOperationalPeer(msg.src)) {
      node = findOrCreatePeer(msg.src);
    }
    if (node == nullptr) {
      lrslog::event("wifi_control_peer_limit", msg.rssi, msg.counter, msg.src);
      return false;
    }
    node->wifi_state_known = true;
    node->wifi_enabled = enabled;
    node->wifi_last_confirm_ms = millis();
    node->last_seen_ms = node->wifi_last_confirm_ms;
    node->uplink_rssi = msg.rssi;
    if (node->wifi_pending && (node->wifi_pending_counter == msg.unix_time_s || msg.unix_time_s == 0)) {
      node->wifi_pending = false;
    }
    lrslog::event(enabled ? "wifi_control_status_on_rx" : "wifi_control_status_off_rx",
                  msg.rssi, msg.unix_time_s, msg.src);
    return true;
  }

  if (op != kWifiControlOpSet) {
    lrslog::event("wifi_control_rx_bad_op", msg.rssi, msg.counter, op);
    return false;
  }
  wifi_control_pending_enabled_ = enabled;
  wifi_control_pending_src_ = msg.src;
  wifi_control_pending_counter_ = msg.counter;
  wifi_control_pending_ = true;
  lrslog::event(enabled ? "wifi_control_enable_rx" : "wifi_control_disable_rx",
                msg.rssi, msg.counter, msg.src);
  return true;
}

bool NodeStateMachine::handleUdpLogControlFrame(const ProtocolMessage &msg) {
  if (runtime_.role_tx) {
    lrslog::event("udp_log_control_tx_ignored", msg.rssi, msg.counter, msg.src);
    return false;
  }
  if (msg.relay_state != kUdpLogControlOpSet) {
    lrslog::event("udp_log_control_bad_op", msg.rssi, msg.counter, msg.relay_state);
    return false;
  }

  const bool enabled = msg.input_state != 0;
  const uint16_t port = static_cast<uint16_t>(msg.flags) |
                        (static_cast<uint16_t>(msg.temp_code) << 8);
  const uint32_t ttlS = static_cast<uint32_t>(msg.sensor_mask) |
                        (static_cast<uint32_t>(msg.sensor_digital0) << 8) |
                        (static_cast<uint32_t>(msg.sensor_analog0 & 0xFFU) << 16) |
                        (static_cast<uint32_t>((msg.sensor_analog0 >> 8) & 0xFFU) << 24);
  const IPAddress host(static_cast<uint8_t>(msg.unix_time_s & 0xFFU),
                       static_cast<uint8_t>((msg.unix_time_s >> 8) & 0xFFU),
                       static_cast<uint8_t>((msg.unix_time_s >> 16) & 0xFFU),
                       static_cast<uint8_t>((msg.unix_time_s >> 24) & 0xFFU));

  if (enabled && (port == 0 || host == IPAddress())) {
    lrslog::event("udp_log_control_bad_target", msg.rssi, msg.counter, msg.src);
    return false;
  }

  udp_log_control_pending_enabled_ = enabled;
  udp_log_control_pending_host_ = host;
  udp_log_control_pending_port_ = port;
  udp_log_control_pending_ttl_s_ = ttlS;
  udp_log_control_pending_src_ = msg.src;
  udp_log_control_pending_ = true;
  lrslog::event(enabled ? "udp_log_control_enable_rx" : "udp_log_control_disable_rx",
                msg.rssi, msg.counter, msg.src);
  return true;
}

bool NodeStateMachine::handleOtaPullControlFrame(const ProtocolMessage &msg) {
  if (runtime_.role_tx) {
    lrslog::event("ota_pull_control_tx_ignored", msg.rssi, msg.counter, msg.src);
    return false;
  }

  // Any OTA traffic actively silences telemetry transmission for 90 seconds
  ota_silence_until_ms_ = millis() + 90000;

  const uint8_t *payload = msg.raw_payload;
  const uint8_t op = payload[0];
  const uint8_t transferId = payload[1];

  if (op == kOtaPullControlOpStart) {
    const uint16_t port = static_cast<uint16_t>(payload[2]) |
                          (static_cast<uint16_t>(payload[3]) << 8);
    const IPAddress host(payload[4], payload[5], payload[6], payload[7]);
    const uint8_t totalChunks = payload[8];
    if (transferId == 0 || port == 0 || host == IPAddress() ||
        totalChunks != kOtaPullControlHashChunks) {
      ota_pull_rx_ = OtaPullRxTransfer{};
      ota_pull_active_ = false;
      lrslog::event("ota_pull_control_bad_start", msg.rssi, msg.counter, op);
      return false;
    }
    ota_pull_rx_ = OtaPullRxTransfer{};
    ota_pull_rx_.active = true;
    ota_pull_rx_.src = msg.src;
    ota_pull_rx_.transfer_id = transferId;
    ota_pull_rx_.host = host;
    ota_pull_rx_.port = port;
    ota_pull_active_ = true;
    ota_pull_start_ms_ = millis();
    lrslog::event("ota_pull_control_start_rx", msg.rssi, msg.counter, msg.src);
    return true;
  }

  if (!ota_pull_rx_.active || ota_pull_rx_.src != msg.src ||
      ota_pull_rx_.transfer_id != transferId) {
    lrslog::event("ota_pull_control_orphan", msg.rssi, msg.counter, op);
    return false;
  }

  if (op == kOtaPullControlOpHash) {
    const uint8_t chunkIndex = payload[2];
    const uint8_t chunkLen = payload[3];
    if (chunkIndex >= kOtaPullControlHashChunks ||
        chunkLen != kOtaPullControlHashChunkBytes) {
      ota_pull_rx_ = OtaPullRxTransfer{};
      ota_pull_active_ = false;
      lrslog::event("ota_pull_control_bad_hash", msg.rssi, msg.counter, chunkIndex);
      return false;
    }
    memcpy(ota_pull_rx_.sha256 + (chunkIndex * kOtaPullControlHashChunkBytes),
           payload + 4, kOtaPullControlHashChunkBytes);
    ota_pull_rx_.received_bitmap |= (1UL << chunkIndex);
    return true;
  }

  if (op == kOtaPullControlOpCommit) {
    const uint8_t totalChunks = payload[2];
    const uint32_t wantBitmap = (1UL << kOtaPullControlHashChunks) - 1UL;
    if (totalChunks != kOtaPullControlHashChunks ||
        (ota_pull_rx_.received_bitmap & wantBitmap) != wantBitmap) {
      lrslog::event("ota_pull_control_incomplete", msg.rssi, msg.counter, totalChunks);
      ota_pull_rx_ = OtaPullRxTransfer{};
      ota_pull_active_ = false;
      return false;
    }

    ota_pull_pending_host_ = ota_pull_rx_.host;
    ota_pull_pending_port_ = ota_pull_rx_.port;
    ota_pull_pending_sha256_ = sha256BytesToHex(ota_pull_rx_.sha256);
    ota_pull_pending_src_ = ota_pull_rx_.src;
    ota_pull_pending_ = true;
    ota_pull_rx_ = OtaPullRxTransfer{};
    lrslog::event("ota_pull_control_rx", msg.rssi, msg.counter, msg.src);
    return true;
  }

  lrslog::event("ota_pull_control_bad_op", msg.rssi, msg.counter, op);
  ota_pull_rx_ = OtaPullRxTransfer{};
  ota_pull_active_ = false;
  return false;
}

bool NodeStateMachine::ackMatchesPendingCommand(const ProtocolMessage &msg) const {
  if (!tx_command_pending_) return false;
  return msg.unix_time_s == tx_pending_command_counter_;
}

void NodeStateMachine::applyReceiverFailsafe(uint32_t now) {
  if (runtime_.role_tx) return;
  if (runtime_.rx_failsafe_mode == RxFailsafeMode::HoldLast) return;
  if (last_rx_control_ms_ == 0) return;
  if (runtime_.rx_failsafe_timeout_ms == 0) return;
  if (static_cast<uint32_t>(now - last_rx_control_ms_) < runtime_.rx_failsafe_timeout_ms) return;

  const uint8_t desiredRelay = (runtime_.rx_failsafe_mode == RxFailsafeMode::ForceOn) ? 1U : 0U;
  if (relay_state_ == desiredRelay) return;

  relay_state_ = desiredRelay;
  digitalWrite(kRelayPin, relay_state_ ? HIGH : LOW);
  last_rx_control_source_ = RxControlSource::Failsafe;
  lrslog::event("rx_failsafe_apply", 0, last_counter_, relay_state_);
}

bool NodeStateMachine::handleFactoryResetFrame(const ProtocolMessage &msg) {
  if (msg.relay_state != kFactoryResetMagic0 || msg.input_state != kFactoryResetMagic1) {
    lrslog::event("factory_reset_rx_bad", msg.rssi, msg.counter, 0);
    return false;
  }
  factory_reset_keep_fleet_pending_ = (msg.flags & kFactoryResetKeepFleetFlag) != 0U;
  factory_reset_keep_wifi_pending_ = (msg.flags & kFactoryResetKeepWifiFlag) != 0U;
  factory_reset_pending_src_ = msg.src;
  factory_reset_pending_ = true;
  {
    lrslog::event(factory_reset_keep_fleet_pending_ ? "factory_reset_rx_keep" : "factory_reset_rx_full",
               msg.rssi, msg.counter, msg.src);
  }
  return true;
}

bool NodeStateMachine::handleRebootFrame(const ProtocolMessage &msg) {
  if (msg.relay_state != kRebootMagic0 || msg.input_state != kRebootMagic1) {
    lrslog::event("reboot_rx_bad", msg.rssi, msg.counter, 0);
    return false;
  }
  reboot_pending_ = true;
  lrslog::event("reboot_rx", msg.rssi, msg.counter, msg.src);
  return true;
}

bool NodeStateMachine::handleSensorConfigFrame(const ProtocolMessage &msg) {
  if (msg.relay_state != kSensorConfigMagic0 || msg.input_state != kSensorConfigMagic1) {
    lrslog::event("sensor_config_rx_bad", msg.rssi, msg.counter, 0);
    return false;
  }
  sensor_config_temp_enabled_ = (msg.flags & 0x01) != 0;
  sensor_config_power_save_enabled_ = (msg.flags & 0x02) != 0;
  sensor_config_power_save_boot_grace_ = (msg.flags & 0x04) == 0;
  sensor_config_tank_enabled_ = (msg.temp_code == 1);
  sensor_config_pending_ = true;
  lrslog::event("sensor_config_rx", msg.rssi, msg.counter, msg.src);
  return true;
}

void NodeStateMachine::tickProvisioningTarget(uint32_t now) {
  if (!radioTxBudgetAvailable()) return;
  if (!isDefaultFleetKey()) return;
  if (!prov_rx_.discover_pending) return;
  if (prov_rx_.announce_remaining == 0) {
    prov_rx_.discover_pending = false;
    prov_rx_.announce_second_at_ms = 0;
    return;
  }
  if (static_cast<int32_t>(now - prov_rx_.announce_at_ms) < 0) return;
  if (sendProvisioningAnnounce(prov_rx_.session_nonce)) {
    if (prov_rx_.announce_remaining > 0) prov_rx_.announce_remaining--;
    if (prov_rx_.announce_remaining == 0) {
      prov_rx_.discover_pending = false;
      prov_rx_.announce_second_at_ms = 0;
    } else {
      prov_rx_.announce_at_ms = prov_rx_.announce_second_at_ms;
    }
    lrslog::event("prov_announce_tx", 0, prov_rx_.session_nonce, runtime_.local_address);
  } else {
    prov_rx_.announce_at_ms = now + kProvAnnounceRetryBackoffMs;
  }
}

void NodeStateMachine::tickProvisioningCoordinator(uint32_t now) {
  if (!prov_.active) return;

  if (prov_.state == ProvisioningSessionState::Discovering) {
    if (prov_.watchdog_last_log_ms == 0 ||
        static_cast<uint32_t>(now - prov_.watchdog_last_log_ms) >= kProvWatchdogLogIntervalMs) {
      const uint32_t remainingMs =
          (static_cast<int32_t>(prov_.phase_deadline_ms - now) > 0) ? (prov_.phase_deadline_ms - now) : 0U;
      LRS_LOGI(API,
               "event=prov_discover_watchdog state=discovering found=%u estimated=%u broadcasts_left=%u deadline_in_ms=%lu heap_free=%lu max_free_block=%lu",
               static_cast<unsigned>(prov_device_count_),
               static_cast<unsigned>(prov_.estimated_count),
               static_cast<unsigned>(prov_.discover_broadcast_remaining),
               static_cast<unsigned long>(remainingMs),
               static_cast<unsigned long>(lrslog::heapFree()),
               static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
      prov_.watchdog_last_log_ms = now;
    }
    if (prov_.discover_broadcast_remaining > 0 && static_cast<int32_t>(now - prov_.next_discover_broadcast_ms) >= 0) {
      if (sendProvisioningDiscoverStart(prov_.session_nonce, prov_.discover_reply_window_ms, prov_.discover_broadcast_window_ms)) {
        prov_.discover_broadcast_remaining--;
        prov_.next_discover_broadcast_ms = now + kProvDiscoverBroadcastGapMs;
        addProvLog("Sent discovery broadcast (nonce=%u)", prov_.session_nonce);
      } else {
        prov_.next_discover_broadcast_ms = now + 120U;
        addProvLog("Failed to send discovery broadcast");
      }
      return;
    }
    if (prov_.estimated_count > 0 && prov_device_count_ >= static_cast<size_t>(prov_.estimated_count)) {
      recomputeProvisioningConflictsAndAssignments();
      LRS_LOGI(API,
               "event=prov_discover_ready reason=target_reached found=%u estimated=%u elapsed_ms=%lu",
               static_cast<unsigned>(prov_device_count_),
               static_cast<unsigned>(prov_.estimated_count),
               static_cast<unsigned long>(now - prov_.started_ms));
      addProvLog("Discovery completed (found %u, target reached)", prov_device_count_);
      lrslog::event("prov_discover_done_target_reached", 0, prov_device_count_, prov_.estimated_count);
      exitProvisioningCoordinatorMode(ProvisioningSessionState::Ready);
      return;
    }
    if (static_cast<int32_t>(now - prov_.phase_deadline_ms) >= 0) {
      recomputeProvisioningConflictsAndAssignments();
      LRS_LOGI(API,
               "event=prov_discover_ready reason=deadline found=%u estimated=%u elapsed_ms=%lu",
               static_cast<unsigned>(prov_device_count_),
               static_cast<unsigned>(prov_.estimated_count),
               static_cast<unsigned long>(now - prov_.started_ms));
      addProvLog("Discovery completed (found %u, deadline reached)", prov_device_count_);
      exitProvisioningCoordinatorMode(ProvisioningSessionState::Ready);
      return;
    }
  }

  if (prov_.state != ProvisioningSessionState::Provisioning || !prov_.provision_all_requested) return;

  if (prov_.watchdog_last_log_ms == 0 ||
      static_cast<uint32_t>(now - prov_.watchdog_last_log_ms) >= kProvWatchdogLogIntervalMs) {
    const uint32_t remainingMs =
        (static_cast<int32_t>(prov_.phase_deadline_ms - now) > 0) ? (prov_.phase_deadline_ms - now) : 0U;
    size_t verified = 0;
    size_t failed = 0;
    for (size_t i = 0; i < prov_device_count_; ++i) {
      const ProvisioningDevice &d = prov_devices_[i];
      if (!d.in_use) continue;
      if (d.state == ProvisioningDeviceState::Verified) ++verified;
      if (d.state == ProvisioningDeviceState::Failed) ++failed;
    }
    LRS_LOGI(API,
             "event=prov_apply_watchdog state=provisioning index=%u total=%u verified=%u failed=%u deadline_in_ms=%lu heap_free=%lu max_free_block=%lu",
             static_cast<unsigned>(prov_.current_index),
             static_cast<unsigned>(prov_device_count_),
             static_cast<unsigned>(verified),
             static_cast<unsigned>(failed),
             static_cast<unsigned long>(remainingMs),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
    prov_.watchdog_last_log_ms = now;
  }

  uint8_t txBurstRemaining = kProvCoordinatorBurstPacketsPerTick;

  while (prov_.current_index < prov_device_count_) {
    ProvisioningDevice &d = prov_devices_[prov_.current_index];
    if (!d.in_use || !d.selected || d.state == ProvisioningDeviceState::Verified ||
        d.state == ProvisioningDeviceState::AppliedUnconfirmed || d.state == ProvisioningDeviceState::Skipped) {
      prov_.current_index++;
      continue;
    }
    if (d.assigned_address == 0) {
      d.state = ProvisioningDeviceState::Failed;
      prov_.current_index++;
      continue;
    }

    if (d.state == ProvisioningDeviceState::AwaitVerify) {
      if (static_cast<int32_t>(now - prov_.phase_deadline_ms) < 0) return;
      if (!d.late_verify_probe_sent) {
        if (txBurstRemaining == 0) return;
        if (sendPollRequest(d.assigned_address, nullptr)) {
          txBurstRemaining--;
          d.late_verify_probe_sent = true;
          prov_.phase_deadline_ms = now + kProvLateVerifyProbeTimeoutMs;
          lrslog::event("prov_verify_probe_tx", 0, static_cast<uint32_t>(d.chip_id & 0xFFFFU), d.assigned_address);
        }
        return;
      }
      if (d.retries < kProvMaxRetriesPerNode) {
        d.retries++;
        d.state = ProvisioningDeviceState::Discovered;
        d.key_total_chunks = 0;
        d.key_len = 0;
        d.key_crc16 = 0;
        d.key_next_chunk = 0;
        d.key_start_sent = false;
        d.key_commit_sent = false;
        d.late_verify_probe_sent = false;
      } else {
        d.state = ProvisioningDeviceState::Failed;
        lrslog::event("prov_verify_exhausted", 0, static_cast<uint32_t>(d.chip_id & 0xFFFFU), d.assigned_address);
        prov_.current_index++;
      }
      continue;
    }

    if (d.state == ProvisioningDeviceState::Discovered || d.state == ProvisioningDeviceState::Assigned ||
        d.state == ProvisioningDeviceState::Keying) {
      if (!settings_) {
        d.state = ProvisioningDeviceState::Failed;
        exitProvisioningCoordinatorMode(ProvisioningSessionState::Error);
        return;
      }
      const char *fleetKey = settings_->fleet_passphrase.c_str();
      const size_t keyLen = strlen(fleetKey);
      if (keyLen == 0 || keyLen > (kProvChunkBitmapMax * kProvKeyChunkBytes)) {
        d.state = ProvisioningDeviceState::Failed;
        lrslog::event("prov_key_len_bad", 0, d.chip_id & 0xFFFFU, static_cast<uint8_t>(keyLen & 0xFFU));
        exitProvisioningCoordinatorMode(ProvisioningSessionState::Error);
        return;
      }
      const uint8_t totalChunks = static_cast<uint8_t>((keyLen + (kProvKeyChunkBytes - 1U)) / kProvKeyChunkBytes);
      const uint16_t keyCrc = crc16Ccitt(reinterpret_cast<const uint8_t *>(fleetKey), keyLen);

      if (d.state == ProvisioningDeviceState::Discovered) {
        d.key_total_chunks = totalChunks;
        d.key_len = static_cast<uint8_t>(keyLen);
        d.key_crc16 = keyCrc;
        d.key_next_chunk = 0;
        d.key_start_sent = false;
        d.key_commit_sent = false;
        d.late_verify_probe_sent = false;
      }

      uint8_t payload[12]{};
      if (d.state == ProvisioningDeviceState::Discovered) {
        payload[0] = kProvOpAssign;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.assigned_address;
        payload[8] = 0;  // RX for v1
        if (txBurstRemaining == 0) return;
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        txBurstRemaining--;
        d.state = ProvisioningDeviceState::Assigned;
        continue;
      }

      if (d.state == ProvisioningDeviceState::Assigned && !d.key_start_sent) {
        payload[0] = kProvOpKeyStart;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.key_total_chunks;
        payload[8] = d.key_len;
        encodeU16LE(payload + 9, d.key_crc16);
        if (txBurstRemaining == 0) return;
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        txBurstRemaining--;
        d.key_start_sent = true;
        d.state = ProvisioningDeviceState::Keying;
        continue;
      }

      if (d.state == ProvisioningDeviceState::Keying && d.key_next_chunk < d.key_total_chunks) {
        const uint8_t idx = d.key_next_chunk;
        payload[0] = kProvOpKeyData;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = idx;
        const size_t offset = static_cast<size_t>(idx) * kProvKeyChunkBytes;
        size_t chunkLen = static_cast<size_t>(d.key_len) - offset;
        if (chunkLen > kProvKeyChunkBytes) chunkLen = kProvKeyChunkBytes;
        payload[8] = static_cast<uint8_t>(chunkLen);
        memcpy(payload + 9, fleetKey + offset, chunkLen);
        if (txBurstRemaining == 0) return;
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        txBurstRemaining--;
        d.key_next_chunk++;
        continue;
      }

      if (d.state == ProvisioningDeviceState::Keying && !d.key_commit_sent) {
        payload[0] = kProvOpKeyCommit;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.key_total_chunks;
        payload[8] = d.key_len;
        encodeU16LE(payload + 9, d.key_crc16);
        if (txBurstRemaining == 0) return;
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        txBurstRemaining--;
        d.key_commit_sent = true;
        continue;
      }

      if (d.state == ProvisioningDeviceState::Keying && d.key_commit_sent) {
        payload[0] = kProvOpApplyCommit;
        encodeU16LE(payload + 1, prov_.session_nonce);
        encodeU32LE(payload + 3, d.chip_id);
        payload[7] = d.assigned_address;
        payload[8] = 0;  // RX
        if (txBurstRemaining == 0) return;
        if (!sendProvisioningCoordinatorPacketFactory(payload, kProvBroadcastAddress)) return;
        txBurstRemaining--;
        d.state = ProvisioningDeviceState::AwaitVerify;
        prov_.phase_deadline_ms = now + kProvVerifyTimeoutMs;
        lrslog::event("prov_node_tx", 0, static_cast<uint32_t>(d.chip_id & 0xFFFFU), d.assigned_address);
        return;
      }
    }

    if (d.state == ProvisioningDeviceState::Failed || d.state == ProvisioningDeviceState::Verified ||
        d.state == ProvisioningDeviceState::AppliedUnconfirmed) {
      prov_.current_index++;
      continue;
    }
    return;
  }

  exitProvisioningCoordinatorMode(ProvisioningSessionState::Complete);
}

bool NodeStateMachine::radioTxBudgetAvailable() const {
  return !radio_tx_budget_active_ || !radio_tx_used_this_tick_;
}

void NodeStateMachine::resetRadioTxBudgetForTick() {
  const bool provisioningBurstActive =
      prov_.active && prov_.state == ProvisioningSessionState::Provisioning;
  // Normal runtime stays one-send-per-tick; active provisioning uses its own bounded burst inside the gateway.
  radio_tx_budget_active_ = !provisioningBurstActive;
  radio_tx_used_this_tick_ = false;
}

void NodeStateMachine::finishRadioTxBudgetForTick() { radio_tx_budget_active_ = false; }

void NodeStateMachine::markRadioTxSentThisTick() {
  if (radio_tx_budget_active_) {
    radio_tx_used_this_tick_ = true;
  }
}

bool NodeStateMachine::handleProvisioningFrame(const ProtocolMessage &msg) {
  uint8_t payload[12]{};
  payload[0] = msg.relay_state;
  payload[1] = msg.input_state;
  payload[2] = msg.flags;
  payload[3] = msg.temp_code;
  payload[4] = msg.sensor_mask;
  payload[5] = msg.sensor_digital0;
  payload[6] = static_cast<uint8_t>(msg.sensor_analog0 & 0xFFU);
  payload[7] = static_cast<uint8_t>((msg.sensor_analog0 >> 8) & 0xFFU);
  payload[8] = static_cast<uint8_t>(msg.unix_time_s & 0xFFU);
  payload[9] = static_cast<uint8_t>((msg.unix_time_s >> 8) & 0xFFU);
  payload[10] = static_cast<uint8_t>((msg.unix_time_s >> 16) & 0xFFU);
  payload[11] = static_cast<uint8_t>((msg.unix_time_s >> 24) & 0xFFU);

  const uint8_t op = payload[0];
  const uint16_t sessionNonce = decodeU16LE(payload + 1);
  const uint32_t now = millis();

  // Coordinator-side handling
  if (runtime_.role_tx && prov_.active) {
    if (op == kProvOpAnnounce && msg.via_factory_key && (msg.dst == runtime_.local_address || msg.dst == kProvBroadcastAddress)) {
      if (sessionNonce != prov_.session_nonce) {
        addProvLog("Announce rejected: nonce mismatch (got %u, expected %u)", sessionNonce, prov_.session_nonce);
        return false;
      }
      const uint32_t chipId = decodeU32LE(payload + 3);
      ProvisioningDevice *d = upsertProvisioningDevice(chipId);
      if (d == nullptr) {
        addProvLog("Announce rejected: out of storage slots for chip=0x%08lx", static_cast<unsigned long>(chipId));
        return false;
      }
      d->current_address = payload[7];
      d->role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
      d->hw_model = static_cast<uint8_t>((payload[8] >> 4) & 0x0FU);
      d->hw_rev = kProvHwRevA1;
      d->fw_build = payload[9];
      d->fw_major = static_cast<uint8_t>((payload[10] >> 4) & 0x0FU);
      d->fw_minor = static_cast<uint8_t>(payload[10] & 0x0FU);
      d->fw_patch = payload[11];
      d->rssi = msg.rssi;
      if (d->first_seen_ms == 0) d->first_seen_ms = now;
      d->last_seen_ms = now;
      if (d->state == ProvisioningDeviceState::Failed) d->state = ProvisioningDeviceState::Discovered;
      recomputeProvisioningConflictsAndAssignments();
      addProvLog("Announce RX: chip=0x%08lx address=%u", static_cast<unsigned long>(chipId), d->current_address);
      lrslog::event("prov_announce_rx", msg.rssi, static_cast<uint32_t>(chipId & 0xFFFFU), d->current_address);
      return true;
    }

    if (op == kProvOpVerify && !msg.via_factory_key && (msg.dst == runtime_.local_address || msg.dst == kProvBroadcastAddress)) {
      if (sessionNonce != prov_.session_nonce) {
        addProvLog("Verify rejected: nonce mismatch (got %u, expected %u)", sessionNonce, prov_.session_nonce);
        return false;
      }
      const uint32_t chipId = decodeU32LE(payload + 3);
      ProvisioningDevice *d = findProvisioningDeviceByChip(chipId);
      if (d == nullptr) {
        addProvLog("Verify rejected: unknown chip=0x%08lx", static_cast<unsigned long>(chipId));
        return false;
      }
      d->current_address = payload[7];
      d->assigned_address = payload[7];
      d->role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
      d->hw_model = static_cast<uint8_t>((payload[8] >> 4) & 0x0FU);
      d->hw_rev = kProvHwRevA1;
      d->fw_build = payload[9];
      d->fw_major = static_cast<uint8_t>((payload[10] >> 4) & 0x0FU);
      d->fw_minor = static_cast<uint8_t>(payload[10] & 0x0FU);
      d->fw_patch = payload[11];
      d->rssi = msg.rssi;
      d->last_seen_ms = now;
      d->state = ProvisioningDeviceState::Verified;
      rememberProvisionedAddress(chipId, payload[7]);
      if (prov_.state == ProvisioningSessionState::Provisioning && prov_.current_index < prov_device_count_ &&
          prov_devices_[prov_.current_index].chip_id == chipId) {
        prov_.current_index++;
      }
      recomputeProvisioningConflictsAndAssignments();
      addProvLog("Verify RX: chip=0x%08lx assigned=%u", static_cast<unsigned long>(chipId), payload[7]);
      lrslog::event("prov_verify_rx", msg.rssi, static_cast<uint32_t>(chipId & 0xFFFFU), payload[7]);
      return true;
    }
  }

  // Target-side factory-key provisioning (only when currently on factory key).
  if (!isDefaultFleetKey()) {
    return false;
  }
  if (!msg.via_factory_key) {
    return false;
  }
  if (!(msg.dst == runtime_.local_address || msg.dst == kProvBroadcastAddress)) {
    return false;
  }
  const uint32_t localChip = ESP.getChipId();
  const uint32_t targetChip = decodeU32LE(payload + 3);

  if (op == kProvOpDiscoverStart) {
    const uint16_t replyWindowTicks = decodeU16LE(payload + 3);
    uint32_t replyWindowMs = static_cast<uint32_t>(replyWindowTicks) * 100U;
    if (replyWindowMs < 1000U) replyWindowMs = 1000U;
    if (replyWindowMs > 180000U) replyWindowMs = 180000U;
    uint32_t broadcastWindowMs = static_cast<uint32_t>(payload[5]) * 100U;
    if (broadcastWindowMs > 30000U) broadcastWindowMs = 30000U;
    prov_rx_.session_nonce = sessionNonce;
    prov_rx_.discover_pending = true;
    prov_rx_.announce_remaining = kProvAnnounceRepeatCount;
    const uint32_t chip = ESP.getChipId();
    const uint32_t seed = fnv1a32(reinterpret_cast<const uint8_t *>(&chip), sizeof(chip));
    const uint32_t halfWindow = (replyWindowMs >= 2U) ? (replyWindowMs / 2U) : 1U;
    const uint32_t slotA = (halfWindow > 0U) ? (seed % halfWindow) : 0U;
    const uint32_t slotB = halfWindow + ((halfWindow > 0U) ? ((seed >> 12) % halfWindow) : 0U);
    uint32_t jitterSpan = halfWindow / 6U;
    if (jitterSpan < 30U) jitterSpan = 30U;
    if (jitterSpan > 220U) jitterSpan = 220U;
    const int32_t jitterA = static_cast<int32_t>(random(-static_cast<long>(jitterSpan), static_cast<long>(jitterSpan + 1U)));
    const int32_t jitterB = static_cast<int32_t>(random(-static_cast<long>(jitterSpan), static_cast<long>(jitterSpan + 1U)));
    int32_t relA = static_cast<int32_t>(slotA) + jitterA;
    int32_t relB = static_cast<int32_t>(slotB) + jitterB;
    if (relA < 0) relA = 0;
    if (relA >= static_cast<int32_t>(halfWindow)) relA = static_cast<int32_t>(halfWindow - 1U);
    if (relB < static_cast<int32_t>(halfWindow)) relB = static_cast<int32_t>(halfWindow);
    if (relB >= static_cast<int32_t>(replyWindowMs)) relB = static_cast<int32_t>(replyWindowMs - 1U);
    prov_rx_.announce_at_ms = now + broadcastWindowMs + static_cast<uint32_t>(relA);
    prov_rx_.announce_second_at_ms = now + broadcastWindowMs + static_cast<uint32_t>(relB);
    lrslog::event("prov_discover_rx", msg.rssi, sessionNonce, 0);
    return true;
  }

  if (sessionNonce == 0 || targetChip != localChip) {
    return false;
  }

  if (op == kProvOpAssign) {
    const uint8_t newAddr = payload[7];
    if (newAddr == 0 || newAddr == 255) return false;
    prov_rx_.have_staged_assignment = true;
    prov_rx_.staged_address = newAddr;
    prov_rx_.staged_role_tx = (payload[8] & kProvRoleTxFlag) != 0U;
    prov_rx_.key_session_nonce = sessionNonce;
    return true;
  }

  if (op == kProvOpKeyStart) {
    const uint8_t totalChunks = payload[7];
    const uint8_t keyLen = payload[8];
    const uint16_t crc = decodeU16LE(payload + 9);
    if (totalChunks == 0 || totalChunks > kProvChunkBitmapMax || keyLen == 0 || keyLen > sizeof(prov_rx_.key_data)) return false;
    prov_rx_.key_transfer_active = true;
    prov_rx_.key_session_nonce = sessionNonce;
    prov_rx_.key_total_chunks = totalChunks;
    prov_rx_.key_len = keyLen;
    prov_rx_.key_crc16 = crc;
    prov_rx_.key_bitmap = 0;
    prov_rx_.key_committed = false;
    memset(prov_rx_.key_data, 0, sizeof(prov_rx_.key_data));
    return true;
  }

  if (!prov_rx_.key_transfer_active || prov_rx_.key_session_nonce != sessionNonce) {
    return false;
  }

  if (op == kProvOpKeyData) {
    const uint8_t idx = payload[7];
    const uint8_t len = payload[8];
    if (idx >= prov_rx_.key_total_chunks || len == 0 || len > kProvKeyChunkBytes) return false;
    const size_t offset = static_cast<size_t>(idx) * kProvKeyChunkBytes;
    if ((offset + len) > prov_rx_.key_len) return false;
    memcpy(prov_rx_.key_data + offset, payload + 9, len);
    prov_rx_.key_bitmap |= (1UL << idx);
    return true;
  }

  if (op == kProvOpKeyCommit) {
    const uint8_t totalChunks = payload[7];
    const uint8_t keyLen = payload[8];
    const uint16_t crc = decodeU16LE(payload + 9);
    if (totalChunks != prov_rx_.key_total_chunks || keyLen != prov_rx_.key_len || crc != prov_rx_.key_crc16) return false;
    const uint32_t want = (1UL << prov_rx_.key_total_chunks) - 1UL;
    if ((prov_rx_.key_bitmap & want) != want) return false;
    const uint16_t got = crc16Ccitt(reinterpret_cast<const uint8_t *>(prov_rx_.key_data), prov_rx_.key_len);
    if (got != prov_rx_.key_crc16) return false;
    prov_rx_.key_committed = true;
    return true;
  }

  if (op == kProvOpApplyCommit) {
    if (!prov_rx_.have_staged_assignment) return false;
    if (!prov_rx_.key_transfer_active) return false;
    if (!prov_rx_.key_committed) return false;
    if (prov_rx_.key_session_nonce != sessionNonce) return false;
    const uint8_t newAddr = payload[7];
    if (newAddr != prov_rx_.staged_address) return false;
    String newKey;
    newKey.reserve(prov_rx_.key_len);
    for (uint8_t i = 0; i < prov_rx_.key_len; ++i) newKey += prov_rx_.key_data[i];
    fleet_prov_apply_session_nonce_ = sessionNonce;
    fleet_prov_apply_address_ = newAddr;
    fleet_prov_apply_role_tx_ = (payload[8] & kProvRoleTxFlag) != 0U;
    fleet_prov_apply_controller_address_ = msg.src;
    fleet_prov_apply_key_ = newKey;
    fleet_prov_apply_pending_ = (fleet_prov_apply_key_.length() > 0);
    prov_rx_.key_transfer_active = false;
    prov_rx_.discover_pending = false;
    prov_rx_.announce_remaining = 0;
    prov_rx_.announce_second_at_ms = 0;
    lrslog::event("prov_apply_rx", msg.rssi, sessionNonce, newAddr);
    return fleet_prov_apply_pending_;
  }

  return false;
}
