import { describe, it, expect, beforeEach, vi, afterEach } from 'vitest';
import { ref } from 'vue';
import { useFleetOta } from './useFleetOta';
import { LoraInventoryDevice } from '../types/fleet';

describe('useFleetOta', () => {
  let loraInventory = ref<LoraInventoryDevice[]>([]);
  let fleetRowHistory = ref<Record<number, any>>({});
  let otaQueue = ref<LoraInventoryDevice[]>([]);
  let activeComposables: any[] = [];
  
  const notifyMock = vi.fn();
  const pushNetworkLogMock = vi.fn();
  const setNetworkStatusMessageMock = vi.fn();
  const triggerOtaCommandMock = vi.fn();
  const runFollowupInventoryScanMock = vi.fn().mockResolvedValue(undefined);
  const serialFeatureErrorMock = vi.fn((feature, err) => `${feature} command failed: ${err}`);
  const checkPreflightMock = vi.fn().mockReturnValue(true);

  beforeEach(() => {
    vi.useFakeTimers();
    loraInventory.value = [];
    fleetRowHistory.value = {};
    otaQueue.value = [];
    activeComposables = [];
    notifyMock.mockClear();
    pushNetworkLogMock.mockClear();
    setNetworkStatusMessageMock.mockClear();
    triggerOtaCommandMock.mockClear();
    runFollowupInventoryScanMock.mockClear();
    serialFeatureErrorMock.mockClear();
    checkPreflightMock.mockClear().mockReturnValue(true);
  });

  afterEach(() => {
    activeComposables.forEach(c => c.cleanupFleetOtaTimers());
    vi.useRealTimers();
  });

  const createComposable = () => {
    const composable = useFleetOta({
      loraInventory,
      fleetRowHistory,
      otaQueue,
      networkFirmwarePath: '/firmware.bin',
      checkPreflight: checkPreflightMock,
      triggerOtaCommand: triggerOtaCommandMock,
      runFollowupInventoryScan: runFollowupInventoryScanMock,
      notify: notifyMock,
      pushNetworkLog: pushNetworkLogMock,
      setNetworkStatusMessage: setNetworkStatusMessageMock,
      serialFeatureError: serialFeatureErrorMock
    });
    activeComposables.push(composable);
    return composable;
  };

  it('queueing marks ota_queued', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'abc123hash'
    });

    composable.flashLoraRemote(dev);

    expect(loraInventory.value[0].row_state).toBe('ota_queued');
    expect(otaQueue.value).toContainEqual(expect.objectContaining({ address: 12 }));
  });

  it('duplicate queue requests are ignored', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];

    composable.flashLoraRemote(dev);
    composable.flashLoraRemote(dev);

    expect(otaQueue.value).toHaveLength(1);
  });

  it('successful command lifecycle marks downloading, starts follow-up, logs/notifies', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '/custom.bin' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'abc123hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(2500);

    expect(loraInventory.value[0].row_state).toBe('ota_downloading');
    expect(notifyMock).toHaveBeenCalledWith('Flash triggered for LoRa 12');
    expect(pushNetworkLogMock).toHaveBeenCalledWith(
      expect.stringContaining('Remote OTA pull: addr 12 -> http://192.168.0.100:8080/firmware.bin (/custom.bin), SHA256 abc123hash')
    );
  });

  it('failed command clears busy state, removes queue item, resets row state', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockRejectedValue(new Error('connection refused'));

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(2500);

    expect(loraInventory.value[0].row_state).toBeUndefined();
    expect(otaQueue.value).toHaveLength(0);
    expect(composable.remoteOtaBusyAddress.value).toBeNull();
  });

  it('terminal row state advances queue', async () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();
    const dev1: LoraInventoryDevice = { address: 12 };
    const dev2: LoraInventoryDevice = { address: 14 };
    loraInventory.value = [dev1, dev2];

    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    // Flash both, they enter the queue
    composable.flashLoraRemote(dev1);
    composable.flashLoraRemote(dev2);

    await vi.advanceTimersByTimeAsync(2500);
    expect(composable.remoteOtaBusyAddress.value).toBe(12);

    // Make dev1 reach a terminal state
    loraInventory.value = loraInventory.value.map(row => 
      row.address === 12 ? { ...row, row_state: 'ota_updated' } : row
    );

    // Run interval watchdog
    vi.advanceTimersByTime(1000);
    expect(pushNetworkLogMock).toHaveBeenCalledWith(expect.stringContaining('OTA Session for Address 12 completed with status: ota_updated. Advancing queue.'));

    // Advancing queue has a 2500ms delay before processing next item
    await vi.advanceTimersByTimeAsync(2500);

    expect(composable.remoteOtaBusyAddress.value).toBe(14);
  });

  it('retry path increments retry count and stops after three attempts', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(2500);

    // Simulate first failure log event
    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    
    // Simulate first failure
    expect(fleetRowHistory.value[12].otaRetryCount).toBe(1);
    expect(loraInventory.value[0].row_state).toBe('ota_retrying');

    // Simulate 2nd failure
    fleetRowHistory.value[12].otaRetryCount = 1;
    loraInventory.value[0].row_state = 'ota_downloading';
    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    expect(fleetRowHistory.value[12].otaRetryCount).toBe(2);

    // Simulate 3rd failure
    fleetRowHistory.value[12].otaRetryCount = 2;
    loraInventory.value[0].row_state = 'ota_downloading';
    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    expect(fleetRowHistory.value[12].otaRetryCount).toBe(3);

    // Simulate 4th failure -> Terminal failure
    fleetRowHistory.value[12].otaRetryCount = 3;
    loraInventory.value[0].row_state = 'ota_downloading';
    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    expect(loraInventory.value[0].row_state).toBe('ota_failed');
    expect(notifyMock).toHaveBeenCalledWith('OTA for Address 12 failed after 3 attempts.');
  });

  it('watchdog triggers retry after 8s stalled activity', async () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();
    const dev: LoraInventoryDevice = { address: 12, row_state: 'ota_downloading' };
    loraInventory.value = [dev];
    
    const now = Date.now();
    fleetRowHistory.value[12] = {
      rowState: 'ota_downloading',
      otaExpectedUntilMs: now + 180000,
      lastOtaActivityMs: now - 9000 // 9s silence
    };

    vi.advanceTimersByTime(1000); // Trigger watchdog interval tick

    expect(notifyMock).toHaveBeenCalledWith('Address 12 OTA chunk progress stalled (8s silence). Retrying...');
    expect(loraInventory.value[0].row_state).toBe('ota_retrying');
  });

  it('watchdog triggers retry after 45s no initial activity', async () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();
    const dev: LoraInventoryDevice = { address: 12, row_state: 'ota_downloading' };
    loraInventory.value = [dev];

    const now = Date.now();
    fleetRowHistory.value[12] = {
      rowState: 'ota_downloading',
      otaExpectedUntilMs: now + 180000 - 46000, // started 46s ago
      lastOtaActivityMs: undefined
    };

    vi.advanceTimersByTime(1000); // Trigger watchdog interval tick

    expect(notifyMock).toHaveBeenCalledWith('Address 12 OTA start request timed out (45s silence). Retrying...');
    expect(loraInventory.value[0].row_state).toBe('ota_retrying');
  });

  it('apply log event moves row to ota_apply_wait', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, row_state: 'ota_downloading' };
    loraInventory.value = [dev];

    composable.handleOtaLogLine('event=ota_pull_control_apply', loraInventory.value[0]);

    expect(loraInventory.value[0].row_state).toBe('ota_apply_wait');
  });

  it('failure log event only triggers retry for active OTA states', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, row_state: 'ota_failed' };
    loraInventory.value = [dev];

    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);

    expect(loraInventory.value[0].row_state).toBe('ota_failed'); // unchanged
  });

  it('cleanup clears follow-up/watchdog timers', () => {
    const composable = createComposable();
    composable.fleetOtaFollowupTimers.value[12] = setTimeout(() => {}, 10000);

    composable.cleanupFleetOtaTimers();

    expect(composable.fleetOtaFollowupTimers.value).toEqual({});
  });

  it('follow-up timer sends inventory follow-up, not remote_ota_pull', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });
    
    composable.flashLoraRemote(dev);
    // Wait for queue delay
    await vi.advanceTimersByTimeAsync(2500);
    
    // Trigger follow-up timer (starts at 2500ms delay)
    await vi.advanceTimersByTimeAsync(2500);
    
    expect(runFollowupInventoryScanMock).toHaveBeenCalledWith(12);
    expect(triggerOtaCommandMock).toHaveBeenCalledTimes(1);
  });

  it('missing gateway/password does not queue or mark a row ota_queued', () => {
    const composable = createComposable();
    checkPreflightMock.mockReturnValue(false);
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];
    
    composable.flashLoraRemote(dev);
    
    expect(loraInventory.value[0].row_state).toBeUndefined();
    expect(otaQueue.value).toHaveLength(0);
  });

  it('watchdog interval is cleaned and does not leak across tests', () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();
    composable.cleanupFleetOtaTimers();
    
    notifyMock.mockClear();
    const dev: LoraInventoryDevice = { address: 12, row_state: 'ota_downloading' };
    loraInventory.value = [dev];
    const now = Date.now();
    fleetRowHistory.value[12] = {
      rowState: 'ota_downloading',
      otaExpectedUntilMs: now + 180000,
      lastOtaActivityMs: now - 9000
    };
    
    vi.advanceTimersByTime(1000);
    expect(notifyMock).not.toHaveBeenCalled();
  });

  it('fleetFlashUnavailableReason preserves original behavior', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    expect(composable.fleetFlashAvailable(dev)).toBe(true);
    expect(composable.fleetFlashUnavailableReason(dev)).toBe('Ready to trigger OTA pull');
  });

  it('public API does not expose watchdog/queue internals unless justified', () => {
    const composable = createComposable();
    expect(composable).not.toHaveProperty('checkOtaProgressWatchdog');
    expect(composable).not.toHaveProperty('processOtaQueue');
  });
});
