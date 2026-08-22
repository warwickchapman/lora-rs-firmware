import { ref, watch, Ref } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { open } from '@tauri-apps/plugin-dialog';

export type RegionCode = 'ZA' | 'EU' | 'US';

export const LOCAL_OPTION = '__local_browse__';
export const LOCAL_LABEL_PREFIX = 'Local: ';

export interface UseFirmwareManagerOptions {
  flasherAppVersion: Ref<string> | (() => string);
  pushLog: (line: string) => void;
  notify: (msg: string) => void;
}

const EU_COUNTRY_CODES = new Set([
  'AT', 'BE', 'BG', 'HR', 'CY', 'CZ', 'DK', 'EE', 'FI', 'FR',
  'DE', 'GR', 'HU', 'IE', 'IT', 'LV', 'LT', 'LU', 'MT', 'NL',
  'PL', 'PT', 'RO', 'SK', 'SI', 'ES', 'SE', 'NO', 'IS', 'LI',
  'CH', 'GB',
]);
const US_COUNTRY_CODES = new Set(['US', 'UM', 'PR', 'GU', 'VI', 'AS', 'MP']);
const ZA_COUNTRY_CODES = new Set(['ZA']);

const REGION_STORAGE_KEY = 'lrs_flasher_region';
const REGION_CONFIDENT_MIN_SCORE = 5;
const REGION_CONFIDENT_MIN_GAP = 2;

function extractCountryCodes(locale: string): string[] {
  return locale
    .split(/[-_]/)
    .filter(part => /^[A-Za-z]{2}$/.test(part))
    .map(part => part.toUpperCase());
}

