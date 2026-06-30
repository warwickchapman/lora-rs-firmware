import { describe, it, expect, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useFleetInventory } from './useFleetInventory';
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
    expect(fleet.fleetRowStatusLabel(dev)).toBe('Unexpected reboot');

    // Test new robust status cases
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_failed' })).toBe('OTA Failed');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_updated' })).toBe('Updated');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_rebooted' })).toBe('Rebooted');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_downloading' })).toBe('Downloading OTA...');
    expect(fleet.fleetRowStatusLabel({ address: 1, row_state: 'ota_retrying' })).toBe('Retrying (0/3)...');

    // Test fleetDeviceUdpLabel
    expect(fleet.fleetDeviceUdpLabel({ address: 12, chip_id: '0xABC123', role: 'remote', mode: 'normal' })).toBe('lrs-abc123 addr 12 (remote/normal)');
    expect(fleet.fleetDeviceUdpLabel({ address: 14, chip_id: '' })).toBe('addr 14 addr 14');

    // Test loraInventoryProgressLabel
    fleet.loraInventoryScan.value = null;
    expect(fleet.loraInventoryProgressLabel.value).toBe('Idle');
    fleet.loraInventoryScan.value = { active: true, start_address: 1, end_address: 10, next_address: 1, sent: 0, now_ms: 0 };
    expect(fleet.loraInventoryProgressLabel.value).toBe('Scanning configured remotes and same-key candidates...');
    fleet.loraInventoryScan.value = { active: false, start_address: 1, end_address: 10, next_address: 10, sent: 8, now_ms: 0 };
    expect(fleet.loraInventoryProgressLabel.value).toBe('Complete, 8 probes sent');
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
});
