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
  const queryOtaStatusCommandMock = vi.fn();
  const refreshLoraPeerCommandMock = vi.fn().mockResolvedValue(undefined);
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
    queryOtaStatusCommandMock.mockClear();
    refreshLoraPeerCommandMock.mockClear();
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
      queryOtaStatusCommand: queryOtaStatusCommandMock,
      refreshLoraPeerCommand: refreshLoraPeerCommandMock,
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
      out: { path: '', transfer_id: 42 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'abc123hash',
      targetVersion: '0.10.0'
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

  it('successful command lifecycle marks sending, logs/notifies', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '/custom.bin', transfer_id: 42 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'abc123hash',
      targetVersion: '0.10.0'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(0);

    expect(loraInventory.value[0].row_state).toBe('ota_sending');
    expect(composable.activeRemoteOta.value?.transferId).toBe(42);
    expect(composable.activeRemoteOta.value?.targetVersion).toBe('0.10.0');
    expect(fleetRowHistory.value[12].otaTargetVersion).toBe('0.10.0');
    expect(fleetRowHistory.value[12].otaExpectedUntilMs).toBeGreaterThan(Date.now());
    expect(notifyMock).toHaveBeenCalledWith('Flash triggered for LoRa 12');
  });

  it('failed command clears busy state, removes queue item, resets row state', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0' };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockRejectedValue(new Error('connection refused'));

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(0);

    expect(loraInventory.value[0].row_state).toBeUndefined();
    expect(otaQueue.value).toHaveLength(0);
    expect(composable.activeRemoteOta.value).toBeNull();
  });

  it('second selected remote remains queued while one operation is active', async () => {
    const composable = createComposable();

    const dev1: LoraInventoryDevice = { address: 10, fw_version: '0.9.0' };
    const dev2: LoraInventoryDevice = { address: 11, fw_version: '0.9.0' };
    loraInventory.value = [dev1, dev2];

    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '', transfer_id: 100 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash',
      targetVersion: '0.10.0'
    });

    composable.flashLoraRemote(dev1);
    composable.flashLoraRemote(dev2);

    await vi.advanceTimersByTimeAsync(0);

    expect(composable.activeRemoteOta.value?.address).toBe(10);
    expect(otaQueue.value).toHaveLength(1);
    expect(otaQueue.value[0].address).toBe(11);
  });

  it('starts the next RF handoff as soon as the previous manifest is accepted', async () => {
    const composable = createComposable();
    const dev1: LoraInventoryDevice = { address: 10, chip_id: 'chip10', fw_version: '0.9.0', uptime_ms: 100000 };
    const dev2: LoraInventoryDevice = { address: 11, chip_id: 'chip11', fw_version: '0.9.0', uptime_ms: 100000 };
    loraInventory.value = [dev1, dev2];

    triggerOtaCommandMock
      .mockResolvedValueOnce({ out: { path: '', transfer_id: 100 }, target: { host: '192.168.0.100', port: 8080 }, sha256: 'hash', targetVersion: '0.10.0' })
      .mockResolvedValueOnce({ out: { path: '', transfer_id: 101 }, target: { host: '192.168.0.100', port: 8080 }, sha256: 'hash', targetVersion: '0.10.0' });

    composable.flashLoraRemote(dev1);
    composable.flashLoraRemote(dev2);
    await vi.advanceTimersByTimeAsync(0);

    queryOtaStatusCommandMock.mockResolvedValue({ addr: 10, transfer_id: 100, stage: 'accepted', error_code: 0 });
    await composable.checkOtaProgressWatchdog();
    await vi.advanceTimersByTimeAsync(0);

    expect(composable.inFlightRemoteOtas.value[10]?.stage).toBe('ota_downloading');
    expect(composable.activeRemoteOta.value?.address).toBe(11);
    expect(otaQueue.value).toHaveLength(0);
  });

  it('watchdog handles status polling transitions and ignores non-matching status', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, fw_version: '0.9.0', uptime_ms: 50000 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '', transfer_id: 42 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash',
      targetVersion: '0.9.0'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(0);
    expect(composable.activeRemoteOta.value?.stage).toBe('ota_sending');

    // Status: unmatched/idle -> should be ignored (does not transition to apply_wait or change anything)
    queryOtaStatusCommandMock.mockResolvedValue({
      addr: 0,
      transfer_id: 0,
      stage: 'idle',
      error_code: 0
    });
    await composable.checkOtaProgressWatchdog();
    expect(composable.activeRemoteOta.value?.stage).toBe('ota_sending');

    // Status: sending -> awaiting_ack
    queryOtaStatusCommandMock.mockResolvedValue({
      addr: 12,
      transfer_id: 42,
      stage: 'awaiting_ack',
      error_code: 0
    });
    await composable.checkOtaProgressWatchdog();
    expect(composable.activeRemoteOta.value?.stage).toBe('ota_awaiting_ack');

    // Status: awaiting_ack -> accepted (ota_downloading)
    queryOtaStatusCommandMock.mockResolvedValue({
      addr: 12,
      transfer_id: 42,
      stage: 'accepted',
      error_code: 0
    });
    await composable.checkOtaProgressWatchdog();
    expect(composable.activeRemoteOta.value).toBeNull();
    expect(composable.inFlightRemoteOtas.value[12]?.stage).toBe('ota_downloading');
  });

  it('same-version reflash confirmation requires target version, correct chip ID, and lower uptime', async () => {
    const composable = createComposable();
    const dev: LoraInventoryDevice = { address: 12, chip_id: 'chip123', fw_version: '0.9.0', uptime_ms: 100000 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '', transfer_id: 42 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash',
      targetVersion: '0.9.0'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(0);

    queryOtaStatusCommandMock.mockResolvedValue({ addr: 12, transfer_id: 42, stage: 'accepted', error_code: 0 });
    await composable.checkOtaProgressWatchdog();
    const session = composable.inFlightRemoteOtas.value[12];
    session.stage = 'ota_apply_wait';
    session.lastScanMs = Date.now() - 1000;
    session.refreshRequested = false;

    // Trigger confirmation scan
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]?.refreshRequested).toBe(true);
    expect(refreshLoraPeerCommandMock).toHaveBeenCalledWith(12);

    // Mock wrong chip ID -> success not triggered
    loraInventory.value = [{ address: 12, chip_id: 'chip999', fw_version: '0.9.0', uptime_ms: 5000 }];
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]).toBeDefined();

    // Mock same-version but uptime is higher (not rebooted yet) -> success not triggered
    loraInventory.value = [{ address: 12, chip_id: 'chip123', fw_version: '0.9.0', uptime_ms: 100010 }];
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]).toBeDefined();

    // Mock same-version, correct chip ID, and uptime is lower (rebooted!) -> success confirmed!
    loraInventory.value = [{ address: 12, chip_id: 'chip123', fw_version: '0.9.0', uptime_ms: 5000 }];
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]).toBeUndefined();
    expect(loraInventory.value[0].row_state).toBe('ota_updated');
  });

  it('fails confirmation if baseline uptime is unknown (preCommandUptimeMs is 0)', async () => {
    const composable = createComposable();
    // Device uptime is unknown/0
    const dev: LoraInventoryDevice = { address: 12, chip_id: 'chip123', fw_version: '0.9.0', uptime_ms: 0 };
    loraInventory.value = [dev];
    triggerOtaCommandMock.mockResolvedValue({
      out: { path: '', transfer_id: 42 },
      target: { host: '192.168.0.100', port: 8080 },
      sha256: 'hash',
      targetVersion: '0.9.0'
    });

    composable.flashLoraRemote(dev);
    await vi.advanceTimersByTimeAsync(0);

    queryOtaStatusCommandMock.mockResolvedValue({ addr: 12, transfer_id: 42, stage: 'accepted', error_code: 0 });
    await composable.checkOtaProgressWatchdog();
    const session = composable.inFlightRemoteOtas.value[12];
    session.stage = 'ota_apply_wait';
    session.lastScanMs = Date.now() - 1000;
    session.refreshRequested = false;

    // Trigger confirmation scan
    await composable.checkOtaProgressWatchdog();

    // Even if remote reports expected version and uptime > 0, it should not confirm because baseline was 0
    loraInventory.value = [{ address: 12, chip_id: 'chip123', fw_version: '0.9.0', uptime_ms: 5000 }];
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]).toBeDefined();

    // Advance beyond watchdog overall deadline (3 minutes) to force timeout/failure
    vi.advanceTimersByTime(200000);
    await composable.checkOtaProgressWatchdog();
    expect(composable.inFlightRemoteOtas.value[12]).toBeUndefined();
    expect(loraInventory.value[0].row_state).toBe('ota_no_reboot');
  });
});
