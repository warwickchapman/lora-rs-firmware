<script setup lang="ts">
import { computed } from 'vue';

export type SettingsTab = 'general' | 'control' | 'network' | 'mqtt' | 'sensors' | 'remote' | 'system';
export type RemoteSubTab = 'serial' | 'mqtt' | 'lora';

export interface SettingsForm {
  selectedPort: string;
  settingsConnectionType: 'serial' | 'mqtt';
  settingsAdminPassword: string;
  selectedMqttManualChipId: string;
  selectedMqttGatewayChipId: string;
  settingsTab: SettingsTab;
  remoteSubTab: RemoteSubTab;
  showSettingsAdminPassword: boolean;
  showSerialWifiPassword: boolean;
  showSerialMqttPassword: boolean;
  showSerialFleetKey: boolean;
  serialFactoryKeepFleet: boolean;
  serialFactoryKeepWifi: boolean;
}

export interface SettingsHeaderState {
  serialStatusSummary: string;
  identifyAvailable: boolean;
  identifyDisabled: boolean;
  isIdentifying: boolean;
  isFlashing: boolean;
  isLoadingInfo: boolean;
  isSerialAdminLoading: boolean;
  isSerialAdminSaving: boolean;
  serialAdminDisabled: boolean;
  serialAdminBusy: boolean;
  serialAdminConfigExists: boolean;
}

export interface SettingsTransportOptionPort {
  port_name: string;
  description?: string;
}

export interface SettingsTransportOptionGateway {
  chip_id: string;
}

export interface SettingsTransportState {
  settingsTransport: 'serial' | 'mqtt';
  ports: SettingsTransportOptionPort[];
  serialPortSelectorDisabled: boolean;
  isRefreshingPorts: boolean;
  mqttGateways: Record<string, SettingsTransportOptionGateway>;
  manualMqttGatewayError: string | null;
  isSelectedMqttGatewayDiscovered: boolean;
  sessionMqttConnected: boolean;
}

export interface SettingsAdminStatusState {
  fw_version: string;
  uptime_ms: number;
  heap_free: number;
  mode: string;
}

export interface SettingsSecretState {
  isWifiStaPasswordConfigured: boolean;
  isMqttPasswordConfigured: boolean;
  isFleetPassphraseConfigured: boolean;
  isMqttFleetPassphraseDefault: boolean;
}

export interface SettingsWifiNetwork {
  ssid: string;
  rssi: number;
  channel: number;
  secure: boolean;
  bssid: string;
}

export interface SettingsWifiState {
  isWifiScanning: boolean;
  settingsWifiNetworks: SettingsWifiNetwork[];
}

export interface LocalSerialAdminConfig {
  mode: string;
  commissioned?: boolean;
  role_tx: boolean;
  local_address: number;
  controller_address?: number;
  allowed_controller_addresses?: number[];
  known_peer_addresses?: number[];
  lora_tx_power?: number;
  lora_spreading_factor?: number;
  lora_bandwidth_hz?: number;
  lora_coding_rate?: number;
  heartbeat_ms?: number;
  heartbeat_enabled?: boolean;
  ack_timeout_ms?: number;
  remote_refresh_enabled?: boolean;
  remote_refresh_cycle_ms?: number;
  input_control_paired_lora_enabled?: boolean;
  tx_command_retry_timeout_ms?: number;
  rx_failsafe_mode?: string;
  rx_failsafe_timeout_ms?: number;
  wifi_sta_ssid: string;
  wifi_sta_password: string;
  lan_hostname?: string;
  ap_always_on?: boolean;
  wifi_tx_power_dbm?: number;
  wifi_sleep_enabled?: boolean;
  wifi_static_ip_enabled?: boolean;
  wifi_static_ip?: string;
  wifi_static_gateway?: string;
  wifi_static_subnet?: string;
  wifi_channel_override?: number;
  wifi_ap_fallback_policy?: string;
  wifi_admin_enabled: boolean;
  mqtt_client_enabled: boolean;
  mqtt_control_enabled: boolean;
  mqtt_controller_addresses?: string;
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  mqtt_topic_root: string;
  fleet_passphrase?: string;
  sensor_temp_enabled: boolean;
  sensor_temp_pin: number;
  sensor_temp_interval_s: number;
  sensor_tank_enabled: boolean;
  sensor_tank_range_mm: number;
  sensor_tank_vref_mv: number;
  sensor_tank_sense_ohms: number;
  sensor_tank_interval_s: number;
  admin_password?: string;
}

const form = defineModel<SettingsForm>('form', { required: true });
const config = defineModel<LocalSerialAdminConfig | null>('config', { required: true });

defineProps<{
  headerState: SettingsHeaderState;
  transportState: SettingsTransportState;
  adminStatus: SettingsAdminStatusState | null;
  secretState: SettingsSecretState;
  wifiState: SettingsWifiState;
  serialAdminIsFactoryDefault: boolean;
  hasActiveDeviceInfo: boolean;
  settingsEmptyMessage: string;
}>();

const emit = defineEmits<{
  (e: 'trigger-identify'): void;
  (e: 'read-device-info'): void;
  (e: 'refresh-status'): void;
  (e: 'fetch-settings'): void;
  (e: 'copy-config-json'): void;
  (e: 'refresh-ports'): void;
  (e: 'manual-chip-input', val: string): void;
  (e: 'open-mqtt-settings'): void;
  (e: 'scan-wifi'): void;
  (e: 'save-config'): void;
  (e: 'reboot-device'): void;
  (e: 'factory-reset'): void;
  (e: 'copy-payload', payload: { text: string; label: string }): void;
}>();

// computed bridges for form fields
const computedSelectedPort = computed({
  get: () => form.value.selectedPort,
  set: (val) => { form.value = { ...form.value, selectedPort: val }; }
});
const computedSettingsConnectionType = computed({
  get: () => form.value.settingsConnectionType,
  set: (val) => { form.value = { ...form.value, settingsConnectionType: val }; }
});
const computedSettingsAdminPassword = computed({
  get: () => form.value.settingsAdminPassword,
  set: (val) => { form.value = { ...form.value, settingsAdminPassword: val }; }
});
const computedSelectedMqttManualChipId = computed({
  get: () => form.value.selectedMqttManualChipId,
  set: (val) => { form.value = { ...form.value, selectedMqttManualChipId: val }; }
});
const computedSelectedMqttGatewayChipId = computed({
  get: () => form.value.selectedMqttGatewayChipId,
  set: (val) => { form.value = { ...form.value, selectedMqttGatewayChipId: val }; }
});
const computedSettingsTab = computed({
  get: () => form.value.settingsTab,
  set: (val) => { form.value = { ...form.value, settingsTab: val }; }
});
const computedRemoteSubTab = computed({
  get: () => form.value.remoteSubTab,
  set: (val) => { form.value = { ...form.value, remoteSubTab: val }; }
});
const computedShowSettingsAdminPassword = computed({
  get: () => form.value.showSettingsAdminPassword,
  set: (val) => { form.value = { ...form.value, showSettingsAdminPassword: val }; }
});
const computedShowSerialWifiPassword = computed({
  get: () => form.value.showSerialWifiPassword,
  set: (val) => { form.value = { ...form.value, showSerialWifiPassword: val }; }
});
const computedShowSerialMqttPassword = computed({
  get: () => form.value.showSerialMqttPassword,
  set: (val) => { form.value = { ...form.value, showSerialMqttPassword: val }; }
});
const computedShowSerialFleetKey = computed({
  get: () => form.value.showSerialFleetKey,
  set: (val) => { form.value = { ...form.value, showSerialFleetKey: val }; }
});
const computedSerialFactoryKeepFleet = computed({
  get: () => form.value.serialFactoryKeepFleet,
  set: (val) => { form.value = { ...form.value, serialFactoryKeepFleet: val }; }
});
const computedSerialFactoryKeepWifi = computed({
  get: () => form.value.serialFactoryKeepWifi,
  set: (val) => { form.value = { ...form.value, serialFactoryKeepWifi: val }; }
});

