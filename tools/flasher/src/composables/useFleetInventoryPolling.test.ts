import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { ref } from 'vue';
import { useFleetInventoryPolling, shouldClearScanStateOnTimeout } from './useFleetInventoryPolling';

describe('useFleetInventoryPolling', () => {
  let activeMode = ref('network');
  let fleetTransport = ref<'serial' | 'mqtt'>('serial');
  let gatewaySelectedPort = ref('COM1');
  let selectedMqttGatewayChipId = ref('abc12345');
  let isLoraInventoryScanning = ref(false);
  let refreshCalls: { port: string; background: boolean; source: string }[] = [];

  const refreshGatewaySnapshot = async (port: string, background: boolean, source: 'fleet' | 'monitor') => {
    refreshCalls.push({ port, background, source });
  };

  const createPolling = () => {
    return useFleetInventoryPolling({
      activeMode,
      fleetTransport,
      gatewaySelectedPort,
      selectedMqttGatewayChipId,
      isLoraInventoryScanning,
      refreshGatewaySnapshot,
      fleetScanPollIntervalMs: 1200,
      fleetCachePollIntervalMs: 5000
    });
  };

  beforeEach(() => {
    vi.useFakeTimers();
    activeMode.value = 'network';
    fleetTransport.value = 'serial';
    gatewaySelectedPort.value = 'COM1';
    selectedMqttGatewayChipId.value = 'abc12345';
    isLoraInventoryScanning.value = false;
    refreshCalls = [];
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('starting scan polling configures one setInterval and does not create duplicate intervals when called repeatedly', () => {
    const polling = createPolling();
    const setIntervalSpy = vi.spyOn(globalThis, 'setInterval');
    const clearIntervalSpy = vi.spyOn(globalThis, 'clearInterval');

    polling.startLoraInventoryPolling();
    expect(setIntervalSpy).toHaveBeenCalledTimes(1);

    polling.startLoraInventoryPolling();
    // It calls clearInterval on the previous one first
    expect(clearIntervalSpy).toHaveBeenCalledTimes(1);
    expect(setIntervalSpy).toHaveBeenCalledTimes(2);

    expect(polling.networkInventoryPollMode.value).toBe('scan');
  });

  it('stopping polling clears the interval and resets the mode', () => {
    const polling = createPolling();
    const clearIntervalSpy = vi.spyOn(globalThis, 'clearInterval');

    polling.startLoraInventoryPolling();
    polling.stopLoraInventoryPolling();

    expect(clearIntervalSpy).toHaveBeenCalledTimes(1);
    expect(polling.networkInventoryPollTimer.value).toBeNull();
    expect(polling.networkInventoryPollMode.value).toBeNull();
  });

  it('cache polling does not run when transport is MQTT', () => {
    fleetTransport.value = 'mqtt';
    const polling = createPolling();
    polling.startFleetCachePolling();

    expect(polling.networkInventoryPollTimer.value).toBeNull();
    expect(polling.networkInventoryPollMode.value).toBeNull();
  });

  it('cache polling does not start if mode is not network or no gateway is selected', () => {
    activeMode.value = 'serial';
    const polling = createPolling();
    polling.startFleetCachePolling();

    expect(polling.networkInventoryPollTimer.value).toBeNull();

    activeMode.value = 'network';
    gatewaySelectedPort.value = '';
    polling.startFleetCachePolling();

    expect(polling.networkInventoryPollTimer.value).toBeNull();
  });

  it('scan polling interval executes the refresh callback and guards overlap', async () => {
    const polling = createPolling();
    polling.startLoraInventoryPolling();

    expect(refreshCalls).toHaveLength(0);

    // Advance by interval
    await vi.advanceTimersByTimeAsync(1200);
    expect(refreshCalls).toHaveLength(1);
    expect(refreshCalls[0]).toEqual({ port: 'COM1', background: true, source: 'fleet' });

    // In-flight guard: if active mode tick runs, it returns early if isFleetScanPollingActive is true
    polling.isFleetScanPollingActive.value = true;
    await vi.advanceTimersByTimeAsync(1200);
    expect(refreshCalls).toHaveLength(1); // Call not duplicated/made because guard blocked it
  });

  it('stop behavior sets/clears isLoraInventoryScanning according to markIdle flag', () => {
    const polling = createPolling();
    isLoraInventoryScanning.value = true;

    polling.stopLoraInventoryPolling(false);
    expect(isLoraInventoryScanning.value).toBe(true);

    polling.stopLoraInventoryPolling(true);
    expect(isLoraInventoryScanning.value).toBe(false);
  });
});

describe('shouldClearScanStateOnTimeout', () => {
  const NOW = 100000;

  it('returns false when not scanning', () => {
    expect(shouldClearScanStateOnTimeout(false, NOW - 80000, NOW)).toBe(false);
  });

  it('returns false when scan timestamp is zero', () => {
    expect(shouldClearScanStateOnTimeout(true, 0, NOW)).toBe(false);
  });

  it('returns false when scan is fresh (30s)', () => {
    expect(shouldClearScanStateOnTimeout(true, NOW - 30000, NOW)).toBe(false);
  });

  it('returns false at exact threshold boundary (75000ms)', () => {
    expect(shouldClearScanStateOnTimeout(true, NOW - 75000, NOW)).toBe(false);
  });

  it('returns true when scan exceeds threshold (80s)', () => {
    expect(shouldClearScanStateOnTimeout(true, NOW - 80000, NOW)).toBe(true);
  });

  it('returns true just past threshold (75001ms)', () => {
    expect(shouldClearScanStateOnTimeout(true, NOW - 75001, NOW)).toBe(true);
  });
});
