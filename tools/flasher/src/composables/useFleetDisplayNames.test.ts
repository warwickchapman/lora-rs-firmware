import { describe, expect, it, vi } from 'vitest';
import {
  canonicalDisplayNameChipId,
  normalizeDisplayName,
  useFleetDisplayNames,
} from './useFleetDisplayNames';

describe('useFleetDisplayNames', () => {
  it('normalizes the same bounded character set as firmware', () => {
    expect(normalizeDisplayName(' North Pump ')).toBe('North Pump');
    expect(normalizeDisplayName('')).toBe('');
    expect(() => normalizeDisplayName('North/Pump')).toThrow('up to 16');
    expect(() => normalizeDisplayName('12345678901234567')).toThrow('up to 16');
    expect(canonicalDisplayNameChipId('LRS-A0B1C2D3')).toBe('a0b1c2d3');
    expect(canonicalDisplayNameChipId('30eb55')).toBe('0030eb55');
  });

  it('loads each gateway-owned chip once and progressively', async () => {
    const send = vi.fn(async (_target, _cmd, payload) => ({
      ok: true,
      display_name: payload.chip_id === '11111111' ? 'Gateway' : 'Pump',
    }));
    const store = useFleetDisplayNames(send);
    await store.load('serial:/dev/gw', '/dev/gw', 'password1', ['11111111', '22222222']);
    expect(store.names.value).toEqual({ '11111111': 'Gateway', '22222222': 'Pump' });
    await store.load('serial:/dev/gw', '/dev/gw', 'password1', ['11111111', '22222222']);
    expect(send).toHaveBeenCalledTimes(2);
  });

  it('clears old names and ignores stale replies when gateway changes', async () => {
    let release!: (value: unknown) => void;
    const pending = new Promise(resolve => { release = resolve; });
    const send = vi.fn(async () => pending as any);
    const store = useFleetDisplayNames(send);
    const loading = store.load('serial:one', 'one', 'password1', ['11111111']);
    store.resetContext('mqtt:two');
    release({ ok: true, display_name: 'Old gateway' });
    await loading;
    expect(store.names.value).toEqual({});
    expect(store.loading.value).toEqual({});
  });

  it('marks all queued rows loading and makes failed reads unavailable', async () => {
    let release!: (value: unknown) => void;
    const first = new Promise(resolve => { release = resolve; });
    const send = vi.fn()
      .mockImplementationOnce(async () => first)
      .mockRejectedValueOnce(new Error('offline'));
    const store = useFleetDisplayNames(send);
    const loading = store.load('serial:gw', 'gw', 'password1', ['11111111', '22222222']);
    expect(store.loading.value).toEqual({ '11111111': true, '22222222': true });
    release({ ok: true, display_name: 'Gateway' });
    await loading;
    expect(store.names.value['11111111']).toBe('Gateway');
    expect(store.unavailable.value['22222222']).toBe(true);
    expect(store.loading.value).toEqual({});
    await expect(store.save('serial:gw', 'gw', 'password1', '22222222', 'Pump'))
      .rejects.toThrow('could not be loaded');
  });

  it('retries one-shot reads after the context is explicitly reset', async () => {
    const send = vi.fn()
      .mockRejectedValueOnce(new Error('offline'))
      .mockResolvedValueOnce({ ok: true, display_name: 'Recovered' });
    const store = useFleetDisplayNames(send);
    await store.load('serial:gw', 'gw', 'password1', ['11111111']);
    expect(store.unavailable.value['11111111']).toBe(true);
    store.resetContext('');
    await store.load('serial:gw', 'gw', 'password1', ['11111111']);
    expect(store.names.value['11111111']).toBe('Recovered');
    expect(send).toHaveBeenCalledTimes(2);
  });

  it('sends the same bounded admin command for serial and MQTT targets', async () => {
    const send = vi.fn(async (_target, _cmd, payload) => ({
      ok: true,
      display_name: payload.display_name,
    }));
    const store = useFleetDisplayNames(send);
    store.resetContext('serial:/dev/gw');
    await store.save('serial:/dev/gw', '/dev/gw', 'password1', '11111111', ' North Pump ');
    store.resetContext('mqtt:22222222');
    await store.save('mqtt:22222222', '22222222', 'password2', '22222222', 'MQTT GW');
    expect(send.mock.calls.map(call => [call[0], call[1], call[2]])).toEqual([
      ['/dev/gw', 'set_display_name', { admin_password: 'password1', chip_id: '11111111', display_name: 'North Pump' }],
      ['22222222', 'set_display_name', { admin_password: 'password2', chip_id: '22222222', display_name: 'MQTT GW' }],
    ]);
  });

  it('keeps the previous name after a failed save', async () => {
    const send = vi.fn()
      .mockResolvedValueOnce({ ok: true, display_name: 'Original' })
      .mockRejectedValueOnce(new Error('save failed'));
    const store = useFleetDisplayNames(send);
    await store.load('serial:gw', 'gw', 'password1', ['11111111']);
    await expect(store.save('serial:gw', 'gw', 'password1', '11111111', 'Changed')).rejects.toThrow('save failed');
    expect(store.names.value['11111111']).toBe('Original');
    expect(store.saving.value['11111111']).toBeUndefined();
  });
});