// computed bridges for config nested properties to avoid child direct mutation issues
const configRoleTx = computed({
  get: () => config.value?.role_tx ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, role_tx: val }; }
});
const configLocalAddress = computed({
  get: () => config.value?.local_address ?? 1,
  set: (val) => { if (config.value) config.value = { ...config.value, local_address: val }; }
});
const configControllerAddress = computed({
  get: () => config.value?.controller_address ?? 254,
  set: (val) => { if (config.value) config.value = { ...config.value, controller_address: val }; }
});
const configFleetPassphrase = computed({
  get: () => config.value?.fleet_passphrase ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, fleet_passphrase: val }; }
});
const configHeartbeatEnabled = computed({
  get: () => config.value?.heartbeat_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, heartbeat_enabled: val }; }
});
const configRxFailsafeMode = computed({
  get: () => config.value?.rx_failsafe_mode ?? 'hold_last',
  set: (val) => { if (config.value) config.value = { ...config.value, rx_failsafe_mode: val }; }
});
const configWifiStaSsid = computed({
  get: () => config.value?.wifi_sta_ssid ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_sta_ssid: val }; }
});
const configWifiStaPassword = computed({
  get: () => config.value?.wifi_sta_password ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_sta_password: val }; }
});
const configWifiAdminEnabled = computed({
  get: () => config.value?.wifi_admin_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_admin_enabled: val }; }
});
const configLanHostname = computed({
  get: () => config.value?.lan_hostname ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, lan_hostname: val }; }
});
const configWifiApFallbackPolicy = computed({
  get: () => config.value?.wifi_ap_fallback_policy ?? 'fallback_on_disconnect',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_ap_fallback_policy: val }; }
});
const configApAlwaysOn = computed({
  get: () => config.value?.ap_always_on ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, ap_always_on: val }; }
});
const configWifiTxPowerDbm = computed({
  get: () => config.value?.wifi_tx_power_dbm ?? 20.5,
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_tx_power_dbm: val }; }
});
const configWifiChannelOverride = computed({
  get: () => config.value?.wifi_channel_override ?? 0,
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_channel_override: val }; }
});
const configWifiSleepEnabled = computed({
  get: () => config.value?.wifi_sleep_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_sleep_enabled: val }; }
});
const configWifiStaticIpEnabled = computed({
  get: () => config.value?.wifi_static_ip_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_static_ip_enabled: val }; }
});
const configWifiStaticIp = computed({
  get: () => config.value?.wifi_static_ip ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_static_ip: val }; }
});
const configWifiStaticGateway = computed({
  get: () => config.value?.wifi_static_gateway ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_static_gateway: val }; }
});
const configWifiStaticSubnet = computed({
  get: () => config.value?.wifi_static_subnet ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, wifi_static_subnet: val }; }
});
const configMqttHost = computed({
  get: () => config.value?.mqtt_host ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_host: val }; }
});
const configMqttPort = computed({
  get: () => config.value?.mqtt_port ?? 1883,
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_port: val }; }
});
const configMqttTopicRoot = computed({
  get: () => config.value?.mqtt_topic_root ?? 'lora',
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_topic_root: val }; }
});
const configMqttClientEnabled = computed({
  get: () => config.value?.mqtt_client_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_client_enabled: val }; }
});
const configMqttUser = computed({
  get: () => config.value?.mqtt_user ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_user: val }; }
});
const configMqttPassword = computed({
  get: () => config.value?.mqtt_password ?? '',
  set: (val) => { if (config.value) config.value = { ...config.value, mqtt_password: val }; }
});
const configSensorTempEnabled = computed({
  get: () => config.value?.sensor_temp_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, sensor_temp_enabled: val }; }
});
const configSensorTankEnabled = computed({
  get: () => config.value?.sensor_tank_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, sensor_tank_enabled: val }; }
});
const configGatewayControlMode = computed<'mqtt' | 'input' | null>({
  get: () => {
    if (!config.value) return null;
    if (config.value.input_control_paired_lora_enabled) return 'input';
    if (config.value.mqtt_control_enabled) return 'mqtt';
    return null;
  },
  set: (mode) => {
    if (!config.value) return;
    if (mode === 'mqtt') {
      config.value = {
        ...config.value,
        mqtt_client_enabled: true,
        mqtt_control_enabled: true,
        input_control_paired_lora_enabled: false
      };
      return;
    }
    config.value = {
      ...config.value,
      mqtt_control_enabled: false,
      input_control_paired_lora_enabled: true
    };
  }
});
const configRemoteRefreshEnabled = computed({
  get: () => config.value?.remote_refresh_enabled ?? false,
  set: (val) => { if (config.value) config.value = { ...config.value, remote_refresh_enabled: val }; }
});

const SETTINGS_TABS: Array<{ key: SettingsTab; label: string }> = [
  { key: 'general', label: 'General' },
  { key: 'control', label: 'Control' },
  { key: 'network', label: 'Network' },
  { key: 'mqtt', label: 'MQTT' },
  { key: 'sensors', label: 'Sensors' },
  { key: 'system', label: 'System' },
  { key: 'remote', label: 'Reference' },
];

function formatUptime(ms: number | null): string {
  if (ms === null) return '';
  const totalSeconds = Math.floor(ms / 1000);
  if (totalSeconds < 60) return `${totalSeconds}s`;

  const totalMinutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  if (totalMinutes < 60) return `${totalMinutes}m ${seconds.toString().padStart(2, '0')}s`;

  const totalHours = Math.floor(totalMinutes / 60);
  const minutes = totalMinutes % 60;
  if (totalHours < 24) return `${totalHours}h ${minutes.toString().padStart(2, '0')}m`;

  const days = Math.floor(totalHours / 24);
  const hours = totalHours % 24;
  return `${days}d ${hours.toString().padStart(2, '0')}h`;
}

function formatBytes(bytes: number | null | undefined): string {
  const n = Number(bytes || 0);
  if (n < 1024) return `${n} B`;
  return `${(n / 1024).toFixed(1)} KB`;
}

function wifiSignalLabel(rssi: number): string {
  if (rssi >= -60) return 'Excellent';
  if (rssi >= -70) return 'Good';
  if (rssi >= -80) return 'Fair';
  return 'Weak';
}

function lrsDeviceName(rawChipId: string | undefined | null): string {
  const chip = String(rawChipId || '').trim().replace(/^0x/i, '').replace(/[^0-9a-f]/gi, '').toLowerCase();
  if (!chip) return '-';
  return `lrs-${chip.length < 8 ? chip.padStart(8, '0') : chip}`;
}

