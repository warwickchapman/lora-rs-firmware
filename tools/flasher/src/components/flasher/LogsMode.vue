<script setup lang="ts">
import { computed } from 'vue';
import type { LogFilter, LogLayout, LogRecord } from '../../composables/useLogSession';

const props = defineProps<{
  records: LogRecord[];
  sources: { id: string; label: string }[];
  availableSources: { id: string; label: string; disabled?: boolean; reason?: string }[];
  selectedSourceIds: string[];
  layout: LogLayout;
  filter: LogFilter;
  status: string;
}>();
const emit = defineEmits<{
  (e: 'update:selectedSourceIds', value: string[]): void;
  (e: 'update:layout', value: LogLayout): void;
  (e: 'update:filter', value: LogFilter): void;
  (e: 'copy'): void;
  (e: 'clear'): void;
  (e: 'export'): void;
  (e: 'stop'): void;
  (e: 'select-source', id: string): void;
}>();
const grouped = computed(() => props.sources.map(source => ({ ...source, records: props.records.filter(record => record.sourceId === source.id) })).filter(group => group.records.length));
function time(value: number) { return new Date(value).toLocaleTimeString(); }
function severityClass(severity: LogRecord['severity']) { return severity === 'error' || severity === 'crash' ? 'text-rose-300' : severity === 'warn' ? 'text-amber-300' : severity === 'reset' ? 'text-sky-300' : 'text-slate-300'; }
function toggleSource(id: string, checked: boolean) { emit('update:selectedSourceIds', checked ? [...props.selectedSourceIds, id] : props.selectedSourceIds.filter(value => value !== id)); }
</script>

<template>
  <section class="glass-card h-full min-h-0 p-3 flex flex-col gap-3 text-left">
    <div class="flex flex-wrap items-center justify-between gap-3 border-b border-slate-700/80 pb-2">
      <div><h2 class="text-lg font-bold text-slate-200">Logs</h2><p class="text-xs text-slate-500">{{ status }} UDP delivery is best-effort diagnostic evidence, not device health.</p></div>
      <div class="flex gap-2"><button @click="emit('copy')" class="glass-input m-0 h-8 px-3 text-xs font-bold">Copy</button><button @click="emit('export')" class="glass-input m-0 h-8 px-3 text-xs font-bold">Export JSONL</button><button @click="emit('clear')" class="glass-input m-0 h-8 px-3 text-xs font-bold">Clear</button><button @click="emit('stop')" class="glass-input m-0 h-8 px-3 text-xs font-bold">Stop forwarding</button></div>
    </div>
    <div class="flex flex-wrap gap-3 text-xs">
      <label>Log source <select :value="selectedSourceIds[0] || ''" @change="emit('select-source', ($event.target as HTMLSelectElement).value)" class="ml-1 glass-input h-7 px-2"><option value="" disabled>Choose a gateway or remote…</option><option v-for="source in availableSources" :key="source.id" :value="source.id" :disabled="source.disabled">{{ source.label }}{{ source.reason ? ` — ${source.reason}` : '' }}</option></select></label>
      <label>View <select :value="layout" @change="emit('update:layout', ($event.target as HTMLSelectElement).value as LogLayout)" class="ml-1 glass-input h-7 px-2"><option value="focused">Focused</option><option value="stacked">Stacked</option><option value="merged">Merged</option></select></label>
      <label>Filter <select :value="filter" @change="emit('update:filter', ($event.target as HTMLSelectElement).value as LogFilter)" class="ml-1 glass-input h-7 px-2"><option value="all">All</option><option value="events">Events</option><option value="problems">Warnings / errors</option></select></label>
      <label v-for="source in sources" :key="source.id" class="flex items-center gap-1 text-slate-400"><input type="checkbox" :checked="selectedSourceIds.includes(source.id)" @change="toggleSource(source.id, ($event.target as HTMLInputElement).checked)">{{ source.label }}</label>
    </div>
    <div class="min-h-0 flex-1 overflow-auto custom-scrollbar font-mono text-[11px] leading-tight rounded border border-slate-800 bg-slate-950/50 p-2">
      <template v-if="layout === 'stacked'"><div v-for="group in grouped" :key="group.id" class="mb-4"><h3 class="sticky top-0 bg-slate-950 py-1 text-xs font-bold text-cyan-300">{{ group.label }}</h3><div v-for="record in group.records" :key="record.id" class="grid grid-cols-[70px_70px_minmax(0,160px)_minmax(0,1fr)] gap-2 border-b border-slate-900 py-1"><span class="text-slate-500">{{ time(record.receivedAt) }}</span><span class="text-slate-500">{{ record.transport }}</span><span :class="['min-w-0 truncate', severityClass(record.severity)]" :title="record.event || record.severity">{{ record.event || record.severity }}</span><span class="min-w-0 whitespace-pre-wrap break-words" :title="record.raw">{{ record.raw }}</span></div></div></template>
      <template v-else><div v-for="record in records" :key="record.id" class="grid grid-cols-[70px_minmax(0,150px)_50px_minmax(0,160px)_minmax(0,1fr)] gap-2 border-b border-slate-900 py-1"><span class="text-slate-500">{{ time(record.receivedAt) }}</span><span class="min-w-0 truncate text-cyan-300" :title="record.sourceLabel">{{ record.sourceLabel }}</span><span class="text-slate-500">{{ record.transport }}</span><span :class="['min-w-0 truncate', severityClass(record.severity)]" :title="record.event || record.severity">{{ record.event || record.severity }}</span><span class="min-w-0 whitespace-pre-wrap break-words" :title="record.raw">{{ record.raw }}</span></div></template>
      <div v-if="!records.length" class="p-4 text-slate-600">Choose a source above. Selecting a remote requests temporary UDP forwarding; no routine polling is performed.</div>
    </div>
  </section>
</template>
