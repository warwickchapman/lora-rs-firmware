<script setup lang="ts">
import { ref, watch, nextTick, computed, onMounted, onUnmounted } from 'vue';

export interface FleetConfig {
  region: string;
  selectedVersion: string;
}

export interface FleetGatewayStatus {
  hasGatewayDeviceWarning: boolean;
  hasUnexpectedReboot: boolean;
  rebootAlertLine: string;
  badgeClass: string;
  badgeLabel: string;
  statusLabel: string;
  summary: string;
  isIdentifyDisabled: boolean;
  isIdentifying: boolean;
  isUpgradeAvailable: boolean;
  isFlashing: boolean;
  flashDisabled: boolean;
  flashUnavailableReason: string;
  name: string;
  firmware: string;
  role: string;
  addressLine: string;
  wifiLine: string;
  wifiIp?: string;
  wifiRssi?: number;
  wifiConnected?: boolean;
  relayLabel: string;
  inputLabel: string;
  uptimeLine: string;
  uptimeClass: string;
  uptimeTitle: string;
}

export interface FleetServerStatus {
  activeReference: string | null;
  statusLine: string;
  progressLabel: string;
  versions: string[];
  localOption: string;
  isFetchingFirmware: boolean;
  networkStatusMessage: string;
}

export interface FleetUdpLogs {
  isMonitoring: boolean;
  target: string | null;
  logs: string[];
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

export interface MqttGatewayOption {
  chip_id: string;
  label: string;
}

export interface FleetTransportState {
  fleetTransport: 'serial' | 'mqtt';
}

export interface FleetInventorySummary {
  totalCount: number;
  selectedCount: number;
  hasAnyRemoteIp: boolean;
}

export interface FleetCandidateSummary {
  total: number;
  truncated: boolean;
}

export interface FleetDisplayRow {
  address: number | string;
  selected: boolean;
  deviceName: string;
  conflict_chip_id?: string;
  fw_version?: string;
  roleModeLabel: string;
  wifi_pending_offline?: boolean;
  wifi_connected_known?: boolean;
  wifi_connected?: boolean;
  pending_power_save_listen_only?: boolean;
  power_save_listen_only?: boolean;
  power_save_active?: boolean;
  identifyPending: boolean;
  ip?: string;
  relayLabel: string;
  inputLabel: string;
  tempLabel: string;
  tankLabel: string;
  tankDetailLabel: string | null;
  uptimeLabel: string;
  rowStatusLabel: string | null;
  rowStatusTitle: string;
  rowState?: string;
  wifi_rssi_dbm?: number;
  rssi?: number;
  ageSeconds: number | null;
  freshnessClass: string;
  rowClass: string;
  flashAvailable: boolean;
  flashUnavailableReason: string;
}

export interface FleetCandidateDisplayRow {
  address: number;
  chip_id?: string;
  deviceName: string;
  rssi: number;
  ageSeconds: number | null;
  reason: string;
  state: string;
  stateText: string;
  stateClass: string;
  showAdoptButton: boolean;
  adoptButtonLabel: string;
  adoptTextClass: string;
}

export interface FleetCandidateActionPayload {
  address: number;
  chip_id?: string;
}

const config = defineModel<FleetConfig>({ required: true });
const activeDropdownAddress = defineModel<number | string | null>('activeDropdownAddress', { required: true });
const networkUdpLogsExpanded = defineModel<boolean>('networkUdpLogsExpanded', { required: true });
const gatewayEventsExpanded = defineModel<boolean>('gatewayEventsExpanded', { required: true });
const gatewayEventsIncludeLogLines = defineModel<boolean>('gatewayEventsIncludeLogLines', { required: true });

const props = defineProps<{
  rows: FleetDisplayRow[];
  candidates: FleetCandidateDisplayRow[];
  gateway: FleetGatewayStatus;
  server: FleetServerStatus;
  udpLogs: FleetUdpLogs;
  gatewayEvents: GatewayEventsState;
  transportState: FleetTransportState;
  inventorySummary: FleetInventorySummary;
  candidateSummary: FleetCandidateSummary;
}>();

const emit = defineEmits<{
  (e: 'toggle-row-selection', address: number | string, selected: boolean): void;
  (e: 'udp-logging-start'): void;
  (e: 'udp-logging-stop'): void;
  (e: 'udp-logs-copy'): void;
  (e: 'gateway-events-clear'): void;
  (e: 'gateway-events-copy', includeLogLines: boolean): void;
  (e: 'lora-inventory-debug-copy'): void;
  (e: 'firmware-fetch'): void;
  (e: 'gateway-identify'): void;
  (e: 'gateway-flash'): void;
  (e: 'gateway-settings'): void;
  (e: 'gateway-reboot'): void;
  (e: 'gateway-view-logs'): void;
  (e: 'gateway-factory-reset'): void;
  (e: 'remote-flash', address: number | string): void;
  (e: 'remote-settings', address: number | string): void;
  (e: 'remote-reboot', address: number | string): void;
  (e: 'remote-identify', address: number | string): void;
  (e: 'remote-view-logs', address: number | string): void;
  (e: 'remote-forget', address: number | string): void;
  (e: 'remote-factory-reset', address: number | string): void;
  (e: 'selected-factory-reset'): void;
  (e: 'candidate-adopt', payload: FleetCandidateActionPayload): void;
}>();

const networkUdpLogContainer = ref<HTMLDivElement | null>(null);
const actionMenuTrigger = ref<HTMLElement | null>(null);
const actionMenuStyle = ref<Record<string, string>>({ visibility: 'hidden' });

function positionActionMenu() {
  const trigger = actionMenuTrigger.value;
  const menu = document.querySelector<HTMLElement>('[data-fleet-action-menu]');
  if (!trigger || !menu || activeDropdownAddress.value === null) return;

  const triggerRect = trigger.getBoundingClientRect();
  const menuRect = menu.getBoundingClientRect();
  const gap = 4;
  const viewportPadding = 8;
  const opensAbove = window.innerHeight - triggerRect.bottom - gap < menuRect.height
    && triggerRect.top - gap >= menuRect.height;
  const top = opensAbove
    ? triggerRect.top - menuRect.height - gap
    : triggerRect.bottom + gap;
  const left = Math.max(
    viewportPadding,
    Math.min(triggerRect.right - menuRect.width, window.innerWidth - menuRect.width - viewportPadding)
  );

  actionMenuStyle.value = {
    top: `${Math.max(viewportPadding, top)}px`,
    left: `${left}px`,
    visibility: 'visible',
  };
}

function toggleActionMenu(address: number | string, event: MouseEvent) {
  if (activeDropdownAddress.value === address) {
    activeDropdownAddress.value = null;
    return;
  }

  actionMenuTrigger.value = event.currentTarget as HTMLElement;
  actionMenuStyle.value = { visibility: 'hidden' };
  activeDropdownAddress.value = address;
  nextTick(() => positionActionMenu());
}

onMounted(() => {
  window.addEventListener('resize', positionActionMenu);
  window.addEventListener('scroll', positionActionMenu, true);
});

onUnmounted(() => {
  window.removeEventListener('resize', positionActionMenu);
  window.removeEventListener('scroll', positionActionMenu, true);
});

function scrollNetworkUdpToBottom() {
  if (networkUdpLogContainer.value) {
    networkUdpLogContainer.value.scrollTop = networkUdpLogContainer.value.scrollHeight;
  }
}

watch(() => props.udpLogs.logs.length, () => {
  nextTick(() => scrollNetworkUdpToBottom());
});

const computedRegion = computed({
  get: () => config.value.region,
  set: (val) => { config.value = { ...config.value, region: val }; }
});
const computedSelectedVersion = computed({
  get: () => config.value.selectedVersion,
  set: (val) => { config.value = { ...config.value, selectedVersion: val }; }
});

const visibleGatewayEvents = computed(() => {
  if (gatewayEventsIncludeLogLines.value) return props.gatewayEvents.events;
  return props.gatewayEvents.events.filter(event => event.level !== 'log_line');
});

function toggleNetworkUdpLogsExpanded() {
  networkUdpLogsExpanded.value = !networkUdpLogsExpanded.value;
  nextTick(() => scrollNetworkUdpToBottom());
}

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
</script>

<template>
  <div class="flex flex-col h-full overflow-hidden gap-3">
    <!-- Gateway Device Validation Warning Callout -->
    <div v-if="gateway.hasGatewayDeviceWarning" class="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-red-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-red-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="10"></circle>
        <line x1="12" y1="8" x2="12" y2="12"></line>
        <line x1="12" y1="16" x2="12.01" y2="16"></line>
      </svg>
      <div>
        <span class="font-bold">Gateway Device Required:</span> Fleet requires a TX/gateway USB device. The selected serial port is a remote; choose the gateway port.
      </div>
    </div>

