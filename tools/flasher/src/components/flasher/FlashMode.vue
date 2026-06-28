<script setup lang="ts">
import { computed } from 'vue';

export interface FlashFormDraft {
  bulkMode: boolean;
  region: string;
  selectedPort: string;
  selectedVersion: string;
  eraseBeforeFlash: boolean;
  monitorAfterFlash: boolean;
  serialFactoryKeepFleet: boolean;
  serialFactoryKeepWifi: boolean;
  bulkSelectedPorts: string[];
}

export interface SingleDeviceDetails {
  runningFirmware: string;
  runningFirmwareSummary: string;
  deviceInfo: Record<string, any> | null;
  orderedEntries: [string, any][];
  isLoadingInfo: boolean;
  isIdentifying: boolean;
  identifyAvailable: boolean;
  identifyDisabled: boolean;
  flashDisabled: boolean;
  isFlashing: boolean;
}

export interface BulkOperationsState {
  bulkFlashDisabled: boolean;
  bulkResetDisabled: boolean;
  isBulkFlashing: boolean;
  isBulkResetting: boolean;
}

export interface SystemConfigState {
  ports: Array<{ port_name: string; description: string | null; score: number }>;
  isRefreshingPorts: boolean;
  serialPortSelectorDisabled: boolean;
  portChipIds: Record<string, string>;
  firmwareVersions: string[];
  isFetchingFirmware: boolean;
  localOptionConstant: string;
}

const form = defineModel<FlashFormDraft>('form', { required: true });

defineProps<{
  deviceState: SingleDeviceDetails;
  bulkState: BulkOperationsState;
  systemState: SystemConfigState;
}>();

const emit = defineEmits<{
  (e: 'trigger-identify'): void;
  (e: 'refresh-ports'): void;
  (e: 'fetch-firmware'): void;
  (e: 'start-flash'): void;
  (e: 'read-device-info'): void;
  (e: 'copy-all-device-info'): void;
  (e: 'copy-to-clipboard', text: string, description: string): void;
  (e: 'toggle-select-all-bulk'): void;
  (e: 'start-bulk-flash'): void;
  (e: 'start-bulk-reset'): void;
}>();

const computedBulkMode = computed({
  get: () => form.value.bulkMode,
  set: (val) => { form.value = { ...form.value, bulkMode: val }; }
});

const computedBulkSelectedPorts = computed({
  get: () => form.value.bulkSelectedPorts,
  set: (val) => { form.value = { ...form.value, bulkSelectedPorts: val }; }
});

const computedRegion = computed({
  get: () => form.value.region,
  set: (val) => { form.value = { ...form.value, region: val }; }
});

const computedSelectedPort = computed({
  get: () => form.value.selectedPort,
  set: (val) => { form.value = { ...form.value, selectedPort: val }; }
});

const computedSelectedVersion = computed({
  get: () => form.value.selectedVersion,
  set: (val) => { form.value = { ...form.value, selectedVersion: val }; }
});

const computedEraseBeforeFlash = computed({
  get: () => form.value.eraseBeforeFlash,
  set: (val) => { form.value = { ...form.value, eraseBeforeFlash: val }; }
});

const computedMonitorAfterFlash = computed({
  get: () => form.value.monitorAfterFlash,
  set: (val) => { form.value = { ...form.value, monitorAfterFlash: val }; }
});

const computedKeepFleet = computed({
  get: () => form.value.serialFactoryKeepFleet,
  set: (val) => { form.value = { ...form.value, serialFactoryKeepFleet: val }; }
});

const computedKeepWifi = computed({
  get: () => form.value.serialFactoryKeepWifi,
  set: (val) => { form.value = { ...form.value, serialFactoryKeepWifi: val }; }
});

function formatLabel(key: string): string {
  const mapping: Record<string, string> = {
    'chip_id': 'Chip ID',
    'local_addr': 'Local addr',
    'remote_addr': 'Remote addr',
    'ssid': 'Soft AP SSID',
    'mac': 'MAC',
    'serial': 'Serial',
    'password': 'Factory password'
  };
  return mapping[key] || key.replace('_', ' ').split(' ').map(s => s.charAt(0).toUpperCase() + s.slice(1)).join(' ');
}
</script>