function handleManualMqttGatewayInput(val: string) {
  emit('manual-chip-input', val);
}
</script>

<template>
  <div class="flex flex-col h-full min-h-0 overflow-hidden gap-3 text-left">
    <div class="glass-card flex flex-col gap-3 p-3 shrink-0">
      <div class="flex flex-col gap-3 lg:flex-row lg:items-center lg:justify-between">
        <div class="min-w-0">
          <h2 class="text-base font-bold text-cyan-300">Settings</h2>
          <p class="mt-1 text-xs text-slate-400">{{ headerState.serialStatusSummary }}</p>
        </div>
        <div class="flex flex-wrap items-center justify-end gap-2">
          <button
            v-if="headerState.identifyAvailable"
            @click="emit('trigger-identify')"
            :disabled="headerState.identifyDisabled"
            :class="[
              'glass-input m-0 h-10 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed',
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
          <button v-if="transportState.settingsTransport !== 'mqtt'" @click="emit('read-device-info')" :disabled="headerState.isFlashing || headerState.isLoadingInfo" class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
            {{ headerState.isLoadingInfo ? 'Reading...' : 'Read identity' }}
          </button>
          <button @click="emit('refresh-status')" :disabled="headerState.serialAdminDisabled" class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
            {{ headerState.isSerialAdminLoading ? 'Loading...' : 'Refresh status' }}
          </button>
          <button @click="emit('fetch-settings')" :disabled="!computedSelectedPort || headerState.isFlashing || headerState.isLoadingInfo || headerState.serialAdminBusy" class="primary-btn m-0 h-10 px-4 text-xs font-bold disabled:opacity-60">
            {{ headerState.isSerialAdminLoading ? 'Fetching...' : 'Fetch settings' }}
          </button>
          <button @click="emit('copy-config-json')" :disabled="!headerState.serialAdminConfigExists" class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
            Copy config JSON
          </button>
        </div>
      </div>

      <div :class="['grid grid-cols-1 gap-2', transportState.settingsTransport === 'mqtt' ? 'md:grid-cols-[minmax(0,1fr)_8rem_10rem_8rem_8rem]' : 'md:grid-cols-[minmax(0,1fr)_10rem]']">
        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">Settings target</label>
          <div class="flex gap-2">
            <select v-if="transportState.settingsTransport === 'serial'" v-model="computedSelectedPort" :disabled="transportState.serialPortSelectorDisabled" class="glass-input h-9 flex-1 appearance-none disabled:opacity-60">
              <option value="" disabled>Select USB device</option>
              <option v-for="port in transportState.ports" :key="port.port_name" :value="port.port_name">
                {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
              </option>
              <option v-if="transportState.ports.length === 0" disabled>Scanning...</option>
            </select>
            <div v-else class="flex-1 flex flex-col gap-1">
              <div v-if="Object.keys(transportState.mqttGateways).length === 0" class="flex flex-col gap-1.5 w-full">
                <input
                  v-model="computedSelectedMqttManualChipId"
                  @input="handleManualMqttGatewayInput(computedSelectedMqttManualChipId)"
                  placeholder="Enter manual gateway chip ID"
                  class="glass-input h-9 px-2 text-xs font-mono w-full"
                />
                <span class="text-[9px] text-slate-400">
                  No gateways discovered yet. Enter the real gateway chip ID after configuring it to use this broker.
                </span>
                <span v-if="transportState.manualMqttGatewayError" class="text-[9px] text-rose-300">
                  {{ transportState.manualMqttGatewayError }}
                </span>
              </div>
              <select v-else v-model="computedSelectedMqttGatewayChipId" class="glass-input h-9 flex-1 appearance-none w-full">
                <option value="" disabled>Select MQTT gateway</option>
                <option v-for="gw in Object.values(transportState.mqttGateways)" :key="gw.chip_id" :value="gw.chip_id">
                  {{ lrsDeviceName(gw.chip_id) }} (lrs-{{ gw.chip_id }})
                </option>
                <!-- Keep manual selection visible when it is not in discovery list -->
                <option v-if="computedSelectedMqttGatewayChipId && !transportState.isSelectedMqttGatewayDiscovered" :value="computedSelectedMqttGatewayChipId">
                  Manual: lrs-{{ computedSelectedMqttGatewayChipId }}
                </option>
              </select>
            </div>
            <button v-if="transportState.settingsTransport === 'serial'" @click="emit('refresh-ports')" :disabled="transportState.isRefreshingPorts || transportState.serialPortSelectorDisabled" class="glass-input h-9 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': transportState.isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
            </button>
          </div>
        </div>
        <div class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">Via</label>
          <select v-model="computedSettingsConnectionType" class="glass-input h-9 appearance-none">
            <option value="serial">USB Serial</option>
            <option value="mqtt">MQTT</option>
          </select>
        </div>
        <div v-if="transportState.settingsTransport === 'mqtt'" class="flex flex-col gap-1.5 text-xs min-w-0">
          <label class="font-medium text-slate-400">Admin password</label>
          <div class="relative w-full">
            <input
              v-model="computedSettingsAdminPassword"
              :type="computedShowSettingsAdminPassword ? 'text' : 'password'"
              class="glass-input h-9 w-full pr-10 font-mono text-xs"
              placeholder="Enter admin password"
            />
            <button
              type="button"
              @click="computedShowSettingsAdminPassword = !computedShowSettingsAdminPassword"
              class="absolute right-3 top-2.5 text-slate-400 hover:text-slate-200"
              style="background: transparent; border: none; padding: 0;"
            >
              <svg v-if="computedShowSettingsAdminPassword" xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13.875 18.825A10.05 10.05 0 0112 19c-4.478 0-8.268-2.943-9.543-7a9.97 9.97 0 011.563-3.029m5.858.908a3 3 0 114.243 4.243M9.878 9.878l4.242 4.242M9.88 9.88l-3.29-3.29m7.532 7.532l3.29 3.29M3 3l3.59 3.59m0 0A9.953 9.953 0 0112 5c4.478 0 8.268 2.943 9.543 7a10.025 10.025 0 01-4.132 5.411m0 0L21 21" /></svg>
              <svg v-else xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" fill="none" viewBox="0 0 24 24" stroke="currentColor"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" /><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M2.458 12C3.732 7.943 7.523 5 12 5c4.478 0 8.268 2.943 9.542 7-1.274 4.057-5.064 7-9.542 7-4.477 0-8.268-2.943-9.542-7z" /></svg>
            </button>
          </div>
        </div>
        <div v-if="transportState.settingsTransport === 'mqtt'" class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">MQTT config</label>
          <button
            @click="emit('open-mqtt-settings')"
            class="glass-input h-9 hover:bg-slate-700/70 text-xs font-bold whitespace-nowrap"
          >
            Broker config
          </button>
        </div>
        <div v-if="transportState.settingsTransport === 'mqtt'" class="flex flex-col gap-1.5 text-xs">
          <label class="font-medium text-slate-400">MQTT broker</label>
          <span :class="['inline-flex h-9 items-center justify-center rounded border px-2 text-[10px] font-bold whitespace-nowrap', transportState.sessionMqttConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
            {{ transportState.sessionMqttConnected ? 'Connected' : 'Offline' }}
          </span>
        </div>
      </div>
    </div>

    <div class="glass-card flex min-h-0 flex-1 flex-col overflow-hidden">
      <div class="flex shrink-0 overflow-x-auto border-b border-slate-800 bg-slate-900/50 text-xs font-bold">
        <button
          v-for="tab in SETTINGS_TABS"
          :key="tab.key"
          @click="computedSettingsTab = tab.key"
          :class="['m-0 h-9 rounded-none border-r border-slate-800 px-4 transition-colors', computedSettingsTab === tab.key ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
        >
          {{ tab.label }}
        </button>
      </div>

      <div class="min-h-0 flex-1 overflow-auto custom-scrollbar p-3">
        <div v-if="serialAdminIsFactoryDefault && computedSettingsTab !== 'remote'" class="mb-3 rounded border border-amber-500/30 bg-amber-500/10 p-2 text-xs text-amber-100">
          Factory default: this device is not commissioned yet. Use Provision before treating it as an operational gateway or remote.
        </div>

        <div v-if="!hasActiveDeviceInfo && transportState.settingsTransport !== 'mqtt' && computedSettingsTab !== 'remote'" class="rounded border border-slate-800 bg-slate-950/30 p-3 text-xs text-slate-500">
          Select a USB device and read device info before loading or saving settings.
        </div>
        <div v-if="transportState.settingsTransport === 'mqtt' && !config && computedSettingsTab !== 'remote'" class="rounded border border-slate-800 bg-slate-950/30 p-3 text-xs text-slate-500">
          Select an MQTT gateway and fetch settings to edit configuration.
        </div>

        <div v-if="computedSettingsTab === 'general'" class="flex flex-col gap-3">
          <div v-if="adminStatus" class="grid grid-cols-2 gap-2 text-xs xl:grid-cols-5">
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-slate-500">Firmware</div>
              <div class="font-mono text-slate-200">{{ adminStatus.fw_version || 'unknown' }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-slate-500">Uptime</div>
              <div class="font-mono text-slate-200">{{ formatUptime(adminStatus.uptime_ms || 0) }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-slate-500">Heap</div>
              <div class="font-mono text-slate-200">{{ formatBytes(adminStatus.heap_free) }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-slate-500">Mode</div>
              <div class="font-mono text-slate-200">{{ adminStatus.mode || '-' }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-slate-500">State</div>
              <div class="font-mono text-slate-200">{{ serialAdminIsFactoryDefault ? 'factory' : 'commissioned' }}</div>
            </div>
          </div>

          <div v-if="config" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
            <label class="self-center text-right font-semibold text-slate-300">Role</label>
            <select v-model="configRoleTx" class="glass-input h-9 appearance-none">
              <option :value="true">Gateway</option>
              <option :value="false">Remote</option>
            </select>
            <label class="self-center text-right font-semibold text-slate-300">Local addr</label>
            <input v-model.number="configLocalAddress" type="number" min="1" max="254" class="glass-input h-9" />
            <template v-if="!config.role_tx">
              <label class="self-center text-right font-semibold text-slate-300">Controller addr</label>
              <input v-model.number="configControllerAddress" type="number" min="1" max="254" class="glass-input h-9" />
            </template>
            <label class="self-center text-right font-semibold text-slate-300">Fleet key</label>
            <div class="flex flex-col gap-1">
              <div class="flex gap-2">
                <input v-model="configFleetPassphrase" :type="computedShowSerialFleetKey ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Fleet key passphrase" />
                <button @click="computedShowSerialFleetKey = !computedShowSerialFleetKey" class="glass-input h-9 px-3 hover:bg-slate-700/70" type="button">
                  {{ computedShowSerialFleetKey ? 'Hide' : 'Show' }}
                </button>
              </div>
              <span v-if="secretState.isFleetPassphraseConfigured" class="text-[10px] font-medium" :class="secretState.isMqttFleetPassphraseDefault ? 'text-amber-400' : 'text-cyan-400'">
                ✓ Currently configured on device {{ secretState.isMqttFleetPassphraseDefault ? '(using default passphrase)' : '' }}
              </span>
            </div>
          </div>
        </div>

        <div v-if="computedSettingsTab === 'control'" class="flex flex-col gap-3 text-xs">
          <template v-if="config">
            <div v-if="config.role_tx" class="flex flex-col gap-4">
              <fieldset class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2">
                <legend class="self-center text-right font-semibold text-slate-300">Control source</legend>
                <div class="flex h-9 items-center gap-5">
                  <label class="flex cursor-pointer items-center gap-2 text-slate-200" title="Accept relay commands from MQTT and forward them to paired remotes.">
                    <input v-model="configGatewayControlMode" type="radio" name="gateway-control-source" value="mqtt" class="accent-cyan-500" />
                    MQTT
                  </label>
                  <label class="flex cursor-pointer items-center gap-2 text-slate-200" title="Use the gateway dry-contact input to control paired remote relays.">
                    <input v-model="configGatewayControlMode" type="radio" name="gateway-control-source" value="input" class="accent-cyan-500" />
                    Gateway input
                  </label>
                </div>
              </fieldset>

              <div v-if="configGatewayControlMode === null" class="rounded border border-amber-500/30 bg-amber-500/10 p-3 text-amber-100">
                Select MQTT or Gateway input, then save the configuration. This gateway has no active relay-control source yet.
              </div>

              <div v-if="configGatewayControlMode === 'mqtt'" class="rounded border border-cyan-500/20 bg-cyan-950/15 p-3 text-slate-300">
                MQTT commands control the gateway and paired remotes. Broker connection and remote-state refresh are configured on the MQTT tab.
              </div>

              <div v-else-if="configGatewayControlMode === 'input'" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 rounded border border-slate-800 bg-slate-950/20 p-3">
                <div class="col-span-2 font-semibold text-slate-100">Input control</div>
                <label class="self-center text-right font-semibold text-slate-300">Control remotes</label>
                <div class="text-slate-300">The gateway dry-contact input controls paired remote relays.</div>
                <label class="self-center text-right font-semibold text-slate-300">Input sync sec</label>
                <div class="flex items-center gap-2">
                  <input :value="Math.round((config.heartbeat_ms || 60000) / 1000)" @input="config.heartbeat_ms = Number(($event.target as HTMLInputElement).value || 60) * 1000" type="number" min="60" max="3600" class="glass-input h-9 flex-1" :disabled="!config.heartbeat_enabled" title="How often the gateway repeats its current input command to paired remotes." />
                  <label class="flex items-center gap-1.5 text-xs text-slate-400 cursor-pointer select-none whitespace-nowrap" title="Enable periodic input synchronisation.">
                    <input v-model="configHeartbeatEnabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
                    Enabled
                  </label>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">Retry window sec</label>
                <input :value="Math.round((config.tx_command_retry_timeout_ms || 180000) / 1000)" @input="config.tx_command_retry_timeout_ms = Number(($event.target as HTMLInputElement).value || 180) * 1000" type="number" min="5" max="3600" class="glass-input h-9" title="How long the gateway retries an unconfirmed input-control command." />
              </div>
            </div>

            <div v-else class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 rounded border border-slate-800 bg-slate-950/20 p-3">
              <div class="col-span-2 font-semibold text-slate-100">Remote failsafe</div>
              <label class="self-center text-right font-semibold text-slate-300">Action</label>
              <select v-model="configRxFailsafeMode" class="glass-input h-9 appearance-none" title="What this remote does after its controller has been silent for the failsafe timeout.">
                <option value="hold_last">Hold last</option>
                <option value="force_off">Force off</option>
                <option value="force_on">Force on</option>
              </select>
              <label class="self-center text-right font-semibold text-slate-300">Timeout sec</label>
              <input :value="Math.round((config.rx_failsafe_timeout_ms || 180000) / 1000)" @input="config.rx_failsafe_timeout_ms = Number(($event.target as HTMLInputElement).value || 180) * 1000" type="number" min="5" max="3600" class="glass-input h-9" title="How long this remote waits for its controller before applying its failsafe action." />
            </div>
          </template>
        </div>

        <div v-if="computedSettingsTab === 'network'" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
          <template v-if="config">
            <div class="col-span-2 rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
              <div class="flex flex-wrap items-center justify-between gap-2">
                <span>WiFi passwords are redacted on fetch. Leave password fields blank to keep the stored value.</span>
                <button @click="emit('scan-wifi')" :disabled="!computedSelectedPort || headerState.isFlashing || headerState.isLoadingInfo || headerState.serialAdminBusy || wifiState.isWifiScanning" class="glass-input h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                  {{ wifiState.isWifiScanning ? 'Scanning...' : 'Scan WiFi' }}
                </button>
              </div>
            </div>
            <label class="self-center text-right font-semibold text-slate-300">WiFi SSID</label>
            <div class="flex gap-2">
              <input v-model="configWifiStaSsid" class="glass-input h-9 flex-1" placeholder="Leave blank for no WiFi" list="settings-wifi-networks" />
              <datalist id="settings-wifi-networks">
                <option v-for="network in wifiState.settingsWifiNetworks" :key="`${network.ssid}-${network.bssid}`" :value="network.ssid">
                  {{ network.ssid }} · {{ wifiSignalLabel(network.rssi) }} · ch {{ network.channel }}{{ network.secure ? ' · secured' : ' · open' }}
                </option>
              </datalist>
            </div>
            <label class="self-center text-right font-semibold text-slate-300">New WiFi password</label>
            <div class="flex flex-col gap-1">
              <div class="flex gap-2">
                <input v-model="configWifiStaPassword" :type="computedShowSerialWifiPassword ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Blank keeps existing password" />
                <button @click="computedShowSerialWifiPassword = !computedShowSerialWifiPassword" class="glass-input h-9 px-3 hover:bg-slate-700/70" type="button">
                  {{ computedShowSerialWifiPassword ? 'Hide' : 'Show' }}
                </button>
              </div>
              <span v-if="secretState.isWifiStaPasswordConfigured" class="text-[10px] text-cyan-400 font-medium">✓ Currently configured on device</span>
            </div>
            <label class="self-center text-right font-semibold text-slate-300">WiFi Enabled</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configWifiAdminEnabled" type="checkbox" />
              Enabled
            </label>
            <label class="self-center text-right font-semibold text-slate-300">Hostname</label>
            <input v-model="configLanHostname" class="glass-input h-9" placeholder="Blank uses lrs-chipid" />
            <label class="self-center text-right font-semibold text-slate-300">Fallback AP</label>
            <select v-model="configWifiApFallbackPolicy" class="glass-input h-9 appearance-none">
              <option value="fallback_on_disconnect">Enable when disconnected</option>
              <option value="secure_sta_only">Keep disabled</option>
            </select>
            <label class="self-center text-right font-semibold text-slate-300">Soft AP</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configApAlwaysOn" type="checkbox" />
              Always on while disconnected
            </label>

            <label class="self-center text-right font-semibold text-slate-300">TX power</label>
            <input v-model.number="configWifiTxPowerDbm" type="number" min="0" max="20.5" step="0.25" class="glass-input h-9" />
            <label class="self-center text-right font-semibold text-slate-300">Channel</label>
            <input v-model.number="configWifiChannelOverride" type="number" min="0" max="13" class="glass-input h-9" />
            <label class="self-center text-right font-semibold text-slate-300">WiFi sleep</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configWifiSleepEnabled" type="checkbox" />
              Allow sleep
            </label>
            <label class="self-center text-right font-semibold text-slate-300">Static IP</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configWifiStaticIpEnabled" type="checkbox" />
              Enabled
            </label>
            <label class="self-center text-right font-semibold text-slate-300">IP address</label>
            <input v-model="configWifiStaticIp" class="glass-input h-9" placeholder="192.168.1.50" />
            <label class="self-center text-right font-semibold text-slate-300">Gateway</label>
            <input v-model="configWifiStaticGateway" class="glass-input h-9" placeholder="192.168.1.1" />
            <label class="self-center text-right font-semibold text-slate-300">Subnet</label>
            <input v-model="configWifiStaticSubnet" class="glass-input h-9" placeholder="255.255.255.0" />
          </template>
        </div>

        <div v-if="computedSettingsTab === 'mqtt'" class="flex flex-col gap-3 text-xs">
          <template v-if="config">
            <!-- Remote devices never connect directly to an MQTT broker. -->
            <div v-if="!config.role_tx" class="flex flex-col gap-3">
              <div class="rounded border border-cyan-500/20 bg-cyan-950/15 p-3 text-cyan-200 leading-relaxed shadow-[inset_0_1px_0_rgba(6,182,212,0.15)] select-text">
                <div class="font-bold text-sm text-cyan-100 mb-1 flex items-center gap-1.5">
                  <span class="inline-block w-2.5 h-2.5 rounded-full bg-cyan-500 shadow-[0_0_8px_rgba(6,182,212,0.6)]"></span>
                  🌐 Remote MQTT Routing Bridge Active
                </div>
                This device is configured with the <span class="font-bold text-cyan-100">Remote</span> role.
                <p class="mt-2 text-slate-300">
                  Remote devices do not run local MQTT clients to conserve power, memory, and local WiFi network capacity. Instead, they communicate securely over LoRa to your central Gateway.
                </p>
                <p class="mt-2 text-slate-300">
                  The Gateway automatically connects to the MQTT broker and bridges all sensor telemetry and command topics to the broker on behalf of this remote device.
                </p>
                <p class="mt-3 text-cyan-300 font-semibold border-t border-cyan-500/20 pt-2 flex items-center gap-2">
                  💡 Remote configuration (like WiFi provisioning, sensor toggles, or reboots) happens over LoRa from the Gateway's MQTT peer command interface.
                </p>
              </div>

            </div>

            <!-- If it's a gateway device -->
            <div v-else class="flex flex-col gap-3">
              <div v-if="configGatewayControlMode === 'input'" class="rounded border border-amber-500/30 bg-amber-500/10 p-2.5 text-amber-200 select-text mb-2">
                MQTT relay commands are disabled because Gateway input is the selected control source. Change the selection on the Control tab to use MQTT commands.
              </div>

              <div class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2">
                <label class="self-center text-right font-semibold text-slate-300">MQTT host</label>
                <input v-model="configMqttHost" class="glass-input h-9" placeholder="venus.local" />
                <label class="self-center text-right font-semibold text-slate-300">MQTT port</label>
                <input v-model.number="configMqttPort" type="number" min="1" max="65535" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">Topic root</label>
                <input v-model="configMqttTopicRoot" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">MQTT client</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="configMqttClientEnabled" type="checkbox" :disabled="configGatewayControlMode === 'mqtt'" />
                  Enabled
                </label>
                <label class="self-center text-right font-semibold text-slate-300">MQTT control</label>
                <div class="text-slate-300">{{ configGatewayControlMode === 'mqtt' ? 'Enabled by the Control tab' : 'Disabled by the Control tab' }}</div>
                <label class="self-center text-right font-semibold text-slate-300">Remote refresh</label>
                <div class="flex flex-col gap-1">
                  <label class="flex items-center gap-2 text-slate-300">
                    <input v-model="configRemoteRefreshEnabled" type="checkbox" />
                    Refresh remote operational state
                  </label>
                  <div class="text-[10px] text-slate-500 leading-normal">Low-priority LoRa polling for confirmed remote relay and input state. Paused while control is active.</div>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">Fleet cycle sec</label>
                <input :value="Math.round((config.remote_refresh_cycle_ms || 60000) / 1000)" @input="config.remote_refresh_cycle_ms = Number(($event.target as HTMLInputElement).value || 60) * 1000" type="number" min="60" max="3600" class="glass-input h-9" :disabled="!configRemoteRefreshEnabled" title="Target time for one low-priority operational refresh cycle across the configured fleet." />
                <label class="self-center text-right font-semibold text-slate-300">MQTT user</label>
                <input v-model="configMqttUser" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">New MQTT password</label>
                <div class="flex flex-col gap-1">
                  <div class="flex gap-2">
                    <input v-model="configMqttPassword" :type="computedShowSerialMqttPassword ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Blank keeps existing password" />
                    <button @click="computedShowSerialMqttPassword = !computedShowSerialMqttPassword" class="glass-input h-9 px-3 hover:bg-slate-700/70" type="button">
                      {{ computedShowSerialMqttPassword ? 'Hide' : 'Show' }}
                    </button>
                  </div>
                  <span v-if="secretState.isMqttPasswordConfigured" class="text-[10px] text-cyan-400 font-medium">✓ Currently configured on device</span>
                </div>
              </div>
            </div>
          </template>
        </div>

        <div v-if="computedSettingsTab === 'sensors'" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
          <template v-if="config">
            <div class="col-span-2 rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
              Sensor wiring is fixed in firmware for this board: DS18B20 uses the configured one-wire pin, and the 4-20 mA tank input uses A0 with the 3553 mV board calibration.
            </div>
            <label class="self-center text-right font-semibold text-slate-300">DS18B20</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configSensorTempEnabled" type="checkbox" />
              Temperature sensor enabled
            </label>
            <label class="self-center text-right font-semibold text-slate-300">Tank level</label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="configSensorTankEnabled" type="checkbox" />
              4-20 mA tank sensor enabled
            </label>
            <div class="col-start-2 text-slate-500">
              5000 mm water range, 120 ohm sense resistor, sampled every {{ config.sensor_tank_interval_s }}s.
            </div>
          </template>
        </div>

        <div v-if="computedSettingsTab === 'system'" class="flex flex-col gap-3 text-xs">
          <div class="rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
            Save applies the loaded settings over USB serial admin. Reboot clears cached live status. Factory reset clears loaded settings in Flasher because the device reboots into a new/default configuration.
          </div>
          <div class="rounded border border-amber-500/30 bg-amber-500/10 p-3 text-amber-100">
            Keep fleet key preserves pairing material. Keep WiFi preserves STA credentials. Clearing either one returns that part of the device to factory-default setup.
          </div>
          <div class="flex flex-wrap items-center gap-3">
            <button @click="emit('save-config')" :disabled="headerState.serialAdminDisabled || headerState.isSerialAdminSaving || !config" class="primary-btn h-9 px-4 text-xs font-bold disabled:opacity-60">
              {{ headerState.isSerialAdminSaving ? 'Saving...' : 'Save config' }}
            </button>
            <button @click="emit('reboot-device')" :disabled="headerState.serialAdminDisabled" class="glass-input h-9 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
              Reboot
            </button>
            <label class="flex items-center gap-2 text-slate-400">
              <input v-model="computedSerialFactoryKeepFleet" type="checkbox" />
              Keep fleet key
            </label>
            <label class="flex items-center gap-2 text-slate-400">
              <input v-model="computedSerialFactoryKeepWifi" type="checkbox" />
              Keep WiFi
            </label>
            <button @click="emit('factory-reset')" :disabled="headerState.serialAdminDisabled" class="glass-input h-9 px-4 hover:bg-red-500/15 text-xs font-bold text-red-200 disabled:opacity-60">
              Factory reset
            </button>
          </div>
        </div>

        <!-- Remote Config Help Reference Tab -->
        <div v-if="computedSettingsTab === 'remote'" class="flex flex-col gap-4 text-xs select-text">
          <div class="rounded border border-cyan-500/20 bg-cyan-950/15 p-3 text-cyan-200 shadow-[inset_0_1px_0_rgba(6,182,212,0.15)]">
            <div class="font-semibold text-sm mb-1 text-cyan-100 flex items-center gap-2">
              <span class="inline-block w-2 h-2 rounded-full bg-cyan-400 animate-pulse"></span>
              Operator Reference: Remote Configuration & Telemetry Protocol
            </div>
            This pane maps the firmware's multi-interface parameters. Use the tabs below to explore the Serial, MQTT, and LoRa capabilities built into your fleet devices.
          </div>

          <!-- Sub-Tab Selector -->
          <div class="flex border-b border-slate-800 bg-slate-900/20 p-0.5 rounded-t-lg">
            <button
              @click="computedRemoteSubTab = 'serial'"
              type="button"
              :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', computedRemoteSubTab === 'serial' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
            >
              <span class="font-mono text-[10px]">🔌</span> Serial Admin API
            </button>
            <button
              @click="computedRemoteSubTab = 'mqtt'"
              type="button"
              :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', computedRemoteSubTab === 'mqtt' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
            >
              <span class="font-mono text-[10px]">🌐</span> MQTT Bridge
            </button>
            <button
              @click="computedRemoteSubTab = 'lora'"
              type="button"
              :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', computedRemoteSubTab === 'lora' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
            >
              <span class="font-mono text-[10px]">📡</span> LoRa OTA Protocol
            </button>
          </div>

          <!-- Content Cards -->
          <div class="flex flex-col gap-4">

            <!-- ================== SERIAL PANEL ================== -->
            <div v-if="computedRemoteSubTab === 'serial'" class="flex flex-col gap-3">
              <div class="rounded border border-slate-800 bg-slate-950/20 p-3 text-slate-300">
                <div class="font-semibold text-slate-100 mb-1">🔌 Local USB Admin Interface</div>
                Devices running this firmware listen on the hardware USB UART (**115200 Baud, 8N1**). All commands are JSON payloads transmitted on lines prefixed with <code class="font-mono text-cyan-400 font-bold bg-slate-950/50 px-1 rounded">LRS:</code> and terminated with a newline (<code class="font-mono text-slate-400">\n</code>).
              </div>

              <div class="grid gap-3 md:grid-cols-2">
                <!-- Read-Only & Telemetry Commands -->
                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1 flex items-center justify-between">
                    <span>Read-Only & Telemetry Commands</span>
                    <span class="text-[9px] bg-slate-800 text-slate-400 px-1.5 py-0.5 rounded font-mono">NO PASSWORD REQUIRED</span>
                  </div>

                  <div class="flex flex-col gap-2.5">
                    <div class="flex flex-col gap-1">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"hello"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;hello&quot;}', label: 'hello command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Verifies serial communications and fetches API limits.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"identity"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;identity&quot;}', label: 'identity command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Returns core device hardware serial, MAC, assigned Addresses, mode, and active firmware version.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"status"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;status&quot;}', label: 'status command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Fetches live state telemetry: free heap blocks, WiFi connection parameters, MQTT status, signal RSSI/downlink level, temperature, and analog tank level details.</span>
                    </div>
                  </div>
                </div>

                <!-- Protected Admin Settings Commands -->
                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1 flex items-center justify-between">
                    <span>Protected Admin Settings</span>
                    <span class="text-[9px] bg-red-950/30 text-rose-300 px-1.5 py-0.5 rounded font-mono">REQUIRES PASSWORD</span>
                  </div>

                  <div class="flex flex-col gap-2.5">
                    <div class="flex flex-col gap-1">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"get_config"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;get_config&quot;,&quot;password&quot;:&quot;admin_pwd&quot;,&quot;include_secrets&quot;:true}', label: 'get_config command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Reads currently stored configuration store. Set <code class="font-mono text-[10px] text-slate-300">include_secrets</code> to true to request raw WiFi credentials and keys.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"set_config"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;set_config&quot;,&quot;password&quot;:&quot;admin_pwd&quot;,&quot;config&quot;:{&quot;wifi_sta_ssid&quot;:&quot;YourSSID&quot;,&quot;wifi_sta_password&quot;:&quot;Password&quot;}}', label: 'set_config command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Applies a configuration patch. Supports changing WiFi client setups, MQTT properties, sensor configs, timing parameters, and addresses.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <div class="flex justify-between items-center">
                        <span class="font-mono font-bold text-cyan-300">"configure_gateway"</span>
                        <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;configure_gateway&quot;,&quot;password&quot;:&quot;admin_pwd&quot;,&quot;fleet_passphrase&quot;:&quot;YourKey&quot;}', label: 'configure_gateway command' })" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                      </div>
                      <span class="text-slate-400">Provisions a factory default device into an operational Gateway, updating security key material and starting administration mode.</span>
                    </div>
                  </div>
                </div>
              </div>

              <!-- Fleet Coordination Commands Card -->
              <div class="glass-card p-3 flex flex-col gap-2">
                <div class="font-bold text-slate-200 border-b border-slate-800 pb-1">📡 Gateway-Only Fleet Coordination Commands</div>
                <div class="grid gap-3 sm:grid-cols-2 lg:grid-cols-3 mt-1 text-slate-400">
                  <div class="bg-slate-950/20 border border-slate-800 p-2.5 rounded flex flex-col gap-1">
                    <div class="flex justify-between items-center">
                      <span class="font-mono text-cyan-300 font-semibold">LoRa Inventory Scan</span>
                      <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;start_lora_inventory&quot;,&quot;password&quot;:&quot;admin_pwd&quot;,&quot;start_addr&quot;:1,&quot;end_addr&quot;:12}', label: 'start_lora_inventory' })" class="text-[10px] text-slate-500 hover:text-cyan-300">Copy</button>
                    </div>
                    Commands gateway to query remote devices via LoRa:
                    <code class="text-[10px] text-slate-500 mt-1">cmd: "start_lora_inventory"<br>cmd: "lora_inventory_status"<br>cmd: "refresh_lora_peer"<br>cmd: "lora_inventory_peer"</code>
                  </div>

                  <div class="bg-slate-950/20 border border-slate-800 p-2.5 rounded flex flex-col gap-1">
                    <div class="flex justify-between items-center">
                      <span class="font-mono text-cyan-300 font-semibold">Remote OTA Firmware Pull</span>
                      <button @click="emit('copy-payload', { text: 'LRS:{&quot;cmd&quot;:&quot;remote_ota_pull&quot;,&quot;password&quot;:&quot;admin_pwd&quot;,&quot;addr&quot;:1,&quot;url&quot;:&quot;http://192.168.1.100/fw.bin&quot;,&quot;sha256&quot;:&quot;YOUR_SHA256_HEX&quot;}', label: 'remote_ota_pull' })" class="text-[10px] text-slate-500 hover:text-cyan-300">Copy</button>
                    </div>
                    Signals a remote device over LoRa carrying an HTTP URL and SHA256 checksum to trigger it to download a firmware update over WiFi.
                  </div>
                </div>
              </div>
            </div>

            <!-- ================== MQTT PANEL ================== -->
            <div v-if="computedRemoteSubTab === 'mqtt'" class="flex flex-col gap-3">
              <div class="rounded border border-slate-800 bg-slate-950/20 p-3 text-slate-300">
                <div class="font-semibold text-slate-100 mb-1">🌐 MQTT Bridge Protocol</div>
                Operational gateways with <code class="font-mono bg-slate-950/50 px-1 rounded text-cyan-400">mqtt_client_enabled</code> configured will publish live local telemetry and all received LoRa remote reports, while listening on local and peer-specific control channels.
              </div>

              <!-- MQTT Topic Structure Details -->
              <div class="grid gap-3 md:grid-cols-2">
                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1 flex items-center justify-between">
                    <span>Gateway MQTT Topics (Subscriptions)</span>
                    <span class="text-[9px] bg-slate-800 text-slate-400 px-1.5 py-0.5 rounded font-mono">WRITE CONTROL</span>
                  </div>

                  <div class="flex flex-col gap-3 mt-1">
                    <div class="flex flex-col gap-1">
                      <span class="font-semibold text-slate-300">Toggle Local Relay</span>
                      <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/set/relay</code>
                      <span class="text-slate-400">Payload: <code class="text-slate-200">1</code> (ON) or <code class="text-slate-200">0</code> (OFF). Publish non-retained.</span>
                    </div>
                  </div>
                </div>

                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1 flex items-center justify-between">
                    <span>Peer MQTT Downlinks (Secured LoRa Forwarding)</span>
                    <span class="text-[9px] bg-sky-950/30 text-sky-300 px-1.5 py-0.5 rounded font-mono">FORWARDED OVER LORA</span>
                  </div>

                  <div class="flex flex-col gap-3 mt-1">
                    <div class="flex flex-col gap-1">
                      <span class="font-semibold text-slate-300">Control Remote Peer Relay</span>
                      <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/set/relay</code>
                      <span class="text-slate-400">Payload: <code class="text-slate-200">1</code> (ON) or <code class="text-slate-200">0</code> (OFF). Publish non-retained.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <span class="font-semibold text-slate-300">Force Peer Polling (Immediate)</span>
                      <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/poll_now</code>
                      <span class="text-slate-400">Payload: any. Forces the gateway to emit a secured LoRa status query reports probe.</span>
                    </div>

                    <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                      <span class="font-semibold text-slate-300">Peer WiFi Hardware Power Setup</span>
                      <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/wifi</code>
                      <span class="text-slate-400">Payload: <code class="text-slate-200">1</code> (Enable WiFi chip) or <code class="text-slate-200">0</code> (Power off WiFi to conserve energy).</span>
                    </div>
                  </div>
                </div>
              </div>

              <!-- Telemetry Publishing Details -->
              <div class="glass-card p-3 flex flex-col gap-2">
                <div class="font-bold text-slate-200 border-b border-slate-800 pb-1">📈 Telemetry & Status Publishing Map</div>
                <div class="text-slate-400 mt-1">
                  The gateway automatically publishes status and payload telemetry to these topics:
                  <div class="grid gap-3 sm:grid-cols-2 mt-2">
                    <div>
                      <div class="font-bold text-slate-300 text-[11px] mb-1">Gateway Telemetry (Local):</div>
                      <ul class="list-disc pl-4 space-y-1 text-slate-400 font-mono text-[10px]">
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/relay</span>: Gateway relay state (1/0)</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/input</span>: Gateway digital input state</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/sensor/&lt;kind&gt;/&lt;instance&gt;/value</span>: Normalized reading value</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/sensor/&lt;kind&gt;/&lt;instance&gt;/state</span>: Sensor status state</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/ota_status</span>: Retained OTA status (<code class="text-cyan-400">downloading</code>, <code class="text-cyan-400">failed:&lt;code&gt;</code>, <code class="text-cyan-400">rebooting</code>)</li>
                      </ul>
                    </div>
                    <div>
                      <div class="font-bold text-slate-300 text-[11px] mb-1">Remote Peer Telemetry (Forwarded):</div>
                      <ul class="list-disc pl-4 space-y-1 text-slate-400 font-mono text-[10px]">
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/relay</span>: Remote device relay state (read-only)</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/input</span>: Remote device dry contact state</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/ack_state</span>: OTA ACK status (<code class="text-emerald-400">Ok</code>, <code class="text-amber-400">Pending</code>, <code class="text-rose-400">Timeout</code>)</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/uplink_rssi_dbm</span>: Reception signal level</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/wifi_rssi_dbm</span>: Remote WiFi STA RSSI (dBm)</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/sensor/&lt;kind&gt;/&lt;instance&gt;/value</span>: Peer sensor value</li>
                        <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peers/&lt;NN_lrs-peer_chipid&gt;/sensor/&lt;kind&gt;/&lt;instance&gt;/state</span>: Peer sensor state</li>
                      </ul>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- ================== LORA PANEL ================== -->
            <div v-if="computedRemoteSubTab === 'lora'" class="flex flex-col gap-3">
              <div class="rounded border border-slate-800 bg-slate-950/20 p-3 text-slate-300">
                <div class="font-semibold text-slate-100 mb-1">📡 LoRa Over-the-Air Secured Protocol</div>
                Devices communicate over the air using highly robust, low-bandwidth sub-GHz LoRa modulation. All payloads are encrypted and signed using dynamic session keys derived from the shared <code class="font-mono bg-slate-950/50 px-1 rounded text-cyan-400">fleet_passphrase</code> via **AES-128 and SHA-256**, protecting the network against spoofing and replay attacks.
              </div>

              <div class="grid gap-3 md:grid-cols-2">
                <!-- Diagnostic Reports (Uplink) -->
                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1">📈 Secured Diagnostic Reports (Uplink)</div>
                  <div class="flex flex-col gap-3 mt-1 text-slate-400">
                    <div>
                      <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::Heartbeat / PollResponse</span>
                      <p class="mt-0.5">Emitted by remote devices for operational state such as relay, input, and sensor updates. Memory diagnostics stay out of the normal control path unless explicitly polled.</p>
                    </div>
                    <div class="border-t border-slate-800/50 pt-2">
                      <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::MaintenanceStatus</span>
                      <p class="mt-0.5">Delivered in high-density segmented paging packets, pulling deep operational metrics to the gateway:</p>
                      <ul class="list-disc pl-4 mt-1 space-y-1 text-slate-400 font-mono text-[10px]">
                        <li><span class="text-slate-300">Version Page:</span> Major, minor, patch, and build version code</li>
                        <li><span class="text-slate-300">Sensors Page:</span> 4-20mA current (mA), voltage (mV), and calibrated water level measurement (mm)</li>
                        <li><span class="text-slate-300">Debug Page:</span> Device uptime in minutes, free heap memory blocks, and free block fragmentation percentage</li>
                      </ul>
                    </div>
                  </div>
                </div>

                <!-- Remote Control Downlinks -->
                <div class="glass-card p-3 flex flex-col gap-2">
                  <div class="font-bold text-slate-200 border-b border-slate-800 pb-1">⚙️ Secured Remote Configuration (Downlink)</div>
                  <div class="flex flex-col gap-3 mt-1 text-slate-400">
                    <div>
                      <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::WifiProvision</span>
                      <p class="mt-0.5">The gateway broadcasts chunked network credentials packets over the air. Remote devices assemble the chunks in memory, verify the full string FNV-1a hash, and permanently commit the SSID & Password configuration store.</p>
                    </div>
                    <div class="border-t border-slate-800/50 pt-2">
                      <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::WifiControl</span>
                      <p class="mt-0.5">Directly manages remote WiFi chip state. Allows toggling WiFi on or off to optimize deep sleep profiles or query current client connection IP and RSSI levels.</p>
                    </div>
                    <div class="border-t border-slate-800/50 pt-2">
                      <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::OtaPullControl</span>
                      <p class="mt-0.5">Tails remote devices to fetch new firmware binaries from a local staging web server over WiFi. Payload contains the temporary update URL and SHA-256 file signature for local validation.</p>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <p v-if="!adminStatus && !config && !headerState.isSerialAdminLoading && computedSettingsTab !== 'remote'" class="mt-3 text-xs text-slate-500">
          {{ settingsEmptyMessage }}
        </p>
      </div>

      <!-- Sticky action footer for saving settings globally -->
      <div v-if="config && computedSettingsTab !== 'remote'" class="flex shrink-0 items-center justify-between border-t border-slate-800 bg-slate-900/60 p-3 text-xs">
        <span class="text-slate-400">
          💡 Changes must be saved to apply to the device.
        </span>
        <div class="flex items-center gap-2">
          <button
            @click="emit('save-config')"
            :disabled="headerState.serialAdminDisabled || headerState.isSerialAdminSaving || !config"
            class="primary-btn m-0 h-9 px-4 text-xs font-bold disabled:opacity-60 flex items-center justify-center gap-1.5"
          >
            <svg v-if="headerState.isSerialAdminSaving" class="animate-spin h-3.5 w-3.5 text-white" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
              <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
              <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
            </svg>
            <span>{{ headerState.isSerialAdminSaving ? 'Saving...' : 'Save config' }}</span>
          </button>
          <button
            @click="emit('reboot-device')"
            :disabled="headerState.serialAdminDisabled || headerState.isSerialAdminSaving || !config"
            class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60"
          >
            Reboot
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
