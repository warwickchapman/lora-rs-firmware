import { computed, ref } from 'vue';

export type LogTransport = 'serial' | 'udp' | 'system';
export type LogSeverity = 'info' | 'warn' | 'error' | 'crash' | 'reset' | 'raw';
export type LogLayout = 'focused' | 'stacked' | 'merged';
export type LogFilter = 'all' | 'events' | 'problems';

export interface LogRecord {
  id: number;
  receivedAt: number;
  sourceId: string;
  sourceLabel: string;
  transport: LogTransport;
  severity: LogSeverity;
  event: string | null;
  deviceTimeMs: number | null;
  raw: string;
}

const MAX_TOTAL_RECORDS = 2000;
const MAX_PER_SOURCE_RECORDS = 500;

function numberField(line: string, name: string): number | null {
  const match = line.match(new RegExp(`\\b${name}=(-?\\d+)\\b`));
  return match ? Number(match[1]) : null;
}

function classify(line: string, event: string | null): LogSeverity {
  if (/User exception|Soft WDT reset|wdt reset|Fatal exception|Unhandled C\+\+ exception/.test(line)) return 'crash';
  if (event === 'boot_banner' || event === 'boot' || event === 'gateway_log_timestamp_reset') return 'reset';
  if (/\[ERROR\]|\b(error|failed)\b/i.test(line) || event?.includes('_fail')) return 'error';
  if (/\[WARN\]|\bwarning\b/i.test(line) || event?.includes('timeout') || event?.includes('_bad')) return 'warn';
  if (event) return 'info';
  return 'raw';
}

export function useLogSession() {
  const records = ref<LogRecord[]>([]);
  const selectedSourceIds = ref<string[]>([]);
  const knownSources = ref<Record<string, string>>({});
  const layout = ref<LogLayout>('focused');
  const filter = ref<LogFilter>('all');
  let nextId = 1;

  function append(input: Omit<LogRecord, 'id' | 'receivedAt' | 'severity' | 'event' | 'deviceTimeMs'> & Partial<Pick<LogRecord, 'severity' | 'event' | 'deviceTimeMs'>>) {
    knownSources.value = { ...knownSources.value, [input.sourceId]: input.sourceLabel };
    const event = input.event ?? input.raw.match(/\bevent=([^\s]+)/)?.[1] ?? null;
    const record: LogRecord = {
      ...input,
      id: nextId++,
      receivedAt: Date.now(),
      event,
      deviceTimeMs: input.deviceTimeMs ?? numberField(input.raw, 't'),
      severity: input.severity ?? classify(input.raw, event),
    };
    records.value.push(record);
    const sourceRecords = records.value.filter(item => item.sourceId === record.sourceId);
    const excess = sourceRecords.length - MAX_PER_SOURCE_RECORDS;
    if (excess > 0) {
      const ids = new Set(sourceRecords.slice(0, excess).map(item => item.id));
      records.value = records.value.filter(item => !ids.has(item.id));
    }
    if (records.value.length > MAX_TOTAL_RECORDS) records.value = records.value.slice(-MAX_TOTAL_RECORDS);
  }

  const sourceOptions = computed(() => {
    const labels = new Map<string, string>(Object.entries(knownSources.value));
    for (const record of records.value) labels.set(record.sourceId, record.sourceLabel);
    return [...labels].map(([id, label]) => ({ id, label }));
  });

  const visibleRecords = computed(() => records.value.filter(record => {
    if (selectedSourceIds.value.length && !selectedSourceIds.value.includes(record.sourceId)) return false;
    if (layout.value === 'focused' && selectedSourceIds.value.length > 1 && record.sourceId !== selectedSourceIds.value[0]) return false;
    if (filter.value === 'events') return record.event !== null;
    if (filter.value === 'problems') return ['warn', 'error', 'crash'].includes(record.severity);
    return true;
  }));

  function selectOnly(sourceId: string) {
    selectedSourceIds.value = [sourceId];
    layout.value = 'focused';
  }

  function registerSource(sourceId: string, sourceLabel: string) {
    knownSources.value = { ...knownSources.value, [sourceId]: sourceLabel };
  }

  function clear() { records.value = []; }

  function exportJsonl(): string {
    return visibleRecords.value.map(record => JSON.stringify(record)).join('\n') + (visibleRecords.value.length ? '\n' : '');
  }

  return { records, selectedSourceIds, layout, filter, sourceOptions, visibleRecords, append, selectOnly, registerSource, clear, exportJsonl };
}
