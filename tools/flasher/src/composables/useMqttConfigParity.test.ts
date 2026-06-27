import { describe, it, expect } from 'vitest';
import { useMqttConfigBuffer, canonicalChipId } from './useMqttConfigBuffer';

// Mock helper matching Vue implementation
function normalizeSerialAdminConfig(raw: any, _status: any, settingsTransport: string) {
  const cfg = raw || {};
  return {
    ...cfg,
    mode: cfg.mode || 'paired',
    role_tx: !!cfg.role_tx,
    wifi_sta_ssid: cfg.wifi_sta_ssid || '',
    wifi_sta_password: settingsTransport === 'mqtt' ? '' : (cfg.wifi_sta_password || ''),
    mqtt_password: '',
    fleet_passphrase: '',
    admin_password: '',
    input_control_paired_lora_enabled: typeof cfg.input_control_paired_lora_enabled === 'boolean' ? cfg.input_control_paired_lora_enabled : true
  };
}

function serialConfigPatch(cfg: any): Record<string, any> {
  if (!cfg) return {};
  const patch: Record<string, any> = {
    mode: cfg.mode,
    role_tx: !!cfg.role_tx,
    input_control_paired_lora_enabled: cfg.input_control_paired_lora_enabled
  };
  if (cfg.wifi_sta_password) patch.wifi_sta_password = cfg.wifi_sta_password;
  if (cfg.mqtt_password) patch.mqtt_password = cfg.mqtt_password;
  if (cfg.fleet_passphrase) patch.fleet_passphrase = cfg.fleet_passphrase;
  if (cfg.admin_password) patch.admin_password = cfg.admin_password;
  return patch;
}

describe('Settings-over-MQTT Parity', () => {
  it('does not load or overwrite config until _complete is received', () => {
    const { mqttConfigBuffers, handleConfigUpdate } = useMqttConfigBuffer();
    const canonical = canonicalChipId('123456');
    let completedConfig: Record<string, any> | null = null;

    handleConfigUpdate('123456', 'input_control_paired_lora_enabled', 'false', (_id, config) => {
      completedConfig = config;
    });

    expect(mqttConfigBuffers.value[canonical].complete).toBe(false);
    expect(completedConfig).toBeNull();

    // Trigger complete
    handleConfigUpdate('123456', '_complete', 'true', (_id, config) => {
      completedConfig = config;
    });

    expect(mqttConfigBuffers.value[canonical].complete).toBe(true);
    expect(completedConfig).not.toBeNull();
    const normalized = normalizeSerialAdminConfig(completedConfig, null, 'mqtt');
    expect(normalized.input_control_paired_lora_enabled).toBe(false);
  });

  it('keeps secret inputs blank on MQTT load and stores metadata separately', () => {
    const { mqttConfigBuffers, handleConfigUpdate } = useMqttConfigBuffer();
    const canonical = canonicalChipId('123456');
    let completedConfig: Record<string, any> | null = null;

    handleConfigUpdate('123456', 'wifi_sta_ssid', 'MySSID');
    handleConfigUpdate('123456', 'wifi_sta_password_set', 'true');
    handleConfigUpdate('123456', '_complete', 'true', (_id, config) => {
      completedConfig = config;
    });

    const normalized = normalizeSerialAdminConfig(completedConfig, null, 'mqtt');
    expect(normalized.wifi_sta_ssid).toBe('MySSID');
    expect(normalized.wifi_sta_password).toBe(''); // Kept blank on MQTT load

    const meta = mqttConfigBuffers.value[canonical].secretsMetadata;
    expect(meta.wifi_sta_password).toBe(true); // Metadata stored separately
  });

  it('clears buffer and metadata when _complete=false is received', () => {
    const { mqttConfigBuffers, handleConfigUpdate } = useMqttConfigBuffer();
    const canonical = canonicalChipId('123456');

    // Load initial config and complete it
    handleConfigUpdate('123456', 'wifi_sta_ssid', 'MySSID');
    handleConfigUpdate('123456', 'wifi_sta_password_set', 'true');
    handleConfigUpdate('123456', '_complete', 'true');

    expect(mqttConfigBuffers.value[canonical].complete).toBe(true);
    expect(mqttConfigBuffers.value[canonical].buffer.wifi_sta_ssid).toBe('MySSID');
    expect(mqttConfigBuffers.value[canonical].secretsMetadata.wifi_sta_password).toBe(true);

    // Now send _complete=false
    handleConfigUpdate('123456', '_complete', 'false');

    expect(mqttConfigBuffers.value[canonical].complete).toBe(false);
    expect(mqttConfigBuffers.value[canonical].buffer).toEqual({});
    expect(mqttConfigBuffers.value[canonical].secretsMetadata).toEqual({});
  });

  it('omits blank secrets from save patch and includes typed secrets', () => {
    const cfg = {
      mode: 'paired',
      role_tx: false,
      wifi_sta_password: '', // Blank
      fleet_passphrase: 'new-passphrase' // Typed
    };

    const patch = serialConfigPatch(cfg);
    expect(patch.wifi_sta_password).toBeUndefined(); // Omitted because blank
    expect(patch.fleet_passphrase).toBe('new-passphrase'); // Included because typed
  });
});

