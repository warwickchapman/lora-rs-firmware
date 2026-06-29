<script setup lang="ts">
import { computed } from 'vue';

export interface ProvisionConfig {
  pairTransport: 'serial' | 'mqtt';
  selectedPort: string;
  selectedMqttManualChipId: string;
  selectedMqttGatewayChipId: string;
  pairExpectedCount: number;
  pairFleetKey: string;
  pairAdminPassword: string;
  pairWifiSsid: string;
  pairWifiPassword: string;
}

export interface ProvisionUiState {
  pairPanelTab: 'pair' | 'wifi';
  showPairFleetKey: boolean;
  showPairAdminPassword: boolean;
  showPairWifiPassword: boolean;
}

export interface ProvisionGatewayState {
  hasGatewayDeviceWarning: boolean;
  hasUncommissionedWarning: boolean;
  gatewayLabel: string;
  isGatewayLoadDisabled: boolean;
  isGatewayLoading: boolean;
  gatewayRoleLabel: string;
}

export interface ProvisionTransportState {
  ports: Array<{ port_name: string; description?: string }>;
  mqttGatewayOptions: Array<{ chip_id: string; label: string }>;
  isSelectedMqttGatewayDiscovered: boolean;
  manualMqttGatewayError: string | null;
  serialPortSelectorDisabled: boolean;
  isRefreshingPorts: boolean;
}

export interface ProvisionSessionSummary {
  state: string;
  foundCount: number;
  maxRemotes: number;
  verifiedCount: number;
  failedCount: number;
}

export interface ProvisionDiscoveredDeviceRow {
  chip_id_hex: string;
  rssi: number;
  current_address: number | string;
  assigned_address: number | string;
  firmware: string;
  isConflict: boolean;
  state: string;
  stateBorderClass: string;
}

export interface ProvisionPairState {
  isPairBusy: boolean;
  pairPrimaryDisabled: boolean;
  gatewayReady: boolean;
  sessionSummary: ProvisionSessionSummary | null;
  discoveredDevices: ProvisionDiscoveredDeviceRow[];
}

export interface WifiNetworkOption {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
  secure: boolean;
}

export interface ProvisionWifiState {
  wifiNetworks: WifiNetworkOption[];
  isWifiScanning: boolean;
  isWifiApplying: boolean;
  isFleetWifiSending: boolean;
  gatewayWifiReady: boolean;
  gatewayWifiStatusText: string;
  gatewayWifiHelpText: string;
  pairWifiSsidInScan: boolean;
}

export interface ProvisionIdentifyState {
  available: boolean;
  disabled: boolean;
  isIdentifying: boolean;
}

const config = defineModel<ProvisionConfig>({ required: true });
const ui = defineModel<ProvisionUiState>('ui', { required: true });

defineProps<{
  gatewayState: ProvisionGatewayState;
  transportState: ProvisionTransportState;
  pairState: ProvisionPairState;
  wifiState: ProvisionWifiState;
  identifyState: ProvisionIdentifyState;
}>();

const emit = defineEmits<{
  (e: 'trigger-identify'): void;
  (e: 'load-gateway'): void;
  (e: 'open-wifi-tab'): void;
  (e: 'refresh-ports'): void;
  (e: 'manual-chip-input', val: string): void;
  (e: 'generate-fleet-key'): void;
  (e: 'mark-fleet-key-manual'): void;
  (e: 'copy-fleet-key'): void;
  (e: 'copy-admin-password'): void;
  (e: 'run-or-cancel-provision'): void;
  (e: 'start-discovery'): void;
  (e: 'scan-wifi'): void;
  (e: 'connect-gateway-wifi'): void;
  (e: 'send-wifi-to-remotes'): void;
  (e: 'refresh-easy-pair-status'): void;
}>();