<template>
  <div class="flex flex-col gap-4 h-full min-h-0 overflow-auto custom-scrollbar pr-1">
    <!-- Segment Control (Single vs Bulk) -->
    <div class="grid grid-cols-2 rounded-lg border border-slate-800 bg-slate-950/40 p-1 text-xs font-bold shrink-0">
      <button
        @click="computedBulkMode = false"
        :class="['m-0 h-9 rounded-md px-3 transition-all flex items-center justify-center gap-1.5 shadow-none border-0', !computedBulkMode ? 'bg-cyan-600/80 text-white shadow-lg shadow-cyan-500/10' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
      >
        <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
          <line x1="9" y1="3" x2="9" y2="21"></line>
        </svg>
        <span>Single Device</span>
      </button>
      <button
        @click="computedBulkMode = true"
        :class="['m-0 h-9 rounded-md px-3 transition-all flex items-center justify-center gap-1.5 shadow-none border-0', computedBulkMode ? 'bg-cyan-600/80 text-white shadow-lg shadow-cyan-500/10' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
      >
        <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <rect x="3" y="3" width="7" height="9" rx="1"></rect>
          <rect x="14" y="3" width="7" height="5" rx="1"></rect>
          <rect x="14" y="12" width="7" height="9" rx="1"></rect>
          <rect x="3" y="16" width="7" height="5" rx="1"></rect>
        </svg>
        <span>Bulk Operations</span>
      </button>
    </div>

    <!-- Mode A: Single Flash Configuration & Details -->
    <template v-if="!computedBulkMode">
      <!-- Device Configuration Panel -->
      <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
        <div class="flex items-start justify-between gap-3">
          <h2 class="text-base font-bold text-cyan-300">
            Device configuration
          </h2>
          <button
            v-if="deviceState.identifyAvailable"
            @click="emit('trigger-identify')"
            :disabled="deviceState.identifyDisabled"
            :class="['glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed', { 'identify-led-active': deviceState.isIdentifying }]"
            title="Identify USB device"
            aria-label="Identify USB device"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 identify-led-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <path d="M9 18h6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M10 22h4" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M8 14a6 6 0 1 1 8 0c-.8.65-1.15 1.25-1.28 2H9.28C9.15 15.25 8.8 14.65 8 14Z" stroke="currentColor" stroke-width="2.2" stroke-linejoin="round"></path>
              <circle cx="12" cy="8" r="2.1" fill="currentColor"></circle>
            </svg>
          </button>
        </div>

        <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Region</label>
            <select v-model="computedRegion" class="glass-input h-10 appearance-none">
              <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
            </select>
          </div>

          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Serial port</label>
            <div class="flex gap-2">
              <select v-model="computedSelectedPort" :disabled="systemState.serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                <option v-for="port in systemState.ports" :key="port.port_name" :value="port.port_name">
                  {{ port.port_name }}
                </option>
                <option v-if="systemState.ports.length === 0" disabled>Scanning...</option>
              </select>
              <button @click="emit('refresh-ports')" :disabled="systemState.isRefreshingPorts || systemState.serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': systemState.isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
              </button>
            </div>
          </div>
        </div>

        <div :class="['rounded-md border p-3', deviceState.runningFirmware ? 'border-cyan-500/30 bg-cyan-500/10' : 'border-slate-800 bg-slate-950/30']">
          <div class="flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
            <div>
              <div class="text-[10px] font-bold uppercase tracking-wide text-slate-500">Running firmware</div>
              <div :class="['mt-1 font-mono text-xl font-bold', deviceState.runningFirmware ? 'text-cyan-100' : 'text-slate-500']">
                {{ deviceState.runningFirmware || '-' }}
              </div>
            </div>
            <div class="text-xs text-slate-400 sm:text-right">
              {{ deviceState.runningFirmwareSummary }}
            </div>
          </div>
        </div>

        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">Firmware version</label>
          <div class="flex gap-2">
            <select v-model="computedSelectedVersion" class="glass-input h-10 flex-1 appearance-none">
              <option v-for="v in systemState.firmwareVersions" :key="v" :value="v">
                {{ v === systemState.localOptionConstant ? 'Choose a file' : v }}
              </option>
              <option v-if="systemState.firmwareVersions.length === 0" disabled>Loading...</option>
            </select>
            <button @click="emit('fetch-firmware')" :disabled="systemState.isFetchingFirmware" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': systemState.isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
            </button>
          </div>
        </div>

        <div class="grid grid-cols-2 gap-3 mt-2">
          <div class="flex flex-col gap-3">
            <button @click="emit('start-flash')" :disabled="deviceState.flashDisabled" class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold w-full active:scale-95 transition-all disabled:opacity-60 disabled:cursor-not-allowed">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': deviceState.isFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
              <span>{{ deviceState.isFlashing ? 'Flashing...' : 'Flash firmware' }}</span>
            </button>
            <div class="flex flex-wrap items-center gap-3 px-1">
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedEraseBeforeFlash" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-amber-500 peer-checked:border-amber-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Erase flash before write</span>
              </label>
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedMonitorAfterFlash" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor when flash complete</span>
              </label>
            </div>
          </div>
          <button @click="emit('read-device-info')" :disabled="deviceState.isFlashing || deviceState.isLoadingInfo" class="glass-input h-9 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs transition-all active:scale-95">
            <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400', { 'animate-spin text-cyan-300': deviceState.isLoadingInfo }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
            <span>{{ deviceState.isLoadingInfo ? 'Reading...' : 'Get device info' }}</span>
          </button>
        </div>
      </div>

      <!-- Device Details Panel -->
      <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
        <div class="flex items-center justify-between">
          <h2 class="text-base font-bold text-slate-300">
            Device details
          </h2>
          <button v-if="deviceState.deviceInfo" @click="emit('copy-all-device-info')" class="text-slate-500 hover:text-cyan-300 transition-colors" title="Copy all">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
          </button>
        </div>

        <div class="space-y-1">
          <div v-for="[key, val] in deviceState.orderedEntries" :key="key" class="group flex items-center justify-between text-xs border-b border-white/5 py-1.5 hover:bg-white/5 px-2 -mx-2 rounded transition-colors">
            <span class="text-slate-500">{{ formatLabel(key) }}</span>
            <div class="flex items-center gap-3">
              <span class="font-mono text-slate-300">{{ val }}</span>
              <button @click="emit('copy-to-clipboard', val.toString(), formatLabel(key).toLowerCase())" class="opacity-0 group-hover:opacity-100 text-slate-600 hover:text-cyan-300 transition-all">
                <svg xmlns="http://www.w3.org/2000/svg" class="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>
          </div>
          <div v-if="!deviceState.deviceInfo && !deviceState.isLoadingInfo" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
            Connect a device and click <br/> "Get device info"
          </div>
          <div v-if="deviceState.isLoadingInfo" class="h-32 flex flex-col items-center justify-center text-cyan-300 italic text-sm gap-2">
            <span class="animate-spin text-2xl">◌</span>
            Reading device descriptors...
          </div>
        </div>
      </div>
    </template>

    <!-- Mode B: Bulk Operations Configuration & Controls -->
    <template v-else>
      <!-- Target Ports Checklist -->
      <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
        <div class="flex items-center justify-between border-b border-slate-800 pb-2">
          <h2 class="text-sm font-bold text-cyan-300">Target USB devices</h2>
          <div class="flex items-center gap-2">
            <button @click="emit('toggle-select-all-bulk')" class="text-xs text-slate-400 hover:text-slate-200 shadow-none bg-transparent border border-slate-700 rounded px-2 py-0.5 transition-all">
              {{ computedBulkSelectedPorts.length === systemState.ports.length ? 'Deselect All' : 'Select All' }}
            </button>
            <button @click="emit('refresh-ports')" :disabled="systemState.isRefreshingPorts" class="glass-input m-0 h-7 w-7 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': systemState.isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
            </button>
          </div>
        </div>

        <!-- Ports checklist -->
        <div class="flex flex-col gap-2 max-h-48 overflow-auto pr-1 custom-scrollbar">
          <div v-for="port in systemState.ports" :key="port.port_name" class="flex items-center justify-between p-2 rounded-md border border-slate-800 bg-slate-900/30 hover:border-slate-700/80 transition-all">
            <label class="flex items-center gap-3 cursor-pointer group flex-1">
              <div class="relative flex items-center">
                <input type="checkbox" :value="port.port_name" v-model="computedBulkSelectedPorts" class="peer hidden" />
                <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
              </div>
              <span class="text-xs font-semibold font-mono text-slate-300 group-hover:text-cyan-300 transition-colors">{{ port.port_name }}</span>
            </label>
            <span v-if="systemState.portChipIds[port.port_name]" class="text-[10px] font-mono text-slate-500 bg-slate-800/60 px-1.5 py-0.5 rounded border border-slate-700/40">
              {{ systemState.portChipIds[port.port_name] }}
            </span>
          </div>
          <div v-if="systemState.ports.length === 0" class="h-16 flex items-center justify-center text-slate-500 italic text-xs">
            No USB devices detected. Check connections.
          </div>
        </div>
      </div>

      <!-- Global Configuration Panel -->
      <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
        <h2 class="text-sm font-bold text-cyan-300 border-b border-slate-800 pb-2">Global configuration</h2>

        <div class="grid grid-cols-2 gap-3">
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Region</label>
            <select v-model="computedRegion" class="glass-input h-10 appearance-none">
              <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
            </select>
          </div>
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Firmware version</label>
            <div class="flex gap-2">
              <select v-model="computedSelectedVersion" class="glass-input h-10 flex-1 appearance-none">
                <option v-for="v in systemState.firmwareVersions" :key="v" :value="v">
                  {{ v === systemState.localOptionConstant ? 'Choose a file' : v }}
                </option>
                <option v-if="systemState.firmwareVersions.length === 0" disabled>Loading...</option>
              </select>
              <button @click="emit('fetch-firmware')" :disabled="systemState.isFetchingFirmware" class="glass-input h-10 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': systemState.isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
              </button>
            </div>
          </div>
        </div>

        <!-- Action execution CTAs & Options -->
        <div class="grid grid-cols-2 gap-4 mt-3 pt-3 border-t border-slate-800">
          <!-- Flash CTA & Options -->
          <div class="flex flex-col gap-3">
            <button
              @click="emit('start-bulk-flash')"
              :disabled="bulkState.bulkFlashDisabled"
              class="primary-btn h-10 flex items-center justify-center gap-2 text-xs font-bold w-full active:scale-95 transition-all disabled:opacity-50 disabled:cursor-not-allowed"
            >
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin': bulkState.isBulkFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
              <span>{{ bulkState.isBulkFlashing ? 'Flashing...' : 'Bulk Flash' }}</span>
            </button>
            <div class="flex flex-col gap-2 px-1">
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedEraseBeforeFlash" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-amber-500 peer-checked:border-amber-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Erase flash before write</span>
              </label>
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedMonitorAfterFlash" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor after flash</span>
              </label>
            </div>
          </div>

          <!-- Reset CTA & Options -->
          <div class="flex flex-col gap-3">
            <button
              @click="emit('start-bulk-reset')"
              :disabled="bulkState.bulkResetDisabled"
              class="glass-input m-0 h-10 hover:bg-slate-700/70 border-amber-500/30 hover:border-amber-500/60 bg-amber-500/5 text-amber-300 flex items-center justify-center gap-2 text-xs transition-all active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed disabled:border-slate-800 disabled:bg-slate-900/10 w-full"
            >
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin': bulkState.isBulkResetting }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
              <span>{{ bulkState.isBulkResetting ? 'Resetting...' : 'Bulk Reset' }}</span>
            </button>
            <div class="flex flex-col gap-2 px-1">
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedKeepFleet" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Keep shared fleet key</span>
              </label>
              <label class="flex items-center gap-2 cursor-pointer group">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="computedKeepWifi" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Keep WiFi credentials</span>
              </label>
            </div>
          </div>
        </div>
      </div>
    </template>
  </div>
</template>
