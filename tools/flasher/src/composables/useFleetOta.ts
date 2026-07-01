import { ref, Ref, onMounted, onUnmounted, getCurrentInstance, computed } from 'vue';
import { LoraInventoryDevice } from '../types/fleet';

export interface UseFleetOtaOptions {
  loraInventory: Ref<LoraInventoryDevice[]>;
  fleetRowHistory: Ref<Record<number, any>>;
  otaQueue: Ref<LoraInventoryDevice[]>;
  networkFirmwarePath: string;
  checkPreflight: () => boolean;
  triggerOtaCommand: (device: LoraInventoryDevice) => Promise<{
    out: any;
    target: { host: string; port: number };
    sha256: string;
  }>;
  runFollowupInventoryScan: (address: number) => Promise<void>;
  notify: (msg: string) => void;
  pushNetworkLog: (msg: string) => void;
  setNetworkStatusMessage: (msg: string) => void;
  serialFeatureError: (feature: string, err: unknown) => string;
}

const FLEET_OTA_MAX_ACTIVE_PULLS = 6;
const FLEET_OTA_TRIGGER_RETRY_MS = 750;
const FLEET_OTA_TRIGGER_SPACING_MS = 500;

export function useFleetOta(options: UseFleetOtaOptions) {
  const {
    loraInventory,
    fleetRowHistory,
    otaQueue,
    networkFirmwarePath,
    checkPreflight,
    triggerOtaCommand,
    runFollowupInventoryScan,
    notify,
    pushNetworkLog,
    setNetworkStatusMessage,
    serialFeatureError
  } = options;

  const otaTriggerBusyAddress = ref<number | null>(null);
  const activeOtaPullAddresses = ref<number[]>([]);
  const fleetOtaFollowupTimers = ref<Record<number, any>>({});
  
  let watchdogInterval: any = null;

  const hasActiveRemoteOtaPulls = computed(() => activeOtaPullAddresses.value.length > 0);

  function hasActiveOtaPull(address: number): boolean {
    return activeOtaPullAddresses.value.includes(address);
  }

  function addActiveOtaPull(address: number) {
    if (!activeOtaPullAddresses.value.includes(address)) {
      activeOtaPullAddresses.value = [...activeOtaPullAddresses.value, address];
    }
  }

  function removeActiveOtaPull(address: number) {
    activeOtaPullAddresses.value = activeOtaPullAddresses.value.filter(addr => addr !== address);
  }

  function startFleetOtaWatchdog() {
    if (watchdogInterval) clearInterval(watchdogInterval);
    watchdogInterval = setInterval(() => {
      checkOtaProgressWatchdog();
    }, 1000);
  }

  if (getCurrentInstance()) {
    onMounted(() => {
      startFleetOtaWatchdog();
    });
    onUnmounted(() => {
      cleanupFleetOtaTimers();
    });
  }

  function cleanupFleetOtaTimers() {
    if (watchdogInterval) {
      clearInterval(watchdogInterval);
      watchdogInterval = null;
    }
    Object.values(fleetOtaFollowupTimers.value).forEach(timer => clearTimeout(timer));
    fleetOtaFollowupTimers.value = {};
  }

  function fleetFlashAvailable(device: LoraInventoryDevice): boolean {
    return fleetFlashUnavailableReason(device) === 'Ready to trigger OTA pull';
  }

  function fleetFlashUnavailableReason(device: LoraInventoryDevice): string {
    if (!device.fw_version) return 'Running firmware version unknown';
    const state = device.row_state || '';
    if (state === 'ota_queued' || state === 'ota_downloading' || state === 'ota_apply_wait' || state === 'ota_retrying') {
      return 'OTA flash is already active or queued for this device';
    }
    return 'Ready to trigger OTA pull';
  }

  function markFleetOtaPending(device: LoraInventoryDevice) {
    const now = Date.now();
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      lastRawUptimeMs: device.uptime_ms || fleetRowHistory.value[device.address]?.lastRawUptimeMs,
      fwVersion: device.fw_version || fleetRowHistory.value[device.address]?.fwVersion,
      otaExpectedUntilMs: now + 180000,
      rowState: 'ota_downloading',
      rowStateUntilMs: now + 180000
    };
    loraInventory.value = loraInventory.value.map(row =>
      row.address === device.address
        ? { ...row, row_state: 'ota_downloading', row_state_until_ms: now + 180000 }
        : row
    );
  }

  async function refreshFleetOtaFollowup(address: number) {
    const history = fleetRowHistory.value[address];
    if (!history?.otaExpectedUntilMs) return;
    const now = Date.now();
    if (history.rowState === 'ota_rebooted' || history.rowState === 'ota_updated') {
      delete fleetOtaFollowupTimers.value[address];
      reconcileActiveOtaPulls();
      return;
    }
    if (now >= history.otaExpectedUntilMs) {
      fleetRowHistory.value[address] = {
        ...history,
        rowState: 'ota_no_reboot',
        rowStateUntilMs: now + 60000
      };
      loraInventory.value = loraInventory.value.map(row =>
        row.address === address
          ? { ...row, row_state: 'ota_no_reboot', row_state_until_ms: now + 60000 }
          : row
      );
      delete fleetOtaFollowupTimers.value[address];
      reconcileActiveOtaPulls();
      return;
    }
    try {
      await runFollowupInventoryScan(address);
      reconcileActiveOtaPulls();
    } catch (e) {
      pushNetworkLog(serialFeatureError(`Flash follow-up ${address}`, e));
    } finally {
      const nextHistory = fleetRowHistory.value[address];
      if (nextHistory?.otaExpectedUntilMs && Date.now() < nextHistory.otaExpectedUntilMs &&
          nextHistory.rowState !== 'ota_rebooted' && nextHistory.rowState !== 'ota_updated') {
        fleetOtaFollowupTimers.value[address] = setTimeout(() => {
          refreshFleetOtaFollowup(address);
        }, 4000);
      } else {
        delete fleetOtaFollowupTimers.value[address];
      }
    }
  }

  function startFleetOtaFollowup(device: LoraInventoryDevice) {
    const existing = fleetOtaFollowupTimers.value[device.address];
    if (existing) clearTimeout(existing);
    fleetOtaFollowupTimers.value[device.address] = setTimeout(() => {
      refreshFleetOtaFollowup(device.address);
    }, 2500);
  }

  function flashLoraRemote(device: LoraInventoryDevice) {
    if (!checkPreflight()) {
      return;
    }
    if (!fleetFlashAvailable(device)) {
      notify(fleetFlashUnavailableReason(device));
      return;
    }
    if (otaQueue.value.some(d => d.address === device.address)) return;
    if (hasActiveOtaPull(device.address)) return;
    
    otaQueue.value = [...otaQueue.value, device];
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      rowState: 'ota_queued',
      rowStateUntilMs: undefined
    };
    
    loraInventory.value = loraInventory.value.map(row => 
      row.address === device.address ? { ...row, row_state: 'ota_queued' } : row
    );
    
    processOtaQueue();
  }

  function isGatewayBusyError(err: unknown): boolean {
    const msg = String((err as any)?.message || err || '').toLowerCase();
    return msg.includes('gateway_busy') || msg.includes('sm_busy');
  }

  async function triggerOtaFailureOrRetry(device: LoraInventoryDevice) {
    const history = fleetRowHistory.value[device.address] || {};
    const currentRetry = (history.otaRetryCount || 0) + 1;

    removeActiveOtaPull(device.address);
    otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);

    if (currentRetry <= 3) {
      const jitter = Math.floor(Math.random() * 3000);
      const retryDelay = 5000 + jitter;
      notify(`OTA failure detected for Address ${device.address}. Retrying (${currentRetry}/3) in ${(retryDelay / 1000).toFixed(1)} seconds...`);
      
      fleetRowHistory.value[device.address] = {
        ...history,
        rowState: 'ota_retrying',
        rowStateUntilMs: Date.now() + retryDelay + 1000,
        otaRetryCount: currentRetry,
        otaExpectedUntilMs: Date.now() + 180000
      };

      loraInventory.value = loraInventory.value.map(row => 
        row.address === device.address 
          ? { ...row, row_state: 'ota_retrying', row_state_until_ms: Date.now() + retryDelay + 1000 } 
          : row
      );

      setTimeout(() => {
        const checkAndRetry = () => {
          const currentDev = loraInventory.value.find(d => d.address === device.address);
          if (!currentDev || currentDev.row_state !== 'ota_retrying') {
            return;
          }
          notify(`Retrying OTA flash for remote ${device.address} now...`);

          const cleanHistory = fleetRowHistory.value[device.address] || {};
          delete cleanHistory.lastOtaActivityMs;

          // Re-queue by clearing current retrying state and running flashLoraRemote
          fleetRowHistory.value[device.address].rowState = undefined;
          loraInventory.value = loraInventory.value.map(row =>
            row.address === device.address ? { ...row, row_state: undefined } : row
          );
          flashLoraRemote(device);
        };
        checkAndRetry();
      }, retryDelay);
    } else {
      notify(`OTA for Address ${device.address} failed after 3 attempts.`);
      fleetRowHistory.value[device.address] = {
        ...history,
        rowState: 'ota_failed',
        rowStateUntilMs: undefined,
        otaExpectedUntilMs: undefined,
        otaRetryCount: 0
      };
      loraInventory.value = loraInventory.value.map(row => 
        row.address === device.address 
          ? { ...row, row_state: 'ota_failed', row_state_until_ms: undefined } 
          : row
      );
      reconcileActiveOtaPulls();
    }
  }

  function reconcileActiveOtaPulls() {
    const terminalStates = ['ota_updated', 'ota_rebooted', 'ota_failed', 'ota_no_reboot'];
    const activeCopy = [...activeOtaPullAddresses.value];

    let changed = false;
    for (const address of activeCopy) {
      const dev = loraInventory.value.find(d => d.address === address);
      if (!dev || terminalStates.includes(dev.row_state || '')) {
        removeActiveOtaPull(address);
        changed = true;
        if (fleetOtaFollowupTimers.value[address]) {
          clearTimeout(fleetOtaFollowupTimers.value[address]);
          delete fleetOtaFollowupTimers.value[address];
        }
      }
    }
    if (changed) {
      processOtaQueue();
    }
  }

  function checkOtaProgressWatchdog() {
    const now = Date.now();
    loraInventory.value.forEach(device => {
      if (device.row_state === 'ota_downloading') {
        const history = fleetRowHistory.value[device.address] || {};
        const otaStartedMs = history.otaExpectedUntilMs ? history.otaExpectedUntilMs - 180000 : now;
        const lastActivity = history.lastOtaActivityMs;
        
        if (lastActivity) {
          if (now - lastActivity > 8000) {
            notify(`Address ${device.address} OTA chunk progress stalled (8s silence). Retrying...`);
            triggerOtaFailureOrRetry(device);
          }
        } else {
          if (now - otaStartedMs > 45000) {
            notify(`Address ${device.address} OTA start request timed out (45s silence). Retrying...`);
            triggerOtaFailureOrRetry(device);
          }
        }
      }
    });

    reconcileActiveOtaPulls();
  }

  async function processOtaQueue() {
    if (otaQueue.value.length === 0) return;
    if (activeOtaPullAddresses.value.length >= FLEET_OTA_MAX_ACTIVE_PULLS) return;
    if (otaTriggerBusyAddress.value !== null) return;

    const device = otaQueue.value[0];
    let success = false;
    let isBusy = false;
    try {
      otaTriggerBusyAddress.value = device.address;
      
      const { out, target, sha256 } = await triggerOtaCommand(device);

      addActiveOtaPull(device.address);
      markFleetOtaPending(device);
      startFleetOtaFollowup(device);

      setNetworkStatusMessage(`Remote OTA pull triggered for LoRa ${device.address} from ${target.host}:${target.port}.`);
      pushNetworkLog(`Remote OTA pull: addr ${device.address} -> http://${target.host}:${target.port}${networkFirmwarePath} (${out.path || networkFirmwarePath}), SHA256 ${sha256}`);
      notify(`Flash triggered for LoRa ${device.address}`);
      success = true;
    } catch (e: any) {
      if (isGatewayBusyError(e)) {
        isBusy = true;
        // Reschedule without incrementing retry or failing
        setTimeout(() => {
          processOtaQueue();
        }, FLEET_OTA_TRIGGER_RETRY_MS);
      } else {
        const msg = serialFeatureError(`Remote flash ${device.address}`, e);
        setNetworkStatusMessage(msg);
        pushNetworkLog(msg);
        notify(msg);
        delete fleetRowHistory.value[device.address];
        loraInventory.value = loraInventory.value.map(row =>
          row.address === device.address ? { ...row, row_state: undefined, row_state_until_ms: undefined } : row
        );
      }
    } finally {
      otaTriggerBusyAddress.value = null;
      if (success) {
        otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);
        setTimeout(() => processOtaQueue(), FLEET_OTA_TRIGGER_SPACING_MS);
      } else if (!isBusy) {
        otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);
        setTimeout(() => processOtaQueue(), FLEET_OTA_TRIGGER_SPACING_MS);
      }
    }
  }

  function handleOtaLogLine(trimmed: string, dev: LoraInventoryDevice) {
    if (trimmed.includes('event=ota_pull_control_start_rx') || trimmed.includes('event=ota_pull_control_rx')) {
      if (!fleetRowHistory.value[dev.address]) {
        fleetRowHistory.value[dev.address] = {};
      }
      fleetRowHistory.value[dev.address].lastOtaActivityMs = Date.now();
    }

    if (trimmed.includes('event=ota_pull_control_apply')) {
      if (dev.row_state === 'ota_downloading' || dev.row_state === 'ota_pending' || dev.row_state === 'ota_retrying') {
        fleetRowHistory.value[dev.address] = {
          ...(fleetRowHistory.value[dev.address] || {}),
          rowState: 'ota_apply_wait',
          rowStateUntilMs: Date.now() + 120000,
          rebootExpectedUntilMs: Date.now() + 240000
        };
        loraInventory.value = loraInventory.value.map(row => 
          row.address === dev.address ? { ...row, row_state: 'ota_apply_wait', row_state_until_ms: Date.now() + 120000 } : row
        );
      }
    }

    if (trimmed.includes('event=ota_pull_control_failed') ||
        trimmed.includes('event=ota_pull_control_incomplete') ||
        trimmed.includes('event=ota_pull_control_bad_hash')) {
      const activeStates = ['ota_pending', 'ota_downloading', 'ota_apply_wait'];
      if (activeStates.includes(dev.row_state || '')) {
        triggerOtaFailureOrRetry(dev);
      }
    }
  }

  return {
    otaQueue,
    otaTriggerBusyAddress,
    activeOtaPullAddresses,
    hasActiveRemoteOtaPulls,
    fleetOtaFollowupTimers,
    fleetFlashAvailable,
    fleetFlashUnavailableReason,
    flashLoraRemote,
    handleOtaLogLine,
    cleanupFleetOtaTimers,
    startFleetOtaWatchdog
  };
}
