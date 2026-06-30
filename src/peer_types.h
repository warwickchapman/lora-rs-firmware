#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stddef.h>
#include "sensor_registry.h"

#ifndef LRS_MAX_PEERS
#define LRS_MAX_PEERS 12
#endif

enum class PeerAckState : uint8_t {
  Unknown,
  Pending,
  Ok,
  Timeout,
};

struct PeerStatusSnapshot {
  uint8_t address = 0;
  uint8_t relay_state = 0;
  uint8_t input_state = 0;
  bool input_state_known = false;
  SensorRegistry sensors;
  int uplink_rssi = -127;
  bool downlink_rssi_valid = false;
  int downlink_rssi = -127;
  uint32_t last_seen_ms = 0;
  uint32_t last_cmd_counter = 0;
  PeerAckState ack_state = PeerAckState::Unknown;
  uint32_t poll_interval_ms = 0;
  uint32_t last_poll_tx_ms = 0;
  bool poll_pending = false;
  bool wifi_state_known = false;
  bool wifi_enabled = true;
  bool wifi_connected_known = false;
  bool wifi_connected = false;
  uint8_t ip[4]{};
  bool mqtt_state_known = false;
  bool mqtt_enabled = false;
  bool mqtt_connected = false;
  uint32_t chip_id = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  uint8_t fw_patch = 0;
  uint16_t fw_build = 0;
  uint32_t uptime_ms = 0;
  bool maintenance_debug_known = false;
  uint32_t heap_free = 0;
  uint32_t heap_max_block = 0;
  uint8_t heap_frag_pct = 0;
  uint32_t debug_uptime_ms = 0;
  uint32_t wifi_last_confirm_ms = 0;
  int16_t wifi_rssi_dbm = 0;
  bool power_save_listen_only = false;
  bool power_save_active = false;
};

struct PeerRuntime {
  bool in_use = false;
  uint8_t address = 0;
  uint8_t relay_state = 0;
  uint8_t input_state = 0;
  bool input_state_known = false;
  SensorRegistry sensors;
  uint32_t sensors_updated_ms = 0;
  int uplink_rssi = -127;
  bool downlink_rssi_valid = false;
  int downlink_rssi = -127;
  uint32_t last_seen_ms = 0;
  uint32_t last_cmd_counter = 0;
  PeerAckState ack_state = PeerAckState::Unknown;
  bool wifi_state_known = false;
  bool wifi_enabled = true;
  bool wifi_connected_known = false;
  bool wifi_connected = false;
  uint8_t ip[4]{};
  bool mqtt_state_known = false;
  bool mqtt_enabled = false;
  bool mqtt_connected = false;
  uint32_t chip_id = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  uint8_t fw_patch = 0;
  uint16_t fw_build = 0;
  uint32_t uptime_ms = 0;
  uint32_t uptime_received_ms = 0;
  bool maintenance_debug_known = false;
  uint32_t heap_free = 0;
  uint32_t heap_max_block = 0;
  uint8_t heap_frag_pct = 0;
  uint32_t debug_uptime_ms = 0;
  uint32_t wifi_last_confirm_ms = 0;
  int16_t wifi_rssi_dbm = 0;
  bool power_save_listen_only = false;
  bool power_save_active = false;
  bool wifi_pending = false;
  bool wifi_pending_enabled = true;
  uint32_t wifi_pending_counter = 0;
  uint32_t wifi_pending_deadline_ms = 0;
  bool pending = false;
  uint8_t pending_relay = 0;
  uint8_t retry_step = 0;
  uint32_t next_retry_ms = 0;
  uint32_t pending_counter = 0;
  uint32_t pending_deadline_ms = 0;
  uint32_t poll_interval_ms = 0;
};
static_assert(sizeof(PeerRuntime) <= 256, "PeerRuntime exceeds budget; review field additions");

struct PollRuntime {
  uint32_t next_poll_ms = 0;
  bool poll_pending = false;
  uint8_t poll_retry_step = 0;
  uint32_t poll_next_retry_ms = 0;
  uint32_t poll_counter = 0;
  uint32_t poll_deadline_ms = 0;
  uint32_t last_poll_tx_ms = 0;
};
