<script setup lang="ts">
import { ref, watch, nextTick } from 'vue';

export interface BulkState {
  bulkMode: boolean;
  bulkSelectedPorts: string[];
  isBulkFlashing: boolean;
  isBulkResetting: boolean;
}

export interface MonitorState {
  isMonitoring: boolean;
  monitorDeviceLabel: string;
  activeMonitorPort: string;
  selectedPort: string;
  serialUptimeLabel: string | null;
}

export interface IdentifyState {
  available: boolean;
  disabled: boolean;
  isIdentifying: boolean;
}

export interface BulkPortDisplayState {
  deviceInfo?: {
    ssid?: string;
  } | null;
  isFlashing?: boolean;
  isResetting?: boolean;
  flashStatus?: string;
  resetStatus?: string;
  flashProgress?: number;
  flashLogs?: string[];
}

const props = defineProps<{
  activeMode: string;
  activityFullscreen: boolean;
  activeLogs: string[];
  activityBusy: boolean;
  crashCount: number;
  hasActiveDeviceInfo: boolean;
  serialDevicesByPort: Record<string, BulkPortDisplayState>;
  bulkState: BulkState;
  monitorState: MonitorState;
  identifyState: IdentifyState;
}>();

const emit = defineEmits<{
  (e: 'copy-activity-log'): void;
  (e: 'clear-activity-log'): void;
  (e: 'trigger-identify'): void;
  (e: 'copy-active-password'): void;
  (e: 'toggle-monitor'): void;
}>();

const bulkShowLogsByPort = ref<Record<string, boolean>>({});
const bulkLogRefs = ref<Record<string, HTMLElement>>({});
const logContainer = ref<HTMLElement | null>(null);
const stickLogToBottom = ref(true);

function scrollToBottom() {
  if (logContainer.value) {
    logContainer.value.scrollTop = logContainer.value.scrollHeight;
  }
}

function scrollBulkLogToBottom(port: string) {
  nextTick(() => {
    const el = bulkLogRefs.value[port];
    if (el) el.scrollTop = el.scrollHeight;
  });
}

function handleLogScroll() {
  if (!logContainer.value) return;
  const { scrollTop, clientHeight, scrollHeight } = logContainer.value;
  stickLogToBottom.value = scrollHeight - (scrollTop + clientHeight) < 24;
}

watch(
  () => props.activeLogs,
  () => {
    if (stickLogToBottom.value) {
      nextTick(() => scrollToBottom());
    }
  },
  { deep: true }
);

defineExpose({
  scrollToBottom,
  scrollBulkLogToBottom
});
</script>

