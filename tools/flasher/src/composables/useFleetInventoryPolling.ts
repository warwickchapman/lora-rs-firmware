import { ref, Ref } from 'vue';

export interface UseFleetInventoryPollingOptions {
  activeMode: Ref<string>;
  fleetTransport: Ref<'serial' | 'mqtt'>;
  gatewaySelectedPort: Ref<string>;
  selectedMqttGatewayChipId: Ref<string>;
  isLoraInventoryScanning: Ref<boolean>;
  refreshGatewaySnapshot: (port: string, background: boolean, source: 'fleet' | 'monitor') => Promise<void>;
  fleetScanPollIntervalMs?: number;
  fleetCachePollIntervalMs?: number;
}

export interface AutoLoadFleetCacheState {
  activeMode: string;
  target: string;
  transport: 'serial' | 'mqtt';
  mqttConnected: boolean;
  mqttGatewayDiscovered: boolean;
  mqttAdminReady: boolean;
  serialTargetAvailable: boolean;
  changeBlocked: boolean;
  loadedTarget: string;
  attemptedTarget: string;
}

export function shouldAutoLoadFleetCache(state: AutoLoadFleetCacheState): boolean {
  if (state.activeMode !== 'network' || !state.target || state.changeBlocked) return false;
  if (state.loadedTarget === state.target || state.attemptedTarget === state.target) return false;
  if (state.transport === 'mqtt') {
    return state.mqttConnected && state.mqttGatewayDiscovered && state.mqttAdminReady;
  }
  return state.serialTargetAvailable;
}

export function shouldClearScanStateOnTimeout(
  isScanning: boolean,
  scanActiveSinceMs: number,
  nowMs: number,
  scanStaleThresholdMs = 75000
): boolean {
  if (!isScanning || scanActiveSinceMs <= 0) return false;
  return (nowMs - scanActiveSinceMs) > scanStaleThresholdMs;
}

export function observedInventoryRefreshMode(discoveryActive: boolean): 'cache_only' | 'forced' {
  return discoveryActive ? 'cache_only' : 'forced';
}

export function useFleetInventoryPolling(options: UseFleetInventoryPollingOptions) {
  const networkInventoryPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
  const networkInventoryPollMode = ref<'cache' | 'scan' | null>(null);
  const isFleetScanPollingActive = ref(false);

  const scanInterval = options.fleetScanPollIntervalMs ?? 1200;
  const cacheInterval = options.fleetCachePollIntervalMs ?? 5000;

  async function refreshLoraInventoryStatus(background = true) {
    const targetPort = options.fleetTransport.value === 'mqtt' ? options.selectedMqttGatewayChipId.value : options.gatewaySelectedPort.value;
    await options.refreshGatewaySnapshot(targetPort, background, 'fleet');
  }

  function startLoraInventoryPolling() {
    stopLoraInventoryPolling(false, false);
    networkInventoryPollMode.value = 'scan';
    const interval = options.fleetTransport.value === 'mqtt' ? 4000 : scanInterval;
    networkInventoryPollTimer.value = setInterval(async () => {
      if (isFleetScanPollingActive.value) return;
      isFleetScanPollingActive.value = true;
      try {
        await refreshLoraInventoryStatus();
      } finally {
        isFleetScanPollingActive.value = false;
      }
    }, interval) as any;
  }

  function startFleetCachePolling() {
    const targetPort = options.fleetTransport.value === 'mqtt'
      ? options.selectedMqttGatewayChipId.value
      : options.gatewaySelectedPort.value;
    if (options.activeMode.value !== 'network' || !targetPort || options.isLoraInventoryScanning.value) return;
    if (networkInventoryPollTimer.value && networkInventoryPollMode.value === 'cache') return;
    stopLoraInventoryPolling(false, false);
    networkInventoryPollMode.value = 'cache';
    networkInventoryPollTimer.value = setInterval(async () => {
      if (isFleetScanPollingActive.value) return;
      isFleetScanPollingActive.value = true;
      try {
        await refreshLoraInventoryStatus(true);
      } finally {
        isFleetScanPollingActive.value = false;
      }
    }, cacheInterval) as any;
  }

  function stopLoraInventoryPolling(markIdle = true, clearMode = true) {
    if (networkInventoryPollTimer.value) {
      clearInterval(networkInventoryPollTimer.value as any);
      networkInventoryPollTimer.value = null;
    }
    if (clearMode) networkInventoryPollMode.value = null;
    if (markIdle) options.isLoraInventoryScanning.value = false;
  }

  return {
    networkInventoryPollTimer,
    networkInventoryPollMode,
    isFleetScanPollingActive,
    refreshLoraInventoryStatus,
    startLoraInventoryPolling,
    startFleetCachePolling,
    stopLoraInventoryPolling
  };
}
