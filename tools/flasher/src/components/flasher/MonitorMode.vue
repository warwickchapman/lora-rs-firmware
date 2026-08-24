<script setup lang="ts">
import { computed } from 'vue';

export interface MonitorForm {
  selectedMonitorDeviceAddress: number | null;
}

export interface MonitorHeaderState {
  monitorHealthSummary: string;
  monitorStatusMessage: string;
  identifyAvailable: boolean;
  identifyDisabled: boolean;
  isIdentifying: boolean;
}

export interface MonitorTransportState {
  transport: 'serial' | 'mqtt';
}

export interface MonitorGatewayWarningState {
  showWarning: boolean;
}

export interface MonitorGatewaySensor {
  kind: string;
  instance: number;
  displayValue: string;
  displayKind: string;
}

export interface MonitorGatewaySummaryState {
  gatewayName: string;
  role: string;
  localAddress: string | number;
  firmwareVersion: string;
  uptime: string;
  memory: string;
  memoryMax: string;
  memoryFrag: string;
  wifiStatus: string;
  wifiIp: string;
  wifiRssi: string | number;
  mqttStatus: string;
  mqttHost: string;
  linkState: string;
  linkPeerCount: string | number;
  sensors: MonitorGatewaySensor[];
  relayBadgeClass: string;
  relayBadgeLabel: string;
  relayStateLabel: string;
  inputStateLabel: string;
}

export interface MonitorFleetSummaryState {
  liveCount: number;
  staleCount: number;
  offlineCount: number;
  totalCount: number;
  hasDiagnosticsData: boolean;
}

export interface MonitorDisplayRow {
  address: number;
  chip_id: string;
  deviceName: string;
  freshnessClass: string;
  freshnessLabel: string;
  firmwareVersion: string;
  ip: string;
  relayLabel: string;
  inputLabel: string;
  tempLabel: string;
  tankLabel: string;
  tankDetailLabel: string;
  wifiConnectedLabel: string;
  rssiLabel: string;
  heapLabel?: string;
  fragLabel?: string;
  uptimeLabel: string;
  poll_pending: boolean;
}

export interface GatewayEventDisplayRecord {
  ms: number;
  event: string;
  rssi: number;
  counter: number;
  state: number;
  raw: string;
  level: 'info' | 'warn' | 'error' | 'crash' | 'reset' | 'log_line' | 'raw';
}

export interface GatewayEventsState {
  events: GatewayEventDisplayRecord[];
  status: string;
  isLoading: boolean;
}

const form = defineModel<MonitorForm>('form', { required: true });
const gatewayEventsExpanded = defineModel<boolean>('gatewayEventsExpanded', { required: true });
const gatewayEventsIncludeLogLines = defineModel<boolean>('gatewayEventsIncludeLogLines', { required: true });

const props = defineProps<{
  headerState: MonitorHeaderState;
  transportState: MonitorTransportState;
  gatewayWarningState: MonitorGatewayWarningState;
  gatewaySummaryState: MonitorGatewaySummaryState;
  fleetSummaryState: MonitorFleetSummaryState;
  rows: MonitorDisplayRow[];
  gatewayEvents: GatewayEventsState;
}>();

const emit = defineEmits<{
  (e: 'trigger-identify'): void;
  (e: 'poll-selected-diagnostics'): void;
  (e: 'poll-device-diagnostics', address: number): void;
  (e: 'gateway-events-clear'): void;
  (e: 'gateway-events-copy', includeLogLines?: boolean): void;
}>();

// Computed bridges to avoid direct mutations in the child
const computedSelectedMonitorDeviceAddress = computed({
  get: () => form.value.selectedMonitorDeviceAddress,
  set: (val) => { form.value = { ...form.value, selectedMonitorDeviceAddress: val }; }
});

function eventLevelClass(event: GatewayEventDisplayRecord): string {
  if (event.level === 'crash' || event.level === 'error') return 'text-rose-300';
  if (event.level === 'reset') return 'text-orange-300';
  if (event.level === 'warn') return 'text-amber-300';
  if (event.level === 'log_line') return 'text-slate-300';
  if (event.level === 'raw') return 'text-slate-500';
  const name = event.event || '';
  if (name.includes('timeout') || name.includes('_fail') || name.includes('failed') || name.includes('_bad')) {
    return 'text-amber-300';
  }
  if (name.includes('maint_') || name.includes('fleet_scan') || name.includes('ota_')) {
    return 'text-cyan-300';
  }
  return 'text-slate-300';
}

