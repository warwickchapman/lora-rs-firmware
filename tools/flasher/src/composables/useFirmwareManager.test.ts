import { describe, it, expect, vi, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useFirmwareManager, LOCAL_OPTION, LOCAL_LABEL_PREFIX } from './useFirmwareManager';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}));

vi.mock('@tauri-apps/plugin-dialog', () => ({
  open: vi.fn(),
}));

import { invoke } from '@tauri-apps/api/core';

describe('useFirmwareManager', () => {
  let logs: string[] = [];
  let notifications: string[] = [];
  const flasherAppVersion = ref('0.9.2');

  const options = {
    flasherAppVersion,
    pushLog: (line: string) => { logs.push(line); },
    notify: (msg: string) => { notifications.push(msg); }
  };

  const localStorageMock = (() => {
    let store: Record<string, string> = {};
    return {
      getItem: (key: string) => store[key] || null,
      setItem: (key: string, value: string) => { store[key] = value.toString(); },
      clear: () => { store = {}; },
      removeItem: (key: string) => { delete store[key]; }
    };
  })();

  beforeEach(() => {
    logs = [];
    notifications = [];
    vi.clearAllMocks();
    Object.defineProperty(globalThis, 'localStorage', {
      value: localStorageMock,
      writable: true
    });
    localStorage.clear();
  });

  it('initializes with default region and local option', () => {
    const manager = useFirmwareManager(options);
    expect(manager.region.value).toBe('ZA');
    expect(manager.firmwareVersions.value).toEqual([LOCAL_OPTION]);
    expect(manager.selectedVersion.value).toBe('');
  });

  describe('selectedFirmwareCandidateVersion', () => {
    it('returns selectedVersion when it is not a local file', () => {
      const manager = useFirmwareManager(options);
      manager.selectedVersion.value = '1.0.0';
      expect(manager.selectedFirmwareCandidateVersion()).toBe('1.0.0');
    });

    it('parses version from local firmware file path if present', () => {
      const manager = useFirmwareManager(options);
      manager.selectedVersion.value = `${LOCAL_LABEL_PREFIX}firmware-v1.2.3~4.bin`;
      manager.selectedLocalPath.value = '/path/to/firmware-1.2.3~4.bin';
      expect(manager.selectedFirmwareCandidateVersion()).toBe('1.2.3~4');
    });

    it('falls back to flasherAppVersion if local file has no version string', () => {
      const manager = useFirmwareManager(options);
      manager.selectedVersion.value = `${LOCAL_LABEL_PREFIX}firmware.bin`;
      manager.selectedLocalPath.value = '/path/to/firmware.bin';
      expect(manager.selectedFirmwareCandidateVersion()).toBe('0.9.2');
    });
  });

  describe('local firmware selection', () => {
    it('updates firmware versions list and selection correctly', () => {
      const manager = useFirmwareManager(options);
      manager.setLocalFirmwareSelection('/some/path/firmware.bin');
      expect(manager.selectedLocalPath.value).toBe('/some/path/firmware.bin');
      expect(manager.selectedVersion.value).toBe(`${LOCAL_LABEL_PREFIX}firmware.bin`);
      expect(manager.firmwareVersions.value).toEqual([LOCAL_OPTION, `${LOCAL_LABEL_PREFIX}firmware.bin`]);
      expect(logs).toContain('Local firmware selected: /some/path/firmware.bin');
    });

    it('refreshes an automatically selected local dev build', async () => {
      vi.mocked(invoke)
        .mockResolvedValueOnce('/build/lrs-firmware-0.10.4~9.bin')
        .mockResolvedValueOnce('/build/lrs-firmware-0.10.4~10.bin');

      const manager = useFirmwareManager(options);
      expect(await manager.refreshDefaultLocalFirmware()).toBe(true);
      expect(await manager.refreshDefaultLocalFirmware()).toBe(true);
      expect(manager.selectedLocalPath.value).toBe('/build/lrs-firmware-0.10.4~10.bin');
      expect(manager.selectedVersion.value).toBe(`${LOCAL_LABEL_PREFIX}lrs-firmware-0.10.4~10.bin`);
    });

    it('does not replace a manually selected local file', async () => {
      vi.mocked(invoke).mockResolvedValue('/build/lrs-firmware-0.10.4~10.bin');

      const manager = useFirmwareManager(options);
      manager.setLocalFirmwareSelection('/chosen/custom.bin');

      expect(await manager.refreshDefaultLocalFirmware()).toBe(false);
      expect(manager.selectedLocalPath.value).toBe('/chosen/custom.bin');
    });

    it('updates the default local entry without replacing a remote release selection', async () => {
      vi.mocked(invoke)
        .mockResolvedValueOnce('/build/lrs-firmware-0.10.4~9.bin')
        .mockResolvedValueOnce('/build/lrs-firmware-0.10.4~10.bin');

      const manager = useFirmwareManager(options);
      await manager.refreshDefaultLocalFirmware();
      manager.selectedVersion.value = '0.10.3';

      expect(await manager.refreshDefaultLocalFirmware()).toBe(true);
      expect(manager.selectedVersion.value).toBe('0.10.3');
      expect(manager.firmwareVersions.value).toContain(`${LOCAL_LABEL_PREFIX}lrs-firmware-0.10.4~10.bin`);
    });
  });

  describe('region detection and initialization', () => {
    it('detects region from system reliably', () => {
      const manager = useFirmwareManager(options);
      // Stub resolvedOptions locale & timezone
      const spyLocale = vi.spyOn(Intl.DateTimeFormat.prototype, 'resolvedOptions').mockReturnValue({
        locale: 'en-US',
        timeZone: 'US/Pacific',
      } as any);

      const detected = manager.detectRegionFromSystem();
      expect(detected.region).toBe('US');
      expect(detected.reliable).toBe(true);

      spyLocale.mockRestore();
    });

    it('falls back to localStorage if auto-detection is not reliable', () => {
      const manager = useFirmwareManager(options);
      localStorage.setItem('lrs_flasher_region', 'EU');

      const spyLocale = vi.spyOn(Intl.DateTimeFormat.prototype, 'resolvedOptions').mockReturnValue({
        locale: '',
        timeZone: '',
      } as any);

      manager.initializeRegion();
      expect(manager.region.value).toBe('EU');
      expect(logs[0]).toContain('using last selected region: EU');

      spyLocale.mockRestore();
    });
  });

  describe('fetchFirmware', () => {
    it('fetches remote versions and inserts local items correctly', async () => {
      vi.mocked(invoke).mockImplementation(async (cmd) => {
        if (cmd === 'get_firmware_list') return ['1.0.0', '0.9.9'];
        if (cmd === 'get_default_local_firmware') return '/default/fw.bin';
        return null;
      });

      const manager = useFirmwareManager(options);
      await manager.fetchFirmware();

      expect(manager.firmwareVersions.value).toEqual([
        LOCAL_OPTION,
        `${LOCAL_LABEL_PREFIX}fw.bin`,
        '1.0.0',
        '0.9.9'
      ]);
      expect(manager.selectedVersion.value).toBe(`${LOCAL_LABEL_PREFIX}fw.bin`);
    });

    it('handles failures by falling back and calling notify', async () => {
      vi.mocked(invoke).mockImplementation(async (cmd) => {
        if (cmd === 'get_firmware_list') throw new Error('Fetch Error');
        if (cmd === 'get_default_local_firmware') return null;
        return null;
      });

      const manager = useFirmwareManager(options);
      await manager.fetchFirmware();

      expect(notifications[0]).toContain('Error fetching firmware: Error: Fetch Error');
    });
  });

  describe('networkOtaFirmwareOptions', () => {
    it('produces options for remote version', () => {
      const manager = useFirmwareManager(options);
      manager.selectedVersion.value = '1.0.0';
      manager.region.value = 'EU';

      expect(manager.networkOtaFirmwareOptions()).toEqual({
        firmware_path: '1.0.0',
        region: 'EU'
      });
    });

    it('produces options for local version', () => {
      const manager = useFirmwareManager(options);
      manager.selectedVersion.value = `${LOCAL_LABEL_PREFIX}firmware.bin`;
      manager.selectedLocalPath.value = '/path/fw.bin';
      manager.region.value = 'EU';

      expect(manager.networkOtaFirmwareOptions()).toEqual({
        firmware_path: '/path/fw.bin',
        region: null
      });
    });
  });
});