export function useFirmwareManager(options: UseFirmwareManagerOptions) {
  const firmwareVersions = ref<string[]>([LOCAL_OPTION]);
  const selectedVersion = ref('');
  const selectedLocalPath = ref('');
  const selectedLocalIsDefault = ref(false);
  const isFetchingFirmware = ref(false);
  const region = ref<RegionCode>('ZA');

  const appVersionGetter = typeof options.flasherAppVersion === 'function'
    ? options.flasherAppVersion
    : () => (options.flasherAppVersion as Ref<string>).value;

  function selectedFirmwareCandidateVersion(): string {
    if (!selectedVersion.value) return '';
    if (!selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX)) return selectedVersion.value;
    const fileMatch = selectedLocalPath.value.match(/(\d+\.\d+\.\d+)(?:~(\d+))?/);
    return fileMatch ? fileMatch[0] : appVersionGetter();
  }

  function setLocalFirmwareSelection(path: string, announce = true, isDefault = false) {
    selectedLocalPath.value = path;
    selectedLocalIsDefault.value = isDefault;
    const filename = path.split(/[\\/]/).pop() || 'firmware.bin';
    const localLabel = `${LOCAL_LABEL_PREFIX}${filename}`;
    firmwareVersions.value = firmwareVersions.value.filter(v => !v.startsWith(LOCAL_LABEL_PREFIX));
    firmwareVersions.value.splice(1, 0, localLabel);
    selectedVersion.value = localLabel;
    if (announce) {
      options.pushLog(`Local firmware selected: ${path}`);
    }
  }

  async function refreshDefaultLocalFirmware(): Promise<boolean> {
    const defaultLocalFirmware = await invoke<string | null>('get_default_local_firmware').catch(() => null);
    if (!defaultLocalFirmware) return false;
    if (selectedLocalPath.value && !selectedLocalIsDefault.value) return false;
    if (selectedLocalPath.value === defaultLocalFirmware) return false;
    const priorSelection = selectedVersion.value;
    setLocalFirmwareSelection(defaultLocalFirmware, false, true);
    if (priorSelection && !priorSelection.startsWith(LOCAL_LABEL_PREFIX)) {
      selectedVersion.value = priorSelection;
    }
    return true;
  }

  async function openLocalFileDialog() {
    try {
      const selected = await open({
        multiple: false,
        filters: [{
          name: 'LRS Firmware',
          extensions: ['bin']
        }]
      });

      if (selected && typeof selected === 'string') {
        setLocalFirmwareSelection(selected);
      } else {
        if (selectedVersion.value === LOCAL_OPTION) {
          selectedVersion.value = firmwareVersions.value[1] || '';
        }
      }
    } catch (e) {
      options.notify('Error opening file dialog: ' + e);
    }
  }

  watch(selectedVersion, (newVal) => {
    if (newVal === LOCAL_OPTION) {
      openLocalFileDialog();
    }
  });

  function detectRegionFromSystem(): { region: RegionCode | null; reliable: boolean; reason: string } {
    const score: Record<RegionCode, number> = { ZA: 0, EU: 0, US: 0 };
    const clues: string[] = [];
    const addScore = (target: RegionCode, weight: number, clue: string) => {
      score[target] += weight;
      clues.push(`${target}+${weight}:${clue}`);
    };

    const localeCandidates: string[] = [];
    const resolvedLocale = Intl.DateTimeFormat().resolvedOptions().locale;
    if (resolvedLocale) localeCandidates.push(resolvedLocale);
    if (navigator.language) localeCandidates.push(navigator.language);
    if (Array.isArray(navigator.languages)) {
      localeCandidates.push(...navigator.languages);
    }

    const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
    if (timezone === 'Africa/Johannesburg') addScore('ZA', 8, `timezone=${timezone}`);
    if (timezone.startsWith('Europe/')) addScore('EU', 6, `timezone=${timezone}`);
    if (timezone.startsWith('US/')) addScore('US', 6, `timezone=${timezone}`);

    const seen = new Set<string>();
    localeCandidates
      .filter(Boolean)
      .forEach((locale, idx) => {
        const norm = String(locale).trim();
        if (!norm || seen.has(norm)) return;
        seen.add(norm);
        const weight = idx === 0 ? 3 : idx === 1 ? 2 : 1;
        for (const code of extractCountryCodes(norm)) {
          if (ZA_COUNTRY_CODES.has(code)) addScore('ZA', weight, `locale=${norm}`);
          if (US_COUNTRY_CODES.has(code)) addScore('US', weight, `locale=${norm}`);
          if (EU_COUNTRY_CODES.has(code)) addScore('EU', weight, `locale=${norm}`);
        }
      });

    const ranked = (Object.entries(score) as Array<[RegionCode, number]>).sort((a, b) => b[1] - a[1]);
    const top = ranked[0];
    const second = ranked[1];
    const hasSignal = top[1] > 0;
    const reliable =
      hasSignal &&
      top[1] >= REGION_CONFIDENT_MIN_SCORE &&
      (top[1] - second[1]) >= REGION_CONFIDENT_MIN_GAP;
    const reason = clues.length ? clues.join(', ') : 'no locale/timezone signal';

    if (!hasSignal) return { region: null, reliable: false, reason };
    return { region: top[0], reliable, reason };
  }

  function initializeRegion() {
    const rememberedRegion = (() => {
      try {
        const saved = localStorage.getItem(REGION_STORAGE_KEY);
        return saved === 'ZA' || saved === 'EU' || saved === 'US' ? (saved as RegionCode) : null;
      } catch (_) {
        return null;
      }
    })();

    const detected = detectRegionFromSystem();
    if (detected.region && detected.reliable) {
      region.value = detected.region;
      options.pushLog(`Region auto-detected: ${detected.region} (${detected.reason})`);
    } else if (rememberedRegion) {
      region.value = rememberedRegion;
      options.pushLog(`Region auto-detect not confident; using last selected region: ${rememberedRegion} (${detected.reason})`);
    } else if (detected.region) {
      region.value = detected.region;
      options.pushLog(`Region auto-detect weak signal; using best guess: ${detected.region} (${detected.reason})`);
    } else {
      options.pushLog(`Region auto-detection unavailable; using default: ${region.value}`);
    }
  }

  watch(region, (next) => {
    try {
      localStorage.setItem(REGION_STORAGE_KEY, next);
    } catch (_) {
      // Ignore storage failures and continue with in-memory value.
    }
  });

  async function fetchFirmware() {
    isFetchingFirmware.value = true;
    try {
      await refreshDefaultLocalFirmware();
      const remoteVersions = await invoke<string[]>('get_firmware_list');
      const localEntry = firmwareVersions.value.find(v => v.startsWith(LOCAL_LABEL_PREFIX));
      firmwareVersions.value = [LOCAL_OPTION, ...(localEntry ? [localEntry] : []), ...remoteVersions];

      if (!selectedVersion.value && firmwareVersions.value.length > 1) {
        selectedVersion.value = firmwareVersions.value[1];
      }
      await new Promise(resolve => setTimeout(resolve, 400));
    } catch (e) {
      await refreshDefaultLocalFirmware();
      const localEntry = firmwareVersions.value.find(v => v.startsWith(LOCAL_LABEL_PREFIX));
      firmwareVersions.value = [LOCAL_OPTION, ...(localEntry ? [localEntry] : [])];
      options.notify('Error fetching firmware: ' + e);
    } finally {
      isFetchingFirmware.value = false;
    }
  }

  function networkOtaFirmwareOptions(): { firmware_path: string; region: RegionCode | null } | null {
    const isLocal = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX);
    const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
    if (!firmwarePath || (isLocal && !selectedLocalPath.value)) {
      options.notify('Local file path missing');
      return null;
    }
    return {
      firmware_path: firmwarePath,
      region: isLocal ? null : region.value
    };
  }

  return {
    firmwareVersions,
    selectedVersion,
    selectedLocalPath,
    isFetchingFirmware,
    region,
    selectedFirmwareCandidateVersion,
    networkOtaFirmwareOptions,
    fetchFirmware,
    refreshDefaultLocalFirmware,
    openLocalFileDialog,
    setLocalFirmwareSelection,
    detectRegionFromSystem,
    initializeRegion
  };
}