const visibleGatewayEvents = computed(() => {
  if (gatewayEventsIncludeLogLines.value) return props.gatewayEvents.events;
  return props.gatewayEvents.events.filter(event => event.level !== 'log_line');
});
</script>

<template>
  <div class="flex flex-col h-full overflow-hidden gap-3">
    <!-- Gateway Device Validation Warning Callout -->
    <div v-if="gatewayWarningState.showWarning" class="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-red-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-red-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="10"></circle>
        <line x1="12" y1="8" x2="12" y2="12"></line>
        <line x1="12" y1="16" x2="12.01" y2="16"></line>
      </svg>
      <div>
        <span class="font-bold">Gateway Device Required:</span> Monitor requires a TX/gateway USB device. The selected serial port is a remote; choose the gateway port.
      </div>
    </div>

    <div class="glass-card flex flex-col text-left shrink-0 p-3 gap-3">
      <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
        <div class="min-w-0">
          <h2 class="text-base font-bold text-cyan-300 flex items-center gap-2">
            <span>Monitor</span>
            <span class="px-1.5 py-0.5 text-[9px] uppercase tracking-wider font-extrabold bg-cyan-500/10 text-cyan-300 border border-cyan-500/30 rounded">Beta</span>
          </h2>
          <p class="mt-1 text-xs text-slate-400 max-w-3xl">
            {{ headerState.monitorHealthSummary }} · {{ headerState.monitorStatusMessage }}
          </p>
        </div>
        <div class="flex flex-wrap items-center justify-end gap-2">
          <button
            v-if="headerState.identifyAvailable"
            @click="emit('trigger-identify')"
            :disabled="headerState.identifyDisabled"
            :class="[
              'glass-input m-0 h-8 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed',
              { 'identify-led-active text-cyan-300': headerState.isIdentifying }
            ]"
            title="Identify selected USB device"
            aria-label="Identify selected USB device"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 identify-led-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <path d="M9 18h6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M10 22h4" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M8 14a6 6 0 1 1 8 0c-.8.65-1.15 1.25-1.28 2H9.28C9.15 15.25 8.8 14.65 8 14Z" stroke="currentColor" stroke-width="2.2" stroke-linejoin="round"></path>
              <circle cx="12" cy="8" r="2.1" fill="currentColor"></circle>
            </svg>
          </button>
        </div>
      </div>

    </div>

    <div class="grid grid-cols-1 gap-3 xl:grid-cols-[minmax(0,1.35fr)_22rem] shrink-0">
      <div class="glass-card p-3 text-left">
        <div class="grid grid-cols-1 gap-3 text-xs md:grid-cols-2 xl:grid-cols-4">
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Gateway</div>
            <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ gatewaySummaryState.gatewayName }}</div>
            <div class="mt-1 text-slate-400">{{ gatewaySummaryState.role }} · addr {{ gatewaySummaryState.localAddress }}</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Firmware</div>
            <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ gatewaySummaryState.firmwareVersion }}</div>
            <div class="mt-1 text-slate-400">uptime {{ gatewaySummaryState.uptime }}</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Memory</div>
            <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ gatewaySummaryState.memory }}</div>
            <div class="mt-1 text-slate-400">max {{ gatewaySummaryState.memoryMax }} · frag {{ gatewaySummaryState.memoryFrag }}%</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Fleet</div>
            <div class="mt-2 text-lg font-bold text-slate-100">{{ fleetSummaryState.liveCount }} live · {{ fleetSummaryState.staleCount }} stale</div>
            <div class="mt-1 text-slate-400">{{ fleetSummaryState.offlineCount }} offline · {{ fleetSummaryState.totalCount }} total</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">WiFi</div>
            <div class="mt-2 text-lg font-bold text-slate-100">{{ gatewaySummaryState.wifiStatus }}</div>
            <div class="mt-1 font-mono text-slate-400">{{ gatewaySummaryState.wifiIp }} · {{ gatewaySummaryState.wifiRssi }} dBm</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">MQTT</div>
            <div class="mt-2 text-lg font-bold text-slate-100">{{ gatewaySummaryState.mqttStatus }}</div>
            <div class="mt-1 text-slate-400">{{ gatewaySummaryState.mqttHost }}</div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Link</div>
            <div class="mt-2 text-lg font-bold text-slate-100">{{ gatewaySummaryState.linkState }}</div>
            <div class="mt-1 text-slate-400">peer {{ gatewaySummaryState.linkPeerCount }}</div>
          </div>
          <template v-if="gatewaySummaryState.sensors && gatewaySummaryState.sensors.length > 0">
            <div v-for="s in gatewaySummaryState.sensors" :key="`${s.kind}-${s.instance}`" class="rounded border border-slate-800 bg-slate-950/25 p-3">
              <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">
                {{ s.displayKind }} [{{ s.instance }}]
              </div>
              <div class="mt-2 text-lg font-bold text-slate-100">
                {{ s.displayValue }}
              </div>
              <div class="mt-1 text-slate-400">local sensor</div>
            </div>
          </template>
          <div v-else class="rounded border border-slate-800 bg-slate-950/25 p-3">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Sensors</div>
            <div class="mt-2 text-lg font-bold text-slate-100">-</div>
            <div class="mt-1 text-slate-400">no active sensors</div>
          </div>
        </div>
      </div>

      <div class="glass-card flex flex-col items-center justify-center gap-3 p-5 text-center">
        <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Gateway Relay</div>
        <div :class="['flex h-36 w-36 items-center justify-center rounded-full border text-lg font-black tracking-widest transition-all', gatewaySummaryState.relayBadgeClass]">
          {{ gatewaySummaryState.relayBadgeLabel }}
        </div>
        <div class="grid w-full grid-cols-2 gap-2 text-xs">
          <div class="rounded border border-slate-800 bg-slate-950/25 p-2">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Relay</div>
            <div class="mt-1 font-mono text-slate-200">
              {{ gatewaySummaryState.relayStateLabel }}
            </div>
          </div>
          <div class="rounded border border-slate-800 bg-slate-950/25 p-2">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Input</div>
            <div class="mt-1 font-mono text-slate-200">
              {{ gatewaySummaryState.inputStateLabel }}
            </div>
          </div>
        </div>
      </div>
    </div>

    <div class="glass-card p-3 flex flex-col gap-2 text-left flex-1 min-h-0 overflow-hidden">
      <div class="flex items-center justify-between gap-3">
        <div>
          <h2 class="text-sm font-bold text-slate-300">Gateway Peer Cache</h2>
          <div class="mt-1 text-xs text-slate-500">Read-only {{ transportState.transport === 'mqtt' ? 'MQTT' : 'serial' }} view of the selected gateway's runtime state. Click a row to select it.</div>
        </div>
        <div class="flex items-center gap-2">
          <button
            @click="gatewayEventsExpanded = !gatewayEventsExpanded"
            :class="['glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold flex items-center gap-1 select-none', gatewayEventsExpanded ? 'border-cyan-500/40 text-cyan-200' : 'text-slate-300']"
          >
            {{ gatewayEventsExpanded ? 'Hide events' : 'Events' }}
          </button>
          <button
            v-if="gatewayEventsExpanded"
            @click="emit('gateway-events-copy', gatewayEventsIncludeLogLines)"
            :disabled="gatewayEvents.events.length === 0"
            class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold flex items-center gap-1 select-none disabled:opacity-40"
          >
            Copy Events
          </button>
          <button
            v-if="gatewayEventsExpanded"
            @click="emit('gateway-events-clear')"
            :disabled="gatewayEvents.events.length === 0"
            class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold flex items-center gap-1 select-none disabled:opacity-40"
          >
            Clear Events
          </button>
          <button
            @click="emit('poll-selected-diagnostics')"
            :disabled="!computedSelectedMonitorDeviceAddress"
            class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold flex items-center gap-1 select-none disabled:opacity-40"
          >
            📊 Poll Diagnostics
          </button>
        </div>
      </div>
      <div v-if="gatewayEventsExpanded" class="shrink-0 rounded-md border border-slate-800 bg-slate-950/40 p-2">
        <div class="mb-1.5 flex items-center justify-between gap-3">
          <div class="min-w-0 truncate text-[10px] text-slate-600">{{ gatewayEvents.status }}</div>
          <label class="flex shrink-0 items-center gap-1.5 text-[10px] text-slate-500">
            <input v-model="gatewayEventsIncludeLogLines" type="checkbox" class="accent-cyan-500">
            Log lines
          </label>
        </div>
        <div class="max-h-32 overflow-auto custom-scrollbar rounded border border-slate-800 bg-slate-950/60 p-2 font-mono text-[10px] leading-tight">
          <div v-for="(event, i) in visibleGatewayEvents.slice(-64)" :key="`${event.ms}-${i}`" class="grid grid-cols-[64px_minmax(0,1fr)_64px_84px_52px] gap-2 border-b border-slate-900/70 py-1 last:border-b-0">
            <span class="text-slate-500">{{ event.ms }}ms</span>
            <span :class="['truncate', eventLevelClass(event)]" :title="event.raw">{{ event.event || '-' }}</span>
            <span class="text-slate-500">rssi {{ event.rssi }}</span>
            <span class="text-slate-500">ctr {{ event.counter }}</span>
            <span class="text-slate-500">st {{ event.state }}</span>
          </div>
          <div v-if="visibleGatewayEvents.length === 0" class="text-slate-600">Firmware event logs captured during Fleet/Monitor serial activity will appear here.</div>
        </div>
      </div>
      <div class="min-h-0 flex-1 overflow-auto custom-scrollbar rounded border border-slate-800">
        <table class="w-full min-w-[1480px] border-collapse text-xs">
          <thead class="sticky top-0 bg-slate-950/95 text-slate-500">
            <tr class="border-b border-slate-800">
              <th class="px-2 py-1.5 text-left font-semibold">Addr</th>
              <th class="px-2 py-1.5 text-left font-semibold">Device</th>
              <th class="px-2 py-1.5 text-left font-semibold">Freshness</th>
              <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
              <th class="px-2 py-1.5 text-left font-semibold">IP</th>
              <th class="px-2 py-1.5 text-left font-semibold">Relay</th>
              <th class="px-2 py-1.5 text-left font-semibold">Input</th>
              <th class="px-2 py-1.5 text-left font-semibold">Temp</th>
              <th class="px-2 py-1.5 text-left font-semibold">Tank</th>
              <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
              <th class="px-2 py-1.5 text-left font-semibold">RSSI</th>
              <th v-if="fleetSummaryState.hasDiagnosticsData" class="px-2 py-1.5 text-left font-semibold">Heap</th>
              <th v-if="fleetSummaryState.hasDiagnosticsData" class="px-2 py-1.5 text-left font-semibold">Frag</th>
              <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
              <th class="px-2 py-1.5 text-left font-semibold">Poll</th>
            </tr>
          </thead>
          <tbody>
            <tr v-if="rows.length === 0">
              <td :colspan="fleetSummaryState.hasDiagnosticsData ? 15 : 13" class="px-3 py-8 text-center text-slate-600">Waiting for gateway peer data.</td>
            </tr>
            <tr v-for="device in rows" :key="device.address"
                @click="computedSelectedMonitorDeviceAddress = device.address"
                :class="['border-b border-slate-900/80 hover:bg-white/5 transition-colors cursor-pointer', computedSelectedMonitorDeviceAddress === device.address ? 'bg-cyan-500/10 border-cyan-500/30' : '']">
              <td class="px-2 py-1.5 font-mono text-slate-200">{{ device.address }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.deviceName }}</td>
              <td class="px-2 py-1.5">
                <span :class="['rounded border px-2 py-1 text-[10px] font-bold', device.freshnessClass]">{{ device.freshnessLabel }}</span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.firmwareVersion }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip }}</td>
              <td class="px-2 py-1.5 text-slate-300">{{ device.relayLabel }}</td>
              <td class="px-2 py-1.5 text-slate-300">{{ device.inputLabel }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.tempLabel }}</td>
              <td class="px-2 py-1.5">
                <div class="font-mono text-slate-300">{{ device.tankLabel }}</div>
                <div v-if="device.tankDetailLabel" class="mt-0.5 font-mono text-[10px] text-slate-500">{{ device.tankDetailLabel }}</div>
              </td>
              <td class="px-2 py-1.5 text-slate-400">{{ device.wifiConnectedLabel }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.rssiLabel }}</td>
              <td v-if="fleetSummaryState.hasDiagnosticsData" class="px-2 py-1.5 font-mono text-slate-300">{{ device.heapLabel }}</td>
              <td v-if="fleetSummaryState.hasDiagnosticsData" class="px-2 py-1.5 font-mono text-slate-300">{{ device.fragLabel }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.uptimeLabel }}</td>
              <td class="px-2 py-1.5 text-slate-400">
                <div class="flex items-center gap-2">
                  <span>{{ device.poll_pending ? 'Pending' : 'Idle' }}</span>
                  <button
                    @click.stop="emit('poll-device-diagnostics', device.address)"
                    class="px-1.5 py-0.5 rounded bg-slate-900 hover:bg-slate-800 border border-slate-700 text-slate-300 text-[10px] font-semibold transition-colors whitespace-nowrap"
                    title="Request one-shot diagnostics (Heap/Frag)"
                  >
                    Poll Diags
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>
