import { describe, it, expect, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useFleetInventory, LoraInventoryDevice, LoraAdoptionCandidate } from './useFleetInventory';

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

  const createFleet = () => {
    return useFleetInventory({
      otaQueue,
      selectedFirmwareCandidateVersion: () => selectedFwCandidateVersion.value,
      fleetClockMs,
      isLoraInventoryScanning,
      canonicalChipId,
      normalizeRole,
      parseVersion,
      compareParsedVersions
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
});