    <div v-if="gateway.hasUnexpectedReboot" class="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-red-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-red-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M21 12a9 9 0 1 1-3-6.7"></path>
        <path d="M21 3v6h-6"></path>
        <path d="M12 7v5l3 2"></path>
      </svg>
      <div>
        <span class="font-bold">Unexpected Gateway Reboot:</span> {{ gateway.rebootAlertLine }}
      </div>
    </div>

    <div class="glass-card flex flex-col text-left shrink-0 p-3 gap-3">
      <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
        <div class="min-w-0">
          <h2 class="text-base font-bold text-cyan-300">
            Fleet
          </h2>
          <p class="mt-1 text-xs text-slate-400 max-w-3xl">
            {{ gateway.statusLabel }} · {{ server.statusLine }} · {{ server.progressLabel }}
          </p>
        </div>
        <div class="flex flex-wrap items-center justify-end gap-3">
          <button
            @click="udpLogs.isMonitoring ? emit('udp-logging-stop') : emit('udp-logging-start')"
            class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
          >
            <span>{{ udpLogs.isMonitoring ? 'Stop UDP Listener' : 'Start UDP Listener' }}</span>
          </button>
        </div>
      </div>

      <div class="grid grid-cols-1 gap-3 lg:grid-cols-2">
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
              <option v-for="v in server.versions" :key="v" :value="v">
                {{ v === server.localOption ? 'Choose a file' : v }}
              </option>
            </select>
            <button @click="emit('firmware-fetch')" :disabled="server.isFetchingFirmware" class="glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center group/btn shrink-0">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': server.isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
            </button>
          </div>
        </div>
      </div>

