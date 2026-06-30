<script setup lang="ts">
import { ref, watch, nextTick, computed } from 'vue';

export interface FleetConfig {
  sessionConnectionType: 'serial' | 'mqtt' | 'local_broker';
  selectedPort: string;
  selectedMqttManualChipId: string;
  selectedMqttGatewayChipId: string;
  pairAdminPassword: string;
  region: string;
  selectedVersion: string;
}

export interface FleetGatewayStatus {
  hasGatewayDeviceWarning: boolean;
  badgeClass: string;
  badgeLabel: string;
  statusLabel: string;
  summary: string;
  isLoading: boolean;
  isIdentifyDisabled: boolean;
  isIdentifying: boolean;
  isUpgradeAvailable: boolean;
  isFlashing: boolean;
  flashDisabled: boolean;
  flashUnavailableReason: string;
  port: string;
  name: string;
  firmware: string;
  role: string;
  addressLine: string;
  wifiLine: string;
  uptimeLine: string;
}

export interface FleetServerStatus {
  isServerOn: boolean;
  serverFilename: string | null;
  serverUrl: string | null;
  isLoraInventoryScanning: boolean;
  scanDisabled: boolean;
  scanLabel: string;
  statusLine: string;
  progressLabel: string;
  isServerStarting: boolean;
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

export interface MqttGatewayOption {
  chip_id: string;
  label: string;
}

export interface FleetTransportState {
  ports: Array<{ port_name: string; description?: string }>;
  mqttGatewayOptions: MqttGatewayOption[];
  isSelectedMqttGatewayDiscovered: boolean;
  manualMqttGatewayError: string | null;
  fleetTransport: 'serial' | 'mqtt';
  serialPortSelectorDisabled: boolean;
  showPairAdminPassword: boolean;
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
  ip?: string;
  relayLabel: string;
  inputLabel: string;
  tempLabel: string;
  tankLabel: string;
  tankDetailLabel: string | null;
  uptimeLabel: string;
  rowStatusLabel: string | null;
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
  adoptTextClass: string;
}

export interface FleetCandidateActionPayload {
  address: number;
  chip_id?: string;
}

const config = defineModel<FleetConfig>({ required: true });
const activeDropdownAddress = defineModel<number | string | null>('activeDropdownAddress', { required: true });
const networkUdpLogsExpanded = defineModel<boolean>('networkUdpLogsExpanded', { required: true });

const props = defineProps<{
  rows: FleetDisplayRow[];
  candidates: FleetCandidateDisplayRow[];
  gateway: FleetGatewayStatus;
  server: FleetServerStatus;
  udpLogs: FleetUdpLogs;
  transportState: FleetTransportState;
  inventorySummary: FleetInventorySummary;
  candidateSummary: FleetCandidateSummary;
}>();

const emit = defineEmits<{
  (e: 'toggle-row-selection', address: number | string, selected: boolean): void;
  (e: 'scan-start'): void;
  (e: 'scan-cancel'): void;
  (e: 'server-start'): void;
  (e: 'server-stop'): void;
  (e: 'udp-logging-start'): void;
  (e: 'udp-logging-stop'): void;
  (e: 'udp-logs-copy'): void;
  (e: 'manual-chip-input', val: string): void;
  (e: 'firmware-fetch'): void;
  (e: 'gateway-load'): void;
  (e: 'gateway-identify'): void;
  (e: 'gateway-flash'): void;
  (e: 'remote-flash', address: number | string): void;
  (e: 'remote-settings', address: number | string): void;
  (e: 'remote-reboot', address: number | string): void;
  (e: 'remote-view-logs', address: number | string): void;
  (e: 'remote-forget', address: number | string): void;
  (e: 'remote-factory-reset', address: number | string): void;
  (e: 'candidate-adopt', payload: FleetCandidateActionPayload): void;
}>();

const networkUdpLogContainer = ref<HTMLDivElement | null>(null);

function scrollNetworkUdpToBottom() {
  if (networkUdpLogContainer.value) {
    networkUdpLogContainer.value.scrollTop = networkUdpLogContainer.value.scrollHeight;
  }
}

watch(() => props.udpLogs.logs.length, () => {
  nextTick(() => scrollNetworkUdpToBottom());
});

const computedSessionConnectionType = computed({
  get: () => config.value.sessionConnectionType,
  set: (val) => { config.value = { ...config.value, sessionConnectionType: val }; }
});
const computedSelectedPort = computed({
  get: () => config.value.selectedPort,
  set: (val) => { config.value = { ...config.value, selectedPort: val }; }
});
const computedSelectedMqttManualChipId = computed({
  get: () => config.value.selectedMqttManualChipId,
  set: (val) => { config.value = { ...config.value, selectedMqttManualChipId: val }; }
});
const computedSelectedMqttGatewayChipId = computed({
  get: () => config.value.selectedMqttGatewayChipId,
  set: (val) => { config.value = { ...config.value, selectedMqttGatewayChipId: val }; }
});
const computedPairAdminPassword = computed({
  get: () => config.value.pairAdminPassword,
  set: (val) => { config.value = { ...config.value, pairAdminPassword: val }; }
});
const computedRegion = computed({
  get: () => config.value.region,
  set: (val) => { config.value = { ...config.value, region: val }; }
});
const computedSelectedVersion = computed({
  get: () => config.value.selectedVersion,
  set: (val) => { config.value = { ...config.value, selectedVersion: val }; }
});

function toggleNetworkUdpLogsExpanded() {
  networkUdpLogsExpanded.value = !networkUdpLogsExpanded.value;
  nextTick(() => scrollNetworkUdpToBottom());
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
          <span :class="['rounded border px-2 py-1 text-[10px] font-bold', server.isServerOn ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
            Firmware server {{ server.isServerOn ? 'on' : 'off' }}
          </span>
          <button
            @click="server.isLoraInventoryScanning ? emit('scan-cancel') : emit('scan-start')"
            :disabled="server.scanDisabled"
            class="primary-btn m-0 h-10 px-4 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
          >
            {{ server.scanLabel }}
          </button>
          <button
            @click="server.isServerOn ? emit('server-stop') : emit('server-start')"
            :disabled="server.isServerStarting"
            class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
          >
            {{ server.isServerOn ? 'Stop server' : (server.isServerStarting ? 'Starting...' : 'Start server') }}
          </button>
          <button
            @click="udpLogs.isMonitoring ? emit('udp-logging-stop') : emit('udp-logging-start')"
            class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
          >
            <span>{{ udpLogs.isMonitoring ? 'Stop UDP Listener' : 'Start UDP Listener' }}</span>
          </button>
        </div>
      </div>

      <div class="grid grid-cols-1 lg:grid-cols-5 gap-3">
        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">Connection Mode</label>
          <select v-model="computedSessionConnectionType" class="glass-input h-10 appearance-none">
            <option value="serial">USB Serial Gateway</option>
            <option value="mqtt">Remote MQTT Broker</option>
            <option value="local_broker">Local MQTT Broker</option>
          </select>
        </div>
        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">{{ transportState.fleetTransport === 'serial' ? 'USB gateway' : 'MQTT gateway' }}</label>
          <select v-if="transportState.fleetTransport === 'serial'" v-model="computedSelectedPort" :disabled="transportState.serialPortSelectorDisabled" class="glass-input h-10 appearance-none disabled:opacity-60">
            <option value="" disabled>Select USB gateway</option>
            <option v-for="port in transportState.ports" :key="port.port_name" :value="port.port_name">
              {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
            </option>
          </select>
          <div v-else-if="transportState.mqttGatewayOptions.length === 0" class="flex flex-col gap-1 w-full">
            <input
              v-model="computedSelectedMqttManualChipId"
              @input="emit('manual-chip-input', computedSelectedMqttManualChipId)"
              placeholder="Enter manual gateway chip ID"
              class="glass-input h-10 px-2 text-xs font-mono w-full"
            />
            <span class="text-[9px] text-slate-400">
              No gateways discovered yet. Enter the real gateway chip ID after configuring it to use this broker.
            </span>
            <span v-if="transportState.manualMqttGatewayError" class="text-[9px] text-rose-300">
              {{ transportState.manualMqttGatewayError }}
            </span>
          </div>
          <select v-else v-model="computedSelectedMqttGatewayChipId" class="glass-input h-10 appearance-none w-full">
            <option value="" disabled>Select MQTT gateway</option>
            <option v-for="gw in transportState.mqttGatewayOptions" :key="gw.chip_id" :value="gw.chip_id">
              {{ gw.label }}
            </option>
            <!-- Keep manual selection visible when it is not in discovery list -->
            <option v-if="computedSelectedMqttGatewayChipId && !transportState.isSelectedMqttGatewayDiscovered" :value="computedSelectedMqttGatewayChipId">
              Manual: lrs-{{ computedSelectedMqttGatewayChipId }}
            </option>
          </select>
        </div>
        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">Gateway admin password</label>
          <input v-model="computedPairAdminPassword" class="glass-input h-10 min-w-0 font-mono" :type="transportState.showPairAdminPassword ? 'text' : 'password'" autocomplete="current-password" />
        </div>
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
        <span v-if="server.isServerOn" class="font-mono text-slate-500 truncate">{{ server.serverFilename }} · {{ server.serverUrl }}</span>
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
            @click="emit('gateway-load')"
            :disabled="gateway.isLoading || (transportState.fleetTransport === 'mqtt' ? !computedSelectedMqttGatewayChipId : !config.selectedPort)"
            class="glass-input m-0 h-9 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60"
          >
            {{ gateway.isLoading ? 'Loading...' : 'Load gateway' }}
          </button>
          <button
            @click="emit('gateway-identify')"
            :disabled="gateway.isIdentifyDisabled"
            :class="['glass-input m-0 h-9 w-11 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50', { 'identify-led-active': gateway.isIdentifying }]"
            title="Identify selected USB gateway"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-6 h-6 identify-led-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6"></path><path d="M10 22h4"></path><path d="M8.5 14.5a6 6 0 1 1 7 0c-.8.7-1.5 1.6-1.5 2.5h-4c0-.9-.7-1.8-1.5-2.5Z"></path><path d="M12 2v2"></path><path d="m4.9 4.9 1.4 1.4"></path><path d="M2 12h2"></path><path d="m19.1 4.9-1.4 1.4"></path><path d="M20 12h2"></path></svg>
          </button>

          <button
            v-if="gateway.isUpgradeAvailable || gateway.isFlashing"
            @click="emit('gateway-flash')"
            :disabled="gateway.flashDisabled"
            :title="gateway.flashUnavailableReason"
            class="primary-btn m-0 h-9 px-4 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
          >
            {{ gateway.isFlashing ? 'Flashing...' : 'Upgrade gateway' }}
          </button>
        </div>
      </div>
      <div class="grid grid-cols-2 md:grid-cols-4 xl:grid-cols-7 gap-2 text-xs">
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Port</div>
          <div class="mt-1 truncate font-mono text-slate-300">{{ gateway.port || '-' }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Name</div>
          <div class="mt-1 truncate font-mono text-slate-300">{{ gateway.name }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Firmware</div>
          <div class="mt-1 truncate font-mono text-slate-300">{{ gateway.firmware }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Role</div>
          <div class="mt-1 truncate text-slate-300">{{ gateway.role }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Address</div>
          <div class="mt-1 truncate font-mono text-slate-300">{{ gateway.addressLine }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">WiFi</div>
          <div class="mt-1 truncate text-slate-300">{{ gateway.wifiLine }}</div>
        </div>
        <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
          <div class="text-[10px] uppercase tracking-wide text-slate-600">Uptime</div>
          <div class="mt-1 truncate font-mono text-slate-300">{{ gateway.uptimeLine }}</div>
        </div>
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
                      'rounded border px-2 py-1 text-[10px] font-bold font-mono',
                      device.wifi_rssi_dbm >= -60 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                      device.wifi_rssi_dbm >= -70 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                      device.wifi_rssi_dbm >= -80 ? 'border-amber-500/30 bg-amber-500/10 text-amber-300' :
                      'border-rose-500/30 bg-rose-500/10 text-rose-300'
                    ]"
                    :title="`WiFi RSSI: ${device.wifi_rssi_dbm} dBm`"
                  >
                    {{ device.wifi_rssi_dbm >= -60 ? '▂▄▆' : device.wifi_rssi_dbm >= -70 ? '▂▄▅' : device.wifi_rssi_dbm >= -80 ? '▂▄_' : '▂__' }} {{ device.wifi_rssi_dbm }}
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
                <div v-if="device.rowStatusLabel" :class="['mt-1 text-[10px] font-bold inline-flex items-center gap-1 cursor-pointer select-none', (device.rowState === 'unexpected_reboot' || device.rowState === 'ota_failed') ? 'text-rose-300' : device.rowState === 'ota_updated' ? 'text-emerald-300' : device.rowState === 'ota_rebooted' ? 'text-orange-400' : 'text-sky-300']" :title="device.rowState === 'unexpected_reboot' ? 'Spontaneous restart detected: Device uptime rolled back (rebooted) without a requested OTA command. Typically caused by power cycles, brownouts, or watchdog resets.' : device.rowState === 'ota_failed' ? 'Download failed: The remote device failed to download the firmware binary from the server.' : device.rowState === 'ota_rebooted' ? 'Normal post-upgrade restart: Device rebooted successfully to boot into the newly written firmware version.' : device.rowState === 'ota_no_reboot' ? 'Upgrade timeout: The firmware binary was served, but the remote did not reboot to apply it within the expected window.' : undefined">
                  {{ device.rowStatusLabel }}
                  <span v-if="device.rowState === 'unexpected_reboot' || device.rowState === 'ota_failed' || device.rowState === 'ota_rebooted' || device.rowState === 'ota_no_reboot'" class="opacity-60 text-[9px]">ⓘ</span>
                </div>
              </td>
              <td class="px-2 py-1.5 font-mono">
                <span
                  v-if="device.rssi !== undefined && device.rssi !== null && device.rssi !== 0 && device.rssi !== -127"
                  :class="[
                    'rounded border px-2 py-1 text-[10px] font-bold',
                    device.rssi >= -90 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                    device.rssi >= -100 ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' :
                    device.rssi >= -110 ? 'border-amber-500/30 bg-amber-500/10 text-amber-300' :
                    'border-rose-500/30 bg-rose-500/10 text-rose-300'
                  ]"
                  :title="`LoRa RSSI: ${device.rssi} dBm`"
                >
                  {{ device.rssi >= -90 ? '▂▄▆' : device.rssi >= -100 ? '▂▄▅' : device.rssi >= -110 ? '▂▄_' : '▂__' }} {{ device.rssi }}
                </span>
                <span v-else class="text-slate-500">-</span>
              </td>
              <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ageSeconds != null ? `${device.ageSeconds}s` : '-' }}</td>
              <td class="px-2 py-1.5 overflow-visible">
                <div class="relative inline-block text-left">
                  <button
                    @click.stop="activeDropdownAddress = (activeDropdownAddress === device.address ? null : device.address)"
                    class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold flex items-center gap-1 select-none"
                  >
                    Actions
                    <svg class="w-3 h-3 text-slate-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2.5" d="M19 9l-7 7-7-7" />
                    </svg>
                  </button>

                  <div
                    v-if="activeDropdownAddress === device.address"
                    class="absolute right-0 mt-1 w-40 z-40 rounded-md border border-slate-800 bg-slate-950/95 backdrop-blur-md py-1 shadow-2xl origin-top-right select-none font-medium"
                  >
                    <button
                      @click="emit('remote-flash', device.address); activeDropdownAddress = null"
                      :disabled="server.isServerStarting || ['ota_queued', 'ota_downloading', 'ota_apply_wait', 'ota_retrying'].includes(device.rowState || '') || !device.flashAvailable"
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
                </div>
              </td>
            </tr>
          </tbody>
        </table>
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
                  Adopt
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
