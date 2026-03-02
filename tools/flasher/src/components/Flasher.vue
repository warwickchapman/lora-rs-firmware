<script setup lang="ts">
import { ref, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';

interface SerialPort {
  port_name: string;
  description: string | null;
  score: number;
}

interface LogEvent {
  message: string;
}

interface MonitorEvent {
  line: string;
}

interface DeviceInfo {
  chip_id: string;
  mac: string;
  serial: string;
  password: string;
  local_addr: number;
  remote_addr: number;
  ssid: string;
}

const ports = ref<SerialPort[]>([]);
const selectedPort = ref('');
const firmwareVersions = ref<string[]>([]);
const selectedVersion = ref('');
const selectedLocalPath = ref('');
const region = ref('ZA');
const isFlashing = ref(false);
const isMonitoring = ref(false);
const logs = ref<string[]>([]);
const deviceInfo = ref<DeviceInfo | null>(null);
const isLoadingInfo = ref(false);
const isRefreshingPorts = ref(false);
const isFetchingFirmware = ref(false);
const showToast = ref(false);
const toastMessage = ref('');
const logContainer = ref<HTMLElement | null>(null);
const monitorAfterFlash = ref(true);

const LOCAL_OPTION = '__local_browse__';

let unlistenFlash: UnlistenFn | null = null;
let unlistenMonitor: UnlistenFn | null = null;

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
      selectedLocalPath.value = selected;
      const filename = selected.split(/[\\/]/).pop();
      // Add or update the local selection in the list
      const localLabel = `Local: ${filename}`;
      // Remove any existing 'Local: ' entries to avoid duplicates
      firmwareVersions.value = firmwareVersions.value.filter(v => !v.startsWith('Local: '));
      firmwareVersions.value.splice(1, 0, localLabel); // Insert after LOCAL_OPTION
      selectedVersion.value = localLabel;
      logs.value.push(`Local firmware selected: ${selected}`);
    } else {
      // If cancelled and we were on "browse", revert to previous or first available
      if (selectedVersion.value === LOCAL_OPTION) {
        selectedVersion.value = firmwareVersions.value[1] || ''; // Select the first remote version
      }
    }
  } catch (e) {
    notify('Error opening file dialog: ' + e);
  }
}

watch(selectedVersion, (newVal) => {
  if (newVal === LOCAL_OPTION) {
    openLocalFileDialog();
  }
});

async function refreshPorts() {
  isRefreshingPorts.value = true;
  try {
    const fetchedPorts: SerialPort[] = await invoke('list_serial_ports');
    ports.value = fetchedPorts.sort((a, b) => b.score - a.score);
    if (ports.value.length > 0 && !selectedPort.value) {
      selectedPort.value = ports.value[0].port_name;
    }
    // Artificial delay to ensure the spin is satisfyingly visible
    await new Promise(resolve => setTimeout(resolve, 300));
  } finally {
    isRefreshingPorts.value = false;
  }
}

async function fetchFirmware() {
  isFetchingFirmware.value = true;
  try {
    const remoteVersions: string[] = await invoke('get_firmware_list');
    // Maintain local selection if it exists
    const localEntry = firmwareVersions.value.find(v => v.startsWith('Local: '));
    firmwareVersions.value = [LOCAL_OPTION, ...(localEntry ? [localEntry] : []), ...remoteVersions];
    
    if (!selectedVersion.value && firmwareVersions.value.length > 1) {
      selectedVersion.value = firmwareVersions.value[1];
    }
    await new Promise(resolve => setTimeout(resolve, 400));
  } catch (e) {
    notify('Error fetching firmware: ' + e);
  } finally {
    isFetchingFirmware.value = false;
  }
}

function notify(msg: string) {
  toastMessage.value = msg;
  showToast.value = true;
  setTimeout(() => {
    showToast.value = false;
  }, 3000);
}

async function copyToClipboard(text: string, label: string) {
  try {
    await navigator.clipboard.writeText(text);
    notify(`Copied ${label} to clipboard`);
  } catch (err) {
    notify('Failed to copy to clipboard');
  }
}

function copyAllDeviceInfo() {
  if (!deviceInfo.value) return;
  const block = Object.entries(deviceInfo.value)
    .map(([key, val]) => `${formatLabel(key).toUpperCase()}: ${val}`)
    .join('\n');
  copyToClipboard(block, 'all device configuration');
}

async function readDeviceInfo() {
  if (!selectedPort.value) return;
  isLoadingInfo.value = true;
  deviceInfo.value = null;
  logs.value.push('Reading device information...');
  try {
    deviceInfo.value = await invoke('get_device_info', { port: selectedPort.value });
    logs.value.push('Device info read successfully');
  } catch (e) {
    logs.value.push('Failed to read device info: ' + e);
  } finally {
    isLoadingInfo.value = false;
  }
}

