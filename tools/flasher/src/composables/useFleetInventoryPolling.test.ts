import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { ref } from 'vue';
import {
  useFleetInventoryPolling,
  shouldAutoLoadFleetCache,
  shouldClearScanStateOnTimeout,
} from './useFleetInventoryPolling';

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

  const createPolling = (refresh = refreshGatewaySnapshot) => {
    return useFleetInventoryPolling({
      activeMode,
      fleetTransport,
      gatewaySelectedPort,
      selectedMqttGatewayChipId,
      isLoraInventoryScanning,
      refreshGatewaySnapshot: refresh,
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

  it('cache polling refreshes the selected MQTT gateway', async () => {
    fleetTransport.value = 'mqtt';
    const polling = createPolling();
    polling.startFleetCachePolling();

    expect(polling.networkInventoryPollTimer.value).not.toBeNull();
    expect(polling.networkInventoryPollMode.value).toBe('cache');

    await vi.advanceTimersByTimeAsync(5000);
    expect(refreshCalls).toEqual([
      { port: 'abc12345', background: true, source: 'fleet' }
    ]);
  });

  it('cache polling never overlaps a slow progressive hydration', async () => {
    let finishRefresh: (() => void) | undefined;
    const slowRefresh = vi.fn(() => new Promise<void>(resolve => {
      finishRefresh = resolve;
    }));
    const polling = createPolling(slowRefresh);
    polling.startFleetCachePolling();

    await vi.advanceTimersByTimeAsync(5000);
    expect(slowRefresh).toHaveBeenCalledTimes(1);

    await vi.advanceTimersByTimeAsync(10000);
    expect(slowRefresh).toHaveBeenCalledTimes(1);

    finishRefresh?.();
    await Promise.resolve();
    await vi.advanceTimersByTimeAsync(5000);
    expect(slowRefresh).toHaveBeenCalledTimes(2);
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

describe('shouldAutoLoadFleetCache', () => {
  const readySerial = {
    activeMode: 'network',
    target: '/dev/cu.usbserial-10',
    transport: 'serial' as const,
    mqttConnected: false,
    mqttGatewayDiscovered: false,
    mqttAdminReady: false,
    serialTargetAvailable: true,
    changeBlocked: false,
    loadedTarget: '',
    attemptedTarget: '',
  };

  it('loads a selected serial gateway cache without requiring Scan', () => {
    expect(shouldAutoLoadFleetCache(readySerial)).toBe(true);
  });

  it('waits for a saved serial target to appear in the current port list', () => {
    expect(shouldAutoLoadFleetCache({
      ...readySerial,
      serialTargetAvailable: false,
    })).toBe(false);
  });

  it('waits until the selected MQTT gateway is connected and discovered', () => {
    const mqttState = {
      ...readySerial,
      target: '0030eb55',
      transport: 'mqtt' as const,
    };
    expect(shouldAutoLoadFleetCache(mqttState)).toBe(false);
    expect(shouldAutoLoadFleetCache({ ...mqttState, mqttConnected: true })).toBe(false);
    expect(shouldAutoLoadFleetCache({
      ...mqttState,
      mqttConnected: true,
      mqttGatewayDiscovered: true,
      mqttAdminReady: true,
    })).toBe(true);
    expect(shouldAutoLoadFleetCache({
      ...mqttState,
      mqttConnected: true,
      mqttGatewayDiscovered: true,
      mqttAdminReady: false,
    })).toBe(false);
  });

  it('does not reload an already loaded target or run outside Fleet', () => {
    expect(shouldAutoLoadFleetCache({
      ...readySerial,
      loadedTarget: readySerial.target,
    })).toBe(false);
    expect(shouldAutoLoadFleetCache({
      ...readySerial,
      attemptedTarget: readySerial.target,
    })).toBe(false);
    expect(shouldAutoLoadFleetCache({ ...readySerial, activeMode: 'monitor' })).toBe(false);
  });

  it('waits while a guarded transaction blocks gateway changes', () => {
    expect(shouldAutoLoadFleetCache({ ...readySerial, changeBlocked: true })).toBe(false);
  });
});
