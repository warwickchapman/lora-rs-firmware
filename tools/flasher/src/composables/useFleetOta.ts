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
    targetVersion: string;
  }>;
  queryOtaStatusCommand: (address: number) => Promise<{
    addr: number;
    transfer_id: number;
    stage_code: number;
    stage: string;
    error_code: number;
    timestamp: number;
  }>;
  refreshLoraPeerCommand: (address: number) => Promise<void>;
  notify: (msg: string) => void;
  pushNetworkLog: (msg: string) => void;
  setNetworkStatusMessage: (msg: string) => void;
  serialFeatureError: (feature: string, err: unknown) => string;
}

export function useFleetOta(options: UseFleetOtaOptions) {
  const LATE_OTA_CONFIRMATION_WINDOW_MS = 600000;
  const {
    loraInventory,
    fleetRowHistory,
    otaQueue,
    networkFirmwarePath,
    checkPreflight,
    triggerOtaCommand,
    queryOtaStatusCommand,
    refreshLoraPeerCommand,
    notify,
    pushNetworkLog,
    setNetworkStatusMessage,
    serialFeatureError
  } = options;

  const otaTriggerBusyAddress = ref<number | null>(null);

  interface RemoteOtaSession {
    address: number;
    chipId: string;
    transferId: number;
    stage: 'ota_sending' | 'ota_awaiting_ack' | 'ota_downloading' | 'ota_apply_wait';
    deadlineMs: number;
    lastScanMs: number;
    targetVersion: string;
    preCommandUptimeMs: number;
    refreshRequested: boolean;
    acceptedAtMs: number;
  }

  // LoRa serializes only the manifest handoff. Accepted HTTP pulls continue independently.
  const activeRemoteOta = ref<RemoteOtaSession | null>(null);
  const inFlightRemoteOtas = ref<Record<number, RemoteOtaSession>>({});
  let inFlightStatusCursor = 0;

  let watchdogInterval: any = null;
  let watchdogBusy = false;

  const hasActiveRemoteOtaPulls = computed(() =>
    activeRemoteOta.value !== null || Object.keys(inFlightRemoteOtas.value).length > 0
  );

  function startFleetOtaWatchdog() {
    if (watchdogInterval) clearInterval(watchdogInterval);
    watchdogInterval = setInterval(async () => {
      if (watchdogBusy) return;
      watchdogBusy = true;
      try {
        await checkOtaProgressWatchdog();
      } finally {
        watchdogBusy = false;
      }
    }, 1500);
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
  }

  function fleetFlashAvailable(device: LoraInventoryDevice): boolean {
    return fleetFlashUnavailableReason(device) === 'Ready to trigger OTA pull';
  }

  function fleetFlashUnavailableReason(device: LoraInventoryDevice): string {
    if (!device.fw_version) return 'Running firmware version unknown';
    const state = device.row_state || '';
    if (state === 'ota_queued' || state === 'ota_sending' || state === 'ota_awaiting_ack' || state === 'ota_downloading' || state === 'ota_apply_wait') {
      return 'OTA flash is already active or queued for this device';
    }
    return 'Ready to trigger OTA pull';
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
    if (activeRemoteOta.value?.address === device.address || inFlightRemoteOtas.value[device.address]) return;

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

  async function checkOtaProgressWatchdog() {
    const now = Date.now();
    const handoff = activeRemoteOta.value;
    if (handoff) {
      if (now > handoff.deadlineMs) {
        finishOta(handoff, 'ota_unconfirmed');
        activeRemoteOta.value = null;
        processOtaQueue();
      } else {
        try {
          const status = await queryOtaStatusCommand(handoff.address);
          if (status.addr === handoff.address && status.transfer_id === handoff.transferId) {
            if (status.stage === 'sending') updateOtaState(handoff, 'ota_sending');
            else if (status.stage === 'awaiting_ack') updateOtaState(handoff, 'ota_awaiting_ack');
            else if (status.stage === 'accepted') {
              handoff.acceptedAtMs = now;
              updateOtaState(handoff, 'ota_downloading');
              inFlightRemoteOtas.value[handoff.address] = handoff;
              activeRemoteOta.value = null;
              processOtaQueue();
            } else if (status.stage === 'unconfirmed') {
              finishOta(handoff, 'ota_unconfirmed');
              activeRemoteOta.value = null;
              processOtaQueue();
            } else if (status.stage === 'failed') {
              finishOta(handoff, 'ota_failed', `error_${status.error_code}`);
              activeRemoteOta.value = null;
              processOtaQueue();
            }
          }
        } catch (e) {
          pushNetworkLog(serialFeatureError('Poll remote OTA status', e));
        }
      }
    }

    const sessions = Object.values(inFlightRemoteOtas.value);
    let refreshIssued = false;
    for (const session of sessions) {
      if (now > session.deadlineMs) {
        finishOta(session, 'ota_no_reboot');
        delete inFlightRemoteOtas.value[session.address];
        continue;
      }

      if (session.stage === 'ota_downloading' && now - session.acceptedAtMs >= 8000) {
        updateOtaState(session, 'ota_apply_wait');
      }

      const dev = loraInventory.value.find(d => d.address === session.address);
      if (dev && session.chipId && dev.chip_id === session.chipId && dev.fw_version === session.targetVersion &&
          session.preCommandUptimeMs > 0 && (dev.uptime_ms || 0) < session.preCommandUptimeMs) {
        notify(`Address ${session.address} updated successfully to ${dev.fw_version}!`);
        finishOta(session, 'ota_updated');
        delete inFlightRemoteOtas.value[session.address];
        continue;
      }

      if (!refreshIssued && session.stage === 'ota_apply_wait' && now >= session.lastScanMs && !session.refreshRequested) {
        session.refreshRequested = true;
        refreshIssued = true;
        try {
          await refreshLoraPeerCommand(session.address);
        } catch (e) {
          pushNetworkLog(serialFeatureError(`Inventory confirmation poll ${session.address}`, e));
        }
      }
    }

    // Poll one accepted session per tick for a correlated late download failure.
    const remaining = Object.values(inFlightRemoteOtas.value);
    if (remaining.length > 0) {
      const session = remaining[inFlightStatusCursor % remaining.length];
      inFlightStatusCursor++;
      try {
        const status = await queryOtaStatusCommand(session.address);
        if (status.addr === session.address && status.transfer_id === session.transferId && status.stage === 'failed') {
          finishOta(session, 'ota_failed', `error_${status.error_code}`);
          delete inFlightRemoteOtas.value[session.address];
        }
      } catch (e) {
        pushNetworkLog(serialFeatureError(`Poll remote OTA status ${session.address}`, e));
      }
    }
  }

  function updateOtaState(current: RemoteOtaSession, newStage: RemoteOtaSession['stage']) {
    if (current.stage === newStage) return;

    current.stage = newStage;
    if (newStage === 'ota_apply_wait') {
      current.lastScanMs = Date.now() + 45000; // 45 seconds reboot delay window
      current.refreshRequested = false;
    }

    fleetRowHistory.value[current.address] = {
      ...(fleetRowHistory.value[current.address] || {}),
      rowState: newStage,
      rowStateUntilMs: newStage === 'ota_apply_wait' ? Date.now() + 120000 : undefined
    };
    loraInventory.value = loraInventory.value.map(row =>
      row.address === current.address ? { ...row, row_state: newStage, row_state_until_ms: newStage === 'ota_apply_wait' ? Date.now() + 120000 : undefined } : row
    );
  }

  function finishOta(current: RemoteOtaSession, finalState: 'ota_failed' | 'ota_unconfirmed' | 'ota_updated' | 'ota_no_reboot', reason?: string) {
    const preserveLateConfirmation = finalState === 'ota_no_reboot';
    fleetRowHistory.value[current.address] = {
      ...(fleetRowHistory.value[current.address] || {}),
      rowState: finalState,
      otaReason: reason,
      rowStateUntilMs: undefined,
      otaExpectedUntilMs: preserveLateConfirmation
        ? fleetRowHistory.value[current.address]?.otaExpectedUntilMs
        : undefined,
      otaTargetVersion: preserveLateConfirmation ? current.targetVersion : undefined
    };
    loraInventory.value = loraInventory.value.map(row =>
      row.address === current.address ? { ...row, row_state: finalState, row_state_until_ms: undefined } : row
    );
  }

  async function processOtaQueue() {
    if (otaQueue.value.length === 0) return;
    if (activeRemoteOta.value !== null) return;
    if (otaTriggerBusyAddress.value !== null) return;

    const device = otaQueue.value[0];
    try {
      otaTriggerBusyAddress.value = device.address;

      const { out, target, sha256, targetVersion } = await triggerOtaCommand(device);

      const startedAtMs = Date.now();
      activeRemoteOta.value = {
        address: device.address,
        chipId: device.chip_id || '',
        transferId: out.transfer_id,
        stage: 'ota_sending',
        deadlineMs: startedAtMs + 180000,
        lastScanMs: 0,
        targetVersion: targetVersion || 'unknown',
        preCommandUptimeMs: device.uptime_ms || 0,
        refreshRequested: false,
        acceptedAtMs: 0
      };

      fleetRowHistory.value[device.address] = {
        ...(fleetRowHistory.value[device.address] || {}),
        lastRawUptimeMs: device.uptime_ms,
        fwVersion: device.fw_version,
        rowState: 'ota_sending',
        otaTargetVersion: targetVersion,
        otaExpectedUntilMs: startedAtMs + LATE_OTA_CONFIRMATION_WINDOW_MS
      };

      loraInventory.value = loraInventory.value.map(row =>
        row.address === device.address ? { ...row, row_state: 'ota_sending' } : row
      );

      setNetworkStatusMessage(`Remote OTA pull triggered for LoRa ${device.address} from ${target.host}:${target.port}.`);
      pushNetworkLog(`Remote OTA pull: addr ${device.address} -> http://${target.host}:${target.port}${networkFirmwarePath} (${out.path || networkFirmwarePath}), SHA256 ${sha256}, transfer ${out.transfer_id}`);
      notify(`Flash triggered for LoRa ${device.address}`);

      otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);
    } catch (e: any) {
      if (isGatewayBusyError(e)) {
        setTimeout(() => {
          processOtaQueue();
        }, 1000);
      } else {
        const msg = serialFeatureError(`Remote flash ${device.address}`, e);
        setNetworkStatusMessage(msg);
        pushNetworkLog(msg);
        notify(msg);
        delete fleetRowHistory.value[device.address];
        loraInventory.value = loraInventory.value.map(row =>
          row.address === device.address ? { ...row, row_state: undefined, row_state_until_ms: undefined } : row
        );
        otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);
      }
    } finally {
      otaTriggerBusyAddress.value = null;
    }
  }

  return {
    otaQueue,
    otaTriggerBusyAddress,
    activeRemoteOta,
    inFlightRemoteOtas,
    hasActiveRemoteOtaPulls,
    fleetFlashAvailable,
    fleetFlashUnavailableReason,
    flashLoraRemote,
    cleanupFleetOtaTimers,
    startFleetOtaWatchdog,
    checkOtaProgressWatchdog
  };
}
