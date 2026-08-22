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
  const maintDeferredReason = ref<string | null>(null);

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

    const {
      age_ms: _cacheAgeMs,
      ...cacheFields
    } = cacheEntry.device;

    Object.assign(row, cacheFields);
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
    const otaTargetVersionMatches = otaExpected && !!history.otaTargetVersion && row.fw_version === history.otaTargetVersion;

    if (previousUptime > 0 && uptime > 0 && uptime + 30000 < previousUptime) {
      if (history.rowState === 'ota_apply_wait') {
        // The active OTA operation owns confirmation. Do not convert its
        // expected reboot into a generic Fleet reboot result.
        rowState = 'ota_apply_wait';
      } else if ((history.rowState === 'ota_updated' || history.rowState === 'reset_confirmed') && knownReboot) {
        rowState = history.rowState;
        rowStateUntilMs = history.rowStateUntilMs;
      } else if (otaTargetVersionMatches) {
        rowState = 'ota_updated';
        rowStateUntilMs = now + 30000;
      } else {
        const isExpected = expectedReboot || otaExpected || knownReboot;
        rowState = isExpected ? 'ota_rebooted' : 'unexpected_reboot';
        rowStateUntilMs = now + (isExpected ? 20000 : 60000);
        history.rebootExpectedUntilMs = undefined;
      }
    }

    let otaExpectedUntilMs = history.otaExpectedUntilMs;
    if (rowState === 'ota_updated') {
      otaExpectedUntilMs = undefined;
      otaExpected = false;
      history.knownRebootUntilMs = now + 120000;
      history.otaTargetVersion = undefined;
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
    if ((lastTelemetryTimestamp === undefined || lastTelemetryTimestamp === null) &&
        row.age_ms !== undefined && row.age_ms !== null && row.age_ms < 600000) {
      lastTelemetryTimestamp = now - row.age_ms;
    }
    const ageMs = (lastTelemetryTimestamp !== undefined && lastTelemetryTimestamp !== null)
      ? now - lastTelemetryTimestamp
      : row.age_ms;

    const powerSaveListenOnly = (row.power_save_listen_only !== undefined && row.power_save_listen_only !== null) ? row.power_save_listen_only : history.power_save_listen_only;
    const powerSaveActive = (row.power_save_active !== undefined && row.power_save_active !== null) ? row.power_save_active : history.power_save_active;
    const isPowerSaveConfigured = !!powerSaveListenOnly;
    const isPowerSaveDeferred = isPowerSaveConfigured && !powerSaveActive;

    const fwVersion = row.fw_version || history.fwVersion;
    const fwBuild = row.fw_build || history.fw_build;

    const hasRowWifiConnectionState = row.wifi_connected_known !== undefined;
    const wifiConnectedKnown = hasRowWifiConnectionState
      ? row.wifi_connected_known === true
      : history.wifi_connected_known === true;
    const wifiConnected = hasRowWifiConnectionState
      ? row.wifi_connected === true
      : history.wifi_connected === true;
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

    const relayState = (row.relay_state !== undefined && row.relay_state !== null) ? row.relay_state : undefined;
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
      min_flasher_compat_revision: row.min_flasher_compat_revision ?? history.min_flasher_compat_revision,
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
      min_flasher_compat_revision: row.min_flasher_compat_revision ?? history.min_flasher_compat_revision,
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

  function rowFreshness(row: LoraInventoryDevice, overrideAgeMs?: number): 'live' | 'stale' | 'offline' | 'unknown' {
    const age = Number(overrideAgeMs ?? row.age_ms ?? 0);
    if (overrideAgeMs === undefined && row.age_ms === undefined && row.age_ms !== 0) return 'unknown';
    if (age <= 90000) return 'live';
    if (age <= 180000) return 'stale';
    return 'offline';
  }

  function fleetRowStatusLabel(device: LoraInventoryDevice): string {
    if (device.row_state === 'unexpected_reboot') return 'Restarted';
    if (device.row_state === 'ota_failed') return 'Failed';
    if (device.row_state === 'ota_updated') return 'Updated';
    if (device.row_state === 'ota_rebooted') return 'Stage 4/4';
    if (device.row_state === 'ota_no_reboot') return 'Not confirmed';
    if (device.row_state === 'ota_pending' || device.row_state === 'ota_apply_wait') return 'Stage 4/4';
    if (device.row_state === 'ota_downloading') return 'Stage 3/4';
    if (device.row_state === 'ota_awaiting_ack') return 'Stage 2/4';
    if (device.row_state === 'ota_sending' || device.row_state === 'ota_retrying') return 'Stage 1/4';
    if (device.row_state === 'ota_unconfirmed') return 'Not confirmed';
    if (device.row_state === 'reset_queued') return 'Reset queued';
    if (device.row_state === 'reset_sending') return 'Reset 1/2';
    if (device.row_state === 'reset_awaiting_ack') return 'Reset 2/2';
    if (device.row_state === 'reset_confirmed') return 'Reset confirmed';
    if (device.row_state === 'reset_unconfirmed') return 'Reset unconfirmed';
    if (device.row_state === 'reset_failed') return 'Reset failed';
    if (device.row_state === 'ota_queued') {
      const qIdx = options.otaQueue.value.findIndex(d => d.address === device.address);
      return qIdx >= 0 ? `Queued #${qIdx + 1}` : 'Queued';
    }
    return '';
  }

  function fleetRowStatusTitle(device: LoraInventoryDevice): string {
    if (device.row_state === 'unexpected_reboot') return 'Device restarted outside a requested firmware update.';
    if (device.row_state === 'ota_failed') {
      const history = fleetRowHistory.value[device.address] || {};
      return history.otaReason ? `Firmware update failed: ${history.otaReason}` : 'Firmware update failed.';
    }
    if (device.row_state === 'ota_updated') return 'Firmware version and reboot confirmed.';
    if (device.row_state === 'ota_rebooted') return 'Expected firmware-update reboot detected.';
    if (device.row_state === 'ota_no_reboot') return 'The target version and reboot were not confirmed before timeout.';
    if (device.row_state === 'ota_pending' || device.row_state === 'ota_apply_wait') return 'Waiting for the target firmware version and a lower uptime.';
    if (device.row_state === 'ota_downloading') return 'The remote accepted the update and is downloading firmware.';
    if (device.row_state === 'ota_awaiting_ack') return 'Waiting for the remote to accept the update details.';
    if (device.row_state === 'ota_sending' || device.row_state === 'ota_retrying') return 'Sending update details to the remote.';
    if (device.row_state === 'ota_unconfirmed') return 'The gateway did not receive confirmation that the remote accepted the update.';
    if (device.row_state === 'reset_queued') return 'Waiting for the active remote factory reset to finish.';
    if (device.row_state === 'reset_sending') return 'Sending the correlated factory-reset request.';
    if (device.row_state === 'reset_awaiting_ack') return 'Waiting for confirmation that the remote saved its reset configuration.';
    if (device.row_state === 'reset_confirmed') return 'The remote confirmed that its reset configuration was saved and is rebooting.';
    if (device.row_state === 'reset_unconfirmed') return 'The reset was not confirmed. The gateway record was retained for a safe retry.';
    if (device.row_state === 'reset_failed') return 'The remote or gateway could not persist the confirmed reset transaction.';
    if (device.row_state === 'ota_queued') return 'Waiting for the active remote update to finish.';
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
    return `Scan finished, ${scan.sent || 0} probes sent; identity and WiFi details may still be pending`;
  });

  const remotesAndCandidatesStatusLine = computed(() => {
    const remotesCount = loraInventory.value.length;
    const remotesLabel = `${remotesCount} remote${remotesCount === 1 ? '' : 's'} configured`;
    
    const total = loraCandidates.value.length;
    if (total === 0) {
      const baseStr = `${remotesLabel} · 0 candidates`;
      return maintDeferredReason.value ? `${baseStr} · refresh deferred by ${maintDeferredReason.value}` : baseStr;
    }
    
    if (total === 1) {
      const c = loraCandidates.value[0];
      const baseStr = `${remotesLabel} · 1 candidate ${candidateStateText(c)}`;
      return maintDeferredReason.value ? `${baseStr} · refresh deferred by ${maintDeferredReason.value}` : baseStr;
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
      const baseStr = `${remotesLabel} · ${total} candidates`;
      return maintDeferredReason.value ? `${baseStr} · refresh deferred by ${maintDeferredReason.value}` : baseStr;
    }
    const finalStr = `${remotesLabel} · ${total} candidates: ${parts.join(', ')}`;
    return maintDeferredReason.value ? `${finalStr} · refresh deferred by ${maintDeferredReason.value}` : finalStr;
  });

  function mergeInventoryRows(rows: LoraInventoryDevice[]): LoraInventoryDevice[] {
    const selected = new Set(loraInventory.value.filter(d => d.selected).map(d => d.address));
    const now = options.fleetClockMs.value;
    const activeGwId = normalizeGatewayId(options.activeGatewayId ? options.activeGatewayId() : null);

    const processed = rows
      .slice()
      .sort((a, b) => a.address - b.address)
      .map(row => {
        if (row.age_ms !== undefined && row.age_ms !== null && row.age_ms >= 0) {
          const reportedTimestamp = now - row.age_ms;
          const history = fleetRowHistory.value[row.address] || {};
          if (history.lastTelemetryTimestamp === undefined ||
              history.lastTelemetryTimestamp === null ||
              reportedTimestamp > history.lastTelemetryTimestamp) {
            fleetRowHistory.value[row.address] = {
              ...history,
              lastTelemetryTimestamp: reportedTimestamp
            };
          }
        }
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

    let recognizedUpdate = false;

    if (f === 'relay') {
      recognizedUpdate = true;
      if (val === '' || val === null || val === undefined) {
        cacheEntry.device.relay_state = undefined;
      } else {
        cacheEntry.device.relay_state = (val === '1' || val === 1) ? 1 : 0;
      }
    }
    else if (f === 'input') {
      recognizedUpdate = true;
      if (val === '' || val === null || val === undefined) {
        cacheEntry.device.input_state_known = false;
        cacheEntry.device.input_state = undefined;
      } else {
        cacheEntry.device.input_state = (val === '1' || val === 1) ? 1 : 0;
        cacheEntry.device.input_state_known = true;
      }
    }
    else if (f === 'rssi' || f === 'uplink_rssi_dbm') { recognizedUpdate = true; cacheEntry.device.rssi = Number(val); }
    else if (f === 'wifi_rssi_dbm') {
      recognizedUpdate = true;
      cacheEntry.device.wifi_rssi_dbm = (val === '' || val === null || val === undefined || val === '0' || val === 0)
        ? undefined
        : Number(val);
    }
    else if (f === 'fw_version') { recognizedUpdate = true; cacheEntry.device.fw_version = String(val); }
    else if (f === 'chip_id') {
      recognizedUpdate = true;
      const canonicalVal = options.canonicalChipId(String(val));
      cacheEntry.device.chip_id = canonicalVal;
      cacheEntry.chip_id = canonicalVal;
    }
    else if (f === 'uptime_ms') { recognizedUpdate = true; cacheEntry.device.uptime_ms = Number(val); }
    else if (f === 'role') { recognizedUpdate = true; cacheEntry.device.role = options.normalizeRole(String(val)); }
    else if (f === 'mode') { recognizedUpdate = true; cacheEntry.device.mode = String(val); }
    else if (f === 'wifi_connected') {
      recognizedUpdate = true;
      if (val === '' || val === null || val === undefined) {
        cacheEntry.device.wifi_connected_known = false;
        cacheEntry.device.wifi_connected = undefined;
      } else {
        cacheEntry.device.wifi_connected = val === '1' || val === 1 || val === true;
        cacheEntry.device.wifi_connected_known = true;
      }
    }
    else if (f === 'ip') {
      recognizedUpdate = true;
      cacheEntry.device.ip = (val === '' || val === null || val === undefined || val === '0.0.0.0')
        ? undefined
        : String(val);
    }
    else if (f === 'power_save_listen_only') {
      recognizedUpdate = true;
      cacheEntry.device.power_save_listen_only = val === '1' || val === 1 || val === true;
    }
    else if (f === 'power_save_active') {
      recognizedUpdate = true;
      cacheEntry.device.power_save_active = val === '1' || val === 1 || val === true;
    }
    else if (f.startsWith('sensor/')) {
      const sensorParts = f.split('/');
      if (sensorParts.length >= 4) {
        recognizedUpdate = true;
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
          if (!fleetRowHistory.value[address]) fleetRowHistory.value[address] = {};
          fleetRowHistory.value[address].lastTelemetryTimestamp = options.fleetClockMs.value;
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

    if (recognizedUpdate) {
      if (!fleetRowHistory.value[address]) fleetRowHistory.value[address] = {};
      fleetRowHistory.value[address].lastTelemetryTimestamp = options.fleetClockMs.value;
    }

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
    maintDeferredReason.value = null;
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
    fleetRowStatusTitle,
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
    clearFleetGatewayCache,
    maintDeferredReason
  };
}

export function deriveCandidateLocalTimestamp(c: LoraAdoptionCandidate, nowMs: number): LoraAdoptionCandidate {
  if (c.age_ms !== undefined && c.age_ms !== null) {
    return { ...c, last_seen_local_ms: nowMs - c.age_ms };
  }
  return c;
}

export function calculateDynamicAgeMs(device: { age_ms?: number, address: number }, history: { lastTelemetryTimestamp?: number } | undefined, nowMs: number): number | undefined {
  if (history?.lastTelemetryTimestamp !== undefined && history.lastTelemetryTimestamp !== null) {
    return nowMs - history.lastTelemetryTimestamp;
  }
  return device.age_ms;
}

export function calculateCandidateAgeMs(c: LoraAdoptionCandidate, nowMs: number): number | undefined {
  if (c.last_seen_local_ms !== undefined && c.last_seen_local_ms !== null) {
    return nowMs - c.last_seen_local_ms;
  }
  return c.age_ms;
}

export function fleetDeviceWithDisplayState(device: LoraInventoryDevice, nowMs: number): LoraInventoryDevice {
  if (device.row_state_until_ms !== undefined &&
      device.row_state_until_ms !== null &&
      device.row_state_until_ms <= nowMs) {
    return {
      ...device,
      row_state: undefined,
      row_state_until_ms: undefined
    };
  }
  return device;
}

export function applySeedWhitelist(existing: Partial<LoraInventoryDevice>, seed: LoraInventoryDevice): LoraInventoryDevice {
  const { age_ms: _existingAge, ...existingRest } = existing;
  const { age_ms: _seedAge, ...seedRest } = seed;

  return {
    ...existingRest,
    address: seedRest.address,
    chip_id: seedRest.chip_id !== undefined ? seedRest.chip_id : existingRest.chip_id,
    role: seedRest.role !== undefined ? seedRest.role : existingRest.role,
    mode: seedRest.mode !== undefined ? seedRest.mode : existingRest.mode,
    relay_state: seedRest.relay_state !== undefined ? seedRest.relay_state : existingRest.relay_state,
    input_state_known: seedRest.input_state_known !== undefined ? seedRest.input_state_known : existingRest.input_state_known,
    input_state: seedRest.input_state !== undefined ? seedRest.input_state : existingRest.input_state,
  } as LoraInventoryDevice;
}