async function startFlash() {
  if (!selectedPort.value || !selectedVersion.value) return;
  
  isFlashing.value = true;
  logs.value.push('--- Preparing Firmware ---');
  
  try {
    const isLocal = selectedVersion.value.startsWith('Local: ');
    const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
    
    if (isLocal && !firmwarePath) throw new Error('Local file path missing');

    const result = await invoke('flash_firmware', { 
      port: selectedPort.value,
      firmwarePath,
      region: isLocal ? null : region.value
    });
    logs.value.push(result as string);
    
    // Auto-monitor transition
    if (monitorAfterFlash.value) {
      await nextTick();
      toggleMonitor();
    }
  } catch (e) {
    logs.value.push('Flash failed: ' + e);
    notify('Flash failed: ' + e);
  } finally {
    isFlashing.value = false;
  }
}

async function toggleMonitor() {
  if (!selectedPort.value) return;
  const targetState = !isMonitoring.value;
  try {
    await invoke('toggle_serial_monitor', { 
      port: selectedPort.value, 
      baud: 115200, 
      enable: targetState 
    });
    isMonitoring.value = targetState;
    logs.value.push(targetState ? 'Serial monitor started' : 'Serial monitor stopped');
  } catch (e) {
    logs.value.push('Monitor error: ' + e);
  }
}

function scrollToBottom() {
  if (logContainer.value) {
    logContainer.value.scrollTop = logContainer.value.scrollHeight;
  }
}

watch(logs, () => {
  nextTick(() => scrollToBottom());
}, { deep: true });

onMounted(async () => {
  refreshPorts();
  fetchFirmware();
  
  unlistenFlash = await listen<LogEvent>('flash-log', (event) => {
    const rawMsg = event.payload.message;
    // Split on both newlines and carriage returns to ensure progress updates 
    // from esptool appear as fresh lines in our Activity Log.
    const lines = rawMsg.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
      if (trimmed) logs.value.push(trimmed);
    });
    
    // Keep log size sane (last 1000 lines)
    if (logs.value.length > 1000) {
      logs.value = logs.value.slice(-1000);
    }
  });
  
  unlistenMonitor = await listen<MonitorEvent>('monitor-log', (event) => {
    const rawLine = event.payload.line;
    const lines = rawLine.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
       if (trimmed) logs.value.push(trimmed);
    });
    
    if (logs.value.length > 2000) {
      logs.value = logs.value.slice(-2000);
    }
  });
});

onUnmounted(() => {
  if (unlistenFlash) unlistenFlash();
  if (unlistenMonitor) unlistenMonitor();
});

function formatLabel(key: string) {
  const mapping: Record<string, string> = {
    'chip_id': 'Chip ID',
    'local_addr': 'Local addr',
    'remote_addr': 'Remote addr',
    'ssid': 'Soft AP SSID',
    'mac': 'MAC',
    'serial': 'Serial',
    'password': 'Password'
  };
  return mapping[key] || key.replace('_', ' ').split(' ').map(s => s.charAt(0).toUpperCase() + s.slice(1)).join(' ');
}
</script>