// Computed bridges for grouped v-model fields
const computedPairTransport = computed({
  get: () => config.value.pairTransport,
  set: (val) => { config.value = { ...config.value, pairTransport: val }; }
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
const computedPairExpectedCount = computed({
  get: () => config.value.pairExpectedCount,
  set: (val) => { config.value = { ...config.value, pairExpectedCount: val }; }
});
const computedPairFleetKey = computed({
  get: () => config.value.pairFleetKey,
  set: (val) => { config.value = { ...config.value, pairFleetKey: val }; }
});
const computedPairAdminPassword = computed({
  get: () => config.value.pairAdminPassword,
  set: (val) => { config.value = { ...config.value, pairAdminPassword: val }; }
});
const computedPairWifiSsid = computed({
  get: () => config.value.pairWifiSsid,
  set: (val) => { config.value = { ...config.value, pairWifiSsid: val }; }
});
const computedPairWifiPassword = computed({
  get: () => config.value.pairWifiPassword,
  set: (val) => { config.value = { ...config.value, pairWifiPassword: val }; }
});

const computedPairPanelTab = computed({
  get: () => ui.value.pairPanelTab,
  set: (val) => { ui.value = { ...ui.value, pairPanelTab: val }; }
});
const computedShowPairFleetKey = computed({
  get: () => ui.value.showPairFleetKey,
  set: (val) => { ui.value = { ...ui.value, showPairFleetKey: val }; }
});
const computedShowPairAdminPassword = computed({
  get: () => ui.value.showPairAdminPassword,
  set: (val) => { ui.value = { ...ui.value, showPairAdminPassword: val }; }
});
const computedShowPairWifiPassword = computed({
  get: () => ui.value.showPairWifiPassword,
  set: (val) => { ui.value = { ...ui.value, showPairWifiPassword: val }; }
});

// Presentational helper copied locally
function wifiSignalLabel(rssi: number): string {
  if (rssi >= -60) return 'Excellent';
  if (rssi >= -70) return 'Good';
  if (rssi >= -80) return 'Fair';
  return 'Weak';
}
</script>

<template>
  <div class="flex flex-col gap-3 h-full overflow-hidden">
    <!-- Gateway Device Validation Warning Callout -->
    <div v-if="gatewayState.hasGatewayDeviceWarning" class="rounded-lg border border-amber-500/30 bg-amber-500/10 p-3 text-amber-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-amber-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
        <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
      </svg>
      <div>
        <span class="font-bold">Gateway Device Required:</span>
        <template v-if="computedPairTransport === 'serial'">
          The device currently connected on <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewayState.gatewayLabel }}</span> is configured as a <span class="font-bold text-amber-200">Remote</span>. Please connect a gateway device instead.
        </template>
        <template v-else>
          The selected MQTT gateway <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewayState.gatewayLabel }}</span> is configured as a <span class="font-bold text-amber-200">Remote</span>. Select a gateway device instead.
        </template>
      </div>
    </div>

    <!-- Gateway Uncommissioned/Factory State Warning Callout -->
    <div v-if="gatewayState.hasUncommissionedWarning" class="rounded-lg border border-cyan-500/30 bg-cyan-500/10 p-3 text-cyan-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-cyan-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="10"></circle>
        <line x1="12" y1="8" x2="12" y2="12"></line>
        <line x1="12" y1="16" x2="12.01" y2="16"></line>
      </svg>
      <div>
        <span class="font-bold">Uncommissioned Gateway:</span>
        <template v-if="computedPairTransport === 'serial'">
          The gateway connected on <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewayState.gatewayLabel }}</span> is in a <span class="font-bold text-cyan-200">Factory / Uncommissioned State</span>. Provisioning it now will assign the new fleet key and commission it.
        </template>
        <template v-else>
          The selected MQTT gateway <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewayState.gatewayLabel }}</span> is in a <span class="font-bold text-cyan-200">Factory / Uncommissioned State</span>. Provisioning it now will assign the new fleet key and commission it.
        </template>
      </div>
    </div>

    <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
      <div class="flex items-start justify-between gap-3">
        <div>
          <h2 class="text-base font-bold text-cyan-300">
            Provision
          </h2>
          <p class="mt-1 text-xs text-slate-400">
            {{ computedPairTransport === 'serial' ? 'Selected USB device becomes the LoRa gateway.' : 'Discovered MQTT gateway will commission remote devices.' }}
          </p>
        </div>
        <div class="flex items-center gap-2">
          <button
            v-if="identifyState.available"
            @click="emit('trigger-identify')"
            :disabled="identifyState.disabled"
            :class="['glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed', { 'identify-led-active': identifyState.isIdentifying }]"
            title="Identify gateway"
            aria-label="Identify gateway"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 identify-led-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true">
              <path d="M9 18h6" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M10 22h4" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"></path>
              <path d="M8 14a6 6 0 1 1 8 0c-.8.65-1.15 1.25-1.28 2H9.28C9.15 15.25 8.8 14.65 8 14Z" stroke="currentColor" stroke-width="2.2" stroke-linejoin="round"></path>
              <circle cx="12" cy="8" r="2.1" fill="currentColor"></circle>
            </svg>
          </button>
          <button
            @click="emit('load-gateway')"
            :disabled="gatewayState.isGatewayLoadDisabled"
            class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
          >
            <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-cyan-300': gatewayState.isGatewayLoading }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
            <span>{{ gatewayState.isGatewayLoading ? 'Loading...' : 'Load Gateway' }}</span>
          </button>
        </div>
      </div>

      <div class="grid grid-cols-2 rounded border border-slate-800 bg-slate-950/30 text-xs font-bold">
        <button
          @click="computedPairPanelTab = 'pair'"
          :class="['m-0 h-8 rounded-none px-3 transition-all', computedPairPanelTab === 'pair' ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
        >
          Pair
        </button>
        <button
          @click="emit('open-wifi-tab')"
          :class="['m-0 h-8 rounded-none px-3 transition-all', computedPairPanelTab === 'wifi' ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
        >
          WiFi
        </button>
      </div>

      <div v-if="computedPairPanelTab === 'pair'" class="flex flex-col gap-3">
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-3">
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Connection Mode</label>
            <select v-model="computedPairTransport" class="glass-input h-10 appearance-none">
              <option value="serial">USB Serial Gateway</option>
              <option value="mqtt">MQTT</option>
            </select>
          </div>
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">{{ computedPairTransport === 'serial' ? 'USB gateway' : 'MQTT gateway' }}</label>
            <div v-if="computedPairTransport === 'serial'" class="flex gap-2">
              <select v-model="computedSelectedPort" :disabled="transportState.serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                <option v-for="port in transportState.ports" :key="port.port_name" :value="port.port_name">
                  {{ port.port_name }}
                </option>
                <option v-if="transportState.ports.length === 0" disabled>Scanning...</option>
              </select>
              <button @click="emit('refresh-ports')" :disabled="transportState.isRefreshingPorts || transportState.serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': transportState.isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
              </button>
            </div>
            <div v-else-if="transportState.mqttGatewayOptions.length === 0" class="flex flex-col gap-1">
              <input
                v-model="computedSelectedMqttManualChipId"
                @input="emit('manual-chip-input', computedSelectedMqttManualChipId)"
                placeholder="Enter manual gateway chip ID (e.g. 0030eb55)"
                class="glass-input h-10 px-2 text-xs font-mono"
              />
              <span class="text-[9px] text-slate-400">
                No gateways discovered yet. Enter the real gateway chip ID after configuring it to use this broker.
              </span>
              <span v-if="transportState.manualMqttGatewayError" class="text-[9px] text-rose-300">
                {{ transportState.manualMqttGatewayError }}
              </span>
            </div>
            <select v-else v-model="computedSelectedMqttGatewayChipId" class="glass-input h-10 appearance-none">
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
            <label class="font-medium text-slate-400">Max remotes</label>
            <input v-model.number="computedPairExpectedCount" class="glass-input h-10" type="number" min="1" max="12" />
            <div class="text-[10px] text-slate-500">Maximum powered factory/unprovisioned remotes to listen for in this scan.</div>
          </div>
        </div>

        <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Fleet key</label>
            <div class="grid grid-cols-[3rem_minmax(0,1fr)_3rem_3rem] gap-2">
              <button @click="emit('generate-fleet-key')" :disabled="gatewayState.gatewayRoleLabel !== '-' && gatewayState.gatewayRoleLabel !== 'uncommissioned'" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50 disabled:cursor-not-allowed" title="Generate fleet key" aria-label="Generate fleet key">
                <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
              </button>
              <input v-model="computedPairFleetKey" @input="emit('mark-fleet-key-manual')" class="glass-input h-10 flex-1 font-mono" :type="computedShowPairFleetKey ? 'text' : 'password'" autocomplete="new-password" />
              <button @click="computedShowPairFleetKey = !computedShowPairFleetKey" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="computedShowPairFleetKey ? 'Hide fleet key' : 'Show fleet key'" :aria-label="computedShowPairFleetKey ? 'Hide fleet key' : 'Show fleet key'">
                <svg v-if="!computedShowPairFleetKey" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
              </button>
              <button @click="emit('copy-fleet-key')" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" title="Copy fleet key" aria-label="Copy fleet key">
                <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>
          </div>
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Gateway admin password</label>
            <div class="grid grid-cols-[minmax(0,1fr)_3rem_3rem] gap-2">
              <input v-model="computedPairAdminPassword" class="glass-input h-10 min-w-0 font-mono" :type="computedShowPairAdminPassword ? 'text' : 'password'" autocomplete="current-password" />
              <button @click="computedShowPairAdminPassword = !computedShowPairAdminPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="computedShowPairAdminPassword ? 'Hide password' : 'Show password'" :aria-label="computedShowPairAdminPassword ? 'Hide password' : 'Show password'">
                <svg v-if="!computedShowPairAdminPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
              </button>
              <button @click="emit('copy-admin-password')" :disabled="!computedPairAdminPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50" title="Copy gateway password" aria-label="Copy gateway password">
                <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>
          </div>
        </div>

        <div class="grid grid-cols-[1fr_auto] gap-3">
          <button @click="emit('run-or-cancel-provision')" :disabled="!pairState.isPairBusy && pairState.pairPrimaryDisabled" :class="['h-9 flex items-center justify-center gap-3 text-sm font-bold transition-colors', pairState.isPairBusy ? 'rounded-md border border-amber-500/40 bg-amber-500/20 text-amber-100 hover:bg-amber-500/30' : 'primary-btn']">
            <svg v-if="pairState.isPairBusy" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><rect x="7" y="7" width="10" height="10" rx="1.5"></rect></svg>
            <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
            <span>{{ pairState.isPairBusy ? 'Cancel' : 'Provision' }}</span>
          </button>
          <button @click="emit('start-discovery')" :disabled="pairState.isPairBusy || gatewayState.isGatewayLoading || !config.selectedPort" class="glass-input h-9 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Discover powered remotes over LoRa without provisioning">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3-7 10-7 10 7 10 7-3 7-10 7-10-7-10-7Z"></path><circle cx="12" cy="12" r="3"></circle></svg>
            Scan
          </button>
        </div>
      </div>

      <div v-else class="flex flex-col gap-3">
        <div class="flex items-start justify-between gap-3">
          <div>
            <h2 class="text-base font-bold text-slate-300">WiFi</h2>
            <p class="mt-1 text-xs text-slate-400">
              {{ wifiState.gatewayWifiHelpText }}
            </p>
          </div>
          <button
            @click="emit('scan-wifi')"
            :disabled="wifiState.isWifiScanning || gatewayState.isGatewayLoading || !config.selectedPort"
            class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
          >
            <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-cyan-300': wifiState.isWifiScanning || gatewayState.isGatewayLoading }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
            <span>{{ gatewayState.isGatewayLoading ? 'Loading...' : wifiState.isWifiScanning ? 'Scanning...' : pairState.gatewayReady ? 'Scan WiFi' : 'Load & Scan' }}</span>
          </button>
        </div>

        <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">WiFi network</label>
            <select v-model="computedPairWifiSsid" class="glass-input h-10 appearance-none">
              <option v-for="network in wifiState.wifiNetworks" :key="`${network.ssid}-${network.bssid}`" :value="network.ssid">
                {{ network.ssid }} · {{ wifiSignalLabel(network.rssi) }} · ch {{ network.channel }}
              </option>
              <option v-if="computedPairWifiSsid && !wifiState.pairWifiSsidInScan" :value="computedPairWifiSsid">
                {{ computedPairWifiSsid }} · {{ wifiState.gatewayWifiReady ? 'already connected' : 'selected' }}
              </option>
              <option v-if="wifiState.wifiNetworks.length === 0" disabled>Scan to choose a network</option>
            </select>
          </div>
          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">WiFi password</label>
            <div class="flex gap-2">
              <input v-model="computedPairWifiPassword" class="glass-input h-10 flex-1" :type="computedShowPairWifiPassword ? 'text' : 'password'" autocomplete="new-password" />
              <button @click="computedShowPairWifiPassword = !computedShowPairWifiPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="computedShowPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'" :aria-label="computedShowPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'">
                <svg v-if="!computedShowPairWifiPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
              </button>
            </div>
          </div>
        </div>

        <div class="flex flex-col gap-2">
          <p :class="['text-xs', wifiState.gatewayWifiReady ? 'text-emerald-300' : 'text-slate-400']">
            {{ wifiState.gatewayWifiStatusText }}
          </p>
          <div class="flex gap-3 mt-1">
            <button
              @click="emit('connect-gateway-wifi')"
              :disabled="wifiState.isWifiApplying || wifiState.isFleetWifiSending || !pairState.gatewayReady || !computedPairWifiSsid"
              class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60 flex-1"
            >
              {{ wifiState.isWifiApplying ? 'Saving Gateway...' : 'Save on Gateway' }}
            </button>
            <button
              @click="emit('send-wifi-to-remotes')"
              :disabled="wifiState.isFleetWifiSending || wifiState.isWifiApplying || !pairState.gatewayReady || !computedPairWifiSsid"
              class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60 flex-1"
            >
              {{ wifiState.isFleetWifiSending ? 'Sending...' : 'Send to Remotes' }}
            </button>
          </div>
        </div>
      </div>
    </div>

    <div class="glass-card p-3 flex flex-col gap-3 text-left flex-1 min-h-0 overflow-hidden">
      <div class="flex items-center justify-between">
        <h2 class="text-base font-bold text-slate-300">Discovered devices</h2>
        <button @click="emit('refresh-easy-pair-status')" :disabled="pairState.isPairBusy || !pairState.gatewayReady" class="text-xs text-slate-500 hover:text-cyan-300">Refresh</button>
      </div>
      <div v-if="pairState.sessionSummary" class="grid grid-cols-4 gap-3 text-xs">
        <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
          <div class="text-slate-500">State</div>
          <div class="font-mono text-slate-300 truncate">{{ pairState.sessionSummary.state }}</div>
        </div>
        <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
          <div class="text-slate-500">Found</div>
          <div class="font-mono text-slate-300">{{ pairState.sessionSummary.foundCount }} / {{ pairState.sessionSummary.maxRemotes }}</div>
        </div>
        <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
          <div class="text-slate-500">Verified</div>
          <div class="font-mono text-slate-300">{{ pairState.sessionSummary.verifiedCount }}</div>
        </div>
        <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
          <div class="text-slate-500">Failed</div>
          <div class="font-mono text-slate-300">{{ pairState.sessionSummary.failedCount }}</div>
        </div>
      </div>

      <div v-if="pairState.discoveredDevices.length === 0" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
        {{ computedPairTransport === 'serial' ? 'Power the remote devices, then scan from the selected USB gateway.' : 'Power the remote devices, then scan from the selected MQTT gateway.' }}
      </div>

      <div v-else class="flex-1 min-h-0 overflow-auto custom-scrollbar pr-1">
        <div
          v-for="device in pairState.discoveredDevices"
          :key="device.chip_id_hex"
          class="rounded-md border border-slate-800 bg-slate-900/30 p-3 mb-3"
        >
          <div class="grid grid-cols-[minmax(8rem,1fr)_repeat(4,minmax(4rem,auto))] gap-3 items-center text-xs">
            <div class="min-w-0">
              <div class="font-mono text-slate-200 truncate">{{ device.chip_id_hex }}</div>
              <div class="font-mono text-slate-500">RSSI {{ device.rssi }}</div>
            </div>
            <div>
              <div class="text-slate-500">Current</div>
              <div class="font-mono text-slate-300">{{ device.current_address }}</div>
            </div>
            <div>
              <div class="text-slate-500">Assigned</div>
              <div class="font-mono text-slate-300">{{ device.assigned_address || '-' }}</div>
            </div>
            <div>
              <div class="text-slate-500">Firmware</div>
              <div class="font-mono text-slate-300">{{ device.firmware }}</div>
            </div>
            <div class="text-right">
              <span :class="['rounded border px-2 py-1 text-[10px] font-bold', device.stateBorderClass]">
                {{ device.isConflict ? 'Conflict' : device.state }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
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

.identify-led-icon {
  color: rgb(148 163 184);
}

.identify-led-active .identify-led-icon {
  animation: identify-led-pattern 2s linear infinite;
}

@keyframes identify-led-pattern {
  0%, 5.9%,
  12%, 17.9%,
  24%, 29.9%,
  49%, 54.9%,
  61%, 66.9%,
  73%, 78.9% {
    color: rgb(34 211 238);
    filter: none;
    opacity: 1;
  }
  6%, 11.9%,
  18%, 23.9%,
  30%, 48.9%,
  55%, 60.9%,
  67%, 72.9%,
  79%, 100% {
    color: rgb(71 85 105);
    filter: none;
    opacity: 0.42;
  }
}
</style>
