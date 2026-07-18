export interface SensorReading {
  kind: 'input' | 'temperature' | 'tank_level';
  state: 'disabled' | 'missing' | 'fault' | 'ok' | 'overrange' | 'waiting';
  instance: number;
  value?: number;
  unit?: string;
}

export interface LoraInventoryDevice {
  address: number;
  chip_id?: string;
  fw_version?: string;
  fw_build?: number;
  role?: string;
  mode?: string;
  wifi_enabled_known?: boolean;
  wifi_enabled?: boolean;
  wifi_connected_known?: boolean;
  wifi_connected?: boolean;
  ip?: string;
  mqtt_known?: boolean;
  mqtt_enabled?: boolean;
  mqtt_connected?: boolean;
  power_save_listen_only?: boolean;
  power_save_active?: boolean;
  relay_state?: number;
  input_state?: number;
  input_state_known?: boolean;
  sensors?: SensorReading[];
  maintenance_debug_known?: boolean;
  heap_free?: number;
  heap_max_block?: number;
  heap_frag_pct?: number;
  debug_uptime_ms?: number;
  wifi_rssi_dbm?: number;
  rssi?: number;
  uptime_ms?: number;
  age_ms?: number;
  poll_pending?: boolean;
  ota_eligible?: boolean;
  ota_reason?: string;
  selected?: boolean;
  row_state?: 'ota_pending' | 'ota_downloading' | 'ota_apply_wait' | 'ota_retrying' | 'ota_rebooted' | 'ota_updated' | 'ota_no_reboot' | 'unexpected_reboot' | 'ota_queued' | 'ota_failed';
  row_state_until_ms?: number;
  pending_power_save_listen_only?: boolean;
  pending_power_save_tx_ms?: number;
  wifi_pending_offline?: boolean;
  power_save_deferred?: boolean;
  conflict_chip_id?: string;
}

export interface LoraAdoptionCandidate {
  address: number;
  chip_id?: string;
  rssi: number;
  last_seen_ms: number;
  last_seen_local_ms?: number;
  age_ms?: number;
  reason: 'ok' | 'known_chip_moved' | 'conflict' | 'out_of_range' | 'full';
  state: 'seen_address_only' | 'identified' | 'readdressing' | 'adopted' | 'failed' | 'reset_requested';
}

export interface LoraAdoptionStatus {
  active: boolean;
  chip_id?: string;
  assigned_address: number;
}

export interface LoraInventoryStatus {
  ok: boolean;
  cmd: string;
  scan?: {
    active: boolean;
    start_address: number;
    end_address: number;
    next_address: number;
    sent: number;
    now_ms: number;
  };
  devices?: LoraInventoryDevice[];
  candidates?: LoraAdoptionCandidate[];
  candidate_total?: number;
  candidate_truncated?: boolean;
  adoption?: LoraAdoptionStatus | null;
}

export interface LoraInventoryPeerStatus {
  ok: boolean;
  cmd: string;
  device?: LoraInventoryDevice;
}