<template>
  <div class="relative h-full flex flex-col">
    <div :class="['grid gap-8 flex-1 min-h-0 transition-all duration-500', isMonitoring ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
      <!-- Log Panel -->
      <div :class="['glass-card p-6 flex flex-col gap-4 text-left overflow-hidden h-full']">
        <div class="flex items-center justify-between border-b border-white/5 pb-4">
          <h2 class="text-lg font-semibold text-slate-300 flex items-center gap-2">
            <span :class="['w-2 h-2 rounded-full', isMonitoring || isFlashing ? 'bg-indigo-500 animate-pulse' : 'bg-slate-600']"></span>
            Activity log
          </h2>
          <div class="flex items-center gap-4">
            <button @click="toggleMonitor" :class="['px-3 py-1 rounded-md text-xs font-bold transition-all border', isMonitoring ? 'bg-indigo-500/20 border-indigo-500 text-indigo-400' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500']">
              {{ isMonitoring ? 'Stop monitor' : 'Start monitor' }}
            </button>
            <button @click="logs = []" class="text-xs text-slate-500 hover:text-slate-300">Clear</button>
          </div>
        </div>
        
        <div ref="logContainer" class="flex-1 overflow-auto font-mono text-[9px] sm:text-[10px] pr-2 custom-scrollbar space-y-0.5 leading-tight tracking-tight whitespace-pre">
          <div v-for="(log, i) in logs" :key="i" class="text-slate-400 border-l border-slate-700/50 pl-2 opacity-90">
            {{ log }}
          </div>
          <div v-if="logs.length === 0" class="h-full flex items-center justify-center text-slate-600 italic text-xs">
            No activity logs to show
          </div>
        </div>
      </div>

      <!-- Right Panel (Controls + Details) - Hidden in Monitor Mode -->
      <div v-if="!isMonitoring" class="flex flex-col gap-6 h-full overflow-hidden transition-opacity duration-300" :class="{ 'opacity-0 pointer-events-none': isMonitoring }">
        <!-- Device Configuration Panel -->
        <div class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <h2 class="text-xl font-bold bg-gradient-to-r from-indigo-400 to-purple-400 bg-clip-text text-transparent">
            Device configuration
          </h2>
          
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Region</label>
              <select v-model="region" class="glass-input h-10 appearance-none">
                <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
              </select>
            </div>

            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Serial port</label>
              <div class="flex gap-2">
                <select v-model="selectedPort" class="glass-input h-10 flex-1 appearance-none">
                  <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                    {{ port.port_name }}
                  </option>
                  <option v-if="ports.length === 0" disabled>Scanning...</option>
                </select>
                <button @click="refreshPorts" :disabled="isRefreshingPorts" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all group/btn shrink-0">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-indigo-400 transition-colors', { 'animate-spin text-indigo-500': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Firmware version</label>
            <div class="flex gap-2">
              <select v-model="selectedVersion" class="glass-input h-10 flex-1 appearance-none">
                <option v-for="v in firmwareVersions" :key="v" :value="v">
                  {{ v === LOCAL_OPTION ? 'Choose a file' : v }}
                </option>
                <option v-if="firmwareVersions.length === 0" disabled>Loading...</option>
              </select>
              <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all group/btn shrink-0">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-indigo-400 transition-colors', { 'animate-bounce text-indigo-500': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
              </button>
            </div>
          </div>

          <div class="grid grid-cols-2 gap-4 mt-2">
            <div class="flex flex-col gap-3">
              <button @click="startFlash" :disabled="isFlashing || isLoadingInfo" class="primary-btn h-12 flex items-center justify-center gap-3 text-sm tracking-wider font-bold w-full active:scale-95 transition-all">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
                <span>{{ isFlashing ? 'Flashing...' : 'Flash firmware' }}</span>
              </button>
              <label class="flex items-center gap-2 cursor-pointer group px-1">
                <div class="relative flex items-center">
                  <input type="checkbox" v-model="monitorAfterFlash" class="peer hidden" />
                  <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-indigo-500 peer-checked:border-indigo-500 transition-all"></div>
                  <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                </div>
                <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor when flash complete</span>
              </label>
            </div>
            <button @click="readDeviceInfo" :disabled="isFlashing || isLoadingInfo" class="glass-input h-12 hover:bg-white/10 flex items-center justify-center gap-3 text-sm tracking-wider transition-all active:scale-95">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400', { 'animate-spin text-indigo-400': isLoadingInfo }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
              <span>{{ isLoadingInfo ? 'Reading...' : 'Get device info' }}</span>
            </button>
          </div>
        </div>

        <!-- Device Details Panel -->
        <div class="glass-card p-5 flex flex-col gap-4 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between">
            <h2 class="text-xl font-bold text-slate-300">
              Device details
            </h2>
            <button v-if="deviceInfo" @click="copyAllDeviceInfo" class="text-slate-500 hover:text-indigo-400 transition-colors" title="Copy all">
              <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            </button>
          </div>
          
          <div class="space-y-1">
            <div v-for="(val, key) in deviceInfo" :key="key" class="group flex items-center justify-between text-xs border-b border-white/5 py-1.5 hover:bg-white/5 px-2 -mx-2 rounded transition-colors">
              <span class="text-slate-500">{{ formatLabel(key) }}</span>
              <div class="flex items-center gap-3">
                <span class="font-mono text-slate-300">{{ val }}</span>
                <button @click="copyToClipboard(val.toString(), formatLabel(key).toLowerCase())" class="opacity-0 group-hover:opacity-100 text-slate-600 hover:text-indigo-400 transition-all">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
            <div v-if="!deviceInfo && !isLoadingInfo" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
              Connect a device and click <br/> "Get device info"
            </div>
            <div v-if="isLoadingInfo" class="h-32 flex flex-col items-center justify-center text-purple-400 italic text-sm gap-2">
              <span class="animate-spin text-4xl">◌</span>
              Reading device descriptors...
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Premium Toast Notification -->
    <Transition name="toast">
      <div v-if="showToast" class="fixed bottom-10 left-1/2 -translate-x-1/2 z-50 glass-card px-6 py-3 border border-indigo-500/50 shadow-lg shadow-indigo-500/20 text-sm font-medium text-slate-200 flex items-center gap-3">
        <span class="w-2 h-2 rounded-full bg-indigo-500 shadow-[0_0_8px_rgba(99,102,241,0.8)]"></span>
        {{ toastMessage }}
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.custom-scrollbar::-webkit-scrollbar {
  width: 4px;
}
.custom-scrollbar::-webkit-scrollbar-track {
  background: transparent;
}
.custom-scrollbar::-webkit-scrollbar-thumb {
  background: rgba(255, 255, 255, 0.1);
  border-radius: 10px;
}

.font-mono {
  font-family: 'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace !important;
}

.toast-enter-active, .toast-leave-active {
  transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
}
.toast-enter-from, .toast-leave-to {
  opacity: 0;
  transform: translate(-50%, 20px);
}
</style>
