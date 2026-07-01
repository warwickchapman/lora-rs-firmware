import { describe, it, expect, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useFleetInventory, deriveCandidateLocalTimestamp, calculateDynamicAgeMs, calculateCandidateAgeMs } from './useFleetInventory';
import { LoraInventoryDevice, LoraAdoptionCandidate } from '../types/fleet';

describe('useFleetInventory Age Ticking', () => {
  let otaQueue = ref<LoraInventoryDevice[]>([]);
  let selectedFwCandidateVersion = ref<string | null>('1.0.0');
  let fleetClockMs = ref(1000000);
  let isLoraInventoryScanning = ref(false);

  const dummyHelpers = {
    canonicalChipId: (id: string | null | undefined) => id || '',
    normalizeRole: (role: string | null | undefined) => role || '',
    parseVersion: (_v: string) => null,
    compareParsedVersions: (_a: any, _b: any) => 0,
  };

  const createFleet = () => {
    return useFleetInventory({
      otaQueue,
      selectedFirmwareCandidateVersion: () => selectedFwCandidateVersion.value,
      fleetClockMs,
      isLoraInventoryScanning,
      ...dummyHelpers,
      activeGatewayId: () => 'gateway-1'
    });
  };

  beforeEach(() => {
    otaQueue.value = [];
    fleetClockMs.value = 1000000; // 1000s
  });

  it('derives lastTelemetryTimestamp from age_ms during merge', () => {
    const fleet = createFleet();
    const now = fleetClockMs.value;
    
    fleet.mergeInventoryRows([
      { address: 1, age_ms: 5000 } // Seen 5s ago
    ]);

    const row = fleet.loraInventory.value[0];
    const history = fleet.fleetRowHistory.value[1];
    
    // lastTelemetryTimestamp should be now - 5000
    expect(history.lastTelemetryTimestamp).toBe(now - 5000);
    expect(row.age_ms).toBe(5000);
  });

  it('updates rowFreshness correctly when passed an override age', () => {
    const fleet = createFleet();
    const row: LoraInventoryDevice = { address: 1, age_ms: 10000 };

    // Default behavior
    expect(fleet.rowFreshness(row)).toBe('live');

    // Override with stale age
    expect(fleet.rowFreshness(row, 100000)).toBe('stale');

    // Override with offline age
    expect(fleet.rowFreshness(row, 300000)).toBe('offline');
  });

  it('maintains lastTelemetryTimestamp when no age_ms is provided in subsequent merges', () => {
    const fleet = createFleet();
    const t0 = fleetClockMs.value;

    fleet.mergeInventoryRows([{ address: 1, age_ms: 5000 }]);
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(t0 - 5000);

    // Advance clock by 10s
    fleetClockMs.value += 10000;

    // Merge again with no age_ms (e.g. partial update or cached response)
    fleet.mergeInventoryRows([{ address: 1 }]);
    
    // Timestamp should remain t0 - 5000
    expect(fleet.fleetRowHistory.value[1].lastTelemetryTimestamp).toBe(t0 - 5000);
    
    // The row age_ms should now be (t1 - (t0 - 5000)) = 10000 + 5000 = 15000
    expect(fleet.loraInventory.value[0].age_ms).toBe(15000);
  });

  it('correctly calculates age for candidates using deriveCandidateLocalTimestamp', () => {
    const t0 = fleetClockMs.value;

    const firmwareLastSeenMs = 1234567;
    const candidate: LoraAdoptionCandidate = {
      address: 10,
      rssi: -70,
      last_seen_ms: firmwareLastSeenMs, // Firmware uptime
      age_ms: 3000,                    // Actually seen 3s ago
      reason: 'ok',
      state: 'identified'
    };

    // We call the production helper
    const processed = deriveCandidateLocalTimestamp(candidate, t0);

    // Verify firmware field remains unchanged
    expect(processed.last_seen_ms).toBe(firmwareLastSeenMs);
    expect(processed.last_seen_local_ms).toBe(t0 - 3000);

    // Advance clock by 5s
    fleetClockMs.value += 5000;
    const t1 = fleetClockMs.value;

    // Dynamic age should be t1 - last_seen_local_ms = 8000
    const dynamicAgeMs = t1 - (processed.last_seen_local_ms || 0);
    expect(dynamicAgeMs).toBe(8000);
  });

  it('handles missing age_ms in deriveCandidateLocalTimestamp gracefully', () => {
    const t0 = fleetClockMs.value;

    const candidate: LoraAdoptionCandidate = {
      address: 10,
      rssi: -70,
      last_seen_ms: 1234567,
      reason: 'ok',
      state: 'identified'
    };

    const processed = deriveCandidateLocalTimestamp(candidate, t0);

    expect(processed.last_seen_local_ms).toBeUndefined();
    expect(processed.last_seen_ms).toBe(1234567);
  });

  it('proves Fleet display row ageSeconds increases when fleetClockMs advances without a new merge', () => {
    const fleet = createFleet();
    const t0 = fleetClockMs.value;

    // 1. merge row with age_ms: 5000
    fleet.mergeInventoryRows([{ address: 1, age_ms: 5000 }]);
    
    // 2. initial displayed ageMs via helper is 5000
    const history0 = fleet.fleetRowHistory.value[1];
    const device0 = fleet.loraInventory.value[0];
    const ageMs0 = calculateDynamicAgeMs(device0, history0, t0);
    expect(ageMs0).toBe(5000);

    // 3. advance fleetClockMs by 10000
    fleetClockMs.value += 10000;
    const t1 = fleetClockMs.value;

    // 4. displayed ageMs becomes 15000 without calling mergeInventoryRows() again
    const ageMs1 = calculateDynamicAgeMs(device0, history0, t1);
    expect(ageMs1).toBe(15000);
  });

  it('proves Candidate display age increases when fleetClockMs advances', () => {
    const t0 = fleetClockMs.value;

    const candidate: LoraAdoptionCandidate = {
      address: 10,
      rssi: -70,
      last_seen_ms: 1234567,
      age_ms: 3000,
      reason: 'ok',
      state: 'identified'
    };

    // 1. process candidate at t0
    const processed = deriveCandidateLocalTimestamp(candidate, t0);
    expect(calculateCandidateAgeMs(processed, t0)).toBe(3000);

    // 2. advance clock by 5000
    fleetClockMs.value += 5000;
    const t1 = fleetClockMs.value;

    // 3. displayed age becomes 8000
    expect(calculateCandidateAgeMs(processed, t1)).toBe(8000);
  });
});
