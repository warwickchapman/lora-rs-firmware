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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];

    composable.flashLoraRemote(dev);
    composable.flashLoraRemote(dev);

    expect(otaQueue.value).toHaveLength(1);
  });

  it('successful command lifecycle marks downloading, starts follow-up, logs/notifies', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '/custom.bin' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'abc123hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

    expect(loraInventory.value[0].row_state).toBe('ota_downloading');
    expect(notifyMock).toHaveBeenCalledWith('Flash triggered for LoRa 12');
    expect(pushNetworkLogMock).toHaveBeenCalledWith(
      expect.stringContaining('Remote OTA pull: addr 12 -> http://192.168.0.100:8080/firmware.bin (/custom.bin), SHA256 abc123hash')
    );
  });

  it('failed command clears busy state, removes queue item, resets row state', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockRejectedValue(new Error('connection refused'));

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

    expect(loraInventory.value[0].row_state).toBeUndefined();
    expect(otaQueue.value).toHaveLength(0);
    expect(composable.otaTriggerBusyAddress.value).toBeNull();
  });

  it('terminal row state advances queue', async () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();

    // We want to test capacity limit (max 6 active pulls)
    const devices: LoraInventoryDevice[] = Array.from({ length: 7 }, (_, i) => ({ address: 10 + i, fw_version: '0.9.0' }));
    loraInventory.value = [...devices];

    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    // Flash all 7
    devices.forEach(d => composable.flashLoraRemote(d));

    // Process first 6 with spacing delay of 500ms each
    for (let i = 0; i < 6; i++) {
      await vi.advanceTimersByTimeAsync(500);
    }

    // First 6 should be in active pull addresses
    expect(composable.activeOtaPullAddresses.value).toHaveLength(6);
    expect(composable.activeOtaPullAddresses.value).toContain(10);
    expect(composable.activeOtaPullAddresses.value).toContain(15);
    // 7th should still be in queue
    expect(otaQueue.value).toHaveLength(1);
    expect(otaQueue.value[0].address).toBe(16);

    // Make device 10 (first active) reach a terminal state
    loraInventory.value = loraInventory.value.map(row => 
      row.address === 10 ? { ...row, row_state: 'ota_updated' } : row
    );

    // Run interval watchdog
    vi.advanceTimersByTime(1000);

    // Now 10 is removed, leaving 5 active pulls, queue pumps and triggers 16
    await vi.advanceTimersByTimeAsync(500);

    expect(composable.activeOtaPullAddresses.value).not.toContain(10);
    expect(composable.activeOtaPullAddresses.value).toContain(16);
    expect(otaQueue.value).toHaveLength(0);
  });

  it('retry path increments retry count and stops after three attempts', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

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

  it('does not consume additional retries from repeated failure logs while retry is waiting', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    expect(fleetRowHistory.value[12].otaRetryCount).toBe(1);
    expect(loraInventory.value[0].row_state).toBe('ota_retrying');

    composable.handleOtaLogLine('event=ota_pull_control_failed', loraInventory.value[0]);
    composable.handleOtaLogLine('event=ota_pull_control_incomplete', loraInventory.value[0]);
    composable.handleOtaLogLine('event=ota_pull_control_bad_hash', loraInventory.value[0]);

    expect(fleetRowHistory.value[12].otaRetryCount).toBe(1);
    expect(loraInventory.value[0].row_state).toBe('ota_retrying');
    expect(notifyMock).not.toHaveBeenCalledWith('OTA for Address 12 failed after 3 attempts.');
  });

  it('watchdog triggers retry after 8s stalled activity', async () => {
    const composable = createComposable();
    composable.startFleetOtaWatchdog();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_downloading' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_downloading' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_downloading' };
    loraInventory.value = [dev];

    composable.handleOtaLogLine('event=ota_pull_control_apply', loraInventory.value[0]);

    expect(loraInventory.value[0].row_state).toBe('ota_apply_wait');
  });

  it('failure log event only triggers retry for active OTA states', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_failed' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });
    
    composable.flashLoraRemote(dev);
    // Wait for queue delay
    await vi.advanceTimersByTimeAsync(500);
    
    // Trigger follow-up timer (starts at 2500ms delay)
    await vi.advanceTimersByTimeAsync(2500);
    
    expect(runFollowupInventoryScanMock).toHaveBeenCalledWith(12);
    expect(triggerOtaCommandMock).toHaveBeenCalledTimes(1);
  });

  it('missing gateway/password does not queue or mark a row ota_queued', () => {
    const composable = createComposable();
    checkPreflightMock.mockReturnValue(false);
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_downloading' };
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
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    expect(composable.fleetFlashAvailable(dev)).toBe(true);
    expect(composable.fleetFlashUnavailableReason(dev)).toBe('Ready to trigger OTA pull');
  });

  it('public API does not expose watchdog/queue internals unless justified', () => {
    const composable = createComposable();
    expect(composable).not.toHaveProperty('checkOtaProgressWatchdog');
    expect(composable).not.toHaveProperty('processOtaQueue');
  });

  it('reschedules gateway_busy without incrementing retry or failing', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];

    triggerOtaCommandMock.mockRejectedValueOnce(new Error('gateway_busy'));

    composable.flashLoraRemote(dev);

    // Wait for trigger rejection to propagate
    await vi.advanceTimersByTimeAsync(0);

    expect(otaQueue.value).toContainEqual(expect.objectContaining({ address: 12 }));
    expect(composable.otaTriggerBusyAddress.value).toBeNull();
    expect(fleetRowHistory.value[12]?.otaRetryCount).toBeUndefined();
    expect(notifyMock).not.toHaveBeenCalled();

    // Eventual successful retry
    triggerOtaCommandMock.mockResolvedValueOnce({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    await vi.advanceTimersByTimeAsync(750);

    expect(composable.activeOtaPullAddresses.value).toContain(12);
    expect(otaQueue.value).toHaveLength(0);
  });

  it('duplicate click on active pull does not queue or call trigger again', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '' },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

    expect(composable.activeOtaPullAddresses.value).toContain(12);
    expect(triggerOtaCommandMock).toHaveBeenCalledTimes(1);

    // Duplicate click
    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(500);

    expect(otaQueue.value).toHaveLength(0);
    expect(triggerOtaCommandMock).toHaveBeenCalledTimes(1);
  });

  it('ota_retrying blocks manual retrigger', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', row_state: 'ota_retrying' };
    loraInventory.value = [dev];

    expect(composable.fleetFlashAvailable(dev)).toBe(false);
    expect(composable.fleetFlashUnavailableReason(dev)).toBe('OTA flash is already active or queued for this device');
  });

  it('missing fw_version blocks manual trigger', () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12 };
    loraInventory.value = [dev];

    expect(composable.fleetFlashAvailable(dev)).toBe(false);
    expect(composable.fleetFlashUnavailableReason(dev)).toBe('Running firmware version unknown');
  });
});
