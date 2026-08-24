import { describe, expect, it } from 'vitest';
import { buildChangedSettingsConfigPatch, buildSettingsConfigPatch, splitMqttSettingsConfigPatches } from './settingsConfigPatch';

describe('settingsConfigPatch', () => {
  it('produces no MQTT patch when the editor still matches its loaded snapshot', () => {
    const loaded = {
      mode: 'paired',
      role_tx: true,
      local_address: 254,
      remote_refresh_enabled: true
    };

    expect(buildChangedSettingsConfigPatch(loaded, loaded)).toEqual({});
  });

  it('sends only the changed MQTT setting', () => {
    const baseline = {
      mode: 'paired',
      role_tx: true,
      local_address: 254,
      remote_refresh_enabled: false
    };
    const current = {
      ...baseline,
      remote_refresh_enabled: true
    };

    expect(buildChangedSettingsConfigPatch(current, baseline)).toEqual({
      remote_refresh_enabled: true
    });
  });

  it('preserves blank secrets and includes replacements', () => {
    const baseline = { mode: 'paired', role_tx: false, local_address: 1 };
    const current = {
      ...baseline,
      wifi_sta_password: '',
      mqtt_password: 'replacement'
    };

    const patch = buildChangedSettingsConfigPatch(current, baseline);
    expect(patch.wifi_sta_password).toBeUndefined();
    expect(patch.mqtt_password).toBe('replacement');
  });

  it('keeps the complete patch for USB serial saves', () => {
    const patch = buildSettingsConfigPatch({
      mode: 'paired',
      role_tx: false,
      local_address: 1,
      controller_address: 254
    });

    expect(patch).toMatchObject({
      mode: 'paired',
      role_tx: false,
      local_address: 1,
      controller_address: 254
    });
  });

  it('splits large MQTT changes while deferring connection changes to the final command', () => {
    const patch = {
      setting_a: 'a'.repeat(220),
      setting_b: 'b'.repeat(220),
      setting_c: 'c'.repeat(220),
      setting_d: 'd'.repeat(220),
      mqtt_host: 'broker.example.test'
    };

    const batches = splitMqttSettingsConfigPatches(patch);
    expect(batches.length).toBeGreaterThan(1);
    expect(batches[batches.length - 1]).toMatchObject({ mqtt_host: 'broker.example.test' });
    expect(batches.flatMap(batch => Object.keys(batch))).toEqual(expect.arrayContaining(Object.keys(patch)));
  });
});