      <div class="flex flex-wrap items-center justify-between gap-3 text-xs text-slate-400">
        <span>{{ server.networkStatusMessage }}</span>
        <span v-if="server.activeReference" class="font-mono text-slate-500 truncate">{{ server.activeReference }}</span>
      </div>
    </div>

    <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
      <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
        <div class="min-w-0">
          <div class="flex items-center gap-2">
            <h2 class="text-lg font-bold text-slate-300">Gateway</h2>
            <span :class="['rounded border px-2 py-1 text-[10px] font-bold', gateway.badgeClass]">
              {{ gateway.badgeLabel }}
            </span>
          </div>
          <div class="mt-1 text-xs text-slate-500">{{ gateway.summary }}</div>
        </div>
        <div class="flex flex-wrap items-center gap-2">
          <button
            @click="gatewayEventsExpanded = !gatewayEventsExpanded"
            :class="['glass-input m-0 h-9 px-3 hover:bg-slate-700/70 text-xs font-bold', gatewayEventsExpanded ? 'border-cyan-500/40 text-cyan-200' : 'text-slate-300']"
          >
            {{ gatewayEventsExpanded ? 'Hide events' : 'Events' }}
          </button>
          <button
            @click="emit('gateway-identify')"
            :disabled="gateway.isIdentifyDisabled"
            :class="['glass-input m-0 h-9 w-11 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50', { 'identify-led-active': gateway.isIdentifying }]"
            :title="transportState.fleetTransport === 'mqtt' ? 'Identify selected MQTT gateway' : 'Identify selected USB gateway'"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-6 h-6 identify-led-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6"></path><path d="M10 22h4"></path><path d="M8.5 14.5a6 6 0 1 1 7 0c-.8.7-1.5 1.6-1.5 2.5h-4c0-.9-.7-1.8-1.5-2.5Z"></path><path d="M12 2v2"></path><path d="m4.9 4.9 1.4 1.4"></path><path d="M2 12h2"></path><path d="m19.1 4.9-1.4 1.4"></path><path d="M20 12h2"></path></svg>
          </button>

