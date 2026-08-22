import { describe, it, expect, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useFleetInventory, applySeedWhitelist, calculateDynamicAgeMs } from './useFleetInventory';
import { LoraInventoryDevice, LoraAdoptionCandidate } from '../types/fleet';

describe('useFleetInventory', () => {
  let otaQueue = ref<LoraInventoryDevice[]>([]);
  let selectedFwCandidateVersion = ref<string | null>('1.0.0');
  let fleetClockMs = ref(Date.now());
  let isLoraInventoryScanning = ref(false);

  const normalizeChipId = (raw: string | undefined | null) => {
    if (!raw) return '';
    const match = raw.toLowerCase().match(/(?:lrs|leg)-([0-9a-f]+)/);
    if (match) return match[1];
    const hex = raw.trim().replace(/^0x/i, '').toLowerCase();
    if (/^[0-9a-f]+$/.test(hex)) return hex;
    return raw.trim();
  };

  const canonicalChipId = (raw: string | undefined | null) => {
    const norm = normalizeChipId(raw);
    if (!norm) return '';
    return norm.length < 8 ? norm.padStart(8, '0') : norm;
  };

  const normalizeRole = (role: string | undefined | null) => {
    if (!role) return '';
    const r = role.toLowerCase().trim();
    if (r === 'tx' || r === 'transmitter' || r === 'gateway') return 'gateway';
    if (r === 'rx' || r === 'receiver' || r === 'remote') return 'remote';
    return r;
  };

  const parseVersion = (v: string) => {
    const clean = v.replace(/^Local:\s*/i, '').trim();
    const match = clean.match(/^(\d+)\.(\d+)\.(\d+)(?:~(\d+))?/);
    if (match) {
      return {
        major: parseInt(match[1], 10),
        minor: parseInt(match[2], 10),
        patch: parseInt(match[3], 10),
        dev: match[4] ? parseInt(match[4], 10) : 0
      };
    }
    return null;
  };

  const compareParsedVersions = (a: any, b: any) => {
    if (!a || !b) return 0;
    if (a.major !== b.major) return a.major - b.major;
    if (a.minor !== b.minor) return a.minor - b.minor;
    if (a.patch !== b.patch) return a.patch - b.patch;
    return a.dev - b.dev;
  };

  const createFleet = (activeGatewayIdVal: string | null = 'lrs-00001234') => {
    return useFleetInventory({
      otaQueue,
      selectedFirmwareCandidateVersion: () => selectedFwCandidateVersion.value,
      fleetClockMs,
      isLoraInventoryScanning,
      canonicalChipId,
      normalizeRole,
      parseVersion,
      compareParsedVersions,
      activeGatewayId: () => activeGatewayIdVal
    });
  };

  beforeEach(() => {
    otaQueue.value = [];
    selectedFwCandidateVersion.value = '1.0.0';
    fleetClockMs.value = 1000000;
  });

  it('mergeInventoryRows preserves selected state of devices', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [
      { address: 1, selected: true, chip_id: '00000001' },
      { address: 2, selected: false, chip_id: '00000002' }
    ];

    const newRows: LoraInventoryDevice[] = [
      { address: 1, chip_id: '00000001' },
      { address: 2, chip_id: '00000002' },
      { address: 3, chip_id: '00000003' }
    ];

    fleet.mergeInventoryRows(newRows);

    expect(fleet.loraInventory.value).toHaveLength(3);
    expect(fleet.loraInventory.value.find(d => d.address === 1)?.selected).toBe(true);
    expect(fleet.loraInventory.value.find(d => d.address === 2)?.selected).toBe(false);
    expect(fleet.loraInventory.value.find(d => d.address === 3)?.selected).toBe(false);
  });

  it('applyTelemetryUpdate patches correct telemetry fields and re-classifies row', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [
      { address: 1, chip_id: '00000001', relay_state: 0 }
    ];

    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'relay',
      value: '1'
    });

    expect(fleet.loraInventory.value[0].relay_state).toBe(1);
  });

  it('keeps a same-version queued remote queued until its active OTA confirms it', () => {
    const fleet = createFleet();
    const row: LoraInventoryDevice = {
      address: 1,
      chip_id: '00000001',
      fw_version: '1.0.0',
      uptime_ms: 120000
    };
    fleet.mergeInventoryRows([row]);
    otaQueue.value = [row];
    fleet.updateRowHistory(1, { rowState: 'ota_queued' });

    expect(fleet.classifyFleetRow(row, fleetClockMs.value).row_state).toBe('ota_queued');
  });

  it('keeps an OTA reboot in the active confirmation stage', () => {
    const fleet = createFleet();
    const before: LoraInventoryDevice = {
      address: 1,
      chip_id: '00000001',
      fw_version: '1.0.0',
      uptime_ms: 120000
    };
    fleet.mergeInventoryRows([before]);
    fleet.updateRowHistory(1, { rowState: 'ota_apply_wait' });

    const after = fleet.classifyFleetRow({ ...before, uptime_ms: 10000 }, fleetClockMs.value);
    expect(after.row_state).toBe('ota_apply_wait');
  });

  it('promotes a late matching OTA reboot to updated after the active watchdog timed out', () => {
    const fleet = createFleet();
    fleet.updateRowHistory(1, {
      rowState: 'ota_no_reboot',
      lastRawUptimeMs: 600000,
      fwVersion: '0.10.4~11',
      otaTargetVersion: '0.10.4~12',
      otaExpectedUntilMs: fleetClockMs.value + 600000
    });

    const after = fleet.classifyFleetRow({
      address: 1,
      chip_id: '00000001',
      fw_version: '0.10.4~12',
      uptime_ms: 30000
    }, fleetClockMs.value);

    expect(after.row_state).toBe('ota_updated');
  });

  it('does not treat a late reboot onto the wrong firmware as OTA success', () => {
    const fleet = createFleet();
    fleet.updateRowHistory(1, {
      rowState: 'ota_no_reboot',
      lastRawUptimeMs: 600000,
      fwVersion: '0.10.4~11',
      otaTargetVersion: '0.10.4~12',
      otaExpectedUntilMs: fleetClockMs.value + 600000
    });

    const after = fleet.classifyFleetRow({
      address: 1,
      chip_id: '00000001',
      fw_version: '0.10.4~13',
      uptime_ms: 30000
    }, fleetClockMs.value);

    expect(after.row_state).toBe('ota_rebooted');
  });

  it('treats empty input telemetry as unknown, clearing previous state, but keeps 0 as valid', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [
      { address: 1, chip_id: '00000001' }
    ];

    // Seed state via telemetry
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'input',
      value: '1'
    });
    expect(fleet.loraInventory.value[0].input_state).toBe(1);
    expect(fleet.loraInventory.value[0].input_state_known).toBe(true);

    // Empty telemetry clears it to unknown
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'input',
      value: ''
    });
    expect(fleet.loraInventory.value[0].input_state).toBeUndefined();
    expect(fleet.loraInventory.value[0].input_state_known).toBe(false);

    // 0 is valid Off/Open state
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'input',
      value: '0'
    });
    expect(fleet.loraInventory.value[0].input_state).toBe(0);
    expect(fleet.loraInventory.value[0].input_state_known).toBe(true);
  });

  it('uses explicit WiFi connection telemetry and never infers Offline from an empty IP', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [{ address: 1, chip_id: '00000001' }];

    fleet.applyTelemetryUpdate({ address: 1, field: 'ip', value: '' });
    expect(fleet.loraInventory.value[0].ip).toBeUndefined();
    expect(fleet.loraInventory.value[0].wifi_connected_known).toBe(false);

    fleet.applyTelemetryUpdate({ address: 1, field: 'wifi_connected', value: '0' });
    expect(fleet.loraInventory.value[0].wifi_connected_known).toBe(true);
    expect(fleet.loraInventory.value[0].wifi_connected).toBe(false);

    fleet.applyTelemetryUpdate({ address: 1, field: 'wifi_connected', value: '' });
    expect(fleet.loraInventory.value[0].wifi_connected_known).toBe(false);
  });

  it('treats empty relay telemetry as unknown, clearing previous state, but keeps 0 as valid', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [
      { address: 1, chip_id: '00000001' }
    ];

    // Seed state via telemetry
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'relay',
      value: '1'
    });
    expect(fleet.loraInventory.value[0].relay_state).toBe(1);

    // Empty telemetry clears it
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'relay',
      value: ''
    });
    expect(fleet.loraInventory.value[0].relay_state).toBeUndefined();

    // 0 is valid Off
    fleet.applyTelemetryUpdate({
      address: 1,
      field: 'relay',
      value: '0'
    });
    expect(fleet.loraInventory.value[0].relay_state).toBe(0);
  });

  it('clears stale serial relay state when inventory no longer reports it', () => {
    const fleet = createFleet();

    fleet.mergeInventoryRows([
      { address: 1, chip_id: '00000001', relay_state: 0, age_ms: 1000 }
    ]);
    expect(fleet.loraInventory.value[0].relay_state).toBe(0);

    fleet.mergeInventoryRows([
      { address: 1, chip_id: '00000001', age_ms: 2000 }
    ]);

    expect(fleet.loraInventory.value[0].relay_state).toBeUndefined();
  });

  it('applies compact inventory control state to every previously hydrated peer', () => {
    const fleet = createFleet();

    fleet.mergeInventoryRows([
      { address: 1, chip_id: '00000001', relay_state: 0, input_state_known: true, input_state: 0 },
      { address: 2, chip_id: '00000002', relay_state: 0, input_state_known: true, input_state: 0 }
    ]);

    // The compact seed follows a group command. It must replace the old
    // hydrated control state without requiring either peer to be refreshed.
    fleet.mergeInventoryRows([
      { address: 1, chip_id: '00000001', relay_state: 1, input_state_known: true, input_state: 1 },
      { address: 2, chip_id: '00000002', relay_state: 1, input_state_known: true, input_state: 1 }
    ]);

    expect(fleet.loraInventory.value.find(row => row.address === 1)?.relay_state).toBe(1);
    expect(fleet.loraInventory.value.find(row => row.address === 1)?.input_state).toBe(1);
    expect(fleet.loraInventory.value.find(row => row.address === 2)?.relay_state).toBe(1);
    expect(fleet.loraInventory.value.find(row => row.address === 2)?.input_state).toBe(1);
  });

  it('rowFreshness correctly maps live, stale, offline, and unknown states', () => {
    const fleet = createFleet();

    const devUnknown: LoraInventoryDevice = { address: 1 };
    expect(fleet.rowFreshness(devUnknown)).toBe('unknown');

    const devLive: LoraInventoryDevice = { address: 2, fw_version: '1.0.0', age_ms: 10000 };
    expect(fleet.rowFreshness(devLive)).toBe('live');

    const devStale: LoraInventoryDevice = { address: 3, fw_version: '1.0.0', age_ms: 150000 };
    expect(fleet.rowFreshness(devStale)).toBe('stale');

    const devOffline: LoraInventoryDevice = { address: 4, fw_version: '1.0.0', age_ms: 400000 };
    expect(fleet.rowFreshness(devOffline)).toBe('offline');
  });

  it('candidate state and fleet row status labels return correct values', () => {
    const fleet = createFleet();

    const candidate: LoraAdoptionCandidate = {
      address: 5,
      rssi: -80,
      last_seen_ms: 10000,
      reason: 'ok',
      state: 'readdressing'
    };
    expect(fleet.candidateStateText(candidate)).toBe('adopting');
    expect(fleet.candidateStateClass(candidate)).toContain('text-sky-400');

    const dev: LoraInventoryDevice = {
      address: 1,
      row_state: 'unexpected_reboot'
    };
    expect(fleet.fleetRowStatusLabel(dev)).toBe('Restarted');

    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_failed' })).toBe('Failed');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_updated' })).toBe('Updated');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_rebooted' })).toBe('Stage 4/4');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_downloading' })).toBe('Stage 3/4');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_sending' })).toBe('Stage 1/4');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_awaiting_ack' })).toBe('Stage 2/4');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_unconfirmed' })).toBe('Not confirmed');
    expect(fleet.fleetRowStatusTitle({ address: 1, row_state: 'ota_awaiting_ack' })).toContain('accept');

    // Test fleetDeviceUdpLabel
    expect(fleet.fleetDeviceUdpLabel({ address: 12, chip_id: '0xABC123', role: 'remote', mode: 'normal' })).toBe('lrs-abc123 addr 12 (remote/normal)');
    expect(fleet.fleetDeviceUdpLabel({ address: 14, chip_id: '' })).toBe('addr 14 addr 14');

    // Test loraInventoryProgressLabel
    fleet.loraInventoryScan.value = null;
    expect(fleet.loraInventoryProgressLabel.value).toBe('Idle');
    fleet.loraInventoryScan.value = { active: true, start_address: 1, end_address: 10, next_address: 1, sent: 0, now_ms: 0 };
    expect(fleet.loraInventoryProgressLabel.value).toBe('Scanning configured remotes and same-key candidates...');
    fleet.loraInventoryScan.value = { active: false, start_address: 1, end_address: 10, next_address: 10, sent: 8, now_ms: 0 };
    expect(fleet.loraInventoryProgressLabel.value).toBe('Scan finished, 8 probes sent; identity and WiFi details may still be pending');
  });

  it('caches MQTT telemetry when no row exists, merges it on mergeInventoryRows, clears it on clear, and ignores commands', () => {
    const fleet = createFleet('00001234'); // normalized gateway id is '00001234'
    expect(fleet.loraInventory.value).toHaveLength(0);

    // 1. Telemetry arrives while loraInventory is empty
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      chip_id: 'lrs-abcde123',
      field: 'relay',
      value: '1'
    });
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'sensor/temperature/0/value',
      value: '22.5'
    });
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'sensor/temperature/0/state',
      value: 'ok'
    });
    
    // Command topic should be ignored and not cached as telemetry
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'set/relay',
      value: '0'
    });

    expect(fleet.loraInventory.value).toHaveLength(0);

    // 2. Later mergeInventoryRows produces a populated row with merged values
    const newRows: LoraInventoryDevice[] = [
      { address: 1, chip_id: 'abcde123' }
    ];
    fleet.mergeInventoryRows(newRows);

    expect(fleet.loraInventory.value).toHaveLength(1);
    const row = fleet.loraInventory.value[0];
    expect(row.relay_state).toBe(1); // restored from cache
    expect(row.chip_id).toBe('abcde123');

    // 3. Nested sensor field is also restored
    expect(row.sensors).toBeDefined();
    const tempSensor = row.sensors?.find(s => s.kind === 'temperature' && s.instance === 0);
    expect(tempSensor).toBeDefined();
    expect(tempSensor?.value).toBe(22.5);
    expect(tempSensor?.state).toBe('ok');

    // 4. clearFleetGatewayCache clears cached telemetry
    fleet.clearFleetGatewayCache();
    expect(fleet.loraInventory.value).toHaveLength(0);

    // If we merge rows again after clear, it shouldn't apply cached values
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);
    expect(fleet.loraInventory.value[0].relay_state).toBeUndefined();
    expect(fleet.loraInventory.value[0].sensors).toBeUndefined();
  });

  it('does not merge cached telemetry when chip IDs mismatch and instead sets conflict_chip_id', () => {
    const fleet = createFleet('00001234');
    
    // Cache telemetry for address 1 with chip_id 'abcde123' (Chip A)
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      chip_id: 'lrs-abcde123',
      field: 'relay',
      value: '1'
    });

    // Merge row for address 1 with chip_id 'xyz98765' (Chip B)
    fleet.mergeInventoryRows([
      { address: 1, chip_id: 'xyz98765' }
    ]);

    expect(fleet.loraInventory.value).toHaveLength(1);
    const row = fleet.loraInventory.value[0];
    expect(row.relay_state).toBeUndefined(); // Should not inherit Chip A's telemetry
    expect(row.conflict_chip_id).toBe('abcde123'); // Should record Chip A as the conflict
  });

  it('treats empty retained sensor telemetry as a delete instead of an enabled sensor', () => {
    const fleet = createFleet('00001234');
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);

    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      chip_id: 'lrs-abcde123',
      field: 'sensor/temperature/0/state',
      value: 'ok'
    });
    expect(fleet.loraInventory.value[0].sensors?.[0]?.state).toBe('ok');

    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      chip_id: 'lrs-abcde123',
      field: 'sensor/temperature/0/state',
      value: ''
    });
    expect(fleet.loraInventory.value[0].sensors).toBeUndefined();

    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);
    expect(fleet.loraInventory.value[0].sensors).toBeUndefined();
  });

  it('serial inventory includes wifi_rssi_dbm when known', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: -67 }]);
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBe(-67);
  });

  it('serial inventory omits wifi_rssi_dbm when unknown', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBeUndefined();
  });

  it('MQTT telemetry maps wifi_rssi_dbm', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [{ address: 1, chip_id: 'abcde123' }];
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'wifi_rssi_dbm',
      value: '-67'
    });
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBe(-67);
  });

  it('MQTT telemetry clears wifi_rssi_dbm when empty', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: -67 }];
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'wifi_rssi_dbm',
      value: ''
    });
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBeUndefined();
  });

  it('fleetRowHistory preserves wifi_rssi_dbm across poll cycles', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: -67 }]);
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]); // second poll, no RSSI
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBe(-67); // preserved from history
  });

  it('serial inventory treats wifi_rssi_dbm value of 0 as unknown/undefined', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: 0 }]);
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBeUndefined();
  });

  it('MQTT telemetry treats value "0" or 0 as unknown/undefined', () => {
    const fleet = createFleet();
    fleet.loraInventory.value = [{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: -67 }];
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'wifi_rssi_dbm',
      value: '0'
    });
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBeUndefined();
  });

  it('serial inventory treats explicit wifi_rssi_dbm value of 0 as clearing history', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: -67 }]);
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', wifi_rssi_dbm: 0 }]); // explicit zero
    expect(fleet.loraInventory.value[0].wifi_rssi_dbm).toBeUndefined(); // history cleared
  });

  it('MQTT cache refresh takes precedence over an older inventory age', () => {
    const fleet = createFleet();
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 1000 }]);
    // A retained MQTT update is fresh gateway telemetry and must reset Age.
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'rssi',
      value: '-50'
    });
    // Trigger merge which invokes applyCacheToRow
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 1500 }]);
    const row = fleet.loraInventory.value[0];
    expect(row.age_ms).toBe(0); // live MQTT telemetry is newer than the inventory snapshot
    expect(row.rssi).toBe(-50); // cache applied
  });

  it('applyTelemetryUpdate updates lastTelemetryTimestamp for non-retained updates', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);

    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'relay',
      value: '1',
      retain: false
    });
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(10000);
  });

  it('applyTelemetryUpdate updates lastTelemetryTimestamp for retained gateway telemetry', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);

    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'relay',
      value: '1',
      retain: true
    });
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(10000);
  });

  it('keeps a newer MQTT telemetry timestamp instead of reusing an older inventory age', () => {
    const fleet = createFleet();
    fleetClockMs.value = 1000000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 160000 }]);

    fleetClockMs.value = 1200000;
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'relay',
      value: '1',
      retain: true
    });

    fleetClockMs.value = 1205000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 160000 }]);

    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(1200000);
    expect(calculateDynamicAgeMs(fleet.loraInventory.value[0], fleet.fleetRowHistory.value[1], 1205000)).toBe(5000);
  });

  it('rebases Age when a newer serial peer detail snapshot arrives', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 9000 }]);
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(1000);

    fleetClockMs.value = 20000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 500 }]);
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(19500);
    expect(calculateDynamicAgeMs(fleet.loraInventory.value[0], fleet.fleetRowHistory.value[1], 21000)).toBe(1500);

    fleetClockMs.value = 21000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 5000 }]);
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(19500);
  });

  it('applyTelemetryUpdate updates lastTelemetryTimestamp for non-retained sensor clears', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'sensor/temperature/0/value',
      value: '22',
      retain: false
    });
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(10000);

    fleetClockMs.value = 20000;
    fleet.applyTelemetryUpdate({
      gateway_id: 'lrs-00001234',
      address: 1,
      field: 'sensor/temperature/0/value',
      value: '', // clear
      retain: false
    });
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(20000);
  });

  it('remotesAndCandidatesStatusLine reflects maintDeferredReason', () => {
    const fleet = createFleet();
    fleet.maintDeferredReason.value = 'control recovery';
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123' }]);
    expect(fleet.remotesAndCandidatesStatusLine.value).toBe('1 remote configured · 0 candidates · refresh deferred by control recovery');

    fleet.maintDeferredReason.value = null;
    expect(fleet.remotesAndCandidatesStatusLine.value).toBe('1 remote configured · 0 candidates');
  });

  it('applySeedWhitelist preserves existing fields and whitelists incoming', () => {
    const existing = {
      address: 1,
      chip_id: 'abcde123',
      fw_version: '1.2.3',
      age_ms: 5000,
      rssi: -50,
      role: 'remote'
    };

    const seed = {
      address: 1,
      chip_id: 'new123',
      role: 'gateway',
      relay_state: 1,
      age_ms: 0,
      fw_version: '9.9.9'
    };

    // @ts-ignore
    const result = applySeedWhitelist(existing, seed);

    expect(result.address).toBe(1);
    expect(result.chip_id).toBe('new123'); // from seed
    expect(result.role).toBe('gateway'); // from seed
    expect(result.relay_state).toBe(1); // from seed
    expect(result.fw_version).toBe('1.2.3'); // preserved, ignored from seed
    expect(result.age_ms).toBeUndefined(); // stripped from both to prevent host age reset
    expect(result.rssi).toBe(-50); // preserved
  });

  it('compact seed merges do not reset host freshness timestamp, letting Age increase', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;

    // Time T: Detail row received with age_ms = 1000
    fleet.mergeInventoryRows([{ address: 1, chip_id: 'abcde123', age_ms: 1000 }]);
    let row = fleet.loraInventory.value[0];
    expect(row.age_ms).toBe(1000); // T = 10000, age = 1000 -> lastTelemetryTimestamp = 9000
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(9000);

    // Time T+5s (15000): Compact seed received
    fleetClockMs.value = 15000;
    // mergeLoraInventorySeedRows simulates compact seed merge via applySeedWhitelist
    const seed1 = { address: 1, chip_id: 'abcde123', role: 'remote', age_ms: 0 };
    const merged1 = applySeedWhitelist(fleet.loraInventory.value[0], seed1 as any);
    fleet.mergeInventoryRows([merged1]);

    row = fleet.loraInventory.value[0];
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(9000); // Host timestamp remains T = 9000
    expect(row.age_ms).toBe(6000); // Age correctly increases (15000 - 9000)

    // Time T+10s (20000): Another compact seed received with age_ms: 0
    fleetClockMs.value = 20000;
    const seed2 = { address: 1, chip_id: 'abcde123', role: 'remote', age_ms: 0 };
    const merged2 = applySeedWhitelist(fleet.loraInventory.value[0], seed2 as any);
    fleet.mergeInventoryRows([merged2]);

    row = fleet.loraInventory.value[0];
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(9000); // Still 9000
    expect(row.age_ms).toBe(11000); // Age increases to 11s (20000 - 9000)
  });

  it('compact-seed-only row has unknown Age', () => {
    const fleet = createFleet();
    fleetClockMs.value = 10000;

    const seed = { address: 1, chip_id: 'abcde123', role: 'remote', age_ms: 0 };
    const merged = applySeedWhitelist({}, seed as any);
    fleet.mergeInventoryRows([merged]);

    const row = fleet.loraInventory.value[0];
    expect(row.age_ms).toBeUndefined(); // Unknown age
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBeUndefined();
  });
});
