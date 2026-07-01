import { ref, computed, Ref } from 'vue';
import {
  SensorReading,
  LoraInventoryDevice,
  LoraAdoptionCandidate,
  LoraInventoryStatus
} from '../types/fleet';
import { ParsedVersion } from '../utils/versionHelper';

export interface UseFleetInventoryOptions {
  otaQueue: Ref<LoraInventoryDevice[]>;
  selectedFirmwareCandidateVersion: () => string | null;
  fleetClockMs: Ref<number>;
  isLoraInventoryScanning: Ref<boolean>;
  canonicalChipId: (raw: string | undefined | null) => string;
  normalizeRole: (role: string | undefined | null) => string;
  parseVersion: (v: string) => ParsedVersion | null;
  compareParsedVersions: (a: ParsedVersion | null, b: ParsedVersion | null) => number;
  activeGatewayId?: () => string | null;
}

export interface TelemetryCacheEntry {
  gateway_id: string;
  address: number;
  chip_id?: string;
  device: Partial<LoraInventoryDevice>;
  sensors: Record<string, SensorReading>;
}

export const CANDIDATE_RECENT_IDENTITY_MS = 90000;

export function useFleetInventory(options: UseFleetInventoryOptions) {
  const loraInventory = ref<LoraInventoryDevice[]>([]);
  const loraInventoryScan = ref<LoraInventoryStatus['scan'] | null>(null);
  const loraCandidates = ref<LoraAdoptionCandidate[]>([]);
  const fleetRowHistory = ref<Record<number, any>>({});
  const fleetForceScanCooldownUntilMs = ref(0);
  const telemetryCache = ref<Record<string, TelemetryCacheEntry>>({});

  function normalizeGatewayId(id: string | undefined | null): string {
    if (!id) return '';
    const norm = id.toLowerCase().trim();
    if (norm.startsWith('lrs-')) {
      return norm.substring(4);
    }
    return norm;
  }

  function applyCacheToRow(row: LoraInventoryDevice, cacheEntry: TelemetryCacheEntry) {
    if (row.chip_id && cacheEntry.chip_id) {
      const rowCanon = options.canonicalChipId(row.chip_id);
      const cacheCanon = options.canonicalChipId(cacheEntry.chip_id);
      if (rowCanon !== cacheCanon) {
        row.conflict_chip_id = cacheEntry.chip_id;
        return;
      } else {
        row.conflict_chip_id = undefined;
      }
    }

    Object.assign(row, cacheEntry.device);
    const sensorList = Object.values(cacheEntry.sensors);
    if (sensorList.length > 0) {
      if (!row.sensors) row.sensors = [];
      for (const cachedSensor of sensorList) {
        let existing = row.sensors.find(s => s.kind === cachedSensor.kind && s.instance === cachedSensor.instance);
        if (!existing) {
          existing = { kind: cachedSensor.kind, instance: cachedSensor.instance, state: 'waiting' };
          row.sensors.push(existing);
        }
        if (cachedSensor.value !== undefined) {
          existing.value = cachedSensor.value;
        }
        if (cachedSensor.state !== undefined) {
          existing.state = cachedSensor.state;
        }
      }
    }
  }

  function findInventoryRow(address: number, cacheEntry: TelemetryCacheEntry) {
    return loraInventory.value.find(d => {
      if (d.address !== address) return false;
      if (d.chip_id && cacheEntry.chip_id) {
        return options.canonicalChipId(d.chip_id) === cacheEntry.chip_id;
      }
      return true;
    });
  }

  function removeSensorFromRow(row: LoraInventoryDevice, kind: SensorReading['kind'], instance: number) {
    if (!row.sensors) return;
    row.sensors = row.sensors.filter(s => !(s.kind === kind && s.instance === instance));
    if (row.sensors.length === 0) {
      row.sensors = undefined;
    }
  }

  function classifyFleetRow(row: LoraInventoryDevice, now = Date.now()): LoraInventoryDevice {
    const history = fleetRowHistory.value[row.address] || {};
    let rowState = history.rowState;
    let rowStateUntilMs = history.rowStateUntilMs;
    const uptime = Number(row.uptime_ms || 0);
    const previousUptime = Number(history.lastRawUptimeMs || 0);
    let otaExpected = Number(history.otaExpectedUntilMs || 0) > now;
    const knownReboot = history.knownRebootUntilMs && history.knownRebootUntilMs > now;
    const expectedReboot = history.rebootExpectedUntilMs && history.rebootExpectedUntilMs > now;

    if (previousUptime > 0 && uptime > 0 && uptime + 30000 < previousUptime) {
      if (history.rowState === 'ota_updated' && knownReboot) {
        rowState = 'ota_updated';
        rowStateUntilMs = history.rowStateUntilMs;
      } else {
        const isExpected = expectedReboot || otaExpected || knownReboot;
        rowState = isExpected ? 'ota_rebooted' : 'unexpected_reboot';
        rowStateUntilMs = now + (isExpected ? 20000 : 60000);
        history.rebootExpectedUntilMs = undefined;
      }
    }

    if (otaExpected && history.fwVersion && row.fw_version && history.fwVersion !== row.fw_version) {
      rowState = 'ota_updated';
      rowStateUntilMs = now + 30000;
    }

    const activeOtaStates = ['ota_pending', 'ota_downloading', 'ota_apply_wait', 'ota_retrying', 'ota_queued'];
    const targetVersion = options.parseVersion(options.selectedFirmwareCandidateVersion() || '');
    const reportedVersion = options.parseVersion(row.fw_version || '');
    if (row.fw_version && targetVersion && reportedVersion &&
        options.compareParsedVersions(reportedVersion, targetVersion) >= 0 &&
        activeOtaStates.includes(rowState || '')) {
      rowState = 'ota_updated';
      rowStateUntilMs = now + 30000;
    }

    let otaExpectedUntilMs = history.otaExpectedUntilMs;
    if (rowState === 'ota_updated') {
      otaExpectedUntilMs = undefined;
      otaExpected = false;
      history.knownRebootUntilMs = now + 120000;
    }

    if (rowState === 'ota_queued' && options.otaQueue.value.some(d => d.address === row.address)) {
      rowStateUntilMs = undefined;
    }

    if (rowStateUntilMs && rowStateUntilMs <= now) {
      rowState = otaExpected ? (rowState || 'ota_downloading') : undefined;
      rowStateUntilMs = otaExpected ? otaExpectedUntilMs : undefined;
    }

    if (otaExpectedUntilMs && otaExpectedUntilMs <= now && (rowState === 'ota_pending' || rowState === 'ota_downloading' || rowState === 'ota_apply_wait' || rowState === 'ota_retrying')) {
      rowState = 'ota_no_reboot';
      rowStateUntilMs = now + 60000;
    }

    let lastTelemetryTimestamp = history.lastTelemetryTimestamp;
    if (row.age_ms !== undefined && row.age_ms !== null && row.age_ms < 600000) {
      lastTelemetryTimestamp = now - row.age_ms;
    }
    const ageMs = (row.age_ms !== undefined && row.age_ms !== null) ? row.age_ms : (lastTelemetryTimestamp ? (now - lastTelemetryTimestamp) : undefined);

    const powerSaveListenOnly = (row.power_save_listen_only !== undefined && row.power_save_listen_only !== null) ? row.power_save_listen_only : history.power_save_listen_only;
    const powerSaveActive = (row.power_save_active !== undefined && row.power_save_active !== null) ? row.power_save_active : history.power_save_active;
    const isPowerSaveConfigured = !!powerSaveListenOnly;
    const isPowerSaveDeferred = isPowerSaveConfigured && !powerSaveActive;

    const fwVersion = row.fw_version || history.fwVersion;
    const fwBuild = row.fw_build || history.fw_build;

    const wifiConnected = ((row.wifi_connected_known ? row.wifi_connected : history.wifi_connected) ?? false);
    const wifiConnectedKnown = row.wifi_connected_known || history.wifi_connected_known || false;
    const ip = wifiConnected ? (row.ip || history.ip) : undefined;

    const wifiEnabled = ((row.wifi_enabled_known ? row.wifi_enabled : history.wifi_enabled) ?? false);
    const wifiEnabledKnown = row.wifi_enabled_known || history.wifi_enabled_known || false;
    const mqttConnected = ((row.mqtt_known ? row.mqtt_connected : history.mqtt_connected) ?? false);
    const mqttEnabled = ((row.mqtt_known ? row.mqtt_enabled : history.mqtt_enabled) ?? false);
    const mqttKnown = row.mqtt_known || history.mqtt_known || false;

    let pendingPowerSaveListenOnly = history.pendingPowerSaveListenOnly;
    let pendingPowerSaveTxMs = history.pendingPowerSaveTxMs;

    const wifiPendingOffline = wifiConnected && (
      (isPowerSaveConfigured && powerSaveActive) ||
      pendingPowerSaveListenOnly === true
    );

    if (row.power_save_listen_only !== undefined && row.power_save_listen_only !== null && pendingPowerSaveListenOnly !== undefined) {
      if (row.power_save_listen_only === pendingPowerSaveListenOnly) {
        pendingPowerSaveListenOnly = undefined;
        pendingPowerSaveTxMs = undefined;
      }
    }

    if (pendingPowerSaveTxMs && now - pendingPowerSaveTxMs > 180000) {
      pendingPowerSaveListenOnly = undefined;
      pendingPowerSaveTxMs = undefined;
    }

    const heapFree = row.heap_free || history.heap_free;
    const heapMaxBlock = row.heap_max_block || history.heap_max_block;
    const heapFragPct = row.heap_frag_pct || history.heap_frag_pct;

    const relayState = (row.relay_state !== undefined && row.relay_state !== null) ? row.relay_state : history.relay_state;
    const inputStateKnown = row.input_state_known === true;
    const inputState = inputStateKnown ? row.input_state : undefined;

    const rssi = (row.rssi !== undefined && row.rssi !== null && row.rssi !== 0 && row.rssi !== -127) ? row.rssi : history.rssi;
    let wifiRssiDbm: number | undefined = undefined;
    if (row.wifi_rssi_dbm !== undefined && row.wifi_rssi_dbm !== null) {
      wifiRssiDbm = row.wifi_rssi_dbm === 0 ? undefined : row.wifi_rssi_dbm;
    } else {
      wifiRssiDbm = history.wifi_rssi_dbm;
    }

    fleetRowHistory.value[row.address] = {
      ...history,
      lastRawUptimeMs: uptime || history.lastRawUptimeMs,
      fwVersion,
      fw_build: fwBuild,
      ip,
      wifi_connected: wifiConnected,
      wifi_connected_known: wifiConnectedKnown,
      wifi_enabled: wifiEnabled,
      wifi_enabled_known: wifiEnabledKnown,
      mqtt_connected: mqttConnected,
      mqtt_enabled: mqttEnabled,
      mqtt_known: mqttKnown,
      power_save_listen_only: powerSaveListenOnly,
      power_save_active: powerSaveActive,
      pendingPowerSaveListenOnly,
      pendingPowerSaveTxMs,
      wifi_pending_offline: wifiPendingOffline,
      power_save_deferred: isPowerSaveDeferred,
      heap_free: heapFree,
      heap_max_block: heapMaxBlock,
      heap_frag_pct: heapFragPct,
      relay_state: relayState,
      input_state: inputState,
      input_state_known: inputStateKnown,
      rssi,
      wifi_rssi_dbm: wifiRssiDbm,
      lastTelemetryTimestamp,
      rowState,
      rowStateUntilMs,
      otaExpectedUntilMs,
      knownRebootUntilMs: history.knownRebootUntilMs,
      rebootExpectedUntilMs: history.rebootExpectedUntilMs
    };

    return {
      ...row,
      fw_version: fwVersion,
      fw_build: fwBuild,
      ip,
      wifi_connected_known: wifiConnectedKnown,
      wifi_connected: wifiConnected,
      wifi_enabled_known: wifiEnabledKnown,
      wifi_enabled: wifiEnabled,
      mqtt_known: mqttKnown,
      mqtt_connected: mqttConnected,
      mqtt_enabled: mqttEnabled,
      power_save_listen_only: powerSaveListenOnly,
      power_save_active: powerSaveActive,
      pending_power_save_listen_only: pendingPowerSaveListenOnly,
      pending_power_save_tx_ms: pendingPowerSaveTxMs,
      wifi_pending_offline: wifiPendingOffline,
      power_save_deferred: isPowerSaveDeferred,
      heap_free: heapFree,
      heap_max_block: heapMaxBlock,
      heap_frag_pct: heapFragPct,
      relay_state: relayState,
      input_state: inputState,
      input_state_known: inputStateKnown,
      rssi: rssi ?? row.rssi,
      wifi_rssi_dbm: wifiRssiDbm,
      age_ms: ageMs,
      row_state: rowState,
      row_state_until_ms: rowStateUntilMs
    };
  }

  function rowFreshness(row: LoraInventoryDevice): 'live' | 'stale' | 'offline' | 'unknown' {
    const age = Number(row.age_ms || 0);
    if (!row.age_ms && row.age_ms !== 0) return 'unknown';
    if (age <= 90000) return 'live';
    if (age <= 180000) return 'stale';
    return 'offline';
  }

  function fleetRowStatusLabel(device: LoraInventoryDevice): string {
    if (device.row_state === 'unexpected_reboot') return 'Unexpected reboot';
    if (device.row_state === 'ota_failed') return 'OTA Failed';
    if (device.row_state === 'ota_updated') return 'Updated';
    if (device.row_state === 'ota_rebooted') return 'Rebooted';
    if (device.row_state === 'ota_no_reboot') return 'No reboot seen';
    if (device.row_state === 'ota_pending') return 'Waiting for reboot';
    if (device.row_state === 'ota_downloading') return 'Downloading OTA...';
    if (device.row_state === 'ota_apply_wait') return 'Waiting for reboot';
    if (device.row_state === 'ota_retrying') {
      const history = fleetRowHistory.value[device.address] || {};
      const retryCount = history.otaRetryCount || 0;
      return `Retrying (${retryCount}/3)...`;
    }
    if (device.row_state === 'ota_queued') {
      const qIdx = options.otaQueue.value.findIndex(d => d.address === device.address);
      return qIdx >= 0 ? `Queued for OTA (#${qIdx + 1})` : 'Queued for OTA';
    }
    return '';
  }

  function candidateStateText(c: LoraAdoptionCandidate): string {
    if (c.state === 'seen_address_only') return 'identifying';
    if (c.state === 'identified') return 'ready to adopt';
    if (c.state === 'readdressing') return 'adopting';
    if (c.state === 'reset_requested') return 'reset requested';
    if (c.state === 'failed') {
      if (c.chip_id) {
        return 'ready to retry adopt';
      } else {
        const ageMs = c.age_ms ?? 0;
        return ageMs < CANDIDATE_RECENT_IDENTITY_MS ? 'retrying identity' : 'identity failed';
      }
    }
    return c.state;
  }

  function candidateStateClass(c: LoraAdoptionCandidate): string {
    if (c.state === 'adopted') return 'text-emerald-400';
    if (c.state === 'readdressing') return 'text-sky-400 animate-pulse';
    if (c.state === 'reset_requested') return 'text-amber-400 animate-pulse';
    if (c.state === 'seen_address_only') return 'text-slate-400 animate-pulse';
    if (c.state === 'identified') return 'text-slate-300';
    if (c.state === 'failed') {
      if (c.chip_id) {
        return 'text-amber-400';
      } else {
        const ageMs = c.age_ms ?? 0;
        return ageMs < CANDIDATE_RECENT_IDENTITY_MS ? 'text-slate-400 animate-pulse' : 'text-rose-400';
      }
    }
    return 'text-slate-400';
  }

  function fleetDeviceUdpLabel(device: LoraInventoryDevice): string {
    const chip = String(device.chip_id || '').trim().replace(/^0x/i, '').toLowerCase();
    const name = chip ? `lrs-${chip}` : `addr ${device.address}`;
    const role = [device.role, device.mode].filter(Boolean).join('/');
    return `${name} addr ${device.address}${role ? ` (${role})` : ''}`;
  }

  const selectedLoraInventoryCount = computed(() => loraInventory.value.filter(d => d.selected).length);

  const hasAnyRemoteIp = computed(() => {
    return loraInventory.value.some(d => !!d.ip);
  });

  const fleetForceScanCooldownRemainingMs = computed(() =>
    Math.max(0, fleetForceScanCooldownUntilMs.value - options.fleetClockMs.value)
  );

  const fleetForceScanLabel = computed(() => {
    if (options.isLoraInventoryScanning.value) return 'Stop scan';
    const remaining = Math.ceil(fleetForceScanCooldownRemainingMs.value / 1000);
    return remaining > 0 ? `Scan ${remaining}s` : 'Scan';
  });

  const loraInventoryProgressLabel = computed(() => {
    const scan = loraInventoryScan.value;
    if (!scan) return 'Idle';
    if (scan.active) {
      return 'Scanning configured remotes and same-key candidates...';
    }
    return `Complete, ${scan.sent || 0} probes sent`;
  });

  const remotesAndCandidatesStatusLine = computed(() => {
    const remotesCount = loraInventory.value.length;
    const remotesLabel = `${remotesCount} remote${remotesCount === 1 ? '' : 's'} configured`;
    
    const total = loraCandidates.value.length;
    if (total === 0) {
      return `${remotesLabel} · 0 candidates`;
    }
    
    if (total === 1) {
      const c = loraCandidates.value[0];
      return `${remotesLabel} · 1 candidate ${candidateStateText(c)}`;
    }
    
    const identifying = loraCandidates.value.filter(c =>
      c.state === 'seen_address_only' ||
      (c.state === 'failed' && !c.chip_id && (c.age_ms ?? 0) < CANDIDATE_RECENT_IDENTITY_MS)
    ).length;
    const ready = loraCandidates.value.filter(c => (c.state === 'identified' || c.state === 'failed') && !!c.chip_id).length;
    const adopting = loraCandidates.value.filter(c => c.state === 'readdressing').length;
    const resetting = loraCandidates.value.filter(c => c.state === 'reset_requested').length;
    const adopted = loraCandidates.value.filter(c => c.state === 'adopted').length;
    const failed = loraCandidates.value.filter(c =>
      c.state === 'failed' && !c.chip_id && (c.age_ms ?? 0) >= CANDIDATE_RECENT_IDENTITY_MS
    ).length;
    
    const parts: string[] = [];
    if (identifying > 0) parts.push(`${identifying} identifying`);
    if (ready > 0) parts.push(`${ready} ready`);
    if (adopting > 0) parts.push(`${adopting} adopting`);
    if (resetting > 0) parts.push(`${resetting} resetting`);
    if (adopted > 0) parts.push(`${adopted} adopted`);
    if (failed > 0) parts.push(`${failed} failed`);
    
    if (parts.length === 0) {
      return `${remotesLabel} · ${total} candidates`;
    }
    return `${remotesLabel} · ${total} candidates: ${parts.join(', ')}`;
  });

  function mergeInventoryRows(rows: LoraInventoryDevice[]): LoraInventoryDevice[] {
    const selected = new Set(loraInventory.value.filter(d => d.selected).map(d => d.address));
    const now = Date.now();
    const activeGwId = normalizeGatewayId(options.activeGatewayId ? options.activeGatewayId() : null);

    const processed = rows
      .slice()
      .sort((a, b) => a.address - b.address)
      .map(row => {
        const cacheKey = `${activeGwId}:${row.address}`;
        const cacheEntry = telemetryCache.value[cacheKey];
        let mergedRow = { ...row };
        if (cacheEntry) {
          applyCacheToRow(mergedRow, cacheEntry);
        }
        return classifyFleetRow({ ...mergedRow, selected: selected.has(row.address) }, now);
      });
    
    loraInventory.value = processed;
    return processed;
  }

  function mergeMonitorRows(rows: LoraInventoryDevice[]): LoraInventoryDevice[] {
    return rows.slice().sort((a, b) => a.address - b.address);
  }

  function applyTelemetryUpdate(payload: any) {
    const address = Number(payload.address);
    if (!address) return;

    const gatewayIdNormalized = normalizeGatewayId(payload.gateway_id);
    const cacheKey = `${gatewayIdNormalized}:${address}`;

    if (!telemetryCache.value[cacheKey]) {
      telemetryCache.value[cacheKey] = {
        gateway_id: gatewayIdNormalized,
        address,
        device: { address },
        sensors: {}
      };
    }
    const cacheEntry = telemetryCache.value[cacheKey];

    const payloadCanonical = payload.chip_id ? options.canonicalChipId(payload.chip_id) : '';
    if (payloadCanonical && !cacheEntry.chip_id) {
      cacheEntry.chip_id = payloadCanonical;
      cacheEntry.device.chip_id = payloadCanonical;
    }

    const val = payload.value;
    const f = payload.field;

    // Ignore command-like topics (e.g. set/relay)
    if (f.startsWith('set/') || f.includes('/set/')) {
      return;
    }

    if (f === 'relay') cacheEntry.device.relay_state = (val === '1' || val === 1) ? 1 : 0;
    else if (f === 'input') {
      cacheEntry.device.input_state = (val === '1' || val === 1) ? 1 : 0;
      cacheEntry.device.input_state_known = true;
    }
    else if (f === 'rssi' || f === 'uplink_rssi_dbm') cacheEntry.device.rssi = Number(val);
    else if (f === 'wifi_rssi_dbm') {
      cacheEntry.device.wifi_rssi_dbm = (val === '' || val === null || val === undefined || val === '0' || val === 0)
        ? undefined
        : Number(val);
    }
    else if (f === 'fw_version') cacheEntry.device.fw_version = String(val);
    else if (f === 'chip_id') {
      const canonicalVal = options.canonicalChipId(String(val));
      cacheEntry.device.chip_id = canonicalVal;
      cacheEntry.chip_id = canonicalVal;
    }
    else if (f === 'uptime_ms') cacheEntry.device.uptime_ms = Number(val);
    else if (f === 'role') cacheEntry.device.role = options.normalizeRole(String(val));
    else if (f === 'mode') cacheEntry.device.mode = String(val);
    else if (f === 'ip') {
      cacheEntry.device.ip = String(val);
      cacheEntry.device.wifi_connected = !!val && val !== '0.0.0.0';
      cacheEntry.device.wifi_connected_known = true;
    }
    else if (f === 'power_save_listen_only') {
      cacheEntry.device.power_save_listen_only = val === '1' || val === 1 || val === true;
    }
    else if (f === 'power_save_active') {
      cacheEntry.device.power_save_active = val === '1' || val === 1 || val === true;
    }
    else if (f.startsWith('sensor/')) {
      const sensorParts = f.split('/');
      if (sensorParts.length >= 4) {
        const kind = sensorParts[1] as any;
        const instance = Number(sensorParts[2]) || 0;
        const prop = sensorParts[3];
        const sKey = `${kind}/${instance}`;
        if (val === '' || val === null || val === undefined) {
          delete cacheEntry.sensors[sKey];
          const existingRow = findInventoryRow(address, cacheEntry);
          if (existingRow) {
            removeSensorFromRow(existingRow, kind, instance);
            loraInventory.value = loraInventory.value.map(row => {
              if (row.address === address) {
                return classifyFleetRow(row, Date.now());
              }
              return row;
            });
          }
          return;
        }
        if (!cacheEntry.sensors[sKey]) {
          cacheEntry.sensors[sKey] = { kind, instance, state: 'waiting' };
        }
        const sensor = cacheEntry.sensors[sKey];
        if (prop === 'value') {
          sensor.value = Number(val);
        } else if (prop === 'state') {
          sensor.state = String(val) as any;
        }
      }
    }
    cacheEntry.device.age_ms = 0;

    let dev = findInventoryRow(address, cacheEntry);

    if (dev) {
      applyCacheToRow(dev, cacheEntry);
      
      loraInventory.value = loraInventory.value.map(row => {
        if (row.address === address) {
          return classifyFleetRow(row, Date.now());
        }
        return row;
      });
    }
  }

  function updateRowHistory(address: number, patch: Partial<any>) {
    const history = fleetRowHistory.value[address] || {};
    fleetRowHistory.value[address] = {
      ...history,
      ...patch
    };
    
    loraInventory.value = loraInventory.value.map(row => {
      if (row.address === address) {
        return classifyFleetRow(row, Date.now());
      }
      return row;
    });
  }

  function clearFleetGatewayCache() {
    loraInventory.value = [];
    loraInventoryScan.value = null;
    loraCandidates.value = [];
    fleetRowHistory.value = {};
    fleetForceScanCooldownUntilMs.value = 0;
    telemetryCache.value = {};
  }

  return {
    loraInventory,
    loraInventoryScan,
    loraCandidates,
    fleetRowHistory,
    fleetForceScanCooldownUntilMs,
    classifyFleetRow,
    rowFreshness,
    candidateStateText,
    candidateStateClass,
    fleetDeviceUdpLabel,
    fleetRowStatusLabel,
    loraInventoryProgressLabel,
    fleetForceScanCooldownRemainingMs,
    fleetForceScanLabel,
    remotesAndCandidatesStatusLine,
    selectedLoraInventoryCount,
    hasAnyRemoteIp,
    mergeInventoryRows,
    mergeMonitorRows,
    applyTelemetryUpdate,
    updateRowHistory,
    clearFleetGatewayCache
  };
}
