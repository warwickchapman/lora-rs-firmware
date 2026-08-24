type Config = Record<string, any>;

const MQTT_DEFERRED_FIELDS = new Set([
  'wifi_sta_ssid',
  'wifi_sta_password',
  'lan_hostname',
  'ap_always_on',
  'wifi_tx_power_dbm',
  'wifi_sleep_enabled',
  'wifi_static_ip_enabled',
  'wifi_static_ip',
  'wifi_static_gateway',
  'wifi_static_subnet',
  'wifi_channel_override',
  'wifi_ap_fallback_policy',
  'wifi_admin_enabled',
  'power_save_listen_only',
  'mqtt_client_enabled',
  'mqtt_control_enabled',
  'mqtt_host',
  'mqtt_port',
  'mqtt_user',
  'mqtt_password',
  'mqtt_topic_root',
  'admin_password'
]);

const MAX_MQTT_CONFIG_PATCH_BYTES = 700;

function sameConfigValue(left: unknown, right: unknown): boolean {
  return JSON.stringify(left) === JSON.stringify(right);
}

export function buildSettingsConfigPatch(cfg: Config | null | undefined): Config {
  if (!cfg) return {};

  const patch: Config = {
    mode: cfg.mode === 'standalone' ? 'standalone' : 'paired',
    role_tx: !!cfg.role_tx,
    local_address: Number(cfg.local_address || 1),
    ...(cfg.role_tx ? {} : { controller_address: Number(cfg.controller_address || 254) }),
    allowed_controller_addresses: cfg.allowed_controller_addresses || [Number(cfg.controller_address || 254)],
    known_peer_addresses: cfg.known_peer_addresses || [],
    lora_tx_power: Number(cfg.lora_tx_power || 17),
    lora_spreading_factor: Number(cfg.lora_spreading_factor || 7),
    lora_bandwidth_hz: Number(cfg.lora_bandwidth_hz || 125000),
    lora_coding_rate: Number(cfg.lora_coding_rate || 5),
    heartbeat_ms: Number(cfg.heartbeat_ms || 60000),
    heartbeat_enabled: cfg.heartbeat_enabled !== false,
    ack_timeout_ms: Number(cfg.ack_timeout_ms || 3000),
    remote_refresh_enabled: !!cfg.remote_refresh_enabled,
    remote_refresh_cycle_ms: Number(cfg.remote_refresh_cycle_ms || 60000),
    input_control_paired_lora_enabled: !!cfg.input_control_paired_lora_enabled,
    tx_command_retry_timeout_ms: Number(cfg.tx_command_retry_timeout_ms || 180000),
    rx_failsafe_mode: cfg.rx_failsafe_mode || 'hold_last',
    rx_failsafe_timeout_ms: Number(cfg.rx_failsafe_timeout_ms || 180000),
    wifi_sta_ssid: cfg.wifi_sta_ssid || '',
    lan_hostname: cfg.lan_hostname || '',
    ap_always_on: !!cfg.ap_always_on,
    wifi_tx_power_dbm: Number(cfg.wifi_tx_power_dbm ?? 20.5),
    wifi_sleep_enabled: !!cfg.wifi_sleep_enabled,
    wifi_static_ip_enabled: !!cfg.wifi_static_ip_enabled,
    wifi_static_ip: cfg.wifi_static_ip || '',
    wifi_static_gateway: cfg.wifi_static_gateway || '',
    wifi_static_subnet: cfg.wifi_static_subnet || '',
    wifi_channel_override: Number(cfg.wifi_channel_override || 0),
    wifi_ap_fallback_policy: cfg.wifi_ap_fallback_policy || 'fallback_on_disconnect',
    wifi_admin_enabled: !!cfg.wifi_admin_enabled,
    mqtt_client_enabled: !!cfg.mqtt_client_enabled,
    mqtt_control_enabled: !!cfg.mqtt_control_enabled,
    mqtt_controller_addresses: cfg.mqtt_controller_addresses || '',
    mqtt_host: cfg.mqtt_host || '',
    mqtt_port: Number(cfg.mqtt_port || 1883),
    mqtt_user: cfg.mqtt_user || '',
    mqtt_topic_root: cfg.mqtt_topic_root || 'lora',
    sensor_temp_enabled: !!cfg.sensor_temp_enabled,
    sensor_temp_pin: Number(cfg.sensor_temp_pin || 0),
    sensor_temp_interval_s: Number(cfg.sensor_temp_interval_s || 10),
    sensor_tank_enabled: !!cfg.sensor_tank_enabled,
    sensor_tank_range_mm: Number(cfg.sensor_tank_range_mm || 5000),
    sensor_tank_vref_mv: Number(cfg.sensor_tank_vref_mv || 3553),
    sensor_tank_sense_ohms: Number(cfg.sensor_tank_sense_ohms || 120),
    sensor_tank_interval_s: Number(cfg.sensor_tank_interval_s || 5)
  };

  // Blank secrets mean preserve the value already on the device.
  if (cfg.wifi_sta_password) patch.wifi_sta_password = cfg.wifi_sta_password;
  if (cfg.mqtt_password) patch.mqtt_password = cfg.mqtt_password;
  if (cfg.fleet_passphrase) patch.fleet_passphrase = cfg.fleet_passphrase;
  if (cfg.admin_password) patch.admin_password = cfg.admin_password;
  return patch;
}

export function buildChangedSettingsConfigPatch(current: Config, baseline: Config): Config {
  const currentPatch = buildSettingsConfigPatch(current);
  const baselinePatch = buildSettingsConfigPatch(baseline);
  return Object.fromEntries(
    Object.entries(currentPatch).filter(([key, value]) => !sameConfigValue(value, baselinePatch[key]))
  );
}

export function splitMqttSettingsConfigPatches(patch: Config): Config[] {
  const entries = Object.entries(patch);
  const normalEntries = entries.filter(([key]) => !MQTT_DEFERRED_FIELDS.has(key));
  const deferredEntries = entries.filter(([key]) => MQTT_DEFERRED_FIELDS.has(key));
  const batches: Config[] = [];
  let batch: Config = {};

  for (const [key, value] of normalEntries) {
    const candidate = { ...batch, [key]: value };
    if (new TextEncoder().encode(JSON.stringify({ config: candidate })).length <= MAX_MQTT_CONFIG_PATCH_BYTES) {
      batch = candidate;
      continue;
    }
    if (Object.keys(batch).length === 0) {
      throw new Error(`MQTT setting ${key} is too large to save.`);
    }
    batches.push(batch);
    batch = { [key]: value };
  }
  if (Object.keys(batch).length > 0) batches.push(batch);

  // Applying any of these can disconnect MQTT or reboot the device, so they
  // must be the final command rather than a sequence of independent patches.
  if (deferredEntries.length > 0) {
    const deferredPatch = Object.fromEntries(deferredEntries);
    if (new TextEncoder().encode(JSON.stringify({ config: deferredPatch })).length > MAX_MQTT_CONFIG_PATCH_BYTES) {
      throw new Error('MQTT connection settings are too large to save together.');
    }
    batches.push(deferredPatch);
  }
  return batches;
}