        </div>
      </div>
      <div class="overflow-x-auto rounded-md border border-slate-800">
        <table class="w-full min-w-[1180px] border-collapse text-xs">
          <thead class="bg-slate-950/95 text-slate-500">
            <tr class="border-b border-slate-800">
              <th class="w-10 px-2 py-1.5 text-left"></th>
              <th class="px-2 py-1.5 text-left font-semibold">Addr</th>
              <th class="px-2 py-1.5 text-left font-semibold">Device</th>
              <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
              <th class="px-2 py-1.5 text-left font-semibold">Role</th>
              <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
              <th class="px-2 py-1.5 text-left font-semibold">Power Save</th>
              <th class="px-2 py-1.5 text-left font-semibold">IP</th>
              <th class="px-2 py-1.5 text-left font-semibold">Relay</th>
              <th class="px-2 py-1.5 text-left font-semibold">Sensors</th>
              <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
              <th class="px-2 py-1.5 text-left font-semibold">LoRa RSSI</th>
              <th class="px-2 py-1.5 text-left font-semibold">Age</th>
              <th class="px-2 py-1.5 text-left font-semibold">Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr class="border-b border-slate-900/80 hover:bg-white/5 transition-colors">
              <td class="px-2 py-1.5"></td>
              <td class="px-2 py-1.5 font-mono">
                <span class="inline-flex min-w-8 items-center justify-center rounded border border-cyan-500/30 bg-cyan-500/10 px-2 py-1 text-[10px] font-bold text-cyan-200">{{ gateway.addressLine }}</span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-300">{{ gateway.name }}</td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ gateway.firmware }}</td>
              <td class="px-2 py-1.5 text-slate-300">{{ gateway.role }}</td>
              <td class="px-2 py-1.5">
                <template v-if="gateway.wifiConnected">
                  <span
                    v-if="gateway.wifiRssi !== undefined && gateway.wifiRssi !== null && gateway.wifiRssi !== 0"
                    :class="[
                      'rounded border px-2 py-1 text-[10px] font-bold font-mono inline-flex items-center gap-1.5',
                      gateway.wifiRssi >= -70 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                      gateway.wifiRssi >= -80 ? 'border-amber-500/30 bg-amber-500/10 text-amber-300' :
                      'border-rose-500/30 bg-rose-500/10 text-rose-300'
                    ]"
                    :title="`Gateway WiFi RSSI: ${gateway.wifiRssi} dBm`"
                  >
                    <span class="inline-flex items-end gap-[1px] h-3 w-3.5 mb-[1px]">
                      <span class="w-[3px] h-[4px] rounded-t-[1px] bg-current"></span>
                      <span :class="['w-[3px] rounded-t-[1px]', gateway.wifiRssi >= -80 ? 'h-[8px] bg-current' : 'h-[8px] bg-current/20']"></span>
                      <span :class="['w-[3px] rounded-t-[1px]', gateway.wifiRssi >= -60 ? 'h-[12px] bg-current' : gateway.wifiRssi >= -70 ? 'h-[10px] bg-current' : 'h-[12px] bg-current/20']"></span>
                    </span>
                    <span>{{ gateway.wifiRssi }}</span>
                  </span>
                  <span v-else class="rounded border border-emerald-500/30 bg-emerald-500/10 px-2 py-1 text-[10px] font-bold text-emerald-300">OK</span>
                </template>
                <span v-else class="rounded border border-slate-800 bg-slate-900/50 px-2 py-1 text-[10px] font-bold text-slate-500">{{ gateway.wifiLine }}</span>
              </td>
              <td class="px-2 py-1.5 text-slate-500">-</td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ gateway.wifiIp || '-' }}</td>
              <td class="px-2 py-1.5">
                <span :class="['rounded border px-2 py-1 text-[10px] font-bold', gateway.relayLabel === 'On' ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : gateway.relayLabel === 'Off' ? 'border-slate-600 bg-slate-800/50 text-slate-300' : 'border-slate-800 bg-slate-900/50 text-slate-500']">{{ gateway.relayLabel }}</span>
              </td>
              <td class="px-2 py-1.5 text-slate-300">in <span :class="gateway.inputLabel === 'Closed' ? 'text-emerald-400 font-semibold' : gateway.inputLabel === 'Open' ? 'text-orange-400 font-semibold' : 'text-slate-400'">{{ gateway.inputLabel }}</span></td>
              <td class="px-2 py-1.5 font-mono"><span :class="gateway.uptimeClass" :title="gateway.uptimeTitle || undefined">{{ gateway.uptimeLine }}</span></td>
              <td class="px-2 py-1.5 text-slate-500">-</td>
              <td class="px-2 py-1.5 text-slate-500">-</td>
              <td class="px-2 py-1.5 overflow-visible">
                <div class="relative inline-block text-left">
                  <button
                    @click.stop="toggleActionMenu('gateway', $event)"
                    class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold flex items-center gap-1 select-none"
                  >
                    Actions
                    <svg class="w-3 h-3 text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 9l-7 7-7-7" /></svg>
                  </button>
                </div>
                <Teleport to="body">
                  <div
                    v-if="activeDropdownAddress === 'gateway'"
                    data-fleet-action-menu
                    :style="actionMenuStyle"
                    @click.stop
                    class="fixed z-[100] w-40 rounded-md border border-slate-800 bg-slate-950/95 backdrop-blur-md py-1 shadow-2xl origin-top-right select-none font-medium"
                  >
                    <button @click="emit('gateway-flash'); activeDropdownAddress = null" :disabled="gateway.flashDisabled || !gateway.isUpgradeAvailable" :title="gateway.flashUnavailableReason" class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 disabled:opacity-40 transition-colors flex items-center gap-2 select-none">⚡ Upgrade</button>
                    <button @click="emit('gateway-settings'); activeDropdownAddress = null" class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none">🛠️ Commands</button>
                    <button @click="emit('gateway-reboot'); activeDropdownAddress = null" class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none">🔄 Reboot</button>
                    <button @click="emit('gateway-view-logs'); activeDropdownAddress = null" class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none">📋 View Logs</button>
                    <div class="h-[1px] bg-slate-800/80 my-1"></div>
                    <button @click="emit('gateway-factory-reset'); activeDropdownAddress = null" class="w-full text-left px-3 py-1.5 hover:bg-rose-500/20 hover:text-rose-200 text-[11px] font-bold text-rose-300/80 transition-colors flex items-center gap-2 select-none">⚠️ Factory Reset</button>
                  </div>
                </Teleport>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>

    <div class="glass-card p-3 flex flex-col gap-3 text-left flex-1 min-h-0 overflow-hidden">
      <div class="flex items-center justify-between gap-3">
        <div>
          <h2 class="text-lg font-bold text-slate-300">Remotes</h2>
          <div class="mt-1 text-xs text-slate-500">Gateway-owned peer cache; Scan asks the gateway to refresh LoRa state.</div>
        </div>
        <div class="flex items-center gap-3">
          <div class="text-xs text-slate-500">{{ inventorySummary.totalCount }} remote{{ inventorySummary.totalCount === 1 ? '' : 's' }} cached · {{ inventorySummary.selectedCount }} selected</div>
          <button
            v-if="inventorySummary.selectedCount > 0"
            @click="emit('selected-factory-reset')"
            class="glass-input m-0 h-8 px-3 text-[11px] font-bold text-rose-300 hover:bg-rose-500/15"
          >
            Factory reset selected
          </button>
        </div>
      </div>
      <div class="min-h-0 flex-1 overflow-auto custom-scrollbar rounded-md border border-slate-800">
        <table class="w-full min-w-[1180px] border-collapse text-xs">
          <thead class="sticky top-0 bg-slate-950/95 text-slate-500">
            <tr class="border-b border-slate-800">
              <th class="w-10 px-2 py-1.5 text-left"></th>
              <th class="px-2 py-1.5 text-left font-semibold">Addr</th>
              <th class="px-2 py-1.5 text-left font-semibold">Device</th>
              <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
              <th class="px-2 py-1.5 text-left font-semibold">Role</th>
              <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
              <th class="px-2 py-1.5 text-left font-semibold">Power Save</th>
              <th v-if="inventorySummary.hasAnyRemoteIp" class="px-2 py-1.5 text-left font-semibold">IP</th>
              <th class="px-2 py-1.5 text-left font-semibold">Relay</th>
              <th class="px-2 py-1.5 text-left font-semibold">Sensors</th>
              <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
              <th class="px-2 py-1.5 text-left font-semibold" title="LoRa uplink signal from remote to gateway">LoRa RSSI</th>
              <th class="px-2 py-1.5 text-left font-semibold">Age</th>
              <th class="px-2 py-1.5 text-left font-semibold">Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr v-if="rows.length === 0">
              <td :colspan="inventorySummary.hasAnyRemoteIp ? 14 : 13" class="px-3 py-8 text-center text-slate-600">
                {{ transportState.fleetTransport === 'mqtt' ? 'Select an MQTT gateway to read its peer cache, or Scan to probe remotes.' : 'Select a USB gateway to read its peer cache, or Scan to probe remotes.' }}
              </td>
            </tr>
            <tr
              v-for="device in rows"
              :key="device.address"
              :class="['border-b border-slate-900/80 hover:bg-white/5 transition-colors', device.rowClass]"
            >
              <td class="px-2 py-1.5">
                <input
                  :checked="device.selected"
                  @change="emit('toggle-row-selection', device.address, ($event.target as HTMLInputElement).checked)"
                  type="checkbox"
                />
              </td>
              <td class="px-2 py-1.5 font-mono">
                <span :class="['inline-flex min-w-8 items-center justify-center rounded border px-2 py-1 text-[10px] font-bold', device.freshnessClass]">
                  {{ device.address }}
                </span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-300">
                <div>{{ device.deviceName }}</div>
                <div
                  v-if="device.conflict_chip_id"
                  class="mt-1 text-[9px] font-bold text-rose-400 bg-rose-950/40 border border-rose-500/20 rounded px-1.5 py-0.5 inline-block select-none animate-pulse"
                  :title="`Telemetry from chip lrs-${device.conflict_chip_id} rejected due to address collision`"
                >
                  ⚠️ Address conflict
                </div>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.fw_version || '-' }}</td>
              <td class="px-2 py-1.5 text-slate-300">{{ device.roleModeLabel }}</td>
              <td class="px-2 py-1.5">
                <span 
                  v-if="device.wifi_pending_offline"
                  class="rounded border px-2 py-1 text-[10px] font-bold border-orange-500/30 bg-orange-500/10 text-orange-300 animate-pulse"
                  title="PowerSave is active. Waiting for WiFi connection to drop."
                >
                  ...
                </span>
                <template v-else-if="device.wifi_connected_known && device.wifi_connected">
                  <span
                    v-if="device.wifi_rssi_dbm !== undefined && device.wifi_rssi_dbm !== null && device.wifi_rssi_dbm !== 0"
                    :class="[
                      'rounded border px-2 py-1 text-[10px] font-bold font-mono inline-flex items-center gap-1.5',
                      device.wifi_rssi_dbm >= -60 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                      device.wifi_rssi_dbm >= -70 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                      device.wifi_rssi_dbm >= -80 ? 'border-amber-500/30 bg-amber-500/10 text-amber-300' :
                      'border-rose-500/30 bg-rose-500/10 text-rose-300'
                    ]"
                    :title="`WiFi RSSI: ${device.wifi_rssi_dbm} dBm`"
                  >
                    <span class="inline-flex items-end gap-[1px] h-3 w-3.5 mb-[1px]">
                      <span class="w-[3px] h-[4px] rounded-t-[1px] bg-current"></span>
                      <span :class="['w-[3px] rounded-t-[1px]', device.wifi_rssi_dbm >= -80 ? 'h-[8px] bg-current' : 'h-[8px] bg-current/20']"></span>
                      <span :class="['w-[3px] rounded-t-[1px]', device.wifi_rssi_dbm >= -60 ? 'h-[12px] bg-current' : device.wifi_rssi_dbm >= -70 ? 'h-[10px] bg-current' : 'h-[12px] bg-current/20']"></span>
                    </span>
                    <span>{{ device.wifi_rssi_dbm }}</span>
                  </span>
                  <span v-else class="rounded border px-2 py-1 text-[10px] font-bold border-emerald-500/30 bg-emerald-500/10 text-emerald-300">
                    OK
                  </span>
                </template>
                <span v-else-if="device.wifi_connected_known && !device.wifi_connected" class="rounded border px-2 py-1 text-[10px] font-bold border-slate-600 bg-slate-800/50 text-slate-400">
                  Offline
                </span>
                <span v-else class="rounded border px-2 py-1 text-[10px] font-bold border-slate-800 bg-slate-900/50 text-slate-500">
                  -
                </span>
              </td>
              <td class="px-2 py-1.5">
                <span 
                  v-if="device.pending_power_save_listen_only !== undefined"
                  class="rounded border px-2 py-1 text-[10px] font-bold border-orange-500/30 bg-orange-500/10 text-orange-300 animate-pulse"
                  title="Command transmitted. Waiting for remote device to check in over LoRa to confirm."
                >
                  Pending...
                </span>
                <template v-else-if="device.power_save_listen_only">
                  <span 
                    class="rounded border px-2 py-1 text-[10px] font-bold border-cyan-500/30 bg-cyan-500/10 text-cyan-300"
                    title="Power save active: device is running in LoRa-only low-power mode"
                  >
                    PowerSave
                  </span>
                </template>
                <span v-else class="text-slate-500">-</span>
              </td>
              <td v-if="inventorySummary.hasAnyRemoteIp" class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip || '-' }}</td>
              <td class="px-2 py-1.5">
                <template v-if="device.ageSeconds !== null">
                  <span :class="['rounded border px-2 py-1 text-[10px] font-bold', device.relayLabel === 'On' ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : device.relayLabel === 'Off' ? 'border-slate-600 bg-slate-800/50 text-slate-300' : 'border-slate-800 bg-slate-900/50 text-slate-500']">
                    {{ device.relayLabel }}
                  </span>
                </template>
                <span v-else class="text-slate-500">-</span>
              </td>
              <td class="px-2 py-1.5">
                <div class="text-slate-300 flex items-center gap-1 flex-wrap">
                  <template v-if="device.ageSeconds !== null">
                    <span>in <span :class="device.inputLabel === 'Closed' ? 'text-emerald-400 font-semibold' : device.inputLabel === 'Open' ? 'text-orange-400 font-semibold' : 'text-slate-400'">{{ device.inputLabel }}</span></span>
                    <span v-if="device.tempLabel !== '-'" class="text-slate-500">·</span>
                    <span v-if="device.tempLabel !== '-'">{{ device.tempLabel }}</span>
                    <span v-if="device.tankLabel !== '-'" class="text-slate-500">·</span>
                    <span v-if="device.tankLabel !== '-'">tank {{ device.tankLabel }}</span>
                  </template>
                  <template v-else>-</template>
                </div>
                <div v-if="device.tankDetailLabel" class="mt-1 font-mono text-[10px] text-slate-500">{{ device.tankDetailLabel }}</div>
              </td>
              <td class="px-2 py-1.5 font-mono">
                <div class="text-slate-300">{{ device.uptimeLabel }}</div>
                <div
                  v-if="device.rowStatusLabel"
                  :class="[
                    'mt-1 inline-flex h-5 w-[96px] items-center justify-center whitespace-nowrap rounded border px-2 text-[10px] font-bold select-none',
                    (device.rowState === 'unexpected_reboot' || device.rowState === 'ota_failed' || device.rowState === 'ota_unconfirmed' || device.rowState === 'ota_no_reboot' || device.rowState === 'reset_unconfirmed' || device.rowState === 'reset_failed')
                      ? 'border-rose-500/30 bg-rose-500/10 text-rose-300'
                      : (device.rowState === 'ota_updated' || device.rowState === 'reset_confirmed')
                        ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300'
                        : device.rowState === 'ota_queued'
                          ? 'border-fuchsia-500/30 bg-fuchsia-500/10 text-fuchsia-300'
                          : 'border-sky-500/30 bg-sky-500/10 text-sky-300'
                  ]"
                  :title="device.rowStatusTitle"
                >
                  {{ device.rowStatusLabel }}
                </div>
              </td>
              <td class="px-2 py-1.5 font-mono">
                <span
                  v-if="device.rssi !== undefined && device.rssi !== null && device.rssi !== 0 && device.rssi !== -127"
                  :class="[
                    'rounded border px-2 py-1 text-[10px] font-bold inline-flex items-center gap-1.5',
                    device.rssi >= -90 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                    device.rssi >= -100 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                    device.rssi >= -110 ? 'border-amber-500/30 bg-amber-500/10 text-amber-300' :
                    'border-rose-500/30 bg-rose-500/10 text-rose-300'
                  ]"
                  :title="`LoRa RSSI: ${device.rssi} dBm`"
                >
                  <span class="inline-flex items-end gap-[1px] h-3 w-3.5 mb-[1px]">
                    <span class="w-[3px] h-[4px] rounded-t-[1px] bg-current"></span>
                    <span :class="['w-[3px] rounded-t-[1px]', device.rssi >= -110 ? 'h-[8px] bg-current' : 'h-[8px] bg-current/20']"></span>
                    <span :class="['w-[3px] rounded-t-[1px]', device.rssi >= -90 ? 'h-[12px] bg-current' : device.rssi >= -100 ? 'h-[10px] bg-current' : 'h-[12px] bg-current/20']"></span>
                  </span>
                  <span>{{ device.rssi }}</span>
                </span>
                <span v-else class="text-slate-500">-</span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ageSeconds != null ? `${device.ageSeconds}s` : '-' }}</td>
              <td class="px-2 py-1.5 overflow-visible">
                <div class="relative inline-block text-left">
                  <button
                    @click.stop="toggleActionMenu(device.address, $event)"
                    class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold flex items-center gap-1 select-none"
                  >
                    Actions
                    <svg class="w-3 h-3 text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 9l-7 7-7-7" />
                    </svg>
                  </button>
                </div>
                <Teleport to="body">
                  <div
                    v-if="activeDropdownAddress === device.address"
                    data-fleet-action-menu
                    :style="actionMenuStyle"
                    @click.stop
                    class="fixed z-[100] w-40 rounded-md border border-slate-800 bg-slate-950/95 backdrop-blur-md py-1 shadow-2xl origin-top-right select-none font-medium"
                  >
                    <button
                      @click="emit('remote-flash', device.address); activeDropdownAddress = null"
                      :disabled="['ota_queued', 'ota_downloading', 'ota_apply_wait', 'ota_retrying'].includes(device.rowState || '') || !device.flashAvailable"
                      class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 disabled:opacity-40 transition-colors flex items-center gap-2 select-none"
                      :title="device.flashUnavailableReason"
                    >
                      ⚡ Flash
                    </button>

                    <button
                      @click="emit('remote-settings', device.address)"
                      class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none"
                    >
                      🛠️ Commands
                    </button>
                    <button
                      @click="emit('remote-reboot', device.address)"
                      class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none"
                    >
                      🔄 Reboot
                    </button>
                    <button
                      @click="emit('remote-identify', device.address); activeDropdownAddress = null"
                      :disabled="device.power_save_active || device.identifyPending"
                      class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none disabled:opacity-40"
                      :title="device.power_save_active ? 'Unavailable while Power Save is active' : (device.identifyPending ? 'Waiting for remote acknowledgement' : 'Flash the remote status LED')"
                    >
                      💡 Flash LED
                    </button>
                    <button
                      @click="emit('remote-view-logs', device.address); activeDropdownAddress = null"
                      :disabled="!device.wifi_connected || !device.ip"
                      class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none disabled:opacity-40"
                      :title="(!device.wifi_connected || !device.ip) ? 'Remote device has no active WiFi or IP' : 'Trigger remote UDP logging'"
                    >
                      📋 View Logs
                    </button>
                    <button
                      @click="emit('remote-forget', device.address); activeDropdownAddress = null"
                      class="w-full text-left px-3 py-1.5 hover:bg-rose-500/20 hover:text-rose-200 text-[11px] font-bold text-rose-300/80 transition-colors flex items-center gap-2 select-none"
                    >
                      🗑️ Forget device
                    </button>
                    <div class="h-[1px] bg-slate-800/80 my-1"></div>
                    <button
                      @click="emit('remote-factory-reset', device.address)"
                      class="w-full text-left px-3 py-1.5 hover:bg-rose-500/20 hover:text-rose-200 text-[11px] font-bold text-rose-300/80 transition-colors flex items-center gap-2 select-none"
                    >
                      ⚠️ Factory Reset
                    </button>
                  </div>
                </Teleport>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div v-if="gatewayEventsExpanded" class="shrink-0 rounded-md border border-slate-800 bg-slate-950/40 p-3">
        <div class="mb-2 flex items-center justify-between gap-3">
          <div class="min-w-0">
            <div class="truncate text-xs font-bold text-slate-300">Gateway events</div>
            <div class="mt-0.5 text-[10px] text-slate-600">{{ gatewayEvents.status }}</div>
          </div>
          <div class="flex items-center gap-2">
            <label class="flex h-8 items-center gap-1.5 rounded border border-slate-700/70 px-2 text-[10px] font-bold text-slate-400">
              <input v-model="gatewayEventsIncludeLogLines" type="checkbox" class="accent-cyan-500">
              Log lines
            </label>
            <button
              @click="emit('lora-inventory-debug-copy')"
              class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold"
            >
              Copy inventory
            </button>
            <button
              @click="emit('gateway-events-copy', gatewayEventsIncludeLogLines)"
              :disabled="visibleGatewayEvents.length === 0"
              class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-50"
            >
              Copy
            </button>
            <button
              @click="emit('gateway-events-clear')"
              :disabled="gatewayEvents.events.length === 0"
              class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-50"
            >
              Clear
            </button>
          </div>
        </div>
        <div class="max-h-36 overflow-auto custom-scrollbar rounded border border-slate-800 bg-slate-950/60 p-2 font-mono text-[10px] leading-tight">
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
      <div
        v-if="udpLogs.isMonitoring"
        :class="networkUdpLogsExpanded ? 'fixed inset-4 z-40 flex flex-col rounded-md border border-slate-700 bg-slate-950 p-4 shadow-2xl' : 'shrink-0 rounded-md border border-slate-800 bg-slate-950/40 p-3'"
      >
        <div class="mb-2 flex items-center justify-between gap-3">
          <div class="min-w-0">
            <div class="truncate text-xs font-bold text-slate-300">UDP logs · {{ udpLogs.target || 'Fleet' }}</div>
            <div class="mt-0.5 text-[10px] text-slate-600">{{ udpLogs.logs.length }} lines · following latest</div>
          </div>
          <div class="flex shrink-0 items-center gap-2">
            <button @click="emit('udp-logs-copy')" :disabled="udpLogs.logs.length === 0" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-50">Copy</button>
            <button @click="toggleNetworkUdpLogsExpanded" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">
              {{ networkUdpLogsExpanded ? 'Collapse' : 'Full screen' }}
            </button>
            <button @click="emit('udp-logging-stop')" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">Stop logs</button>
          </div>
        </div>
        <div ref="networkUdpLogContainer" :class="['overflow-auto custom-scrollbar font-mono text-[10px] leading-tight text-slate-400', networkUdpLogsExpanded ? 'min-h-0 flex-1 rounded border border-slate-800 bg-slate-950/60 p-2' : 'max-h-44']">
          <div v-for="(log, i) in udpLogs.logs.slice(-200)" :key="i">{{ log }}</div>
          <div v-if="udpLogs.logs.length === 0" class="text-slate-600">Waiting for UDP log lines...</div>
        </div>
      </div>
    </div>

    <!-- Discovered Candidates -->
    <div v-if="candidates.length > 0" class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
      <div class="flex items-center justify-between gap-3">
        <div>
          <h2 class="text-lg font-bold text-slate-300">Same-Key Adoption Candidates</h2>
          <div class="mt-1 text-xs text-slate-500">Unconfigured same-key remotes heard by the gateway.</div>
        </div>
        <div class="text-xs text-slate-500">
          <span v-if="candidateSummary.truncated">Showing {{ candidates.length }} of {{ candidateSummary.total }} candidates</span>
          <span v-else>{{ candidates.length }} candidate{{ candidates.length === 1 ? '' : 's' }} discovered</span>
        </div>
      </div>
      
      <div class="overflow-auto custom-scrollbar rounded-md border border-slate-800 max-h-64">
        <table class="w-full border-collapse text-xs">
          <thead class="bg-slate-950/95 text-slate-500 sticky top-0">
            <tr class="border-b border-slate-800">
              <th class="px-2 py-1.5 text-left font-semibold">Address</th>
              <th class="px-2 py-1.5 text-left font-semibold">Device</th>
              <th class="px-2 py-1.5 text-left font-semibold">RSSI</th>
              <th class="px-2 py-1.5 text-left font-semibold">Age</th>
              <th class="px-2 py-1.5 text-left font-semibold">Reason</th>
              <th class="px-2 py-1.5 text-left font-semibold">State</th>
              <th class="px-2 py-1.5 text-left font-semibold">Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="c in candidates"
              :key="c.chip_id || c.address"
              class="border-b border-slate-900/80 hover:bg-white/5 transition-colors"
            >
              <td class="px-2 py-1.5 font-mono text-slate-300">
                <span class="inline-flex min-w-8 items-center justify-center rounded border px-2 py-1 text-[10px] font-bold border-slate-700 bg-slate-800/20 text-slate-400">
                  {{ c.address }}
                </span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-300">
                {{ c.deviceName }}
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-300">{{ c.rssi }} dBm</td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ c.ageSeconds != null ? `${c.ageSeconds}s` : '-' }}</td>
              <td class="px-2 py-1.5">
                <span :class="['rounded border px-2 py-0.5 text-[10px] font-semibold',
                  c.reason === 'ok' ? 'border-emerald-500/20 bg-emerald-500/5 text-emerald-400' :
                  c.reason === 'conflict' ? 'border-rose-500/20 bg-rose-500/5 text-rose-400' :
                  c.reason === 'out_of_range' ? 'border-amber-500/20 bg-amber-500/5 text-amber-400' :
                  c.reason === 'known_chip_moved' ? 'border-sky-500/20 bg-sky-500/5 text-sky-400' :
                  c.reason === 'full' ? 'border-rose-500/20 bg-rose-500/5 text-rose-400' :
                  'border-slate-800 bg-slate-900/50 text-slate-500'
                ]">
                  {{ c.reason }}
                </span>
              </td>
              <td class="px-2 py-1.5 font-mono">
                <span :class="['text-[10px] font-bold', c.stateClass]">
                  {{ c.stateText }}
                </span>
              </td>
              <td class="px-2 py-1.5 flex items-center gap-2">
                <button
                  v-if="c.showAdoptButton"
                  @click="emit('candidate-adopt', { address: c.address, chip_id: c.chip_id })"
                  class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold flex items-center justify-center select-none"
                >
                  {{ c.adoptButtonLabel }}
                </button>
                <span
                  v-else
                  :class="['text-[10px]', c.adoptTextClass]"
                >
                  {{ c.stateText }}
                </span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </div>
  </div>
</template>
