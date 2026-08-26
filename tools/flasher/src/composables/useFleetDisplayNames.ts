import { ref } from 'vue';

export const DISPLAY_NAME_MAX_CHARS = 16;
const DISPLAY_NAME_PATTERN = /^[A-Za-z0-9 _.-]*$/;

export interface DisplayNameResponse {
  ok: boolean;
  cmd: string;
  chip_id: string;
  display_name: string;
}

export type DisplayNameCommandSender = (
  target: string,
  cmd: string,
  payload: Record<string, unknown>,
  timeoutMs: number,
) => Promise<any>;

export function canonicalDisplayNameChipId(value: unknown): string {
  const text = String(value || '').trim().replace(/^lrs-/i, '').toLowerCase();
  if (!/^[0-9a-f]{1,8}$/.test(text)) return '';
  const canonical = text.padStart(8, '0');
  return canonical !== '00000000' ? canonical : '';
}

export function normalizeDisplayName(value: string): string {
  const normalized = value.trim();
  if (normalized.length > DISPLAY_NAME_MAX_CHARS || !DISPLAY_NAME_PATTERN.test(normalized)) {
    throw new Error('Names may use up to 16 letters, numbers, spaces, dots, dashes, or underscores.');
  }
  return normalized;
}

export function useFleetDisplayNames(sendCommand: DisplayNameCommandSender) {
  const names = ref<Record<string, string>>({});
  const saving = ref<Record<string, boolean>>({});
  const loading = ref<Record<string, boolean>>({});
  const unavailable = ref<Record<string, boolean>>({});
  const attempted = new Set<string>();
  let activeContext = '';
  let generation = 0;

  function resetContext(context: string) {
    if (context === activeContext) return;
    activeContext = context;
    generation += 1;
    names.value = {};
    saving.value = {};
    loading.value = {};
    unavailable.value = {};
    attempted.clear();
  }

  async function load(
    context: string,
    target: string,
    adminPassword: string,
    chipIds: unknown[],
  ) {
    resetContext(context);
    if (!context || !target || !adminPassword) return;
    const loadGeneration = generation;
    const uniqueChipIds = [...new Set(chipIds.map(canonicalDisplayNameChipId).filter(Boolean))];
    const pendingChipIds = uniqueChipIds.filter(chipId => !attempted.has(chipId));
    if (pendingChipIds.length > 0) {
      const nextLoading = { ...loading.value };
      for (const chipId of pendingChipIds) nextLoading[chipId] = true;
      loading.value = nextLoading;
    }
    for (const chipId of pendingChipIds) {
      attempted.add(chipId);
      try {
        const response: DisplayNameResponse = await sendCommand(
          target,
          'get_display_name',
          { admin_password: adminPassword, chip_id: chipId },
          5000,
        );
        if (generation !== loadGeneration || activeContext !== context) return;
        names.value = {
          ...names.value,
          [chipId]: typeof response.display_name === 'string' ? response.display_name : '',
        };
        const nextUnavailable = { ...unavailable.value };
        delete nextUnavailable[chipId];
        unavailable.value = nextUnavailable;
      } catch {
        // A background name read is optional. Keep the blank state and do not
        // turn normal Fleet refresh into a retry loop.
        if (generation === loadGeneration && activeContext === context) {
          unavailable.value = { ...unavailable.value, [chipId]: true };
        }
      } finally {
        if (generation === loadGeneration) {
          const nextLoading = { ...loading.value };
          delete nextLoading[chipId];
          loading.value = nextLoading;
        }
      }
    }
  }

  async function save(
    context: string,
    target: string,
    adminPassword: string,
    chipIdValue: unknown,
    displayName: string,
  ): Promise<string> {
    const chipId = canonicalDisplayNameChipId(chipIdValue);
    const normalized = normalizeDisplayName(displayName);
    if (!context || context !== activeContext || !target || !adminPassword || !chipId) {
      throw new Error('The selected gateway is not ready to save names.');
    }
    if (loading.value[chipId] || unavailable.value[chipId]) {
      throw new Error('The current name could not be loaded; reconnect the gateway before editing it.');
    }
    const saveGeneration = generation;
    saving.value = { ...saving.value, [chipId]: true };
    try {
      const response: DisplayNameResponse = await sendCommand(
        target,
        'set_display_name',
        { admin_password: adminPassword, chip_id: chipId, display_name: normalized },
        5000,
      );
      if (generation !== saveGeneration || activeContext !== context) {
        throw new Error('The selected gateway changed before the name was saved.');
      }
      const savedName = typeof response.display_name === 'string'
        ? response.display_name
        : normalized;
      names.value = { ...names.value, [chipId]: savedName };
      const nextUnavailable = { ...unavailable.value };
      delete nextUnavailable[chipId];
      unavailable.value = nextUnavailable;
      attempted.add(chipId);
      return savedName;
    } finally {
      if (generation === saveGeneration) {
        const nextSaving = { ...saving.value };
        delete nextSaving[chipId];
        saving.value = nextSaving;
      }
    }
  }

  return { names, saving, loading, unavailable, resetContext, load, save };
}