<template>
  <!-- Log Panel -->
  <div v-if="activeMode !== 'network' && activeMode !== 'monitor' && activeMode !== 'logs'" :class="['glass-card p-3 flex flex-col gap-2 text-left overflow-hidden h-full']">
    <!-- New Bulk Operations Status Grid -->
    <div v-if="activeMode === 'serial' && bulkState.bulkMode" class="flex flex-col gap-3 h-full min-h-0 overflow-hidden">
      <div class="flex items-center justify-between border-b border-slate-700/80 pb-2">
        <h2 class="text-sm font-semibold text-slate-300 flex items-center gap-2">
          <span :class="['w-2 h-2 rounded-full', bulkState.isBulkFlashing || bulkState.isBulkResetting ? 'bg-cyan-500 animate-pulse' : 'bg-slate-600']"></span>
          Bulk Operations Status
        </h2>
        <div class="text-xs text-slate-500 font-mono">
          Selected: {{ bulkState.bulkSelectedPorts.length }} ports
        </div>
      </div>
      
      <div class="flex-1 overflow-auto custom-scrollbar pr-1 space-y-3">
        <div v-for="port in bulkState.bulkSelectedPorts" :key="port" class="glass-card p-3 flex flex-col gap-2 border border-slate-800 bg-slate-900/40 hover:border-slate-700 transition-all rounded-lg text-left">
          <div class="flex items-center justify-between gap-3 text-xs">
            <span class="font-bold text-slate-200 font-mono">{{ port }}</span>
            <div class="flex items-center gap-2">
              <span v-if="serialDevicesByPort[port]?.deviceInfo?.ssid" class="font-mono text-[10px] text-slate-500">
                SSID: {{ serialDevicesByPort[port]?.deviceInfo?.ssid }}
              </span>
              <span :class="[
                'px-2 py-0.5 rounded-[4px] text-[10px] font-bold border uppercase',
                serialDevicesByPort[port]?.isFlashing ? 'bg-cyan-500/10 border-cyan-500/30 text-cyan-300' :
                serialDevicesByPort[port]?.isResetting ? 'bg-amber-500/10 border-amber-500/30 text-amber-300' :
                serialDevicesByPort[port]?.flashStatus === 'Completed' || serialDevicesByPort[port]?.resetStatus === 'Success' ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300' :
                serialDevicesByPort[port]?.flashStatus === 'Failed' || serialDevicesByPort[port]?.resetStatus === 'Failed' ? 'bg-red-500/10 border-red-500/30 text-red-300' :
                'bg-slate-800 border-slate-700 text-slate-400'
              ]">
                {{ serialDevicesByPort[port]?.isFlashing ? serialDevicesByPort[port]?.flashStatus :
                   serialDevicesByPort[port]?.isResetting ? serialDevicesByPort[port]?.resetStatus :
                   serialDevicesByPort[port]?.flashStatus !== 'Idle' ? serialDevicesByPort[port]?.flashStatus :
                   serialDevicesByPort[port]?.resetStatus !== 'Idle' ? serialDevicesByPort[port]?.resetStatus : 'Queued' }}
              </span>
            </div>
          </div>
          
          <!-- Progress bar -->
          <div v-if="serialDevicesByPort[port]?.isFlashing || serialDevicesByPort[port]?.flashStatus === 'Completed' || serialDevicesByPort[port]?.flashStatus === 'Failed'" class="w-full flex items-center gap-3 mt-1">
            <div class="flex-1 h-1.5 rounded-full bg-slate-800 overflow-hidden border border-slate-700/50">
              <div :style="{ width: `${serialDevicesByPort[port]?.flashProgress || 0}%` }" class="h-full bg-cyan-500 rounded-full transition-all duration-300"></div>
            </div>
            <span class="text-[10px] font-bold text-slate-400 font-mono">{{ serialDevicesByPort[port]?.flashProgress || 0 }}%</span>
          </div>

          <!-- Log toggle button -->
          <div class="flex justify-end mt-1">
            <button @click="bulkShowLogsByPort[port] = !bulkShowLogsByPort[port]" class="text-[10px] text-slate-500 hover:text-cyan-300 font-semibold transition-colors flex items-center gap-1 shadow-none bg-transparent hover:bg-transparent border-0 p-0 m-0">
              <svg xmlns="http://www.w3.org/2000/svg" class="w-3.5 h-3.5 transition-transform shrink-0" :class="{ 'rotate-90': bulkShowLogsByPort[port] }" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><path d="m9 18 6-6-6-6"/></svg>
              <span>{{ bulkShowLogsByPort[port] ? 'Hide console output' : 'Show console output' }}</span>
            </button>
          </div>

          <!-- Expandable console terminal drawer -->
          <div v-if="bulkShowLogsByPort[port]" :ref="(el: any) => { if (el) bulkLogRefs[port] = el as HTMLElement }" class="mt-1 bg-slate-950/60 border border-slate-800 rounded p-2 max-h-36 overflow-auto font-mono text-[9px] text-slate-400 custom-scrollbar leading-tight whitespace-pre pr-2">
            <div v-for="(log, idx) in serialDevicesByPort[port]?.flashLogs" :key="idx" class="border-l border-slate-800 pl-1.5 py-0.5">
              {{ log }}
            </div>
            <div v-if="!serialDevicesByPort[port]?.flashLogs?.length" class="text-slate-600 italic text-center">
              No console output yet.
            </div>
          </div>
        </div>

        <div v-if="bulkState.bulkSelectedPorts.length === 0" class="h-full flex items-center justify-center text-slate-600 italic text-xs">
          Select target USB devices from the control panel to view progress.
        </div>
      </div>
    </div>

    <!-- Existing Single Activity Log -->
    <div v-else class="flex flex-col gap-2 h-full min-h-0 overflow-hidden">
      <div class="flex items-center justify-between border-b border-slate-700/80 pb-2">
        <div class="flex flex-col gap-1">
          <h2 class="text-sm font-semibold text-slate-300 flex items-center gap-2">
            <span :class="['w-2 h-2 rounded-full', activityBusy ? 'bg-cyan-600 animate-pulse' : 'bg-slate-600']"></span>
            Activity log
          </h2>
          <div
            v-if="activeMode === 'serial' && monitorState.isMonitoring"
            class="flex items-center gap-1 pl-4 text-xs text-slate-500"
          >
            <span>Monitoring</span>
            <span class="font-mono text-slate-300">{{ monitorState.monitorDeviceLabel }}</span>
            <span class="text-slate-500">on</span>
            <span class="font-mono text-slate-400">{{ monitorState.activeMonitorPort || monitorState.selectedPort }}</span>
            <template v-if="monitorState.serialUptimeLabel">
              <span class="text-slate-600">·</span>
              <span>Uptime</span>
              <span class="font-mono text-slate-400">{{ monitorState.serialUptimeLabel }}</span>
            </template>
          </div>
        </div>
        <div class="flex items-center gap-2">
          <div
            v-if="crashCount > 0"
            class="flex h-7 items-center gap-2 rounded border border-amber-500/40 bg-amber-500/10 px-2 text-xs font-semibold text-amber-300"
            title="Crash signatures detected in the current log"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path>
              <line x1="12" y1="9" x2="12" y2="13"></line>
              <line x1="12" y1="17" x2="12.01" y2="17"></line>
            </svg>
            <span>{{ crashCount }}</span>
          </div>
          <button
            @click="emit('copy-activity-log')"
            :disabled="activeLogs.length === 0"
            :class="[
              'p-1 rounded border transition-all',
              activeLogs.length > 0
                ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
            ]"
            title="Copy activity log"
            aria-label="Copy activity log"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect>
              <path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path>
            </svg>
          </button>
          <button
            v-if="activeMode === 'serial' && identifyState.available"
            @click="emit('trigger-identify')"
            :disabled="identifyState.disabled"
            :class="[
              'p-1 rounded border transition-all disabled:opacity-50 disabled:cursor-not-allowed',
              identifyState.isIdentifying
                ? 'identify-led-active border-cyan-500 bg-cyan-500/20 text-cyan-300'
                : 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
            ]"
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
          <button
            v-if="activeMode === 'serial' && monitorState.isMonitoring"
            @click="emit('copy-active-password')"
            :disabled="!hasActiveDeviceInfo"
            :class="[
              'p-1 rounded border transition-all',
              hasActiveDeviceInfo
                ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
            ]"
            title="Copy active device password"
            aria-label="Copy active device password"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <circle cx="7.5" cy="15.5" r="3.5"></circle>
              <path d="m10.5 13 8-8"></path>
              <path d="m16 5 3 3"></path>
              <path d="m14 7 3 3"></path>
            </svg>
          </button>
          <button
            v-if="activeMode === 'serial'"
            @click="emit('toggle-monitor')"
            :class="[
              'rounded border transition-all',
              monitorState.isMonitoring
                ? 'p-1 border-cyan-500 bg-cyan-500/20 text-cyan-300 hover:text-cyan-100'
                : 'p-1 border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
            ]"
            :title="monitorState.isMonitoring ? 'Stop monitor' : 'Start monitor'"
            :aria-label="monitorState.isMonitoring ? 'Stop monitor' : 'Start monitor'"
          >
            <svg v-if="monitorState.isMonitoring" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <rect x="7" y="7" width="10" height="10" rx="1.5" fill="currentColor"></rect>
            </svg>
            <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M8 2h8l4 4v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a4 4 0 0 1 4-4z"></path>
              <path d="M16 2v4h4"></path>
              <path d="M8 9h5"></path>
              <path d="M8 13h4"></path>
              <path d="M8 17h3"></path>
              <path d="m14 14 4 2.5-4 2.5z"></path>
            </svg>
          </button>
          <button @click="emit('clear-activity-log')" class="text-xs text-slate-500 hover:text-slate-300">Clear</button>
        </div>
      </div>

      <div ref="logContainer" @scroll="handleLogScroll" class="flex-1 overflow-auto font-mono text-[9px] sm:text-[10px] pr-2 custom-scrollbar space-y-0.5 leading-tight tracking-tight whitespace-pre">
        <div v-for="(log, i) in activeLogs" :key="i" class="text-slate-400 border-l border-slate-700/50 pl-2 opacity-90">
          {{ log }}
        </div>
        <div v-if="activeLogs.length === 0" class="h-full flex items-center justify-center text-slate-600 italic text-xs">
          No activity logs to show
        </div>
      </div>
    </div>
  </div>
</template>
