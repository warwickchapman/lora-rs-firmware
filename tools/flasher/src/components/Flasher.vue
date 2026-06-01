<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';

type ActiveMode = 'pair' | 'serial' | 'network' | 'monitor' | 'settings';

const activeMode = defineModel<ActiveMode>('activeMode', { default: 'pair' });

type SettingsTab = 'general' | 'network' | 'mqtt' | 'sensors' | 'remote' | 'system';
type SerialJobPriority = 'user' | 'background';
type FleetGatewayFlashPhase = 'idle' | 'flashing' | 'rebooting' | 'waiting' | 'updated' | 'failed';

interface SerialJobOptions {
  label?: string;
  priority?: SerialJobPriority;
  dropIfBusy?: boolean;
}

interface ConfirmDialogState {
  message: string;
  confirmText: string;
  cancelText: string;
  danger: boolean;
  resolve: ((confirmed: boolean) => void) | null;
}

interface SerialPort {
  port_name: string;
  description: string | null;
  score: number;
}

interface LogEvent {
  port: string;
  message: string;
}

interface MonitorEvent {
  port: string;
  line: string;
}

interface PortsChangedEvent {
  ports: string[];
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

interface FirmwareServerInfo {
  filename: string;
  sha256: string;
  size_bytes: number;
  port: number;
  urls: string[];
}

interface EasyPairDevice {
  chip_id_hex: string;
  current_address: number;
  assigned_address: number;
  role_tx: boolean;
  fw_major: number;
  fw_minor: number;
  fw_patch: number;
  fw_build?: number;
  rssi: number;
  selected: boolean;
  address_conflict: boolean;
  state: string;
}

interface EasyPairStatus {
  ok: boolean;
  cmd: string;
  session?: {
    active: boolean;
    state: string;
    estimated_count: number;
    discovered_count: number;
    selected_count: number;
    verified_count: number;
    failed_count: number;
    conflict_count: number;
  };
  devices?: EasyPairDevice[];
}

interface NetworkInterface {
  ip: string;
  netmask: string;
}

interface LoraInventoryDevice {
  address: number;
  chip_id?: string;
  fw_version?: string;
  fw_build?: number;
  role?: string;
  mode?: string;
  wifi_enabled_known?: boolean;
  wifi_enabled?: boolean;
  wifi_connected_known?: boolean;
  wifi_connected?: boolean;
  ip?: string;
  mqtt_known?: boolean;
  mqtt_enabled?: boolean;
  mqtt_connected?: boolean;
  power_save_listen_only?: boolean;
  power_save_active?: boolean;
  maintenance_debug_known?: boolean;
  heap_free?: number;
  heap_max_block?: number;
  heap_frag_pct?: number;
  relay_state?: number;
  relay_feedback?: number;
  input_state?: number;
  input_feedback?: number;
  temp_enabled?: boolean;
  temp_valid?: boolean;
  temp_c?: number;
  tank_enabled?: boolean;
  tank_valid?: boolean;
  tank_status?: string;
  tank_depth_mm?: number;
  tank_current_ma?: number;
  tank_current_centi_ma?: number;
  tank_voltage_mv?: number;
  debug_uptime_ms?: number;
  rssi?: number;
  downlink_rssi_known?: boolean;
  downlink_rssi?: number;
  uptime_ms?: number;
  age_ms?: number;
  poll_pending?: boolean;
  ota_eligible?: boolean;
  ota_reason?: string;
  selected?: boolean;
  row_state?: 'ota_pending' | 'ota_downloading' | 'ota_apply_wait' | 'ota_retrying' | 'ota_rebooted' | 'ota_updated' | 'ota_no_reboot' | 'unexpected_reboot' | 'ota_queued' | 'ota_failed';
  row_state_until_ms?: number;
  pending_power_save_listen_only?: boolean;
  pending_power_save_tx_ms?: number;
  wifi_pending_offline?: boolean;
  power_save_deferred?: boolean;
}

interface LoraInventoryStatus {
  ok: boolean;
  cmd: string;
  scan?: {
    active: boolean;
    start_address: number;
    end_address: number;
    next_address: number;
    sent: number;
    now_ms: number;
  };
  devices?: LoraInventoryDevice[];
}

interface WifiNetwork {
  ssid: string;
  rssi: number;
  channel: number;
  bssid: string;
  secure: boolean;
}

interface WifiScanResponse {
  ok: boolean;
  cmd: string;
  networks?: WifiNetwork[];
}

interface SerialAdminStatus {
  ok: boolean;
  cmd: string;
  fw_version: string;
  chip_id: string;
  serial: string;
  uptime_ms: number;
  heap_free: number;
  heap_frag_pct: number;
  heap_max_block: number;
  mode: string;
  role: string;
  role_tx: boolean;
  local_address: number;
  remote_address: number;
  commissioned: boolean;
  fleet_passphrase_default?: boolean;
  wifi?: {
    admin_enabled: boolean;
    sta_ssid: string;
    sta_connected: boolean;
    status: string;
    ip: string;
    rssi: number;
    ap_active: boolean;
  };
  mqtt?: {
    client_enabled: boolean;
    control_enabled: boolean;
    host: string;
    port?: number;
    topic_root: string;
  };
  link_state?: string;
  relay_state?: number;
  relay_feedback?: number;
  input_state?: number;
  peer_count?: number;
  local_temp_valid?: boolean;
  local_temp_c?: number;
  local_tank_enabled?: boolean;
  local_tank_valid?: boolean;
  local_tank_status?: string;
  local_tank_depth_mm?: number;
  local_tank_current_ma?: number;
  local_tank_current_centi_ma?: number;
  local_tank_voltage_mv?: number;
}

interface SerialAdminConfig {
  mode: string;
  commissioned?: boolean;
  role_tx: boolean;
  local_address: number;
  remote_address: number;
  paired_target_addresses?: number[];
  allowed_controller_addresses?: number[];
  known_peer_addresses?: number[];
  lora_tx_power?: number;
  lora_spreading_factor?: number;
  lora_bandwidth_hz?: number;
  lora_coding_rate?: number;
  heartbeat_ms?: number;
  heartbeat_enabled?: boolean;
  ack_timeout_ms?: number;
  mqtt_remote_retry_timeout_ms?: number;
  tx_mqtt_remote_polling_enabled?: boolean;
  tx_mqtt_remote_default_poll_interval_ms?: number;
  maintenance_debug_telemetry_enabled?: boolean;
  rx_push_on_change_enabled?: boolean;
  rx_push_min_interval_ms?: number;
  input_control_paired_lora_enabled?: boolean;
  tx_command_retry_timeout_ms?: number;
  rx_failsafe_mode?: string;
  rx_failsafe_timeout_ms?: number;
  wifi_sta_ssid: string;
  wifi_sta_password: string;
  lan_hostname?: string;
  ap_always_on?: boolean;
  wifi_phy_mode?: string;
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

interface SerialDeviceState {
  deviceInfo: DeviceInfo | null;
  adminSupported: boolean;
  status: SerialAdminStatus | null;
  config: SerialAdminConfig | null;
  adminPassword: string;
  wifiNetworks: WifiNetwork[];
  wifiScanned: boolean;
  wifiSsid: string;
  gatewayWifiReadySsid: string;
  gatewayWifiReadyIp: string;
  flashLogs: string[];
  monitorLogs: string[];

  // Phase 2 Operational State Fields
  isFlashing: boolean;
  flashProgress: number;
  flashStatus: string;
  flashPhase: 'idle' | 'erase' | 'write' | 'verify';
  isResetting: boolean;
  resetStatus: string;
}

type RegionCode = 'ZA' | 'EU' | 'US';

const READABLE_KEY_CONSONANTS = 'bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS = 'aeiou';
const NETWORK_FIRMWARE_PATH = '/firmware.bin';
const LRS_REMOTE_SCAN_CAP = 12;
const DISCONNECTED_PORT_CACHE_GRACE_MS = 120000;
const SETTINGS_TABS: Array<{ key: SettingsTab; label: string }> = [
  { key: 'general', label: 'General' },
  { key: 'network', label: 'Network' },
  { key: 'mqtt', label: 'MQTT' },
  { key: 'sensors', label: 'Sensors' },
  { key: 'system', label: 'System' },
  { key: 'remote', label: 'Reference' },
];

const ports = ref<SerialPort[]>([]);
const flashSelectedPort = ref('');
const gatewaySelectedPort = ref('');
const monitorSelectedPort = ref('');
const settingsSelectedPort = ref('');
const selectedPort = computed<string>({
  get() {
    if (activeMode.value === 'pair') return gatewaySelectedPort.value;
    if (activeMode.value === 'network') return gatewaySelectedPort.value;
    if (activeMode.value === 'monitor') return monitorSelectedPort.value;
    if (activeMode.value === 'settings') return settingsSelectedPort.value;
    return flashSelectedPort.value;
  },
  set(port) {
    if (activeMode.value === 'pair' || activeMode.value === 'network') {
      gatewaySelectedPort.value = port;
    } else if (activeMode.value === 'monitor') {
      monitorSelectedPort.value = port;
    } else if (activeMode.value === 'settings') {
      settingsSelectedPort.value = port;
    } else {
      flashSelectedPort.value = port;
    }
  }
});
const firmwareVersions = ref<string[]>(['__local_browse__']);
const selectedVersion = ref('');
const selectedLocalPath = ref('');
const flasherAppVersion = ref('');
const region = ref<RegionCode>('ZA');
const isFlashing = ref(false);
const isBulkFlashing = ref(false);
const isBulkResetting = ref(false);
const bulkSelectedPorts = ref<string[]>([]);
const bulkMode = ref(false);
const bulkShowLogsByPort = ref<Record<string, boolean>>({});
const bulkLogRefs = ref<Record<string, HTMLElement>>({});
const isMonitoring = ref(false);
const isNetworkUdpMonitoring = ref(false);
const isFirmwareServerStarting = ref(false);

const remoteOtaBusyAddress = ref<number | null>(null);
const otaQueue = ref<LoraInventoryDevice[]>([]);
const remoteUdpBusyAddress = ref<number | null>(null);
const firmwareServerInfo = ref<FirmwareServerInfo | null>(null);
const serialLogs = ref<string[]>([]);
const networkLogs = ref<string[]>([]);
const filteredNetworkLogs = computed(() => {
  const targetLabel = networkUdpTarget.value;
  if (!targetLabel) {
    return networkLogs.value.map(line => formatUdpLogLine(line));
  }
  
  const dev = loraInventory.value.find(d => fleetDeviceUdpLabel(d) === targetLabel);
  const targetIp = dev?.ip;
  const targetAddress = dev?.address;

  return networkLogs.value
    .filter(line => {
      const ipMatch = line.match(/^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\s+/);
      if (!ipMatch) {
        if (line.includes('addr ') || line.includes('follow-up ')) {
          const addrMatch = line.match(/(?:addr|follow-up)\s+(\d+)/);
          if (addrMatch) {
            const addr = Number(addrMatch[1]);
            if (targetAddress !== undefined && addr !== targetAddress) {
              return false;
            }
          }
        }
        return true;
      }
      return targetIp && ipMatch[1] === targetIp;
    })
    .map(line => formatUdpLogLine(line));
});

function formatUdpLogLine(line: string): string {
  if (!line) return '';
  const ipMatch = line.match(/^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\s+(.*)$/);
  if (!ipMatch) return line;

  const ip = ipMatch[1];
  const rest = ipMatch[2];

  const dev = loraInventory.value.find(d => d.ip === ip);
  if (dev) {
    const chip = String(dev.chip_id || '').trim().replace(/^0x/i, '').toLowerCase();
    const name = chip ? `lrs-${chip}` : `Addr ${dev.address}`;
    return `[${name}] ${rest}`;
  }

  const gwIp = fleetGatewayStatus.value?.wifi?.ip;
  if (gwIp && gwIp === ip) {
    return `[Gateway] ${rest}`;
  }

  return `[${ip}] ${rest}`;
}

const pairLogs = ref<string[]>([]);
const serialUptimeMs = ref<number | null>(null);
const networkUptimeMs = ref<number | null>(null);
const serialDevicesByPort = ref<Record<string, SerialDeviceState>>({});
const disconnectedSerialPortSince = ref<Record<string, number>>({});
const serialAdminPortBusy = ref<Record<string, string>>({});
const serialAdminPortQueues = new Map<string, Promise<void>>();
const FLEET_CACHE_POLL_INTERVAL_MS = 5000;
const FLEET_SCAN_POLL_INTERVAL_MS = 1200;
const FLEET_FORCE_SCAN_COOLDOWN_MS = 60000;
const FLEET_FRESH_MS = 90000;
const FLEET_STALE_MS = 180000;
const provisionCacheRefreshedChips = new Set<string>();
const portsReadingDeviceInfo = ref<Set<string>>(new Set());
const isLoadingInfo = computed(() => portsReadingDeviceInfo.value.has(selectedPort.value));
const isRefreshingPorts = ref(false);
const isFetchingFirmware = ref(false);
const fleetGatewayFlashPhase = ref<FleetGatewayFlashPhase>('idle');
const showToast = ref(false);
const toastMessage = ref('');
const confirmDialog = ref<ConfirmDialogState | null>(null);
const activeDropdownAddress = ref<number | null>(null);

interface SettingsModalState {
  device: LoraInventoryDevice;
  activeTab: 'sensors' | 'power' | 'wifi' | 'security';
  // WiFi tab
  wifi_ssid: string;
  wifi_password: string;
  // Sensors tab
  sensor_temp_enabled: boolean;
  sensor_tank_enabled: boolean;
  power_save_listen_only: boolean;
  // Security tab
  fleet_key: string;
  fleet_key_confirmed: boolean;
  show_fleet_key: boolean;
}
const settingsDeviceModal = ref<SettingsModalState | null>(null);

interface FactoryResetModalState {
  device: LoraInventoryDevice;
  keep_shared_fleet_key: boolean;
  keep_wifi_credentials: boolean;
}
const factoryResetTargetModal = ref<FactoryResetModalState | null>(null);

const logContainer = ref<HTMLElement | null>(null);
const networkUdpLogContainer = ref<HTMLElement | null>(null);
const deviceInfoReadSeqByPort = ref<Record<string, number>>({});
const monitorAfterFlash = ref(true);
const eraseBeforeFlash = ref(false);
const lastPortSnapshot = ref<string[]>([]);
const portSeenSequence = ref<Record<string, number>>({});
const portSeenCounter = ref(0);
const stickLogToBottom = ref(true);
const networkUdpLogsExpanded = ref(false);
const activeMonitorPort = ref('');
const activeMonitorSsid = ref('');
const networkStatusMessage = ref('Select a USB gateway to read its fleet cache.');
const monitorStatusMessage = ref('Select a USB gateway and refresh monitor data.');
const monitorTransport = ref<'serial' | 'mqtt'>('serial');
const monitorMqttHost = ref('venus.local');
const monitorMqttPort = ref(1883);
const monitorMqttUser = ref('');
const monitorMqttPassword = ref('');
const monitorMqttTopicRoot = ref('lora');
const monitorMqttConnected = ref(false);
const showMonitorMqttPassword = ref(false);
const showMonitorMqttSettings = ref(false);
const monitorMqttDraftHost = ref('venus.local');
const monitorMqttDraftPort = ref(1883);
const monitorMqttDraftUser = ref('');
const monitorMqttDraftPassword = ref('');
const monitorMqttDraftTopicRoot = ref('lora');
const monitorFleetRows = ref<LoraInventoryDevice[]>([]);
const isMonitorRefreshing = ref(false);
const isMonitorLoopRunning = ref(false);
const monitorPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const monitorAutoRefresh = ref(true);
const gatewaySnapshotPauseCount = ref(0);
const settingsTransport = ref<'serial' | 'mqtt' | 'lora'>('serial');
const settingsTab = ref<SettingsTab>('general');
const remoteSubTab = ref<'serial' | 'mqtt' | 'lora'>('serial');
const networkUdpTarget = ref('');
const loraInventory = ref<LoraInventoryDevice[]>([]);
const loraInventoryScan = ref<LoraInventoryStatus['scan'] | null>(null);
const isLoraInventoryScanning = ref(false);
const isNetworkGatewayLoading = ref(false);
const networkInventoryPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const networkInventoryPollMode = ref<'cache' | 'scan' | null>(null);
const fleetForceScanCooldownUntilMs = ref(0);
const fleetClockMs = ref(Date.now());
const fleetClockTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const fleetOtaFollowupTimers = ref<Record<number, ReturnType<typeof window.setTimeout>>>({});
const fleetRowHistory = ref<Record<number, {
  uptimeMs?: number;
  fwVersion?: string;
  otaExpectedUntilMs?: number;
  rowState?: LoraInventoryDevice['row_state'];
  rowStateUntilMs?: number;
  
  otaRetryCount?: number;
  lastOtaActivityMs?: number;
  rebootExpectedUntilMs?: number;
  pendingPowerSaveListenOnly?: boolean;
  pendingPowerSaveTxMs?: number;
  wifi_pending_offline?: boolean;
  power_save_deferred?: boolean;
  
  // Volatile telemetry cache
  ip?: string;
  wifi_connected?: boolean;
  wifi_connected_known?: boolean;
  wifi_enabled?: boolean;
  wifi_enabled_known?: boolean;
  mqtt_connected?: boolean;
  mqtt_enabled?: boolean;
  mqtt_known?: boolean;
  power_save_listen_only?: boolean;
  power_save_active?: boolean;
  fw_build?: number;
  heap_free?: number;
  heap_max_block?: number;
  heap_frag_pct?: number;
  relay_state?: number;
  relay_feedback?: number;
  input_state?: number;
  input_feedback?: number;
  temp_enabled?: boolean;
  temp_valid?: boolean;
  temp_c?: number;
  tank_enabled?: boolean;
  tank_valid?: boolean;
  tank_status?: string;
  tank_depth_mm?: number;
  tank_current_ma?: number;
  tank_current_centi_ma?: number;
  tank_voltage_mv?: number;
  rssi?: number;
  lastTelemetryTimestamp?: number;
  knownRebootUntilMs?: number;
}>>({});
const pairExpectedCount = ref(12);
const pairPanelTab = ref<'pair' | 'wifi'>('pair');
const pairFleetKey = ref('');
const showPairFleetKey = ref(false);
const showPairAdminPassword = ref(false);
const pairStatus = ref<EasyPairStatus | null>(null);
const isPairBusy = ref(false);
const isGatewayLoading = ref(false);
const pairStatusPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const pairWifiPassword = ref('');
const showPairWifiPassword = ref(false);
const isWifiScanning = ref(false);
const isWifiApplying = ref(false);
const wifiApplyAttemptId = ref(0);
const isFleetWifiSending = ref(false);
const isIdentifying = ref(false);
const identifyTimer = ref<ReturnType<typeof window.setTimeout> | null>(null);
const isSerialAdminLoading = ref(false);
const isSerialAdminSaving = ref(false);
const isSerialSystemAction = ref(false);
const showSerialWifiPassword = ref(false);
const showSerialMqttPassword = ref(false);
const showSerialFleetKey = ref(false);
const serialFactoryKeepFleet = ref(true);
const serialFactoryKeepWifi = ref(true);

const LOCAL_OPTION = '__local_browse__';
const LOCAL_LABEL_PREFIX = 'Local: ';
const DEVICE_INFO_ORDER: Array<keyof DeviceInfo> = [
  'ssid',
  'password',
  'local_addr',
  'remote_addr',
  'mac',
  'chip_id',
  'serial',
];
const EU_COUNTRY_CODES = new Set([
  'AT', 'BE', 'BG', 'HR', 'CY', 'CZ', 'DK', 'EE', 'FI', 'FR',
  'DE', 'GR', 'HU', 'IE', 'IT', 'LV', 'LT', 'LU', 'MT', 'NL',
  'PL', 'PT', 'RO', 'SK', 'SI', 'ES', 'SE', 'NO', 'IS', 'LI',
  'CH', 'GB',
]);
const US_COUNTRY_CODES = new Set(['US', 'UM', 'PR', 'GU', 'VI', 'AS', 'MP']);
const ZA_COUNTRY_CODES = new Set(['ZA']);
const REGION_STORAGE_KEY = 'lrs_flasher_region';
const MONITOR_AFTER_FLASH_STORAGE_KEY = 'lrs_flasher_monitor_after_flash';
const ERASE_BEFORE_FLASH_STORAGE_KEY = 'lrs_flasher_erase_before_flash';
const MONITOR_AUTO_REFRESH_STORAGE_KEY = 'lrs_flasher_monitor_auto_refresh';
const TAB_PORT_STORAGE_KEYS = {
  serial: 'lrs_flasher_flash_port',
  pair: 'lrs_flasher_provision_port',
  network: 'lrs_flasher_fleet_port',
  monitor: 'lrs_flasher_monitor_port',
  settings: 'lrs_flasher_settings_port'
} as const;
const REGION_CONFIDENT_MIN_SCORE = 5;
const REGION_CONFIDENT_MIN_GAP = 2;

const WIFI_CREDENTIALS_CACHE_KEY = 'lrs_wifi_credentials_cache';

function getCachedWifiPassword(ssid: string): string {
  if (!ssid) return '';
  try {
    const cache = JSON.parse(localStorage.getItem(WIFI_CREDENTIALS_CACHE_KEY) || '{}');
    return cache[ssid] || '';
  } catch (e) {
    return '';
  }
}

function saveCachedWifiPassword(ssid: string, password_value: string) {
  if (!ssid) return;
  try {
    const cache = JSON.parse(localStorage.getItem(WIFI_CREDENTIALS_CACHE_KEY) || '{}');
    cache[ssid] = password_value;
    localStorage.setItem(WIFI_CREDENTIALS_CACHE_KEY, JSON.stringify(cache));
  } catch (e) {
    // Ignore
  }
}

function newSerialDeviceState(): SerialDeviceState {
  return {
    deviceInfo: null,
    adminSupported: false,
    status: null,
    config: null,
    adminPassword: '',
    wifiNetworks: [],
    wifiScanned: false,
    wifiSsid: '',
    gatewayWifiReadySsid: '',
    gatewayWifiReadyIp: '',
    flashLogs: [],
    monitorLogs: [],
    isFlashing: false,
    flashProgress: 0,
    flashStatus: 'Idle',
    flashPhase: 'idle',
    isResetting: false,
    resetStatus: 'Idle'
  };
}

function serialDeviceState(port = selectedPort.value): SerialDeviceState | null {
  if (!port) return null;
  if (!serialDevicesByPort.value[port]) {
    serialDevicesByPort.value[port] = newSerialDeviceState();
  }
  return serialDevicesByPort.value[port];
}

const activeSerialDevice = computed(() => serialDeviceState());
const deviceInfo = computed<DeviceInfo | null>({
  get: () => activeSerialDevice.value?.deviceInfo || null,
  set: (info) => {
    const state = serialDeviceState(selectedPort.value);
    if (state) state.deviceInfo = info;
  }
});
const pairAdminPassword = computed<string>({
  get: () => {
    const state = serialDeviceState(gatewaySelectedPort.value);
    return state?.adminPassword || state?.deviceInfo?.password?.trim() || '';
  },
  set: (password) => {
    const state = serialDeviceState(gatewaySelectedPort.value);
    if (state) state.adminPassword = password;
  }
});
const wifiNetworks = computed<WifiNetwork[]>({
  get: () => serialDeviceState(gatewaySelectedPort.value)?.wifiNetworks || [],
  set: (networks) => {
    const state = serialDeviceState(gatewaySelectedPort.value);
    if (state) state.wifiNetworks = networks;
  }
});
const settingsWifiNetworks = computed<WifiNetwork[]>({
  get: () => serialDeviceState(settingsSelectedPort.value)?.wifiNetworks || [],
  set: (networks) => {
    const state = serialDeviceState(settingsSelectedPort.value);
    if (state) state.wifiNetworks = networks;
  }
});
const pairWifiSsid = computed<string>({
  get: () => serialDeviceState(gatewaySelectedPort.value)?.wifiSsid || '',
  set: (ssid) => {
    const state = serialDeviceState(gatewaySelectedPort.value);
    if (state) state.wifiSsid = ssid;
  }
});
const serialAdminStatus = computed<SerialAdminStatus | null>({
  get: () => activeSerialDevice.value?.status || null,
  set: (status) => {
    const state = serialDeviceState(selectedPort.value);
    if (state) state.status = status;
  }
});
const serialAdminConfig = computed<SerialAdminConfig | null>({
  get: () => activeSerialDevice.value?.config || null,
  set: (config) => {
    const state = serialDeviceState(selectedPort.value);
    if (state) state.config = config;
  }
});

const hasActiveDeviceInfo = computed(() =>
  !!activeSerialDevice.value?.deviceInfo
);
const orderedDeviceInfoEntries = computed((): Array<[keyof DeviceInfo, string | number]> => {
  if (!deviceInfo.value) return [];
  const info = deviceInfo.value;
  const entries: Array<[keyof DeviceInfo, string | number]> = [];
  for (const key of DEVICE_INFO_ORDER) {
    entries.push([key, info[key]]);
  }
  return entries;
});
const activeLogs = computed(() => {
  if (activeMode.value === 'monitor') return networkLogs.value;
  if (activeMode.value === 'network') return networkLogs.value;
  if (activeMode.value === 'pair') return pairLogs.value;
  return serialLogs.value;
});
const crashCount = computed(() => countCrashEvents(activeLogs.value));
const monitorDeviceLabel = computed(() => {
  if (activeMonitorSsid.value) {
    return activeMonitorSsid.value;
  }
  if (hasActiveDeviceInfo.value && deviceInfo.value?.ssid) {
    return deviceInfo.value.ssid;
  }
  return 'Unknown device';
});
const monitorContextLabel = computed(() => {
  const parts = [monitorDeviceLabel.value];
  const port = activeMonitorPort.value || selectedPort.value;
  if (port) {
    parts.push(port);
  }
  return parts.join(' on ');
});
const selectedLoraInventoryCount = computed(() => loraInventory.value.filter(d => d.selected).length);
const pairDiscoveredDeviceCount = computed(() => pairStatus.value?.devices?.length || 0);
const fleetGatewayDevice = computed(() => serialDeviceState(gatewaySelectedPort.value));
const fleetGatewayIdentity = computed(() => fleetGatewayDevice.value?.deviceInfo || null);
const fleetGatewayStatus = computed(() => fleetGatewayDevice.value?.status || null);

function parseVersion(v: string) {
  const clean = v.replace(/^Local:\s*/i, '').trim();
  const match = clean.match(/^(\d+)\.(\d+)\.(\d+)(?:~(\d+))?/);
  if (match) {
    return {
      major: parseInt(match[1], 10),
      minor: parseInt(match[2], 10),
      patch: parseInt(match[3], 10),
      dev: match[4] ? parseInt(match[4], 10) : 0
    };
  }
  return null;
}

const isGatewayUpgradeAvailable = computed(() => {
  const currentFw = fleetGatewayStatus.value?.fw_version;
  if (!currentFw || !selectedVersion.value) return false;

  // Verify firmware selection is present/valid
  if (selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX) && !selectedLocalPath.value) {
    return false;
  }

  let candidateStr = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX)
    ? flasherAppVersion.value
    : selectedVersion.value;

  if (selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX) && selectedLocalPath.value) {
    const fileMatch = selectedLocalPath.value.match(/(\d+\.\d+\.\d+)(?:~(\d+))?/);
    if (fileMatch) {
      candidateStr = fileMatch[0];
    } else {
      // The ~68 update was a flasher-only release. If using a generic local firmware.bin
      // and flasher version is ~68, cap/treat candidate firmware version as 0.9.2~67.
      const pFlasher = parseVersion(flasherAppVersion.value);
      if (pFlasher && pFlasher.major === 0 && pFlasher.minor === 9 && pFlasher.patch === 2 && pFlasher.dev === 68) {
        candidateStr = '0.9.2~67';
      }
    }
  }

  const p1 = parseVersion(candidateStr);
  const p2 = parseVersion(currentFw);
  if (!p1 || !p2) return false;

  if (p1.major !== p2.major) return p1.major > p2.major;
  if (p1.minor !== p2.minor) return p1.minor > p2.minor;
  if (p1.patch !== p2.patch) return p1.patch > p2.patch;
  return p1.dev > p2.dev;
});
const fleetGatewayReady = computed(() =>
  !!gatewaySelectedPort.value &&
  !!fleetGatewayIdentity.value &&
  !!fleetGatewayDevice.value?.adminSupported &&
  !!adminPasswordForPort(gatewaySelectedPort.value)
);
const fleetGatewayIsFactoryDefault = computed(() => {
  const st = fleetGatewayStatus.value;
  return !!st && (!st.commissioned || !!st.fleet_passphrase_default);
});
const hasAnyRemoteIp = computed(() => {
  return loraInventory.value.some(d => !!d.ip);
});
const fleetGatewayFlashDisabled = computed(() =>
  !gatewaySelectedPort.value ||
  fleetGatewayFlashPhase.value !== 'idle' ||
  isNetworkGatewayLoading.value ||
  isLoraInventoryScanning.value ||
  remoteOtaBusyAddress.value !== null ||
  isFirmwareServerStarting.value ||
  isPairBusy.value
);
const loraInventoryProgressLabel = computed(() => {
  const scan = loraInventoryScan.value;
  if (!scan) return 'Idle';
  if (!scan.active) return `Complete, ${scan.sent || 0} probes sent`;
  const next = scan.next_address || scan.start_address || 1;
  return `Scanning ${next}-${scan.end_address || LRS_REMOTE_SCAN_CAP}, ${scan.sent || 0} probes sent`;
});
const gatewayReady = computed(() =>
  !!gatewaySelectedPort.value &&
  !!serialDeviceState(gatewaySelectedPort.value)?.deviceInfo &&
  !!serialDeviceState(gatewaySelectedPort.value)?.adminSupported &&
  !!pairPassword()
);
function portGatewayReady(port: string): boolean {
  const state = serialDeviceState(port);
  return !!port && !!state?.deviceInfo && !!state.adminSupported && !!adminPasswordForPort(port);
}
const pairPrimaryDisabled = computed(() => isPairBusy.value || isGatewayLoading.value || !gatewayReady.value);
const activeSerialAdminPasswordValue = computed(() =>
  activeMode.value === 'pair' ? pairPassword() : deviceInfo.value?.password?.trim() || ''
);
const identifyAvailable = computed(() => hasActiveDeviceInfo.value && !!activeSerialAdminPasswordValue.value);
const isSelectedPortMonitoring = computed(() => isMonitoring.value && !!selectedPort.value && activeMonitorPort.value === selectedPort.value);
const identifyDisabled = computed(() => !selectedPort.value || !identifyAvailable.value || isFlashing.value || isLoadingInfo.value || isGatewayLoading.value || isPairBusy.value);
const serialPortSelectorDisabled = computed(() =>
  isLoadingInfo.value ||
  isFlashing.value ||
  isGatewayLoading.value ||
  isPairBusy.value ||
  isWifiScanning.value ||
  isWifiApplying.value ||
  isFleetWifiSending.value ||
  isIdentifying.value
);
const flashDisabled = computed(() =>
  isFlashing.value ||
  isLoadingInfo.value ||
  !selectedPort.value ||
  !selectedVersion.value
);
const bulkFlashDisabled = computed(() =>
  isBulkFlashing.value ||
  isBulkResetting.value ||
  bulkSelectedPorts.value.length === 0 ||
  !selectedVersion.value
);
const bulkResetDisabled = computed(() =>
  isBulkFlashing.value ||
  isBulkResetting.value ||
  bulkSelectedPorts.value.length === 0
);
const serialAdminPassword = computed(() => deviceInfo.value?.password?.trim() || '');
const serialAdminBusy = computed(() => isSerialAdminLoading.value || isSerialAdminSaving.value || isSerialSystemAction.value);
const serialAdminDisabled = computed(() => !selectedPort.value || !hasActiveDeviceInfo.value || isFlashing.value || isSelectedPortMonitoring.value || isLoadingInfo.value || serialAdminBusy.value);
const serialStatusSummary = computed(() => {
  const st = serialAdminStatus.value;
  if (!st && serialAdminConfig.value) return 'Settings loaded. Refresh status to inspect live firmware health.';
  if (!st) return 'Fetch settings to edit configuration, or refresh status for live health.';
  if (!st.commissioned || st.fleet_passphrase_default) {
    return `Factory default · awaiting commissioning · addr ${st.local_address}->${st.remote_address} · heap ${formatBytes(st.heap_free)} free`;
  }
  const wifi = st.wifi?.sta_connected ? `WiFi ${st.wifi.ip || 'connected'}` : `WiFi ${st.wifi?.status || 'offline'}`;
  return `${st.role || 'unknown'} ${st.local_address}->${st.remote_address} · ${wifi} · heap ${formatBytes(st.heap_free)} free`;
});
const flashRunningFirmware = computed(() => serialDeviceState(flashSelectedPort.value)?.status?.fw_version || '');
const flashRunningFirmwareSummary = computed(() => {
  const st = serialDeviceState(flashSelectedPort.value)?.status;
  if (!st) return 'Click Get device info to read running firmware.';
  const identity = serialDeviceState(flashSelectedPort.value)?.deviceInfo;
  const addr = `${st.local_address}->${st.remote_address}`;
  return `${st.role || 'device'} ${addr}${identity?.chip_id ? ` · chip ${identity.chip_id}` : ''}`;
});
const serialAdminIsFactoryDefault = computed(() => {
  const st = serialAdminStatus.value;
  return !!st && (!st.commissioned || !!st.fleet_passphrase_default);
});
const settingsEmptyMessage = computed(() => {
  if (settingsTab.value === 'remote') {
    return '';
  }
  if (!hasActiveDeviceInfo.value) {
    return 'Select a USB device, then read identity or fetch settings.';
  }
  if (settingsTab.value === 'general') {
    return 'Fetch settings to edit role, addresses, timing, and failsafe values. Refresh status for live firmware health.';
  }
  if (settingsTab.value === 'network') {
    return 'Fetch settings to edit WiFi, hostname, Soft AP, and static IP values.';
  }
  if (settingsTab.value === 'mqtt') {
    return 'Fetch settings to edit MQTT broker, topic, control, and controller values.';
  }
  if (settingsTab.value === 'sensors') {
    return 'Fetch settings to edit DS18B20 sensor enablement.';
  }
  return 'Fetch settings before saving config, or use the guarded reboot and factory reset actions when needed.';
});
const fleetGatewayStatusLabel = computed(() => {
  if (fleetGatewayFlashPhase.value === 'flashing') return 'gateway flashing';
  if (fleetGatewayFlashPhase.value === 'rebooting') return 'gateway rebooting';
  if (fleetGatewayFlashPhase.value === 'waiting') return 'gateway waiting';
  if (fleetGatewayFlashPhase.value === 'updated') return 'gateway updated';
  if (fleetGatewayFlashPhase.value === 'failed') return 'gateway flash failed';
  if (fleetGatewayIsFactoryDefault.value) return 'gateway factory default';
  if (fleetGatewayReady.value) return 'gateway ready';
  return 'gateway not loaded';
});
const fleetGatewayBadgeLabel = computed(() => {
  if (fleetGatewayFlashPhase.value === 'flashing') return 'flashing...';
  if (fleetGatewayFlashPhase.value === 'rebooting') return 'rebooting...';
  if (fleetGatewayFlashPhase.value === 'waiting') return 'waiting...';
  if (fleetGatewayFlashPhase.value === 'updated') return 'updated';
  if (fleetGatewayFlashPhase.value === 'failed') return 'flash failed';
  if (fleetGatewayStatus.value) return 'status loaded';
  if (fleetGatewayIdentity.value) return 'identity loaded';
  return 'not loaded';
});
const fleetGatewayBadgeClass = computed(() => {
  if (fleetGatewayFlashPhase.value === 'flashing' || fleetGatewayFlashPhase.value === 'rebooting' || fleetGatewayFlashPhase.value === 'waiting') {
    return 'border-amber-500/40 bg-amber-500/15 text-amber-200';
  }
  if (fleetGatewayFlashPhase.value === 'updated') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (fleetGatewayFlashPhase.value === 'failed') return 'border-rose-500/40 bg-rose-500/10 text-rose-300';
  if (fleetGatewayStatus.value) return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (fleetGatewayIdentity.value) return 'border-sky-500/30 bg-sky-500/10 text-sky-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
});
const fleetGatewaySummary = computed(() => {
  if (fleetGatewayFlashPhase.value === 'flashing') return 'Writing firmware over USB serial.';
  if (fleetGatewayFlashPhase.value === 'rebooting') return 'Flash completed; gateway is resetting.';
  if (fleetGatewayFlashPhase.value === 'waiting') return 'Waiting for serial admin to return after reboot.';
  if (fleetGatewayFlashPhase.value === 'updated') return 'Gateway responded after flash; status refreshed.';
  if (fleetGatewayFlashPhase.value === 'failed') return 'Gateway flash did not complete; check activity log.';
  const status = fleetGatewayStatus.value;
  if (status) {
    const wifi = status.wifi?.sta_connected ? `WiFi ${status.wifi.ip || 'connected'}` : `WiFi ${status.wifi?.status || 'offline'}`;
    return `${status.role || 'gateway'} addr ${status.local_address} · ${wifi} · heap ${formatBytes(status.heap_free)} free`;
  }
  if (fleetGatewayIdentity.value) {
    return `Identity loaded · addr ${fleetGatewayIdentity.value.local_addr}->${fleetGatewayIdentity.value.remote_addr}`;
  }
  return 'Select or load the USB gateway to inspect and flash it.';
});
const fleetScanDisabled = computed(() =>
  isNetworkGatewayLoading.value ||
  !selectedPort.value ||
  (!isLoraInventoryScanning.value && fleetForceScanCooldownRemainingMs.value > 0)
);
const fleetForceScanCooldownRemainingMs = computed(() =>
  Math.max(0, fleetForceScanCooldownUntilMs.value - fleetClockMs.value)
);
const fleetForceScanLabel = computed(() => {
  if (isLoraInventoryScanning.value) return 'Stop scan';
  const remaining = Math.ceil(fleetForceScanCooldownRemainingMs.value / 1000);
  return remaining > 0 ? `Force Scan ${remaining}s` : 'Force Scan';
});
const gatewayWifiReady = computed(() =>
  !!gatewaySelectedPort.value &&
  serialDeviceState(gatewaySelectedPort.value)?.gatewayWifiReadySsid === pairWifiSsid.value.trim() &&
  !!serialDeviceState(gatewaySelectedPort.value)?.gatewayWifiReadyIp
);
const pairWifiSsidInScan = computed(() =>
  !!pairWifiSsid.value.trim() &&
  wifiNetworks.value.some(n => n.ssid === pairWifiSsid.value.trim())
);
const gatewayWifiStatusText = computed(() => {
  if (gatewayWifiReady.value) return `Gateway connected at ${serialDeviceState(gatewaySelectedPort.value)?.gatewayWifiReadyIp}`;
  return 'Connect the gateway before sending credentials to remotes.';
});
const activityBusy = computed(() => isMonitoring.value || isFlashing.value || isNetworkUdpMonitoring.value || isFirmwareServerStarting.value || isPairBusy.value || isGatewayLoading.value || isWifiScanning.value || isWifiApplying.value || isFleetWifiSending.value || isIdentifying.value || serialAdminBusy.value);
const activityFullscreen = computed(() =>
  (activeMode.value === 'serial' && isMonitoring.value) ||
  (activeMode.value === 'network' && isNetworkUdpMonitoring.value)
);
const serialUptimeLabel = computed(() => formatUptime(serialUptimeMs.value));
const monitorGatewayStatus = computed(() => serialAdminStatus.value);
const monitorHealthSummary = computed(() => {
  const st = monitorGatewayStatus.value;
  if (!st) return 'No gateway status loaded';
  const wifi = st.wifi?.sta_connected ? `${st.wifi.rssi ?? 0} dBm` : st.wifi?.status || 'offline';
  return `${st.role || 'gateway'} addr ${st.local_address} · heap ${formatBytes(st.heap_free)} · WiFi ${wifi}`;
});
const monitorRelayKnown = computed(() => {
  const st = monitorGatewayStatus.value;
  return st?.relay_feedback !== undefined || st?.relay_state !== undefined;
});
const monitorRelayOn = computed(() => {
  const st = monitorGatewayStatus.value;
  const relay = st?.relay_feedback ?? st?.relay_state;
  return Number(relay) === 1;
});
const monitorRelayLabel = computed(() => {
  if (!monitorRelayKnown.value) return 'UNKNOWN';
  return monitorRelayOn.value ? 'ON' : 'OFF';
});
const monitorRelayBadgeClass = computed(() => {
  if (!monitorRelayKnown.value) return 'border-slate-700 bg-slate-800/60 text-slate-500 shadow-inner';
  if (monitorRelayOn.value) return 'border-emerald-300/40 bg-emerald-500/20 text-emerald-200 shadow-[0_0_34px_rgba(16,185,129,0.28),inset_0_0_18px_rgba(16,185,129,0.18)]';
  return 'border-slate-700 bg-slate-900/80 text-slate-300 shadow-inner';
});
const monitorFleetLiveCount = computed(() => monitorFleetRows.value.filter(row => rowFreshness(row) === 'live').length);
const monitorFleetStaleCount = computed(() => monitorFleetRows.value.filter(row => rowFreshness(row) === 'stale').length);
const monitorFleetOfflineCount = computed(() => monitorFleetRows.value.filter(row => rowFreshness(row) === 'offline').length);

let unlistenFlash: UnlistenFn | null = null;
let unlistenMonitor: UnlistenFn | null = null;
let unlistenPortsChanged: UnlistenFn | null = null;
let unlistenNetworkMonitor: UnlistenFn | null = null;

function pushSerialLog(line: string) {
  if (!line) return;
  serialLogs.value.push(line);
  updateUptimeFromLog(line, serialUptimeMs);
  if (serialLogs.value.length > 2000) {
    serialLogs.value = serialLogs.value.slice(-2000);
  }
}

function pushSerialLogForPort(port: string, line: string) {
  if (!line || !port) return;
  const state = serialDeviceState(port);
  if (state) {
    state.flashLogs.push(line);
    if (state.flashLogs.length > 2000) {
      state.flashLogs = state.flashLogs.slice(-2000);
    }
    
    // Detect flash phase transitions from esptool log content
    if (state.flashPhase === 'erase' && /erase complete/i.test(line)) {
      state.flashPhase = 'write';
      state.flashStatus = 'Flashing...';
      state.flashProgress = 30;
    }
    if (state.flashPhase !== 'verify' && /hash of data verified/i.test(line)) {
      state.flashPhase = 'verify';
      state.flashProgress = 90;
    }
    // Also catch the transition when write starts after erase
    if (state.flashPhase === 'erase' && /writing at/i.test(line)) {
      state.flashPhase = 'write';
      state.flashStatus = 'Flashing...';
      if (state.flashProgress < 30) state.flashProgress = 30;
    }

    // Parse progress percentage from esptool logs and map into weighted segments:
    // With erase:    erase=0-30%, write=30-90%, verify=90-100%
    // Without erase: write=0-90%, verify=90-100%
    const match = line.match(/(\d+)(?:\.\d+)?\s*%/);
    if (match) {
      const raw = parseInt(match[1], 10);
      const withErase = eraseBeforeFlash.value;
      let mapped = raw;
      if (state.flashPhase === 'erase') {
        mapped = Math.round(raw * 0.3);                    // 0–30%
      } else if (state.flashPhase === 'write') {
        const base = withErase ? 30 : 0;
        mapped = Math.round(base + raw * 0.6);             // 30–90% or 0–60%
      } else if (state.flashPhase === 'verify') {
        mapped = Math.round(90 + raw * 0.1);               // 90–100%
      }
      // Only move forward — never let the bar go backwards
      if (mapped > state.flashProgress) {
        state.flashProgress = mapped;
      }
    }

    // Auto-scroll the bulk console drawer to the latest line
    nextTick(() => {
      const el = bulkLogRefs.value[port];
      if (el) el.scrollTop = el.scrollHeight;
    });
  }
  if (port === selectedPort.value) {
    pushSerialLog(line);
  }
}

function pushMonitorLogForPort(port: string, line: string) {
  if (!line || !port) return;
  const state = serialDeviceState(port);
  if (state) {
    state.monitorLogs.push(line);
    if (state.monitorLogs.length > 2000) {
      state.monitorLogs = state.monitorLogs.slice(-2000);
    }
  }
  if (port === selectedPort.value) {
    pushSerialLog(line);
  }
}

function pushNetworkLog(line: string) {
  if (!line) return;
  networkLogs.value.push(line);
  updateUptimeFromLog(line, networkUptimeMs);
  if (networkLogs.value.length > 2000) {
    networkLogs.value = networkLogs.value.slice(-2000);
  }
}

function pushPairLog(line: string) {
  if (!line) return;
  pairLogs.value.push(line);
  if (pairLogs.value.length > 2000) {
    pairLogs.value = pairLogs.value.slice(-2000);
  }
}

function clearActivityLog() {
  if (activeMode.value === 'pair') {
    pairLogs.value = [];
  } else if (activeMode.value === 'network') {
    networkLogs.value = [];
    networkUptimeMs.value = null;
  } else {
    serialLogs.value = [];
    serialUptimeMs.value = null;
  }
}

function updateUptimeFromLog(line: string, target: typeof serialUptimeMs) {
  const uptime = parseLogUptimeMs(line);
  if (uptime !== null) {
    target.value = uptime;
  }
}

function parseLogUptimeMs(line: string): number | null {
  const match = line.match(/\bt=(\d+)\b/);
  if (!match) return null;
  const parsed = Number(match[1]);
  if (!Number.isSafeInteger(parsed) || parsed < 0) return null;
  return parsed;
}

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

function setLocalFirmwareSelection(path: string, announce = true) {
  selectedLocalPath.value = path;
  const filename = path.split(/[\\/]/).pop() || 'firmware.bin';
  const localLabel = `${LOCAL_LABEL_PREFIX}${filename}`;
  firmwareVersions.value = firmwareVersions.value.filter(v => !v.startsWith(LOCAL_LABEL_PREFIX));
  firmwareVersions.value.splice(1, 0, localLabel);
  selectedVersion.value = localLabel;
  if (announce) {
    pushSerialLog(`Local firmware selected: ${path}`);
  }
}

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
      setLocalFirmwareSelection(selected);
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

function extractCountryCodes(locale: string): string[] {
  return locale
    .split(/[-_]/)
    .filter(part => /^[A-Za-z]{2}$/.test(part))
    .map(part => part.toUpperCase());
}

function detectRegionFromSystem(): { region: RegionCode | null; reliable: boolean; reason: string } {
  const score: Record<RegionCode, number> = { ZA: 0, EU: 0, US: 0 };
  const clues: string[] = [];
  const addScore = (target: RegionCode, weight: number, clue: string) => {
    score[target] += weight;
    clues.push(`${target}+${weight}:${clue}`);
  };

  const localeCandidates: string[] = [];
  const resolvedLocale = Intl.DateTimeFormat().resolvedOptions().locale;
  if (resolvedLocale) localeCandidates.push(resolvedLocale);
  if (navigator.language) localeCandidates.push(navigator.language);
  if (Array.isArray(navigator.languages)) {
    localeCandidates.push(...navigator.languages);
  }

  const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
  if (timezone === 'Africa/Johannesburg') addScore('ZA', 8, `timezone=${timezone}`);
  if (timezone.startsWith('Europe/')) addScore('EU', 6, `timezone=${timezone}`);
  if (timezone.startsWith('US/')) addScore('US', 6, `timezone=${timezone}`);

  const seen = new Set<string>();
  localeCandidates
    .filter(Boolean)
    .forEach((locale, idx) => {
      const norm = String(locale).trim();
      if (!norm || seen.has(norm)) return;
      seen.add(norm);
      const weight = idx === 0 ? 3 : idx === 1 ? 2 : 1;
      for (const code of extractCountryCodes(norm)) {
        if (ZA_COUNTRY_CODES.has(code)) addScore('ZA', weight, `locale=${norm}`);
        if (US_COUNTRY_CODES.has(code)) addScore('US', weight, `locale=${norm}`);
        if (EU_COUNTRY_CODES.has(code)) addScore('EU', weight, `locale=${norm}`);
      }
    });

  const ranked = (Object.entries(score) as Array<[RegionCode, number]>).sort((a, b) => b[1] - a[1]);
  const top = ranked[0];
  const second = ranked[1];
  const hasSignal = top[1] > 0;
  const reliable =
    hasSignal &&
    top[1] >= REGION_CONFIDENT_MIN_SCORE &&
    (top[1] - second[1]) >= REGION_CONFIDENT_MIN_GAP;
  const reason = clues.length ? clues.join(', ') : 'no locale/timezone signal';

  if (!hasSignal) return { region: null, reliable: false, reason };
  return { region: top[0], reliable, reason };
}

async function refreshPorts(fromPortChange: boolean | Event = false) {
  const allowPortChangeAutoSwitch = fromPortChange === true;
  if (isRefreshingPorts.value) return;
  isRefreshingPorts.value = true;
  try {
    const fetchedPorts: SerialPort[] = await invoke('list_serial_ports');
    const sortedPorts = fetchedPorts.sort((a, b) => b.score - a.score);
    ports.value = sortedPorts;

    const currentNames = sortedPorts.map(p => p.port_name);
    const previousSet = new Set(lastPortSnapshot.value);
    const newPorts = currentNames.filter(name => !previousSet.has(name));
    const hasActiveOperation = isFlashing.value || isLoadingInfo.value || isMonitoring.value || isGatewayLoading.value;

    // Track first-seen order so Windows can prefer most recently connected devices.
    for (const portName of newPorts) {
      portSeenCounter.value += 1;
      portSeenSequence.value[portName] = portSeenCounter.value;

      // Clear cached credentials, status, and configuration for recycled ports
      const state = serialDeviceState(portName);
      if (state) {
        state.deviceInfo = null;
        state.adminPassword = '';
        state.status = null;
        state.config = null;
        // Clear flash/reset operational state so old results don't carry to a new device
        state.isFlashing = false;
        state.flashProgress = 0;
        state.flashStatus = 'Idle';
        state.flashPhase = 'idle';
        state.flashLogs = [];
        state.isResetting = false;
        state.resetStatus = 'Idle';
      }
    }
    for (const known of Object.keys(portSeenSequence.value)) {
      if (!currentNames.includes(known)) {
        delete portSeenSequence.value[known];
      }
    }
    const now = Date.now();
    const nextDisconnectedSince = { ...disconnectedSerialPortSince.value };
    for (const portName of currentNames) {
      delete nextDisconnectedSince[portName];
    }
    for (const known of Object.keys(serialDevicesByPort.value)) {
      if (currentNames.includes(known)) continue;
      const missingSince = nextDisconnectedSince[known] || now;
      nextDisconnectedSince[known] = missingSince;
      if (now - missingSince >= DISCONNECTED_PORT_CACHE_GRACE_MS) {
        delete serialDevicesByPort.value[known];
        delete nextDisconnectedSince[known];
      }
    }
    disconnectedSerialPortSince.value = nextDisconnectedSince;

    reconcileTabPortSelections(currentNames, newPorts, allowPortChangeAutoSwitch && !hasActiveOperation);

    // Remove disconnected ports from bulk selection so completed/unplugged devices vanish from the status grid
    if (bulkSelectedPorts.value.length > 0) {
      const connectedSet = new Set(currentNames);
      bulkSelectedPorts.value = bulkSelectedPorts.value.filter(p => connectedSet.has(p));
    }

    lastPortSnapshot.value = currentNames;
    syncDeviceInfoForSelectedPort();

    // Note: gateway loading is handled reactively by watch(gatewayPortDeviceInfo),
    // so no explicit loadEasyPairGateway/loadNetworkGateway call is needed here.

    // Artificial delay to ensure the spin is satisfyingly visible
    await new Promise(resolve => setTimeout(resolve, 300));
  } finally {
    isRefreshingPorts.value = false;
  }
}

function reconcileTabPortSelections(currentNames: string[], newPorts: string[], allowAutoSwitchToNewPort: boolean) {
  const defaultPort = chooseDefaultPort(currentNames);
  const preferredNewPort = chooseMostRecentPort(newPorts) ?? defaultPort;
  const activeReplacement = allowAutoSwitchToNewPort && preferredNewPort ? preferredNewPort : defaultPort;

  const ensureSelection = (port: string, active: boolean): string => {
    if (currentNames.length === 0) return '';
    if (!port || !currentNames.includes(port)) return activeReplacement;
    if (active && allowAutoSwitchToNewPort && newPorts.length > 0 && preferredNewPort) return preferredNewPort;
    return port;
  };

  flashSelectedPort.value = ensureSelection(flashSelectedPort.value, activeMode.value === 'serial');
  gatewaySelectedPort.value = ensureSelection(gatewaySelectedPort.value, activeMode.value === 'pair' || activeMode.value === 'network');
  monitorSelectedPort.value = ensureSelection(monitorSelectedPort.value, activeMode.value === 'monitor');
  settingsSelectedPort.value = ensureSelection(settingsSelectedPort.value, activeMode.value === 'settings');
}

function portLooksLikeLrsAdapter(port: SerialPort | undefined): boolean {
  if (!port) return false;
  const combined = `${port.port_name} ${port.description || ''}`.toLowerCase();
  if (combined.includes('bluetooth') ||
      combined.includes('soundcore') ||
      combined.includes('headphone') ||
      combined.includes('headset') ||
      combined.includes('airpods') ||
      combined.includes('bose')) {
    return false;
  }
  return port.score > 0 ||
    combined.includes('usbserial') ||
    combined.includes('usbmodem') ||
    combined.includes('ttyusb') ||
    combined.includes('ttyacm') ||
    combined.includes('cp210') ||
    combined.includes('ch34') ||
    combined.includes('silicon labs') ||
    combined.includes('qinheng') ||
    combined.includes('espressif');
}

function isLrsAdapterPort(port: SerialPort | undefined): port is SerialPort {
  return portLooksLikeLrsAdapter(port);
}

function chooseMostRecentPort(candidates: string[]): string | null {
  const ranked = candidates
    .map(name => ({ name, seq: portSeenSequence.value[name] ?? -1, port: ports.value.find(p => p.port_name === name) }))
    .filter((candidate): candidate is { name: string; seq: number; port: SerialPort } => isLrsAdapterPort(candidate.port))
    .sort((a, b) => b.seq - a.seq);
  return ranked.length > 0 ? ranked[0].name : null;
}

function chooseDefaultPort(portNames: string[]): string {
  const ranked = portNames
    .map(name => ports.value.find(p => p.port_name === name))
    .filter(isLrsAdapterPort)
    .sort((a, b) => b.score - a.score);
  return ranked[0]?.port_name || '';
}

function loadSavedTabPorts() {
  try {
    flashSelectedPort.value = localStorage.getItem(TAB_PORT_STORAGE_KEYS.serial) || flashSelectedPort.value;
    gatewaySelectedPort.value = localStorage.getItem(TAB_PORT_STORAGE_KEYS.pair) || localStorage.getItem(TAB_PORT_STORAGE_KEYS.network) || gatewaySelectedPort.value;
    monitorSelectedPort.value = localStorage.getItem(TAB_PORT_STORAGE_KEYS.monitor) || monitorSelectedPort.value;
    settingsSelectedPort.value = localStorage.getItem(TAB_PORT_STORAGE_KEYS.settings) || settingsSelectedPort.value;
  } catch {
    // Ignore storage failures; runtime auto-selection still works.
  }
}

function saveTabPort(key: keyof typeof TAB_PORT_STORAGE_KEYS, port: string) {
  try {
    if (port) {
      localStorage.setItem(TAB_PORT_STORAGE_KEYS[key], port);
    } else {
      localStorage.removeItem(TAB_PORT_STORAGE_KEYS[key]);
    }
  } catch {
    // Ignore storage failures; in-memory selection still works.
  }
}

async function fetchFirmware() {
  isFetchingFirmware.value = true;
  try {
    const defaultLocalFirmware = await invoke<string | null>('get_default_local_firmware').catch(() => null);
    const remoteVersions = await invoke<string[]>('get_firmware_list');
    if (defaultLocalFirmware && !selectedLocalPath.value) {
      setLocalFirmwareSelection(defaultLocalFirmware, false);
    }
    // Maintain local selection if it exists
    const localEntry = firmwareVersions.value.find(v => v.startsWith(LOCAL_LABEL_PREFIX));
    firmwareVersions.value = [LOCAL_OPTION, ...(localEntry ? [localEntry] : []), ...remoteVersions];

    if (!selectedVersion.value && firmwareVersions.value.length > 1) {
      selectedVersion.value = firmwareVersions.value[1];
    }
    await new Promise(resolve => setTimeout(resolve, 400));
  } catch (e) {
    // Keep local-flash path available even when network release fetch fails.
    try {
      const defaultLocalFirmware = await invoke<string | null>('get_default_local_firmware');
      if (defaultLocalFirmware && !selectedLocalPath.value) {
        setLocalFirmwareSelection(defaultLocalFirmware, false);
      }
    } catch {
      // Ignore default-local lookup failure; the manual chooser remains available.
    }
    const localEntry = firmwareVersions.value.find(v => v.startsWith(LOCAL_LABEL_PREFIX));
    firmwareVersions.value = [LOCAL_OPTION, ...(localEntry ? [localEntry] : [])];
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

function confirmOperatorAction(message: string, options: { confirmText?: string; cancelText?: string; danger?: boolean } = {}): Promise<boolean> {
  if (confirmDialog.value?.resolve) {
    confirmDialog.value.resolve(false);
  }
  return new Promise(resolve => {
    confirmDialog.value = {
      message,
      confirmText: options.confirmText || 'Continue',
      cancelText: options.cancelText || 'Cancel',
      danger: !!options.danger,
      resolve
    };
  });
}

function resolveConfirmDialog(confirmed: boolean) {
  const active = confirmDialog.value;
  confirmDialog.value = null;
  active?.resolve?.(confirmed);
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
  const block = orderedDeviceInfoEntries.value
    .map(([key, val]) => `${formatLabel(key)}: ${val}`)
    .join('\n');
  copyToClipboard(block, 'all device configuration');
}

function copyActivityLog() {
  if (activeLogs.value.length === 0) {
    notify('No activity logs to copy');
    return;
  }
  copyToClipboard(activeLogs.value.join('\n'), 'activity log');
}

function copyNetworkUdpLog() {
  if (filteredNetworkLogs.value.length === 0) {
    notify('No UDP logs to copy');
    return;
  }
  copyToClipboard(filteredNetworkLogs.value.join('\n'), 'UDP log');
}

function copyActivePassword() {
  const password = deviceInfo.value?.password?.trim();
  if (!password) {
    notify('Load device info first to copy password');
    return;
  }
  copyToClipboard(password, 'factory password');
}

function syncDeviceInfoForSelectedPort() {
  serialDeviceState();
}

function randomIndex(max: number): number {
  if (max <= 1) return 0;
  try {
    if (window.crypto && window.crypto.getRandomValues) {
      const arr = new Uint32Array(1);
      const lim = Math.floor(0x100000000 / max) * max;
      let value = 0;
      do {
        window.crypto.getRandomValues(arr);
        value = arr[0];
      } while (value >= lim);
      return value % max;
    }
  } catch (_) {
    // Fall through to Math.random below.
  }
  return Math.floor(Math.random() * max);
}

function generateReadableFleetKey(): string {
  const groups: string[] = [];
  for (let i = 0; i < 4; i += 1) {
    groups.push(
      READABLE_KEY_CONSONANTS[randomIndex(READABLE_KEY_CONSONANTS.length)] +
      READABLE_KEY_VOWELS[randomIndex(READABLE_KEY_VOWELS.length)] +
      READABLE_KEY_CONSONANTS[randomIndex(READABLE_KEY_CONSONANTS.length)] +
      READABLE_KEY_VOWELS[randomIndex(READABLE_KEY_VOWELS.length)] +
      READABLE_KEY_CONSONANTS[randomIndex(READABLE_KEY_CONSONANTS.length)]
    );
  }
  return groups.join('-');
}

function generatePairFleetKey(force = false) {
  if (!force && pairFleetKey.value.trim()) return;
  pairFleetKey.value = generateReadableFleetKey();
}

function copyPairFleetKey() {
  const key = pairFleetKey.value.trim();
  if (!key) {
    notify('Generate a fleet key first');
    return;
  }
  copyToClipboard(key, 'fleet key');
}

function copyPairAdminPassword() {
  const password = pairAdminPassword.value.trim();
  if (!password) {
    notify('Load gateway first to copy the password');
    return;
  }
  copyToClipboard(password, 'gateway password');
}


function networkOtaFirmwareOptions(): { firmware_path: string; region: RegionCode | null } | null {
  const isLocal = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX);
  const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
  if (!firmwarePath || (isLocal && !selectedLocalPath.value)) {
    notify('Local file path missing');
    return null;
  }
  return {
    firmware_path: firmwarePath,
    region: isLocal ? null : region.value
  };
}

async function startNetworkUdpListener() {
  try {
    const started = await invoke<string>('start_network_udp_monitor');
    isNetworkUdpMonitoring.value = true;
    networkUdpTarget.value = 'admin-enabled devices';
    pushNetworkLog(started);
  } catch (e) {
    pushNetworkLog('UDP monitor error: ' + e);
    notify('UDP monitor error: ' + e);
  }
}

async function stopNetworkUdpMonitor() {
  try {
    const stopped = await invoke<string>('stop_network_udp_monitor');
    pushNetworkLog(stopped);
  } catch (e) {
    pushNetworkLog('UDP monitor stop error: ' + e);
  } finally {
    isNetworkUdpMonitoring.value = false;
    networkUdpTarget.value = '';
    networkUdpLogsExpanded.value = false;
  }
}

async function startFirmwareServer() {
  const firmwareOptions = networkOtaFirmwareOptions();
  await startFirmwareServerWithOptions(firmwareOptions);
}

async function startFirmwareServerWithOptions(firmwareOptions: { firmware_path: string; region: RegionCode | null } | null) {
  if (!firmwareOptions) return;
  isFirmwareServerStarting.value = true;
  activeMode.value = 'network';
  pushNetworkLog('--- Firmware file server ---');
  try {
    const info = await invoke<FirmwareServerInfo>('start_firmware_file_server', {
      options: firmwareOptions
    });
    firmwareServerInfo.value = info;
    networkStatusMessage.value = `Serving ${info.filename} on ${info.urls[0] || `port ${info.port}`}.`;
    pushNetworkLog(`${networkStatusMessage.value} SHA256 ${info.sha256}`);
  } catch (e) {
    pushNetworkLog('Firmware server failed: ' + e);
    notify('Firmware server failed: ' + e);
  } finally {
    isFirmwareServerStarting.value = false;
  }
}

async function ensureRemoteFlashFirmwareServer(): Promise<FirmwareServerInfo> {
  if (firmwareServerInfo.value) return firmwareServerInfo.value;
  const firmwareOptions = networkOtaFirmwareOptions();
  if (!firmwareOptions) throw new Error('Choose a firmware file or release first');
  await startFirmwareServerWithOptions(firmwareOptions);
  if (!firmwareServerInfo.value) throw new Error('Firmware server did not start');
  return firmwareServerInfo.value;
}

async function stopFirmwareServer() {
  try {
    const stopped = await invoke<string>('stop_firmware_file_server');
    pushNetworkLog(stopped);
  } catch (e) {
    pushNetworkLog('Firmware server stop error: ' + e);
  } finally {
    firmwareServerInfo.value = null;
    networkStatusMessage.value = 'Firmware server stopped.';
  }
}

function pairPassword(): string {
  return adminPasswordForPort(gatewaySelectedPort.value);
}

function adminPasswordForPort(port: string): string {
  const state = serialDeviceState(port);
  return state?.adminPassword?.trim() ||
    state?.deviceInfo?.password?.trim() ||
    '';
}

function normalizeChipId(raw: string | undefined | null): string {
  return String(raw || '').trim().replace(/^0x/i, '').replace(/[^0-9a-f]/gi, '').toLowerCase();
}

function lrsDeviceName(rawChipId: string | undefined | null): string {
  const chip = normalizeChipId(rawChipId);
  if (!chip) return '-';
  return `lrs-${chip.length < 8 ? chip.padStart(8, '0') : chip}`;
}

function displayFirmwareVersion(rawVersion: string | undefined | null): string {
  const version = String(rawVersion || '').trim();
  if (version === '0.0.0') return '-';
  return version || '-';
}

function compactFirmwareVersion(major: number, minor: number, patch: number, build?: number): string {
  const core = `${major}.${minor}.${patch}`;
  // The ~N dev build convention was introduced in 0.9.2; earlier firmware has
  // unrelated data in the build byte, so suppress it to avoid e.g. "0.9.1~161".
  const supportsDevBuild = major > 0 || minor > 9 || (minor === 9 && patch >= 2);
  return supportsDevBuild && build && build > 0 ? `${core}~${build}` : core;
}

function provisionedRemoteChipIds(): Set<string> {
  return new Set((pairStatus.value?.devices || [])
    .filter(d => d.selected && isProvisionedTargetState(d) && d.assigned_address > 0 && d.assigned_address < 255)
    .map(d => normalizeChipId(d.chip_id_hex))
    .filter(Boolean));
}

async function refreshProvisionedSerialDeviceCaches() {
  const chips = provisionedRemoteChipIds();
  if (chips.size === 0) return;
  const pendingChips = [...chips].filter(chip => !provisionCacheRefreshedChips.has(chip));
  if (pendingChips.length === 0) return;

  for (const [port, state] of Object.entries(serialDevicesByPort.value)) {
    if (port === gatewaySelectedPort.value) continue;
    const chip = normalizeChipId(state.deviceInfo?.chip_id || state.status?.chip_id);
    if (!chip || !pendingChips.includes(chip)) continue;

    state.status = null;
    state.config = null;
    state.deviceInfo = null;
    provisionCacheRefreshedChips.add(chip);

    if (isMonitoring.value && activeMonitorPort.value === port) {
      continue;
    }
    if (serialAdminBusyForPort(port)) {
      continue;
    }

    pushPairLog(`Refreshing serial details for provisioned remote on ${port} chip ${chip}...`);
    await readDeviceInfoForPort(port, 'serial');
  }
}

async function sendPairCommand<T = any>(cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000, options: SerialJobOptions = {}): Promise<T> {
  return sendEasyPairCommandOnPort<T>(gatewaySelectedPort.value, cmd, payload, timeoutMs, options);
}

function noteMonitorReleasedForPort(port: string, reason: string) {
  if (!isMonitoring.value || activeMonitorPort.value !== port) return;
  isMonitoring.value = false;
  pushSerialLog(`Serial monitor released for ${port}: ${reason}.`);
  activeMonitorPort.value = '';
  activeMonitorSsid.value = '';
}

async function sendEasyPairCommand<T = any>(cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000, options: SerialJobOptions = {}): Promise<T> {
  return sendEasyPairCommandOnPort<T>(selectedPort.value, cmd, payload, timeoutMs, options);
}

function serialAdminBusyForPort(port: string): boolean {
  return !!serialAdminPortBusy.value[port] || serialAdminPortQueues.has(port);
}

async function runSerialAdminJob<T>(port: string, label: string, options: SerialJobOptions, job: () => Promise<T>): Promise<T> {
  if (options.dropIfBusy && serialAdminBusyForPort(port)) {
    throw new Error('serial_admin_background_skipped');
  }

  const previous = serialAdminPortQueues.get(port) || Promise.resolve();
  let releaseQueue!: () => void;
  const current = new Promise<void>(resolve => {
    releaseQueue = resolve;
  });
  const queued = previous.then(() => current);
  serialAdminPortQueues.set(port, queued);

  try {
    await previous;
    serialAdminPortBusy.value = { ...serialAdminPortBusy.value, [port]: label };
    return await job();
  } finally {
    const nextBusy = { ...serialAdminPortBusy.value };
    if (nextBusy[port] === label) delete nextBusy[port];
    serialAdminPortBusy.value = nextBusy;
    releaseQueue();
    if (serialAdminPortQueues.get(port) === queued) {
      serialAdminPortQueues.delete(port);
    }
  }
}

function serialBackgroundSkipped(err: unknown): boolean {
  return String(err || '').includes('serial_admin_background_skipped');
}

function stringValue(value: unknown, fallback = ''): string {
  return typeof value === 'string' ? value : fallback;
}

function numberValue(value: unknown, fallback: number): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function boolValue(value: unknown, fallback = false): boolean {
  return typeof value === 'boolean' ? value : fallback;
}

function normalizeAddressArray(value: unknown, fallback: number[] = []): number[] {
  if (!Array.isArray(value)) return fallback;
  return value
    .map(item => Number(item))
    .filter(item => Number.isInteger(item) && item >= 1 && item <= 254);
}

function uniqueSortedAddresses(values: number[]): number[] {
  return [...new Set(values.filter(value => Number.isInteger(value) && value >= 1 && value <= 254))]
    .sort((a, b) => a - b);
}

function isProvisionedTargetState(device: EasyPairDevice): boolean {
  return device.state === 'verified';
}

function normalizeSerialAdminConfig(raw: Partial<SerialAdminConfig> | null | undefined, status: SerialAdminStatus | null): SerialAdminConfig {
  const cfg = raw || {};
  const roleTx = typeof cfg.role_tx === 'boolean' ? cfg.role_tx : !!status?.role_tx;
  const localAddress = numberValue(cfg.local_address, status?.local_address || (roleTx ? 254 : 1));
  const remoteAddress = numberValue(cfg.remote_address, status?.remote_address || (roleTx ? 1 : 254));
  const wifi = status?.wifi;
  const mqtt = status?.mqtt;

  return {
    ...cfg,
    mode: cfg.mode === 'standalone' || (!cfg.mode && status?.mode === 'standalone') ? 'standalone' : 'paired',
    commissioned: typeof cfg.commissioned === 'boolean' ? cfg.commissioned : status?.commissioned,
    role_tx: roleTx,
    local_address: localAddress,
    remote_address: remoteAddress,
    paired_target_addresses: normalizeAddressArray(cfg.paired_target_addresses, [remoteAddress]),
    allowed_controller_addresses: normalizeAddressArray(cfg.allowed_controller_addresses, [remoteAddress]),
    known_peer_addresses: normalizeAddressArray(cfg.known_peer_addresses),
    lora_tx_power: numberValue(cfg.lora_tx_power, 17),
    lora_spreading_factor: numberValue(cfg.lora_spreading_factor, 12),
    lora_bandwidth_hz: numberValue(cfg.lora_bandwidth_hz, 125000),
    lora_coding_rate: numberValue(cfg.lora_coding_rate, 5),
    heartbeat_ms: numberValue(cfg.heartbeat_ms, 60000),
    heartbeat_enabled: boolValue(cfg.heartbeat_enabled, true),
    ack_timeout_ms: numberValue(cfg.ack_timeout_ms, 3000),
    mqtt_remote_retry_timeout_ms: numberValue(cfg.mqtt_remote_retry_timeout_ms, 180000),
    tx_mqtt_remote_polling_enabled: boolValue(cfg.tx_mqtt_remote_polling_enabled, false),
    tx_mqtt_remote_default_poll_interval_ms: numberValue(cfg.tx_mqtt_remote_default_poll_interval_ms, 300000),
    maintenance_debug_telemetry_enabled: boolValue(cfg.maintenance_debug_telemetry_enabled, false),
    rx_push_on_change_enabled: boolValue(cfg.rx_push_on_change_enabled, false),
    rx_push_min_interval_ms: numberValue(cfg.rx_push_min_interval_ms, 60000),
    input_control_paired_lora_enabled: boolValue(cfg.input_control_paired_lora_enabled, true),
    tx_command_retry_timeout_ms: numberValue(cfg.tx_command_retry_timeout_ms, 180000),
    rx_failsafe_mode: stringValue(cfg.rx_failsafe_mode, 'hold_last'),
    rx_failsafe_timeout_ms: numberValue(cfg.rx_failsafe_timeout_ms, 180000),
    wifi_sta_ssid: stringValue(cfg.wifi_sta_ssid, wifi?.sta_ssid || ''),
    wifi_sta_password: getCachedWifiPassword(stringValue(cfg.wifi_sta_ssid, wifi?.sta_ssid || '')),
    lan_hostname: stringValue(cfg.lan_hostname, ''),
    ap_always_on: boolValue(cfg.ap_always_on, false),
    wifi_phy_mode: stringValue(cfg.wifi_phy_mode, '11b'),
    wifi_tx_power_dbm: numberValue(cfg.wifi_tx_power_dbm, 20.5),
    wifi_sleep_enabled: boolValue(cfg.wifi_sleep_enabled, false),
    wifi_static_ip_enabled: boolValue(cfg.wifi_static_ip_enabled, false),
    wifi_static_ip: stringValue(cfg.wifi_static_ip, ''),
    wifi_static_gateway: stringValue(cfg.wifi_static_gateway, ''),
    wifi_static_subnet: stringValue(cfg.wifi_static_subnet, ''),
    wifi_channel_override: numberValue(cfg.wifi_channel_override, 0),
    wifi_ap_fallback_policy: stringValue(cfg.wifi_ap_fallback_policy, 'fallback_on_disconnect'),
    wifi_admin_enabled: boolValue(cfg.wifi_admin_enabled, wifi?.admin_enabled || false),
    mqtt_client_enabled: boolValue(cfg.mqtt_client_enabled, mqtt?.client_enabled || false),
    mqtt_control_enabled: boolValue(cfg.mqtt_control_enabled, mqtt?.control_enabled || false),
    mqtt_controller_addresses: stringValue(cfg.mqtt_controller_addresses, ''),
    mqtt_host: stringValue(cfg.mqtt_host, mqtt?.host || ''),
    mqtt_port: numberValue(cfg.mqtt_port, mqtt?.port || 1883),
    mqtt_user: stringValue(cfg.mqtt_user, ''),
    mqtt_password: '',
    mqtt_topic_root: stringValue(cfg.mqtt_topic_root, mqtt?.topic_root || 'lora'),
    sensor_temp_enabled: boolValue(cfg.sensor_temp_enabled, !!status?.local_temp_valid),
    sensor_temp_pin: numberValue(cfg.sensor_temp_pin, 0),
    sensor_temp_interval_s: numberValue(cfg.sensor_temp_interval_s, 10),
    sensor_tank_enabled: boolValue(cfg.sensor_tank_enabled, !!status?.local_tank_enabled),
    sensor_tank_range_mm: numberValue(cfg.sensor_tank_range_mm, 5000),
    sensor_tank_vref_mv: numberValue(cfg.sensor_tank_vref_mv, 3553),
    sensor_tank_sense_ohms: numberValue(cfg.sensor_tank_sense_ohms, 120),
    sensor_tank_interval_s: numberValue(cfg.sensor_tank_interval_s, 5),
    fleet_passphrase: stringValue(cfg.fleet_passphrase, ''),
    admin_password: ''
  };
}

async function sendEasyPairCommandOnPort<T = any>(port: string, cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000, options: SerialJobOptions = {}): Promise<T> {
  if (!port) throw new Error('Select the USB gateway first');
  const label = options.label || cmd.replace(/_/g, ' ');
  return await runSerialAdminJob<T>(port, label, options, async () => {
    noteMonitorReleasedForPort(port, 'serial admin command needs this port');
    return await invoke<T>('serial_admin_command', {
      port,
      request: { cmd, ...payload },
      timeoutMs
    });
  });
}

async function loadNetworkGateway() {
  const port = gatewaySelectedPort.value;
  if (!port || isNetworkGatewayLoading.value) return;
  // Require device info to be already loaded before proceeding.
  // If not yet available, exit silently — the reactive watcher on
  // gatewayPortDeviceInfo will call us again once it arrives.
  if (!serialDeviceState(port)?.deviceInfo) {
    networkStatusMessage.value = `Waiting for device on ${port} to be identified...`;
    return;
  }
  isNetworkGatewayLoading.value = true;
  try {
    const state = serialDeviceState(port);
    const hello = await waitForSerialAdminHello(port, 6000);
    if (state) {
      state.adminSupported = true;
      state.adminPassword = state.deviceInfo?.password || '';
    }
    const status = await ensureFleetGatewayStatus(true);
    if (status && !status.role_tx) {
      const message = gatewayRequiredMessage('Fleet');
      networkStatusMessage.value = message;
      notify(message);
      loraInventory.value = [];
      loraInventoryScan.value = null;
      isLoraInventoryScanning.value = false;
      if (gatewaySelectedPort.value === port) {
        gatewaySelectedPort.value = '';
      }
      return;
    }
    networkStatusMessage.value = `Gateway loaded on ${port}; firmware ${hello.fw_version || 'unknown'}.`;
    await refreshLoraInventoryStatus(false);
    startFleetCachePolling();
  } catch (e) {
    networkStatusMessage.value = serialFeatureError('Gateway load', e);
    notify(networkStatusMessage.value);
  } finally {
    isNetworkGatewayLoading.value = false;
  }
}

function classifyFleetRow(row: LoraInventoryDevice, now = Date.now()): LoraInventoryDevice {
  const history = fleetRowHistory.value[row.address] || {};
  let rowState = history.rowState;
  let rowStateUntilMs = history.rowStateUntilMs;
  const uptime = Number(row.uptime_ms || 0);
  const previousUptime = Number(history.uptimeMs || 0);
  let otaExpected = Number(history.otaExpectedUntilMs || 0) > now;
  const knownReboot = history.knownRebootUntilMs && history.knownRebootUntilMs > now;

  const expectedReboot = history.rebootExpectedUntilMs && history.rebootExpectedUntilMs > now;

  if (previousUptime > 0 && uptime > 0 && uptime + 30000 < previousUptime) {
    if (history.rowState === 'ota_updated' && knownReboot) {
      // Ignore the uptime drop if we already flagged a successful OTA update
      rowState = 'ota_updated';
      rowStateUntilMs = history.rowStateUntilMs;
    } else {
      const isExpected = expectedReboot || otaExpected || knownReboot;
      rowState = isExpected ? 'ota_rebooted' : 'unexpected_reboot';
      rowStateUntilMs = now + (isExpected ? 20000 : 60000);
      
      // Consume the expected reboot once processed to prevent repeat triggers
      history.rebootExpectedUntilMs = undefined;
    }
  }
  if (otaExpected && history.fwVersion && row.fw_version && history.fwVersion !== row.fw_version) {
    rowState = 'ota_updated';
    rowStateUntilMs = now + 30000;
  }

  let otaExpectedUntilMs = history.otaExpectedUntilMs;
  if (rowState === 'ota_updated') {
    otaExpectedUntilMs = undefined;
    otaExpected = false;
    history.knownRebootUntilMs = now + 120000;
  }

  if (rowStateUntilMs && rowStateUntilMs <= now) {
    rowState = otaExpected ? (rowState || 'ota_downloading') : undefined;
    rowStateUntilMs = otaExpected ? otaExpectedUntilMs : undefined;
  }
  if (otaExpectedUntilMs && otaExpectedUntilMs <= now && (rowState === 'ota_pending' || rowState === 'ota_downloading' || rowState === 'ota_apply_wait' || rowState === 'ota_retrying')) {
    rowState = 'ota_no_reboot';
    rowStateUntilMs = now + 60000;
  }

  // Track real telemetry freshness timestamps to tick up age_ms continuously
  let lastTelemetryTimestamp = history.lastTelemetryTimestamp;
  if (row.age_ms !== undefined && row.age_ms !== null && row.age_ms < 600000) {
    lastTelemetryTimestamp = now - row.age_ms;
  }
  const ageMs = (row.age_ms !== undefined && row.age_ms !== null) ? row.age_ms : (lastTelemetryTimestamp ? (now - lastTelemetryTimestamp) : undefined);

  // Evaluate smart sleep states
  const powerSaveListenOnly = (row.power_save_listen_only !== undefined && row.power_save_listen_only !== null) ? row.power_save_listen_only : history.power_save_listen_only;
  const powerSaveActive = (row.power_save_active !== undefined && row.power_save_active !== null) ? row.power_save_active : history.power_save_active;
  
  const isPowerSaveConfigured = !!powerSaveListenOnly;
  
  const isPowerSaveDeferred = isPowerSaveConfigured && !powerSaveActive;

  // Hydrate volatile telemetry fields from cache if gateway wiped them
  const fwVersion = row.fw_version || history.fwVersion;
  const fwBuild = row.fw_build || history.fw_build;
  
  const wifiConnected = ((row.wifi_connected_known ? row.wifi_connected : history.wifi_connected) ?? false);
  const wifiConnectedKnown = row.wifi_connected_known || history.wifi_connected_known || false;
  const ip = wifiConnected ? (row.ip || history.ip) : undefined;
  
  const wifiEnabled = ((row.wifi_enabled_known ? row.wifi_enabled : history.wifi_enabled) ?? false);
  const wifiEnabledKnown = row.wifi_enabled_known || history.wifi_enabled_known || false;
  const mqttConnected = ((row.mqtt_known ? row.mqtt_connected : history.mqtt_connected) ?? false);
  const mqttEnabled = ((row.mqtt_known ? row.mqtt_enabled : history.mqtt_enabled) ?? false);
  const mqttKnown = row.mqtt_known || history.mqtt_known || false;

  // Transition state: when PowerSave is on but WiFi has not dropped offline yet,
  // we show a pending status in the WiFi column until confirmed offline.
  const wifiPendingOffline = isPowerSaveConfigured && powerSaveActive && wifiConnected;
  
  let pendingPowerSaveListenOnly = history.pendingPowerSaveListenOnly;
  let pendingPowerSaveTxMs = history.pendingPowerSaveTxMs;

  // Check if incoming row values match the pending state to clear it
  if (row.power_save_listen_only !== undefined && row.power_save_listen_only !== null && pendingPowerSaveListenOnly !== undefined) {
    if (row.power_save_listen_only === pendingPowerSaveListenOnly) {
      pendingPowerSaveListenOnly = undefined;
      pendingPowerSaveTxMs = undefined;
    }
  }

  // Timeout pending state after 45 seconds if no response
  if (pendingPowerSaveTxMs && now - pendingPowerSaveTxMs > 45000) {
    pendingPowerSaveListenOnly = undefined;
    pendingPowerSaveTxMs = undefined;
  }

  const heapFree = row.heap_free || history.heap_free;
  const heapMaxBlock = row.heap_max_block || history.heap_max_block;
  const heapFragPct = row.heap_frag_pct || history.heap_frag_pct;
  
  const relayState = (row.relay_state !== undefined && row.relay_state !== null) ? row.relay_state : history.relay_state;
  const relayFeedback = (row.relay_feedback !== undefined && row.relay_feedback !== null) ? row.relay_feedback : history.relay_feedback;
  const inputState = (row.input_state !== undefined && row.input_state !== null) ? row.input_state : history.input_state;
  const inputFeedback = (row.input_feedback !== undefined && row.input_feedback !== null) ? row.input_feedback : history.input_feedback;
  
  const tempEnabled = (row.temp_enabled !== undefined && row.temp_enabled !== null) ? row.temp_enabled : history.temp_enabled;
  const tempValid = (row.temp_valid !== undefined && row.temp_valid !== null) ? row.temp_valid : history.temp_valid;
  const tempC = (row.temp_valid) ? row.temp_c : (tempValid ? history.temp_c : undefined);
  
  const tankEnabled = (row.tank_enabled !== undefined && row.tank_enabled !== null) ? row.tank_enabled : history.tank_enabled;
  const tankValid = (row.tank_valid !== undefined && row.tank_valid !== null) ? row.tank_valid : history.tank_valid;
  const tankStatus = row.tank_status || history.tank_status;
  const tankDepthMm = row.tank_depth_mm || history.tank_depth_mm;
  const tankCurrentMa = row.tank_current_ma || history.tank_current_ma;
  const tankCurrentCentiMa = row.tank_current_centi_ma || history.tank_current_centi_ma;
  const tankVoltageMv = row.tank_voltage_mv || history.tank_voltage_mv;
  
  const rssi = (row.rssi !== undefined && row.rssi !== null && row.rssi !== 0 && row.rssi !== -127) ? row.rssi : history.rssi;

  fleetRowHistory.value[row.address] = {
    ...history,
    uptimeMs: uptime || history.uptimeMs,
    fwVersion,
    fw_build: fwBuild,
    ip,
    wifi_connected: wifiConnected,
    wifi_connected_known: wifiConnectedKnown,
    wifi_enabled: wifiEnabled,
    wifi_enabled_known: wifiEnabledKnown,
    mqtt_connected: mqttConnected,
    mqtt_enabled: mqttEnabled,
    mqtt_known: mqttKnown,
    power_save_listen_only: powerSaveListenOnly,
    power_save_active: powerSaveActive,
    pendingPowerSaveListenOnly,
    pendingPowerSaveTxMs,
    wifi_pending_offline: wifiPendingOffline,
    power_save_deferred: isPowerSaveDeferred,
    heap_free: heapFree,
    heap_max_block: heapMaxBlock,
    heap_frag_pct: heapFragPct,
    relay_state: relayState,
    relay_feedback: relayFeedback,
    input_state: inputState,
    input_feedback: inputFeedback,
    temp_enabled: tempEnabled,
    temp_valid: tempValid,
    temp_c: tempC,
    tank_enabled: tankEnabled,
    tank_valid: tankValid,
    tank_status: tankStatus,
    tank_depth_mm: tankDepthMm,
    tank_current_ma: tankCurrentMa,
    tank_current_centi_ma: tankCurrentCentiMa,
    tank_voltage_mv: tankVoltageMv,
    rssi,
    lastTelemetryTimestamp,
    rowState,
    rowStateUntilMs,
    otaExpectedUntilMs,
    knownRebootUntilMs: history.knownRebootUntilMs,
    rebootExpectedUntilMs: history.rebootExpectedUntilMs
  };

  return {
    ...row,
    fw_version: fwVersion,
    fw_build: fwBuild,
    ip,
    wifi_connected_known: wifiConnectedKnown,
    wifi_connected: wifiConnected,
    wifi_enabled_known: wifiEnabledKnown,
    wifi_enabled: wifiEnabled,
    mqtt_known: mqttKnown,
    mqtt_connected: mqttConnected,
    mqtt_enabled: mqttEnabled,
    power_save_listen_only: powerSaveListenOnly,
    power_save_active: powerSaveActive,
    pending_power_save_listen_only: pendingPowerSaveListenOnly,
    pending_power_save_tx_ms: pendingPowerSaveTxMs,
    wifi_pending_offline: wifiPendingOffline,
    power_save_deferred: isPowerSaveDeferred,
    heap_free: heapFree,
    heap_max_block: heapMaxBlock,
    heap_frag_pct: heapFragPct,
    relay_state: relayState,
    relay_feedback: relayFeedback,
    input_state: inputState,
    input_feedback: inputFeedback,
    temp_enabled: tempEnabled,
    temp_valid: tempValid,
    temp_c: tempC,
    tank_enabled: tankEnabled,
    tank_valid: tankValid,
    tank_status: tankStatus,
    tank_depth_mm: tankDepthMm,
    tank_current_ma: tankCurrentMa,
    tank_current_centi_ma: tankCurrentCentiMa,
    tank_voltage_mv: tankVoltageMv,
    rssi: rssi ?? row.rssi,
    age_ms: ageMs,
    row_state: rowState,
    row_state_until_ms: rowStateUntilMs
  };
}

function fleetRowClass(device: LoraInventoryDevice): string {
  if (device.row_state === 'unexpected_reboot') return 'bg-rose-950/50 ring-1 ring-rose-500/50';
  if (device.row_state === 'ota_failed') return 'bg-rose-950/40 ring-1 ring-rose-500/40';
  if (device.row_state === 'ota_updated') return 'bg-emerald-950/40 ring-1 ring-emerald-500/40';
  if (device.row_state === 'ota_rebooted') return 'bg-orange-950/40 ring-1 ring-orange-500/40';
  if (device.row_state === 'ota_no_reboot') return 'bg-amber-950/40 ring-1 ring-amber-500/40';
  if (device.row_state === 'ota_pending') return 'bg-cyan-950/30';
  if (device.row_state === 'ota_downloading') return 'bg-cyan-950/40 ring-1 ring-cyan-500/40 animate-pulse';
  if (device.row_state === 'ota_apply_wait') return 'bg-sky-950/40 ring-1 ring-sky-500/40';
  if (device.row_state === 'ota_retrying') return 'bg-amber-950/30 ring-1 ring-amber-500/30 animate-pulse';
  if (device.row_state === 'ota_queued') return 'bg-fuchsia-950/30 ring-1 ring-fuchsia-500/30';
  return 'bg-slate-950/20';
}

function fleetRowStatusLabel(device: LoraInventoryDevice): string {
  if (device.row_state === 'unexpected_reboot') return 'Unexpected reboot';
  if (device.row_state === 'ota_failed') return 'OTA Failed';
  if (device.row_state === 'ota_updated') return 'Updated';
  if (device.row_state === 'ota_rebooted') return 'Rebooted';
  if (device.row_state === 'ota_no_reboot') return 'No reboot seen';
  if (device.row_state === 'ota_pending') return 'Waiting for reboot';
  if (device.row_state === 'ota_downloading') return 'Downloading OTA...';
  if (device.row_state === 'ota_apply_wait') return 'Waiting for reboot';
  if (device.row_state === 'ota_retrying') {
    const history = fleetRowHistory.value[device.address] || {};
    const retryCount = history.otaRetryCount || 0;
    return `Retrying (${retryCount}/3)...`;
  }
  if (device.row_state === 'ota_queued') {
    const qIdx = otaQueue.value.findIndex(d => d.address === device.address);
    return qIdx >= 0 ? `Queued for OTA (#${qIdx + 1})` : 'Queued for OTA';
  }
  return '';
}

function mergeMonitorPeerRows(rows: LoraInventoryDevice[]) {
  monitorFleetRows.value = rows.slice().sort((a, b) => a.address - b.address);
}

function mergeLoraInventoryRows(rows: LoraInventoryDevice[]) {
  const selected = new Set(loraInventory.value.filter(d => d.selected).map(d => d.address));
  const now = Date.now();
  loraInventory.value = rows
    .slice()
    .sort((a, b) => a.address - b.address)
    .map(row => classifyFleetRow({ ...row, selected: selected.has(row.address) }, now));
  if (gatewaySelectedPort.value && gatewaySelectedPort.value === monitorSelectedPort.value) {
    mergeMonitorPeerRows(rows);
  }
}

async function refreshLoraInventoryStatus(background = true) {
  await refreshGatewaySnapshot(gatewaySelectedPort.value, background, 'fleet');
}

function startLoraInventoryPolling() {
  stopLoraInventoryPolling(false, false);
  networkInventoryPollMode.value = 'scan';
  networkInventoryPollTimer.value = window.setInterval(() => {
    refreshLoraInventoryStatus();
  }, FLEET_SCAN_POLL_INTERVAL_MS);
}

function startFleetCachePolling() {
  if (activeMode.value !== 'network' || !gatewaySelectedPort.value || isLoraInventoryScanning.value) return;
  if (networkInventoryPollTimer.value && networkInventoryPollMode.value === 'cache') return;
  stopLoraInventoryPolling(false, false);
  networkInventoryPollMode.value = 'cache';
  networkInventoryPollTimer.value = window.setInterval(() => {
    refreshLoraInventoryStatus(true);
  }, FLEET_CACHE_POLL_INTERVAL_MS);
}

function stopLoraInventoryPolling(markIdle = true, clearMode = true) {
  if (networkInventoryPollTimer.value) {
    window.clearInterval(networkInventoryPollTimer.value);
    networkInventoryPollTimer.value = null;
  }
  if (clearMode) networkInventoryPollMode.value = null;
  if (markIdle) isLoraInventoryScanning.value = false;
}

function rowFreshness(row: LoraInventoryDevice): 'live' | 'stale' | 'offline' | 'unknown' {
  const age = Number(row.age_ms || 0);
  if (!row.age_ms && row.age_ms !== 0) return 'unknown';
  if (age <= FLEET_FRESH_MS) return 'live';
  if (age <= FLEET_STALE_MS) return 'stale';
  return 'offline';
}

function fleetFreshnessClass(row: LoraInventoryDevice): string {
  const state = rowFreshness(row);
  if (state === 'live') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (state === 'stale') return 'border-amber-500/30 bg-amber-500/10 text-amber-300';
  if (state === 'offline') return 'border-slate-600 bg-slate-800/50 text-slate-400';
  return 'border-slate-800 bg-slate-900/50 text-slate-500';
}

function monitorFreshnessClass(row: LoraInventoryDevice): string {
  const state = rowFreshness(row);
  if (state === 'live') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (state === 'stale') return 'border-amber-500/30 bg-amber-500/10 text-amber-300';
  if (state === 'offline') return 'border-rose-500/30 bg-rose-500/10 text-rose-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
}

function monitorFreshnessLabel(row: LoraInventoryDevice): string {
  const state = rowFreshness(row);
  if (state === 'unknown') return 'Unknown';
  const label = state.charAt(0).toUpperCase() + state.slice(1);
  if (row.age_ms === undefined || row.age_ms === null) return label;
  return `${label} · ${formatUptime(Number(row.age_ms || 0))}`;
}

function monitorDebugValue<T>(row: LoraInventoryDevice, value: T | undefined | null, formatter: (value: T) => string): string {
  if (value !== undefined && value !== null) return formatter(value);
  return row.maintenance_debug_known ? '0' : 'waiting';
}

function monitorHeapLabel(row: LoraInventoryDevice): string {
  if (row.heap_free !== undefined || row.heap_max_block !== undefined) {
    return `${formatBytes(row.heap_free)} / ${formatBytes(row.heap_max_block)}`;
  }
  return row.maintenance_debug_known ? '0 B / 0 B' : 'waiting';
}

function monitorFragLabel(row: LoraInventoryDevice): string {
  return monitorDebugValue(row, row.heap_frag_pct, value => `${value}%`);
}

function monitorUptimeLabel(row: LoraInventoryDevice): string {
  const uptime = row.uptime_ms || row.debug_uptime_ms;
  return uptime ? formatUptime(uptime) : (row.maintenance_debug_known ? '0s' : 'waiting');
}

function remoteInputLabel(row: LoraInventoryDevice): string {
  const value = row.input_feedback;
  if (value === undefined || value === null) return 'waiting';
  return Number(value) === 1 ? 'Closed' : 'Open';
}

function remoteTempLabel(row: LoraInventoryDevice): string {
  return row.temp_valid && row.temp_c !== undefined && row.temp_c !== null ? `${row.temp_c} °C` : '-';
}

function tankLabel(row: LoraInventoryDevice): string {
  if (!row.tank_enabled) return '-';
  if (row.tank_status === 'fault_low') return 'Fault low';
  if (row.tank_status === 'overrange') return 'Overrange';
  if (row.tank_valid && row.tank_depth_mm !== undefined && row.tank_depth_mm !== null) {
    return `${row.tank_depth_mm} mm`;
  }
  return 'waiting';
}

function tankDetailLabel(row: LoraInventoryDevice): string {
  if (!row.tank_enabled) return '';
  const parts: string[] = [];
  if (row.tank_current_ma !== undefined && row.tank_current_ma !== null) {
    parts.push(`${Number(row.tank_current_ma).toFixed(2)} mA`);
  } else if (row.tank_current_centi_ma !== undefined && row.tank_current_centi_ma !== null) {
    parts.push(`${(Number(row.tank_current_centi_ma) / 100).toFixed(2)} mA`);
  }
  if (row.tank_voltage_mv !== undefined && row.tank_voltage_mv !== null) {
    parts.push(`${row.tank_voltage_mv} mV`);
  }
  return parts.join(' / ');
}

function adoptMonitorMqttFromStatus(st: SerialAdminStatus | null) {
  if (!st?.mqtt) return;
  monitorMqttHost.value = st.mqtt.host || monitorMqttHost.value;
  monitorMqttPort.value = Number(st.mqtt.port || monitorMqttPort.value || 1883);
  monitorMqttTopicRoot.value = st.mqtt.topic_root || monitorMqttTopicRoot.value || 'lora';
}

function openMonitorMqttSettings() {
  monitorMqttDraftHost.value = monitorMqttHost.value || 'venus.local';
  monitorMqttDraftPort.value = Number(monitorMqttPort.value || 1883);
  monitorMqttDraftUser.value = monitorMqttUser.value;
  monitorMqttDraftPassword.value = monitorMqttPassword.value;
  monitorMqttDraftTopicRoot.value = monitorMqttTopicRoot.value || 'lora';
  showMonitorMqttSettings.value = true;
}

function closeMonitorMqttSettings() {
  showMonitorMqttSettings.value = false;
}

async function refreshMonitorData(background = false) {
  const port = monitorSelectedPort.value;
  if (!port || isMonitorRefreshing.value) return;
  if (background && activeMode.value !== 'monitor') return;
  isMonitorRefreshing.value = true;
  try {
    await refreshGatewaySnapshot(port, background, 'monitor');
  } catch (e) {
    if (serialBackgroundSkipped(e)) return;
    monitorStatusMessage.value = serialFeatureError('Monitor refresh', e);
    notify(monitorStatusMessage.value);
  } finally {
    isMonitorRefreshing.value = false;
  }
}

async function refreshGatewaySnapshot(port: string, background = true, source: 'fleet' | 'monitor' = 'monitor') {
  if (!port) return;
  if (background && gatewaySnapshotPauseCount.value > 0) return;
  try {
    const status = await sendEasyPairCommandOnPort<SerialAdminStatus>(
      port,
      'status',
      {},
      5000,
      { label: 'Gateway status snapshot', priority: background ? 'background' : 'user', dropIfBusy: background }
    );
    applySerialAdminStatus(status, port);
    if (port === monitorSelectedPort.value) adoptMonitorMqttFromStatus(status);
    if (!status.role_tx) {
      if (source === 'fleet') {
        networkStatusMessage.value = gatewayRequiredMessage('Fleet');
        loraInventory.value = [];
        loraInventoryScan.value = null;
        isLoraInventoryScanning.value = false;
        stopLoraInventoryPolling(false);
        if (gatewaySelectedPort.value === port) {
          gatewaySelectedPort.value = '';
        }
      } else {
        monitorStatusMessage.value = gatewayRequiredMessage('Monitor');
        monitorFleetRows.value = [];
        stopMonitorPolling();
        if (monitorSelectedPort.value === port) {
          monitorSelectedPort.value = '';
        }
      }
      if (!background) notify(source === 'fleet' ? networkStatusMessage.value : monitorStatusMessage.value);
      return;
    }

    const inventory = await sendEasyPairCommandOnPort<LoraInventoryStatus>(
      port,
      'lora_inventory_status',
      {},
      5000,
      { label: 'Gateway peer cache snapshot', priority: background ? 'background' : 'user', dropIfBusy: background }
    );

    if (port === gatewaySelectedPort.value) {
      loraInventoryScan.value = inventory.scan || null;
      mergeLoraInventoryRows(inventory.devices || []);
      isLoraInventoryScanning.value = !!inventory.scan?.active;
      networkStatusMessage.value = `${loraInventoryProgressLabel.value}; gateway cache has ${loraInventory.value.length} peer${loraInventory.value.length === 1 ? '' : 's'}.`;
      if (!inventory.scan?.active && networkInventoryPollMode.value === 'scan') {
        stopLoraInventoryPolling(false);
        startFleetCachePolling();
      }
    }
    if (port === monitorSelectedPort.value) {
      mergeMonitorPeerRows(inventory.devices || []);
      monitorStatusMessage.value = `Updated ${new Date().toLocaleTimeString()} · ${monitorFleetRows.value.length} peer${monitorFleetRows.value.length === 1 ? '' : 's'} visible.`;
    }
  } catch (e) {
    if (serialBackgroundSkipped(e)) return;
    if (source === 'fleet') {
      networkStatusMessage.value = serialFeatureError('Fleet status', e);
      return;
    }
    throw e;
  }
}

async function withGatewayForeground<T>(port: string, work: () => Promise<T>): Promise<T> {
  const resumeMonitorLoop = isMonitorLoopRunning.value && monitorAutoRefresh.value && monitorSelectedPort.value === port;
  gatewaySnapshotPauseCount.value++;
  stopMonitorPolling();
  stopLoraInventoryPolling(false);
  try {
    return await work();
  } finally {
    gatewaySnapshotPauseCount.value = Math.max(0, gatewaySnapshotPauseCount.value - 1);
    if (resumeMonitorLoop && monitorAutoRefresh.value && monitorSelectedPort.value === port) {
      startMonitorPolling();
    }
    if (activeMode.value === 'network' && gatewaySelectedPort.value === port && !isLoraInventoryScanning.value) {
      startFleetCachePolling();
    }
  }
}

function startMonitorPolling() {
  stopMonitorPolling();
  isMonitorLoopRunning.value = true;
  if (monitorAutoRefresh.value) {
    monitorPollTimer.value = window.setInterval(() => {
      refreshMonitorData(true);
    }, 5000);
  }
}

function stopMonitorPolling() {
  isMonitorLoopRunning.value = false;
  if (monitorPollTimer.value) {
    window.clearInterval(monitorPollTimer.value);
    monitorPollTimer.value = null;
  }
}

function toggleMonitorLoop() {
  if (isMonitorLoopRunning.value) {
    stopMonitorPolling();
    return;
  }
  startMonitorPolling();
  refreshMonitorData(false).finally(() => {
    if (!monitorAutoRefresh.value) stopMonitorPolling();
  });
}

function toggleMonitorMqttConnection() {
  monitorMqttHost.value = monitorMqttDraftHost.value.trim() || 'venus.local';
  monitorMqttPort.value = Number(monitorMqttDraftPort.value || 1883);
  monitorMqttUser.value = monitorMqttDraftUser.value;
  monitorMqttPassword.value = monitorMqttDraftPassword.value;
  monitorMqttTopicRoot.value = monitorMqttDraftTopicRoot.value.trim() || 'lora';
  monitorTransport.value = 'mqtt';
  monitorMqttConnected.value = !monitorMqttConnected.value;
  monitorStatusMessage.value = monitorMqttConnected.value
    ? 'MQTT monitor configuration saved locally. Subscription backend is not active yet.'
    : 'MQTT monitor disconnected.';
  showMonitorMqttSettings.value = false;
}

async function ensureFleetGatewayStatus(force = false): Promise<SerialAdminStatus | null> {
  const port = gatewaySelectedPort.value;
  const state = serialDeviceState(port);
  if (!force && state?.status) return state.status;
  try {
    const out = await sendEasyPairCommandOnPort<SerialAdminStatus>(port, 'status', {}, 5000);
    applySerialAdminStatus(out, port);
    return out;
  } catch {
    return null;
  }
}

function fleetScanBlockedMessage(st: SerialAdminStatus | null): string | null {
  if (!st) return null;
  if (!st.role_tx) {
    return gatewayRequiredMessage('Fleet');
  }
  if (!st.commissioned) {
    return 'Fleet scan needs a commissioned gateway. Use Provision first to assign the fleet key, role, address, and WiFi.';
  }
  if (st.fleet_passphrase_default) {
    return 'Fleet scan needs a secure fleet key. Use Provision first to replace the factory key.';
  }
  return null;
}

function fleetScanErrorMessage(err: unknown): string {
  const text = String(err || '');
  if (text.includes('not_commissioned')) {
    return 'Fleet scan needs a commissioned gateway. Use Provision first to assign the fleet key, role, address, and WiFi.';
  }
  if (text.includes('factory_fleet_key')) {
    return 'Fleet scan needs a secure fleet key. Use Provision first to replace the factory key.';
  }
  if (text.includes('not_gateway')) {
    return gatewayRequiredMessage('Fleet');
  }
  return serialFeatureError('LoRa inventory scan', err);
}

async function startLoraInventoryScan() {
  const port = gatewaySelectedPort.value;
  if (!port) {
    notify('Select the USB gateway first');
    return;
  }
  const remaining = fleetForceScanCooldownRemainingMs.value;
  if (remaining > 0) {
    notify(`Force Scan available in ${Math.ceil(remaining / 1000)}s`);
    return;
  }
  await beginLoraInventoryScan(port, true);
}

async function beginLoraInventoryScan(port: string, showErrors = true) {
  await withGatewayForeground(port, async () => {
    if (!portGatewayReady(port)) await loadNetworkGateway();
    const password = adminPasswordForPort(port);
    if (!password) {
      if (showErrors) notify('Unable to read the gateway admin password from device details');
      return;
    }
    const gatewayStatus = await ensureFleetGatewayStatus(true);
    const blockedMessage = fleetScanBlockedMessage(gatewayStatus);
    if (blockedMessage) {
      isLoraInventoryScanning.value = false;
      networkStatusMessage.value = blockedMessage;
      if (showErrors) notify(blockedMessage);
      return;
    }
    try {
      isLoraInventoryScanning.value = true;
      fleetForceScanCooldownUntilMs.value = Math.max(fleetForceScanCooldownUntilMs.value, Date.now() + FLEET_FORCE_SCAN_COOLDOWN_MS);
      await sendEasyPairCommandOnPort(port, 'start_lora_inventory', {
        admin_password: password,
        start_address: 1,
        end_address: LRS_REMOTE_SCAN_CAP,
        interval_ms: 1500
      }, 8000);
      networkStatusMessage.value = 'LoRa inventory scan started.';
      await refreshLoraInventoryStatus(false);
      startLoraInventoryPolling();
    } catch (e) {
      isLoraInventoryScanning.value = false;
      networkStatusMessage.value = fleetScanErrorMessage(e);
      if (showErrors) notify(networkStatusMessage.value);
    }
  });
}

async function cancelLoraInventoryScan() {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    await withGatewayForeground(port, async () => {
      await sendEasyPairCommandOnPort(port, 'cancel_lora_inventory', { admin_password: password }, 5000);
      stopLoraInventoryPolling();
      await refreshLoraInventoryStatus(false);
    });
    startFleetCachePolling();
  } catch (e) {
    notify(serialFeatureError('Cancel LoRa inventory', e));
  }
}

function firmwareServerTarget(info: FirmwareServerInfo): { host: string; port: number } {
  const raw = info.urls.find(u => !u.includes('127.0.0.1')) || info.urls[0] || '';
  if (!raw) throw new Error('Firmware server has no reachable URL');
  const parsed = new URL(raw);
  return {
    host: parsed.hostname,
    port: Number(parsed.port || info.port)
  };
}

function fleetLogsAvailable(device: LoraInventoryDevice): boolean {
  return !!device.wifi_connected_known && !!device.wifi_connected && !!device.ip;
}

function fleetDeviceUdpLabel(device: LoraInventoryDevice): string {
  const chip = String(device.chip_id || '').trim().replace(/^0x/i, '').toLowerCase();
  const name = chip ? `lrs-${chip}` : `addr ${device.address}`;
  const role = [device.role, device.mode].filter(Boolean).join('/');
  return `${name} addr ${device.address}${role ? ` (${role})` : ''}`;
}

const flasherInterfaces = ref<NetworkInterface[]>([]);

function fleetFlashAvailable(device: LoraInventoryDevice): boolean {
  return fleetFlashUnavailableReason(device) === 'Ready to trigger OTA pull';
}

function fleetFlashUnavailableReason(_device: LoraInventoryDevice): string {
  // Allow triggering remote OTA pull even if the device's WiFi is currently offline or it has no IP,
  // since the LoRa start command will wake up the Wi-Fi stack and connect dynamically.
  return 'Ready to trigger OTA pull';
}

function markFleetOtaPending(device: LoraInventoryDevice) {
  const now = Date.now();
  fleetRowHistory.value[device.address] = {
    ...(fleetRowHistory.value[device.address] || {}),
    uptimeMs: device.uptime_ms || fleetRowHistory.value[device.address]?.uptimeMs,
    fwVersion: device.fw_version || fleetRowHistory.value[device.address]?.fwVersion,
    otaExpectedUntilMs: now + 180000,
    rowState: 'ota_downloading',
    rowStateUntilMs: now + 180000
  };
  loraInventory.value = loraInventory.value.map(row =>
    row.address === device.address
      ? { ...row, row_state: 'ota_downloading', row_state_until_ms: now + 180000 }
      : row
  );
}

async function refreshFleetOtaFollowup(address: number) {
  const history = fleetRowHistory.value[address];
  if (!history?.otaExpectedUntilMs) return;
  const now = Date.now();
  if (history.rowState === 'ota_rebooted' || history.rowState === 'ota_updated') {
    delete fleetOtaFollowupTimers.value[address];
    return;
  }
  if (now >= history.otaExpectedUntilMs) {
    fleetRowHistory.value[address] = {
      ...history,
      rowState: 'ota_no_reboot',
      rowStateUntilMs: now + 60000
    };
    loraInventory.value = loraInventory.value.map(row =>
      row.address === address
        ? { ...row, row_state: 'ota_no_reboot', row_state_until_ms: now + 60000 }
        : row
    );
    delete fleetOtaFollowupTimers.value[address];
    return;
  }
  try {
    const port = gatewaySelectedPort.value;
    const password = adminPasswordForPort(port);
    if (!password) throw new Error('missing gateway password');
    await sendEasyPairCommandOnPort(port, 'start_lora_inventory', {
      admin_password: password,
      start_address: address,
      end_address: address,
      interval_ms: 250
    }, 8000);
    await new Promise(resolve => setTimeout(resolve, 900));
    await refreshLoraInventoryStatus();
  } catch (e) {
    pushNetworkLog(serialFeatureError(`Flash follow-up ${address}`, e));
  } finally {
    const nextHistory = fleetRowHistory.value[address];
    if (nextHistory?.otaExpectedUntilMs && Date.now() < nextHistory.otaExpectedUntilMs &&
        nextHistory.rowState !== 'ota_rebooted' && nextHistory.rowState !== 'ota_updated') {
      fleetOtaFollowupTimers.value[address] = window.setTimeout(() => {
        refreshFleetOtaFollowup(address);
      }, 4000);
    } else {
      delete fleetOtaFollowupTimers.value[address];
    }
  }
}

function startFleetOtaFollowup(device: LoraInventoryDevice) {
  const existing = fleetOtaFollowupTimers.value[device.address];
  if (existing) window.clearTimeout(existing);
  fleetOtaFollowupTimers.value[device.address] = window.setTimeout(() => {
    refreshFleetOtaFollowup(device.address);
  }, 2500);
}

async function startFleetUdpLogs(device: LoraInventoryDevice) {
  if (remoteUdpBusyAddress.value != null) return;
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!fleetLogsAvailable(device)) {
    notify('UDP logs need confirmed WiFi connection and IP from fleet status');
    return;
  }
  const targetLabel = fleetDeviceUdpLabel(device);
  try {
    remoteUdpBusyAddress.value = device.address;
    if (!portGatewayReady(port)) await loadNetworkGateway();
    const hosts = await invoke<string[]>('local_udp_log_hosts');
    const host = hosts[0];
    if (!host) throw new Error('No reachable Flasher LAN address found');
    if (!isNetworkUdpMonitoring.value) {
      await startNetworkUdpListener();
    }
    await sendEasyPairCommandOnPort(port, 'remote_udp_log_control', {
      admin_password: password,
      address: device.address,
      enabled: true,
      host,
      port: 5514,
      ttl_s: 300
    }, 8000);
    networkUdpTarget.value = targetLabel;
    networkStatusMessage.value = `UDP logging enabled for ${targetLabel} to ${host}:5514.`;
    notify(`UDP logs enabled for ${targetLabel}`);
  } catch (e) {
    const msg = serialFeatureError(`UDP logs ${targetLabel}`, e);
    networkStatusMessage.value = msg;
    pushNetworkLog(msg);
    notify(msg);
  } finally {
    remoteUdpBusyAddress.value = null;
  }
}

async function flashLoraRemote(device: LoraInventoryDevice) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!fleetFlashAvailable(device)) {
    notify(fleetFlashUnavailableReason(device));
    return;
  }
  if (otaQueue.value.find(d => d.address === device.address)) return;
  
  otaQueue.value.push(device);
  fleetRowHistory.value[device.address] = {
    ...(fleetRowHistory.value[device.address] || {}),
    rowState: 'ota_queued',
    rowStateUntilMs: Date.now() + 300000
  };
  
  loraInventory.value = loraInventory.value.map(row => 
    row.address === device.address ? { ...row, row_state: 'ota_queued' } : row
  );
  
  if (otaQueue.value.length === 1 && remoteOtaBusyAddress.value === null) {
    processOtaQueue();
  }
}

async function triggerOtaFailureOrRetry(device: LoraInventoryDevice) {
  const history = fleetRowHistory.value[device.address] || {};
  const currentRetry = (history.otaRetryCount || 0) + 1;

  if (remoteOtaBusyAddress.value === device.address) {
    remoteOtaBusyAddress.value = null;
  }
  otaQueue.value = otaQueue.value.filter(d => d.address !== device.address);

  if (currentRetry <= 3) {
    const jitter = Math.floor(Math.random() * 3000);
    const retryDelay = 5000 + jitter;
    notify(`OTA failure detected for Address ${device.address}. Retrying (${currentRetry}/3) in ${(retryDelay / 1000).toFixed(1)} seconds...`);
    
    fleetRowHistory.value[device.address] = {
      ...history,
      rowState: 'ota_retrying',
      rowStateUntilMs: Date.now() + retryDelay + 1000,
      otaRetryCount: currentRetry,
      otaExpectedUntilMs: Date.now() + 180000
    };

    loraInventory.value = loraInventory.value.map(row => 
      row.address === device.address 
        ? { ...row, row_state: 'ota_retrying', row_state_until_ms: Date.now() + retryDelay + 1000 } 
        : row
    );

    setTimeout(() => {
      const checkAndRetry = () => {
        const currentDev = loraInventory.value.find(d => d.address === device.address);
        if (!currentDev || currentDev.row_state !== 'ota_retrying') {
          // The device successfully completed/applied the OTA or was cancelled, halt the retry!
          return;
        }
        if (remoteOtaBusyAddress.value === null) {
          notify(`Retrying OTA flash for remote ${device.address} now...`);
          
          // Clear watchdog activity for the retry attempt
          const cleanHistory = fleetRowHistory.value[device.address] || {};
          delete cleanHistory.lastOtaActivityMs;
          
          flashLoraRemote(device);
        } else {
          setTimeout(checkAndRetry, 5000);
        }
      };
      checkAndRetry();
    }, retryDelay);
  } else {
    notify(`OTA for Address ${device.address} failed after 3 attempts.`);
    fleetRowHistory.value[device.address] = {
      ...history,
      rowState: 'ota_failed',
      rowStateUntilMs: undefined,
      otaExpectedUntilMs: undefined,
      otaRetryCount: 0
    };
    loraInventory.value = loraInventory.value.map(row => 
      row.address === device.address 
        ? { ...row, row_state: 'ota_failed', row_state_until_ms: undefined } 
        : row
    );
  }
}

function checkOtaProgressWatchdog() {
  const now = Date.now();
  loraInventory.value.forEach(device => {
    if (device.row_state === 'ota_downloading') {
      const history = fleetRowHistory.value[device.address] || {};
      const otaStartedMs = history.otaExpectedUntilMs ? history.otaExpectedUntilMs - 180000 : now;
      const lastActivity = history.lastOtaActivityMs;
      
      if (lastActivity) {
        if (now - lastActivity > 8000) {
          notify(`Address ${device.address} OTA chunk progress stalled (8s silence). Retrying...`);
          triggerOtaFailureOrRetry(device);
        }
      } else {
        if (now - otaStartedMs > 10000) {
          notify(`Address ${device.address} OTA start request timed out (10s silence). Retrying...`);
          triggerOtaFailureOrRetry(device);
        }
      }
    }
  });
}

async function processOtaQueue() {
  if (remoteOtaBusyAddress.value != null || otaQueue.value.length === 0) return;



  const device = otaQueue.value[0];
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);

  try {
    remoteOtaBusyAddress.value = device.address;
    if (!portGatewayReady(port)) await loadNetworkGateway();
    const info = await ensureRemoteFlashFirmwareServer();
    const target = firmwareServerTarget(info);
    
    // Automatically start UDP logs to monitor the OTA progress over WiFi before sending the OTA pull command.
    // If we send it after, the device is already busy with the OTA and might drop the LoRa packet.
    await startFleetUdpLogs(device).catch(e => pushNetworkLog(`Failed to start UDP logs for ${device.address}: ${e}`));

    // Wait 3.0 seconds to give the remote device time to process the UDP log command and transmit
    // any resulting ACK/telemetry over LoRa. This prevents half-duplex radio collisions that drop OTA chunks.
    await new Promise(r => setTimeout(r, 3000));

    const out = await sendEasyPairCommandOnPort<any>(port, 'remote_ota_pull', {
      admin_password: password,
      address: device.address,
      host: target.host,
      port: target.port,
      sha256: info.sha256
    }, 8000);
    markFleetOtaPending(device);
    startFleetOtaFollowup(device);
    networkStatusMessage.value = `Remote OTA pull triggered for LoRa ${device.address} from ${target.host}:${target.port}.`;
    pushNetworkLog(`Remote OTA pull: addr ${device.address} -> http://${target.host}:${target.port}${NETWORK_FIRMWARE_PATH} (${out.path || NETWORK_FIRMWARE_PATH}), SHA256 ${info.sha256}`);
    notify(`Flash triggered for LoRa ${device.address}`);
  } catch (e) {
    const msg = serialFeatureError(`Remote flash ${device.address}`, e);
    networkStatusMessage.value = msg;
    pushNetworkLog(msg);
    notify(msg);
    delete fleetRowHistory.value[device.address];
    loraInventory.value = loraInventory.value.map(row => 
      row.address === device.address ? { ...row, row_state: undefined, row_state_until_ms: undefined } : row
    );
  } finally {
    remoteOtaBusyAddress.value = null;
    otaQueue.value.shift();
    if (otaQueue.value.length > 0) {
      setTimeout(() => processOtaQueue(), 2500);
    }
  }
}

function toggleFleetDropdown(address: number) {
  if (activeDropdownAddress.value === address) {
    activeDropdownAddress.value = null;
  } else {
    activeDropdownAddress.value = address;
  }
}


function openSettingsModal(device: LoraInventoryDevice, tab: SettingsModalState['activeTab'] = 'sensors') {
  activeDropdownAddress.value = null;
  const ssid = pairWifiSsid.value || '';
  settingsDeviceModal.value = {
    device,
    activeTab: tab,
    wifi_ssid: ssid,
    wifi_password: getCachedWifiPassword(ssid) || pairAdminPassword.value || '',
    sensor_temp_enabled: !!device.temp_enabled || !!device.temp_valid || (device.temp_c !== undefined && device.temp_c !== null),
    sensor_tank_enabled: !!device.tank_enabled,
    power_save_listen_only: !!device.power_save_listen_only,
    fleet_key: '',
    fleet_key_confirmed: false,
    show_fleet_key: false
  };
}

async function executeRemoteWifi(device: LoraInventoryDevice, ssid: string, password_value: string) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    notify(`Sending WiFi details to remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'provision_fleet_wifi', {
      admin_password: password,
      target_address: device.address,
      ssid: ssid,
      password_value: password_value
    }, 15000);
    notify(`WiFi credentials sent successfully to remote ${device.address}`);
    settingsDeviceModal.value = null;
  } catch (e) {
    const msg = serialFeatureError(`Remote WiFi`, e);
    notify(msg);
  }
}


async function executeRemoteSensors(device: LoraInventoryDevice, tempEnabled: boolean, tankEnabled: boolean, powerSaveEnabled: boolean, graceEnabled: boolean = true) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    const isPowerSaveChange = powerSaveEnabled !== device.power_save_listen_only;
    const actionLabel = isPowerSaveChange ? 'power configuration' : 'sensor configuration';
    notify(`Transmitting ${actionLabel} to remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'remote_sensor_config', {
      admin_password: password,
      target_address: device.address,
      sensor_temp_enabled: tempEnabled,
      sensor_tank_enabled: tankEnabled,
      power_save_listen_only: powerSaveEnabled,
      power_save_boot_grace: graceEnabled
    }, 8000);
    notify(`${actionLabel.charAt(0).toUpperCase() + actionLabel.slice(1)} transmitted successfully to remote ${device.address}`);
    // Save to history so it survives refreshes
    const now = Date.now();
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      pendingPowerSaveListenOnly: powerSaveEnabled,
      pendingPowerSaveTxMs: now
    };
    loraInventory.value = loraInventory.value.map(row => {
      if (row.address === device.address) {
        return { 
          ...row, 
          temp_enabled: tempEnabled, 
          tank_enabled: tankEnabled,
          pending_power_save_listen_only: powerSaveEnabled,
          pending_power_save_tx_ms: now
        };
      }
      return row;
    });
    settingsDeviceModal.value = null;
  } catch (e) {
    const msg = serialFeatureError(`Remote sensors update`, e);
    notify(msg);
  }
}

async function executeRemoteReboot(device: LoraInventoryDevice) {
  activeDropdownAddress.value = null;
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!await confirmOperatorAction(`Reboot remote device ${device.address}?`, { confirmText: 'Reboot remote', danger: true })) {
    return;
  }
  try {
    notify(`Sending reboot command to remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'remote_reboot', {
      admin_password: password,
      target_address: device.address
    }, 8000);
    notify(`Reboot command sent to remote ${device.address}`);
    
    // Track known reboot to prevent unexpected reboot status
    const now = Date.now();
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      knownRebootUntilMs: now + 60000,
      rebootExpectedUntilMs: now + 240000
    };
  } catch (e) {
    const msg = serialFeatureError(`Remote reboot`, e);
    notify(msg);
  }
}

function openFactoryResetModal(device: LoraInventoryDevice) {
  activeDropdownAddress.value = null;
  factoryResetTargetModal.value = {
    device,
    keep_shared_fleet_key: true,
    keep_wifi_credentials: true
  };
}

async function executeRemoteFactoryReset(device: LoraInventoryDevice, keepFleet: boolean, keepWifi: boolean) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    notify(`Triggering factory reset on remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'remote_factory_reset', {
      admin_password: password,
      target_address: device.address,
      keep_shared_fleet_key: keepFleet,
      keep_wifi_credentials: keepWifi
    }, 8000);
    notify(`Factory reset triggered on remote ${device.address}. Device is rebooting.`);

    // Track known reboot to prevent unexpected reboot status
    const now = Date.now();
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      knownRebootUntilMs: now + 60000,
      rebootExpectedUntilMs: now + 240000
    };

    if (!keepFleet) {
      const deviceName = device.chip_id ? lrsDeviceName(device.chip_id) : `Address ${device.address}`;
      notify(`Forgetting remote ${deviceName} from gateway settings...`);
      try {
        await sendEasyPairCommandOnPort(port, 'forget_gateway_target', {
          admin_password: password,
          address: device.address
        });
        notify(`Successfully forgot remote ${deviceName} from gateway`);
      } catch (forgetErr) {
        console.error('Failed to forget gateway target:', forgetErr);
      }
    }

    factoryResetTargetModal.value = null;
    refreshLoraInventoryStatus(false);
  } catch (e) {
    const msg = serialFeatureError(`Remote factory reset`, e);
    notify(msg);
  }
}

async function executeForgetRemote(device: LoraInventoryDevice) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  const deviceName = device.chip_id ? lrsDeviceName(device.chip_id) : `Address ${device.address}`;
  const confirmed = await confirmOperatorAction(
    `Forget remote device ${deviceName}?\n\nThis will permanently delete its address and name pairing from the gateway configuration.`,
    { confirmText: 'Forget device', danger: true }
  );
  if (!confirmed) return;

  notify(`Forgetting remote ${deviceName}...`);
  try {
    await sendEasyPairCommandOnPort(port, 'forget_gateway_target', {
      admin_password: password,
      address: device.address
    });
    notify(`Successfully forgot remote ${deviceName}`);
    refreshLoraInventoryStatus(false);
  } catch (e) {
    const msg = serialFeatureError(`Forget remote`, e);
    notify(msg);
  }
}


async function executeRemoteFleetKeyChange(device: LoraInventoryDevice, newKey: string) {
  const port = gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!newKey || newKey.length < 8) {
    notify('Enter a fleet key of at least 8 characters');
    return;
  }
  if (newKey.length > 64) {
    notify('Enter a fleet key of under 64 characters');
    return;
  }
  isSerialAdminSaving.value = true;
  try {
    notify(`Triggering remote fleet key change on remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'remote_fleet_key_change', {
      admin_password: password,
      target_address: device.address,
      new_fleet_passphrase: newKey
    }, 15000);
    notify(`Remote fleet key change sequence transmitted. Remote ${device.address} is applying and rebooting.`);
    
    // Track known reboot to prevent unexpected reboot status
    const now = Date.now();
    fleetRowHistory.value[device.address] = {
      ...(fleetRowHistory.value[device.address] || {}),
      knownRebootUntilMs: now + 60000,
      rebootExpectedUntilMs: now + 240000
    };
    settingsDeviceModal.value = null;
  } catch (e) {
    const msg = serialFeatureError(`Remote fleet key change`, e);
    notify(msg);
  } finally {
    isSerialAdminSaving.value = false;
  }
}

function fleetGatewayFlashUnavailableReason(): string {
  if (!gatewaySelectedPort.value) return 'Select a USB gateway first';
  if (fleetGatewayFlashPhase.value !== 'idle') return 'Gateway flash is already running';
  if (isNetworkGatewayLoading.value) return 'Gateway identity is loading';
  if (isLoraInventoryScanning.value) return 'Stop the fleet scan before flashing the gateway';
  if (remoteOtaBusyAddress.value !== null) return 'Wait for the remote flash command to finish';
  if (isFirmwareServerStarting.value) return 'Firmware server is starting';
  if (isPairBusy.value) return 'Provisioning is active';
  return 'Upgrade the selected USB gateway';
}

async function flashFleetGateway() {
  const port = gatewaySelectedPort.value;
  if (!port) {
    notify('Select a USB gateway first');
    return;
  }
  if (fleetGatewayFlashDisabled.value) {
    notify(fleetGatewayFlashUnavailableReason());
    return;
  }
  const firmwareOptions = networkOtaFirmwareOptions();
  if (!firmwareOptions) return;
  const label = fleetGatewayIdentity.value?.ssid || fleetGatewayIdentity.value?.serial || port;
  const currentFw = fleetGatewayStatus.value?.fw_version || 'unknown';
  const targetFw = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX)
    ? `${flasherAppVersion.value} (local build)`
    : selectedVersion.value;

  const confirmed = await confirmOperatorAction(
    `Upgrade the USB gateway ${label} on ${port}?\n\n` +
    `• Current version: ${currentFw}\n` +
    `• Upgrade version: ${targetFw}\n\n` +
    `This will reboot the gateway and pause Fleet operations while upgrading.`,
    { confirmText: 'Upgrade gateway', danger: true }
  );
  if (!confirmed) {
    return;
  }
  isFlashing.value = true;
  fleetGatewayFlashPhase.value = 'flashing';
  noteMonitorReleasedForPort(port, 'gateway firmware flash needs this port');
  networkStatusMessage.value = `Flashing USB gateway on ${port}...`;
  pushNetworkLog(`Flashing USB gateway on ${port} with ${firmwareOptions.firmware_path}.`);
  try {
    const out = await invoke<string>('flash_firmware', {
      port,
      firmwarePath: firmwareOptions.firmware_path,
      region: firmwareOptions.region,
      eraseFirst: false
    });
    pushNetworkLog(out || `Gateway flash completed on ${port}.`);
    fleetGatewayFlashPhase.value = 'rebooting';
    networkStatusMessage.value = 'Gateway flash complete; gateway is rebooting.';
    notify('Gateway flash complete');
    await new Promise(resolve => setTimeout(resolve, 4000));
    fleetGatewayFlashPhase.value = 'waiting';
    networkStatusMessage.value = 'Waiting for gateway serial admin after reboot...';
    await waitForSerialAdminHello(port, 18000);
    fleetGatewayFlashPhase.value = 'updated';
    networkStatusMessage.value = 'Gateway rebooted; refreshing status.';
    await loadNetworkGateway();
    window.setTimeout(() => {
      if (fleetGatewayFlashPhase.value === 'updated') {
        fleetGatewayFlashPhase.value = 'idle';
      }
    }, 8000);
  } catch (e) {
    fleetGatewayFlashPhase.value = 'failed';
    const msg = `Gateway flash failed: ${e}`;
    networkStatusMessage.value = msg;
    pushNetworkLog(msg);
    notify(msg);
    window.setTimeout(() => {
      if (fleetGatewayFlashPhase.value === 'failed') {
        fleetGatewayFlashPhase.value = 'idle';
      }
    }, 15000);
  } finally {
    isFlashing.value = false;
  }
}

async function probeSerialAdminSupport(port = selectedPort.value): Promise<boolean> {
  if (!port) return false;
  const state = serialDeviceState(port);
  try {
    await sendEasyPairCommandOnPort<any>(port, 'hello', {}, 1200, { label: 'Probe serial admin' });
    if (state) state.adminSupported = true;
    return true;
  } catch {
    if (state) state.adminSupported = false;
    return false;
  }
}

async function waitForSerialAdminHello(port: string, timeoutMs = 18000): Promise<any> {
  const started = Date.now();
  let lastError: unknown = null;
  while (Date.now() - started < timeoutMs) {
    try {
      const hello = await sendEasyPairCommandOnPort<any>(port, 'hello', {}, 1800, { label: 'Wait for serial admin' });
      const state = serialDeviceState(port);
      if (state) state.adminSupported = true;
      return hello;
    } catch (e) {
      lastError = e;
      await new Promise(resolve => setTimeout(resolve, 900));
    }
  }
  const state = serialDeviceState(port);
  if (state) state.adminSupported = false;
  throw lastError || new Error('timed out waiting for hello response');
}

function serialFeatureError(feature: string, err: unknown): string {
  const text = String(err || 'serial command failed');
  if (text.includes('unknown_cmd')) {
    return `${feature} failed: unknown_cmd - Flash this gateway with the latest firmware and try again.`;
  }
  if (text.includes(' failed: ')) return text;
  return `${feature} failed: ${text}`;
}

function gatewayRequiredMessage(surface: 'Fleet' | 'Monitor'): string {
  return `${surface} requires a TX/gateway USB device. The selected serial port is a remote; choose the gateway port.`;
}

function startIdentifyUiPattern(durationMs: number) {
  if (identifyTimer.value) {
    window.clearTimeout(identifyTimer.value);
    identifyTimer.value = null;
  }
  isIdentifying.value = true;
  identifyTimer.value = window.setTimeout(() => {
    isIdentifying.value = false;
    identifyTimer.value = null;
  }, Math.max(1000, durationMs));
}

async function activeSerialAdminPassword(): Promise<string> {
  if (activeMode.value === 'pair') {
    return pairPassword();
  }
  return deviceInfo.value?.password?.trim() || '';
}

async function triggerIdentify() {
  if (!selectedPort.value) {
    notify('Select a USB device first');
    return;
  }
  if (!hasActiveDeviceInfo.value) {
    notify('Load device details first');
    return;
  }
  if (isIdentifying.value) return;
  isIdentifying.value = true;
  const log = activeMode.value === 'pair' ? pushPairLog : pushSerialLog;
  log('Starting identify LED pattern...');
  try {
    const password = await activeSerialAdminPassword();
    if (!password) throw new Error('factory password unavailable');
    const out = await sendEasyPairCommand<any>('identify', {
      admin_password: password,
      duration_ms: 6000
    }, 5000, { label: 'Identify device' });
    startIdentifyUiPattern(Number(out.duration_ms || 6000));
    log('Identify pattern started: 3 fast flashes, pause, 3 fast flashes.');
  } catch (e) {
    isIdentifying.value = false;
    const msg = serialFeatureError('Identify', e);
    log(msg);
    notify(msg);
  }
}

async function refreshSerialAdminStatus() {
  if (!selectedPort.value) {
    notify('Select a USB device first');
    return;
  }
  const port = selectedPort.value;
  isSerialAdminLoading.value = true;
  pushSerialLog('Refreshing local admin status...');
  try {
    if (!activeSerialDevice.value?.adminSupported) {
      await probeSerialAdminSupport(port);
    }
    const out = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000, { label: 'Refresh status' });
    applySerialAdminStatus(out, port);
    if (!out.commissioned || out.fleet_passphrase_default) {
      pushSerialLog(`Status loaded: factory default, awaiting commissioning, addr ${out.local_address}->${out.remote_address}, heap ${formatBytes(out.heap_free)} free.`);
    } else {
      pushSerialLog(`Status loaded: ${out.role || 'unknown'} ${out.local_address}->${out.remote_address}, heap ${formatBytes(out.heap_free)} free.`);
    }
  } catch (e) {
    const msg = serialFeatureError('Status', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialAdminLoading.value = false;
  }
}

async function loadSerialAdminConfig() {
  if (!selectedPort.value) {
    notify('Select a USB device first');
    return;
  }
  const port = selectedPort.value;
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  isSerialAdminLoading.value = true;
  pushSerialLog('Loading local device configuration...');
  try {
    if (!activeSerialDevice.value?.adminSupported) {
      await probeSerialAdminSupport(port);
    }
    const out = await sendEasyPairCommand<{ ok: boolean; cmd: string; config: SerialAdminConfig }>('get_config', {
      admin_password: password,
      include_secrets: true
    }, 15000, { label: 'Fetch settings' });
    const state = serialDeviceState(port);
    if (state) {
      state.config = normalizeSerialAdminConfig(out.config, state.status);
    }
    pushSerialLog('Local configuration loaded. Password fields stay blank unless you enter new values.');
  } catch (e) {
    const msg = serialFeatureError('Config load', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialAdminLoading.value = false;
  }
}

async function fetchSerialDeviceSettings() {
  if (!selectedPort.value) {
    notify('Select a USB device first');
    return;
  }
  if (!hasActiveDeviceInfo.value) {
    const ok = await readDeviceInfo();
    if (!ok) return;
  }
  await refreshSerialAdminStatus();
  await loadSerialAdminConfig();
}

function serialConfigPatch(): Record<string, any> {
  const cfg = serialAdminConfig.value;
  if (!cfg) return {};
  const patch: Record<string, any> = {
    mode: cfg.mode === 'standalone' ? 'standalone' : 'paired',
    role_tx: !!cfg.role_tx,
    local_address: Number(cfg.local_address || 1),
    remote_address: Number(cfg.remote_address || 254),
    paired_target_addresses: cfg.paired_target_addresses || [Number(cfg.remote_address || 254)],
    allowed_controller_addresses: cfg.allowed_controller_addresses || [Number(cfg.remote_address || 254)],
    known_peer_addresses: cfg.known_peer_addresses || [],
    lora_tx_power: Number(cfg.lora_tx_power || 17),
    lora_spreading_factor: Number(cfg.lora_spreading_factor || 12),
    lora_bandwidth_hz: Number(cfg.lora_bandwidth_hz || 125000),
    lora_coding_rate: Number(cfg.lora_coding_rate || 5),
    heartbeat_ms: Number(cfg.heartbeat_ms || 60000),
    heartbeat_enabled: cfg.heartbeat_enabled !== false,
    ack_timeout_ms: Number(cfg.ack_timeout_ms || 3000),
    mqtt_remote_retry_timeout_ms: Number(cfg.mqtt_remote_retry_timeout_ms || 180000),
    tx_mqtt_remote_polling_enabled: !!cfg.tx_mqtt_remote_polling_enabled,
    tx_mqtt_remote_default_poll_interval_ms: Number(cfg.tx_mqtt_remote_default_poll_interval_ms || 300000),
    maintenance_debug_telemetry_enabled: !!cfg.maintenance_debug_telemetry_enabled,
    rx_push_on_change_enabled: !!cfg.rx_push_on_change_enabled,
    rx_push_min_interval_ms: Number(cfg.rx_push_min_interval_ms || 60000),
    input_control_paired_lora_enabled: !!cfg.input_control_paired_lora_enabled,
    tx_command_retry_timeout_ms: Number(cfg.tx_command_retry_timeout_ms || 180000),
    rx_failsafe_mode: cfg.rx_failsafe_mode || 'hold_last',
    rx_failsafe_timeout_ms: Number(cfg.rx_failsafe_timeout_ms || 180000),
    wifi_sta_ssid: cfg.wifi_sta_ssid || '',
    lan_hostname: cfg.lan_hostname || '',
    ap_always_on: !!cfg.ap_always_on,
    wifi_phy_mode: cfg.wifi_phy_mode || '11b',
    wifi_tx_power_dbm: Number(cfg.wifi_tx_power_dbm ?? 20.5),
    wifi_sleep_enabled: !!cfg.wifi_sleep_enabled,
    wifi_static_ip_enabled: !!cfg.wifi_static_ip_enabled,
    wifi_static_ip: cfg.wifi_static_ip || '',
    wifi_static_gateway: cfg.wifi_static_gateway || '',
    wifi_static_subnet: cfg.wifi_static_subnet || '',
    wifi_channel_override: Number(cfg.wifi_channel_override || 0),
    wifi_ap_fallback_policy: cfg.wifi_ap_fallback_policy || 'fallback_on_disconnect',
    wifi_admin_enabled: !!cfg.wifi_admin_enabled,
    mqtt_client_enabled: !!cfg.mqtt_client_enabled,
    mqtt_control_enabled: !!cfg.mqtt_control_enabled,
    mqtt_controller_addresses: cfg.mqtt_controller_addresses || '',
    mqtt_host: cfg.mqtt_host || '',
    mqtt_port: Number(cfg.mqtt_port || 1883),
    mqtt_user: cfg.mqtt_user || '',
    mqtt_topic_root: cfg.mqtt_topic_root || 'lora',
    sensor_temp_enabled: !!cfg.sensor_temp_enabled,
    sensor_temp_pin: Number(cfg.sensor_temp_pin || 0),
    sensor_temp_interval_s: Number(cfg.sensor_temp_interval_s || 10),
    sensor_tank_enabled: !!cfg.sensor_tank_enabled,
    sensor_tank_range_mm: Number(cfg.sensor_tank_range_mm || 5000),
    sensor_tank_vref_mv: Number(cfg.sensor_tank_vref_mv || 3553),
    sensor_tank_sense_ohms: Number(cfg.sensor_tank_sense_ohms || 120),
    sensor_tank_interval_s: Number(cfg.sensor_tank_interval_s || 5)
  };
  if (cfg.wifi_sta_password) patch.wifi_sta_password = cfg.wifi_sta_password;
  if (cfg.mqtt_password) patch.mqtt_password = cfg.mqtt_password;
  if (cfg.fleet_passphrase) patch.fleet_passphrase = cfg.fleet_passphrase;
  if (cfg.admin_password) patch.admin_password = cfg.admin_password;
  return patch;
}

async function saveSerialAdminConfig() {
  if (!serialAdminConfig.value) {
    notify('Load config first');
    return;
  }
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  isSerialAdminSaving.value = true;
  pushSerialLog('Saving local device configuration...');
  try {
    const out = await sendEasyPairCommand<any>('set_config', {
      admin_password: password,
      config: serialConfigPatch()
    }, 12000, { label: 'Save settings' });
    const rebooting = !!(out.rebooting || out.ota_auth_changed);
    const effects = [
      out.network_restarted ? 'networking restarted' : '',
      rebooting ? 'admin password changed; device is rebooting' : ''
    ].filter(Boolean);
    pushSerialLog(`Configuration saved${effects.length ? `; ${effects.join('; ')}` : ''}.`);
    if (rebooting) {
      serialAdminStatus.value = null;
      serialAdminConfig.value = null;
    } else {
      await refreshSerialAdminStatus();
      await loadSerialAdminConfig();
    }
    notify(`Configuration saved${effects.length ? `; ${effects.join('; ')}` : ''}.`);
  } catch (e) {
    const msg = serialFeatureError('Config save', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialAdminSaving.value = false;
  }
}

async function rebootSerialDevice() {
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  if (!await confirmOperatorAction('Reboot the selected USB device now?', { confirmText: 'Reboot' })) return;
  isSerialSystemAction.value = true;
  pushSerialLog('Sending reboot command...');
  try {
    await sendEasyPairCommand('reboot', { admin_password: password }, 5000, { label: 'Reboot device' });
    serialAdminStatus.value = null;
    pushSerialLog('Reboot command accepted; cached live status was cleared until the device responds again.');
  } catch (e) {
    const msg = serialFeatureError('Reboot', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialSystemAction.value = false;
  }
}

async function factoryResetSerialDevice() {
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  const summary = [
    serialFactoryKeepFleet.value ? 'keep fleet key' : 'clear fleet key',
    serialFactoryKeepWifi.value ? 'keep WiFi' : 'clear WiFi'
  ].join(', ');
  if (!await confirmOperatorAction(`Factory reset the selected USB device (${summary})?`, { confirmText: 'Factory reset', danger: true })) return;
  isSerialSystemAction.value = true;
  pushSerialLog(`Sending factory reset command (${summary})...`);
  try {
    await sendEasyPairCommand('factory_reset', {
      admin_password: password,
      keep_shared_fleet_key: serialFactoryKeepFleet.value,
      keep_wifi_credentials: serialFactoryKeepWifi.value
    }, 6000, { label: 'Factory reset' });
    serialAdminStatus.value = null;
    serialAdminConfig.value = null;
    settingsWifiNetworks.value = [];
    pushSerialLog('Factory reset command accepted; device is rebooting and loaded settings were invalidated.');
  } catch (e) {
    const msg = serialFeatureError('Factory reset', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialSystemAction.value = false;
  }
}

let loadGatewayInFlight = false;
async function loadEasyPairGateway(isAuto = false) {
  if (activeMode.value !== 'pair') return;
  const port = gatewaySelectedPort.value;
  if (!port) return;
  // Prevent concurrent calls from multiple watchers firing on startup
  if (loadGatewayInFlight) return;
  loadGatewayInFlight = true;
  isGatewayLoading.value = true;
  pushPairLog('Loading USB gateway...');
  try {
    // Require device info to be already loaded before proceeding.
    // If not yet available, exit silently — the reactive watcher will call us once it arrives.
    const state = serialDeviceState(port);
    if (!state?.deviceInfo) {
      pushPairLog(`Waiting for device on ${port} to be identified...`);
      return;
    }
    state.adminPassword = state.deviceInfo.password || '';
    pushPairLog('Waiting for serial admin to become ready...');
    const hello = await waitForSerialAdminHello(port);
    pushPairLog(`Gateway ready on ${port}; firmware ${hello.fw_version || 'unknown'}, max remotes ${hello.max_remotes || 12}.`);
    await refreshGatewayStatusForPair();
    const password = pairPassword();
    if (password) {
      pushPairLog('Fetching gateway configuration...');
      try {
        const out = await sendPairCommand<{ ok: boolean; cmd: string; config: Partial<SerialAdminConfig> }>('get_config', {
          admin_password: password,
          include_secrets: true
        }, 15000);
        const retrievedKey = out.config?.fleet_passphrase?.trim() || '';
        const isCommissioned = serialDeviceState(port)?.status?.commissioned;
        const isDefaultKey = serialDeviceState(port)?.status?.fleet_passphrase_default;
        
        if (isCommissioned && retrievedKey && retrievedKey !== 'lora-default-passphrase' && isDefaultKey === false) {
          pairFleetKey.value = retrievedKey;
          pushPairLog(`Retrieved commissioned fleet key from gateway.`);
        } else if (!isCommissioned || isDefaultKey === true) {
          generatePairFleetKey(true);
          pushPairLog('Gateway is uncommissioned; generated a new random fleet key.');
        } else {
          pushPairLog('Gateway has default or unconfigured fleet key.');
        }
      } catch (configErr) {
        pushPairLog('Gateway config fetch failed: ' + configErr);
      }
    }
  } catch (e) {
    const state = serialDeviceState(port);
    if (state) state.adminPassword = '';
    pushPairLog('Gateway check failed: ' + e);
    if (!isAuto) {
      notify('Gateway check failed: ' + e);
    }
  } finally {
    loadGatewayInFlight = false;
    isGatewayLoading.value = false;
  }
}



async function runEasyPair() {
  const expected = Math.max(1, Math.min(12, Number(pairExpectedCount.value) || 12));
  pairExpectedCount.value = expected;
  isPairBusy.value = true;
  provisionCacheRefreshedChips.clear();
  pushPairLog('--- EasyPair ---');
  try {
    // Reload gateway only if not already loaded (avoids duplicate "uncommissioned" messages)
    if (!gatewayReady.value) {
      await loadEasyPairGateway();
      if (!gatewayReady.value) throw new Error('Unable to load gateway');
    }
    
    const fleetKey = pairFleetKey.value.trim();
    if (!fleetKey) {
      throw new Error('Fleet key is empty');
    }
    const password = pairPassword();
    await sendPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      expected_remotes: expected
    }, 10000);
    pushPairLog(`Gateway prepared. Scanning for ${expected} remote device${expected === 1 ? '' : 's'}...`);
    await sendPairCommand('start_discovery', {
      admin_password: password,
      expected_remotes: expected
    }, 10000);
    startEasyPairStatusPolling();
    await waitForEasyPairState(['ready', 'error'], 130000);
    if (pairStatus.value?.session?.state === 'error') throw new Error('Discovery ended with an error');
    const found = pairStatus.value?.session?.discovered_count || 0;
    if (found === 0) throw new Error('No remote devices found');
    pushPairLog(`Found ${found} remote device${found === 1 ? '' : 's'}. Provisioning...`);
    await sendPairCommand('provision_all', { admin_password: password }, 10000);
    await waitForEasyPairState(['complete', 'error'], 180000);
    if (pairStatus.value?.session?.state === 'error') throw new Error('Provisioning ended with an error');
    await saveEasyPairTargets();
  } catch (e) {
    pushPairLog('EasyPair failed: ' + e);
    notify('EasyPair failed: ' + e);
  } finally {
    isPairBusy.value = false;
  }
}

async function refreshEasyPairStatus(log = false) {
  try {
    pairStatus.value = await sendPairCommand<EasyPairStatus>('provisioning_status', {}, 5000);
    if (log && pairStatus.value.session) {
      const s = pairStatus.value.session;
      const visibleCount = pairStatus.value.devices?.length ?? s.discovered_count;
      if (s.state === 'discovering') {
        pushPairLog(`Status: discovering; ${visibleCount} device${visibleCount === 1 ? '' : 's'} visible so far, waiting for replies.`);
      } else {
        pushPairLog(`Status: ${s.state}, found ${visibleCount}, verified ${s.verified_count}, failed ${s.failed_count}.`);
      }
    }
    if (pairStatus.value?.session?.state === 'complete') {
      await refreshProvisionedSerialDeviceCaches();
    }
  } catch (e) {
    if (log) pushPairLog('Status refresh failed: ' + e);
  }
}

async function waitForEasyPairState(states: string[], timeoutMs: number) {
  const started = Date.now();
  while (Date.now() - started < timeoutMs) {
    await refreshEasyPairStatus(false);
    const state = pairStatus.value?.session?.state || '';
    if (states.includes(state)) return;
    await new Promise(resolve => setTimeout(resolve, 1500));
  }
  throw new Error(`Timed out waiting for ${states.join(' or ')}`);
}

function startEasyPairStatusPolling() {
  stopEasyPairStatusPolling();
  pairStatusPollTimer.value = window.setInterval(() => {
    refreshEasyPairStatus(false);
  }, 2000);
}

function stopEasyPairStatusPolling() {
  if (pairStatusPollTimer.value) {
    window.clearInterval(pairStatusPollTimer.value);
    pairStatusPollTimer.value = null;
  }
}

async function startEasyPairDiscovery() {
  isPairBusy.value = true;
  provisionCacheRefreshedChips.clear();
  try {
    // Always load gateway status/config before discovery to ensure fleet key is fetched or generated correctly
    await loadEasyPairGateway();
    if (!gatewayReady.value) throw new Error('Unable to load gateway');
    
    const fleetKey = pairFleetKey.value.trim();
    if (!fleetKey) {
      throw new Error('Fleet key is empty');
    }
    const password = pairPassword();
    if (!password) throw new Error('gateway password unavailable');
    await sendPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      expected_remotes: pairExpectedCount.value
    }, 10000);
    pushPairLog(`Gateway prepared. Scanning for up to ${pairExpectedCount.value} powered remote devices...`);
    await sendPairCommand('start_discovery', {
      admin_password: password,
      expected_remotes: pairExpectedCount.value
    }, 10000);
    startEasyPairStatusPolling();
    await refreshEasyPairStatus(true);
  } catch (e) {
    pushPairLog('Discovery failed: ' + e);
    notify('Discovery failed: ' + e);
  } finally {
    isPairBusy.value = false;
  }
}



async function loadGatewayTargetAddresses(password: string): Promise<number[]> {
  const out = await sendPairCommand<{ ok: boolean; cmd: string; config: Partial<SerialAdminConfig> }>('get_config', {
    admin_password: password
  }, 15000);
  return uniqueSortedAddresses(normalizeAddressArray(out.config?.paired_target_addresses));
}

async function saveEasyPairTargets() {
  const password = pairPassword();
  const devices = (pairStatus.value?.devices || [])
    .filter(d =>
      d.selected &&
      isProvisionedTargetState(d) &&
      d.assigned_address > 0 &&
      d.assigned_address < 255
    );
  const deviceAddresses = devices.map(d => d.assigned_address);
  if (!password || deviceAddresses.length === 0) {
    notify('No provisioned target addresses to save yet');
    return;
  }
  const seen = new Set<number>();
  const duplicate = deviceAddresses.find(addr => {
    if (seen.has(addr)) return true;
    seen.add(addr);
    return false;
  });
  if (duplicate !== undefined) {
    devices
      .filter(d => d.assigned_address === duplicate)
      .forEach(d => pushPairLog(`Address allocation duplicate: addr ${duplicate} chip ${d.chip_id_hex || 'unknown'}`));
    notify(`Duplicate target address ${duplicate}; scan/provision again with updated firmware`);
    return;
  }
  const newAddresses = uniqueSortedAddresses(deviceAddresses);
  isPairBusy.value = true;
  devices.forEach(d => pushPairLog(`Address allocation: addr ${d.assigned_address} chip ${d.chip_id_hex || 'unknown'}`));

  try {
    const existingAddresses = await loadGatewayTargetAddresses(password);
    const mergedAddresses = uniqueSortedAddresses([...existingAddresses, ...newAddresses]);
    if (mergedAddresses.length > 12) {
      throw new Error(`target list would exceed 12 addresses (${mergedAddresses.join(', ')})`);
    }
    pushPairLog(`Saving gateway target list: existing ${existingAddresses.join(', ') || 'none'} + new ${newAddresses.join(', ')} -> ${mergedAddresses.join(', ')}`);
    const out = await sendPairCommand<any>('set_gateway_targets', {
      admin_password: password,
      addresses: mergedAddresses
    }, 10000);
    if (out.target_count) {
      pushPairLog(`Gateway target list now has ${out.target_count} address${out.target_count === 1 ? '' : 'es'}.`);
    }
    pushPairLog('Provisioning complete.');
    await refreshGatewayStatusForPair();
    await refreshEasyPairStatus(false);
    await refreshProvisionedSerialDeviceCaches();
  } catch (e) {
    pushPairLog('Saving target list failed: ' + e);
    notify('Saving target list failed: ' + e);
  } finally {
    isPairBusy.value = false;
  }
}

async function cancelEasyPair() {
  const password = pairPassword();
  if (!password) return;
  try {
    await sendPairCommand('cancel_provisioning', { admin_password: password }, 5000);
    stopEasyPairStatusPolling();
    await refreshEasyPairStatus(true);
  } catch (e) {
    pushPairLog('Cancel failed: ' + e);
  }
}

function wifiSignalLabel(rssi: number): string {
  if (rssi >= -60) return 'Excellent';
  if (rssi >= -70) return 'Good';
  if (rssi >= -80) return 'Fair';
  return 'Weak';
}

async function scanGatewayWifi() {
  if (!gatewayReady.value) {
    await loadEasyPairGateway();
    if (gatewayWifiReady.value) {
      return;
    }
  }
  const password = pairPassword();
  if (!password) {
    notify('Load gateway first to use the factory password');
    return;
  }
  isWifiScanning.value = true;
  pushPairLog('Scanning WiFi networks from the USB gateway...');
  try {
    const out = await sendPairCommand<WifiScanResponse>('wifi_scan', {
      admin_password: password
    }, 20000);
    const networks = (out.networks || [])
      .filter(n => n && n.ssid)
      .sort((a, b) => Number(b.rssi || -999) - Number(a.rssi || -999));
    wifiNetworks.value = networks;
    if (!networks.some(n => n.ssid === pairWifiSsid.value)) {
      clearGatewayWifiReady();
    }
    if (!pairWifiSsid.value && networks.length > 0) {
      pairWifiSsid.value = networks[0].ssid;
    }
    const state = serialDeviceState();
    if (state) state.wifiScanned = true;
    pushPairLog(`Found ${networks.length} WiFi network${networks.length === 1 ? '' : 's'}.`);
  } catch (e) {
    const msg = serialFeatureError('WiFi scan', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    isWifiScanning.value = false;
  }
}

async function scanSettingsWifi() {
  if (!selectedPort.value) {
    notify('Select a USB device first');
    return;
  }
  if (!hasActiveDeviceInfo.value) {
    const ok = await readDeviceInfo();
    if (!ok) return;
  }
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Read identity first to use the factory password');
    return;
  }
  isWifiScanning.value = true;
  pushSerialLog('Scanning WiFi networks from the selected USB device...');
  try {
    if (!activeSerialDevice.value?.adminSupported) {
      await probeSerialAdminSupport(selectedPort.value);
    }
    const out = await sendEasyPairCommand<WifiScanResponse>('wifi_scan', {
      admin_password: password
    }, 20000, { label: 'Scan WiFi' });
    const networks = (out.networks || [])
      .filter(n => n && n.ssid)
      .sort((a, b) => Number(b.rssi || -999) - Number(a.rssi || -999));
    settingsWifiNetworks.value = networks;
    const state = serialDeviceState(settingsSelectedPort.value);
    if (state) state.wifiScanned = true;
    if (serialAdminConfig.value && !serialAdminConfig.value.wifi_sta_ssid && networks.length > 0) {
      serialAdminConfig.value.wifi_sta_ssid = networks[0].ssid;
    }
    pushSerialLog(`Settings WiFi scan found ${networks.length} network${networks.length === 1 ? '' : 's'}.`);
  } catch (e) {
    const msg = serialFeatureError('WiFi scan', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isWifiScanning.value = false;
  }
}

async function openPairWifiTab() {
  pairPanelTab.value = 'wifi';
  if (!gatewaySelectedPort.value || isGatewayLoading.value || isWifiScanning.value) {
    return;
  }
  if (gatewayWifiReady.value) {
    return;
  }
  if (serialDeviceState(gatewaySelectedPort.value)?.wifiScanned && wifiNetworks.value.length > 0) {
    return;
  }
  if (gatewayReady.value && await refreshGatewayStatusForPair()) {
    return;
  }
  await scanGatewayWifi();
}

function clearGatewayWifiReady() {
  const state = serialDeviceState(gatewaySelectedPort.value);
  if (!state) return;
  state.gatewayWifiReadySsid = '';
  state.gatewayWifiReadyIp = '';
}

function applySerialAdminStatus(out: SerialAdminStatus, port = selectedPort.value) {
  const state = serialDeviceState(port);
  if (!state) return;
  state.status = out;
  serialUptimeMs.value = Number(out.uptime_ms || 0);
  state.adminSupported = true;
  adoptGatewayWifiFromStatus(out, port);
}

function adoptGatewayWifiFromStatus(out: SerialAdminStatus, port = selectedPort.value): boolean {
  if (!port) return false;
  const wifi = out.wifi;
  const ssid = wifi?.sta_ssid?.trim() || '';
  const ip = wifi?.ip?.trim() || '';
  const status = wifi?.status?.trim().toLowerCase() || '';
  const connected = (!!wifi?.sta_connected || status === 'connected') && !!ssid && !!ip && ip !== '0.0.0.0';
  if (!connected) {
    const state = serialDeviceState(port);
    if (state) {
      state.gatewayWifiReadySsid = '';
      state.gatewayWifiReadyIp = '';
    }
    return false;
  }

  if (port === gatewaySelectedPort.value) pairWifiSsid.value = ssid;
  const state = serialDeviceState(port);
  if (!state) return false;
  state.gatewayWifiReadySsid = ssid;
  state.gatewayWifiReadyIp = ip;
  return true;
}

async function refreshGatewayStatusForPair(): Promise<boolean> {
  const port = gatewaySelectedPort.value;
  if (!port) return false;
  try {
    const status = await sendPairCommand<SerialAdminStatus>('status', {}, 5000);
    applySerialAdminStatus(status, port);
    if (gatewayWifiReady.value) {
      const state = serialDeviceState(port);
      pushPairLog(`Gateway already connected to ${state?.gatewayWifiReadySsid} at ${state?.gatewayWifiReadyIp}.`);
      return true;
    }
  } catch (statusErr) {
    pushPairLog('Gateway status check skipped: ' + statusErr);
  }
  return false;
}

function cancelGatewayWifiConnect(reason = 'credentials changed') {
  if (!isWifiApplying.value) return;
  wifiApplyAttemptId.value += 1;
  isWifiApplying.value = false;
  pushPairLog(`Gateway WiFi connection cancelled: ${reason}.`);
}

function assertGatewayWifiAttemptActive(attemptId: number) {
  if (attemptId !== wifiApplyAttemptId.value || !isWifiApplying.value) {
    throw new Error('gateway_wifi_cancelled');
  }
}

async function waitForGatewayWifiConnection(ssid: string, attemptId: number, timeoutMs = 15000): Promise<SerialAdminStatus> {
  const started = Date.now();
  let lastStatus: SerialAdminStatus | null = null;
  while (Date.now() - started < timeoutMs) {
    assertGatewayWifiAttemptActive(attemptId);
    const out = await sendPairCommand<SerialAdminStatus>('status', {}, 5000);
    assertGatewayWifiAttemptActive(attemptId);
    lastStatus = out;
    const wifi = out.wifi;
    const wifiStatus = wifi?.status?.trim().toLowerCase() || '';
    const wifiIp = wifi?.ip?.trim() || '';
    const wifiSsid = wifi?.sta_ssid?.trim() || '';
    if ((wifi?.sta_connected || wifiStatus === 'connected') && wifiSsid === ssid && wifiIp && wifiIp !== '0.0.0.0') {
      applySerialAdminStatus(out, gatewaySelectedPort.value);
      return out;
    }
    const label = wifi?.status || 'connecting';
    pushPairLog(`Waiting for gateway WiFi (${label})...`);
    await new Promise(resolve => setTimeout(resolve, 1500));
  }
  const detail = lastStatus?.wifi?.status ? `last status: ${lastStatus.wifi.status}` : 'no status received';
  throw new Error(`gateway did not confirm WiFi connection (${detail})`);
}

async function connectGatewayWifi() {
  const password = pairPassword();
  const ssid = pairWifiSsid.value.trim();
  if (!password || !ssid) {
    notify('Select a WiFi network and load the gateway password first');
    return;
  }
  const attemptId = wifiApplyAttemptId.value + 1;
  wifiApplyAttemptId.value = attemptId;
  isWifiApplying.value = true;
  clearGatewayWifiReady();
  pushPairLog(`Saving WiFi credentials on gateway for ${ssid}...`);
  try {
    await sendPairCommand('configure_wifi', {
      admin_password: password,
      wifi_sta_ssid: ssid,
      wifi_sta_password: pairWifiPassword.value
    }, 10000);
    assertGatewayWifiAttemptActive(attemptId);
    pushPairLog('Gateway WiFi saved. Waiting for connection confirmation...');
    const status = await waitForGatewayWifiConnection(ssid, attemptId);
    assertGatewayWifiAttemptActive(attemptId);
    const state = serialDeviceState();
    if (state) {
      state.gatewayWifiReadySsid = ssid;
      state.gatewayWifiReadyIp = status.wifi?.ip || '';
    }
    pushPairLog(`Gateway connected to ${ssid} at ${serialDeviceState(gatewaySelectedPort.value)?.gatewayWifiReadyIp || status.wifi?.ip || 'unknown IP'}. You can now send WiFi to remotes.`);
  } catch (e) {
    if (String(e).includes('gateway_wifi_cancelled')) return;
    const msg = serialFeatureError('WiFi save', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    if (attemptId === wifiApplyAttemptId.value) {
      isWifiApplying.value = false;
    }
  }
}

async function sendWifiToRemotes() {
  const ssid = pairWifiSsid.value;
  const password = pairAdminPassword.value;
  if (!ssid) {
    notify('Select a WiFi network to send first');
    return;
  }
  isFleetWifiSending.value = true;
  pushPairLog(`Sending WiFi credentials to remotes over LoRa for ${ssid}...`);
  try {
    const out = await sendPairCommand<any>('provision_fleet_wifi', {
      admin_password: password,
      wifi_sta_ssid: ssid,
      wifi_sta_password: pairWifiPassword.value
    }, 60000);
    pushPairLog(`LoRa WiFi provisioning sent (${out.packets || '?'} packets).`);
  } catch (e) {
    const msg = serialFeatureError('WiFi provisioning', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    isFleetWifiSending.value = false;
  }
}

async function readDeviceInfo() {
  if (!selectedPort.value) return;
  if (isLoadingInfo.value) return; // Prevent concurrent reads on the active port
  const port = selectedPort.value;
  return await readDeviceInfoForPort(port, activeMode.value || 'serial');
}

async function readDeviceInfoForPort(port: string, _ownerMode: ActiveMode | 'network'): Promise<boolean> {
  // Per-port re-entrancy guard — only one read per port at a time
  if (portsReadingDeviceInfo.value.has(port)) return false;
  portsReadingDeviceInfo.value = new Set([...portsReadingDeviceInfo.value, port]);

  const seq = (deviceInfoReadSeqByPort.value[port] || 0) + 1;
  deviceInfoReadSeqByPort.value = { ...deviceInfoReadSeqByPort.value, [port]: seq };
  if (isMonitoring.value && activeMonitorPort.value === port) {
    pushSerialLog(`Skipped device info read for ${port}: serial monitor owns this port.`);
    const s1 = new Set(portsReadingDeviceInfo.value); s1.delete(port); portsReadingDeviceInfo.value = s1;
    return false;
  }
  pushSerialLog(`Reading device information from ${port}...`);
  try {
    const info = await invoke<DeviceInfo>('get_device_info', { port });
    if (deviceInfoReadSeqByPort.value[port] !== seq) {
      pushSerialLog(`Ignored stale device info from ${port}`);
      return false;
    }
    const state = serialDeviceState(port);
    if (state) {
      state.deviceInfo = info;
      state.adminPassword = info.password || '';
    }
    pushSerialLog('Device info read successfully');
    // Probe serial admin support and refresh running status for all modes to ensure correct warnings are immediately shown on tabs
    await probeSerialAdminSupport(port);
    await refreshFlashPortStatus(port);
    return true;
  } catch (e) {
    if (deviceInfoReadSeqByPort.value[port] !== seq) {
      pushSerialLog(`Ignored stale device info error from ${port}`);
      return false;
    }
    pushSerialLog('Failed to read device info: ' + e);
    return false;
  } finally {
    const s = new Set(portsReadingDeviceInfo.value); s.delete(port); portsReadingDeviceInfo.value = s;
  }
}

async function refreshFlashPortStatus(port: string): Promise<void> {
  try {
    const out = await sendEasyPairCommandOnPort<SerialAdminStatus>(
      port,
      'status',
      {},
      5000,
      { label: 'Read running firmware' }
    );
    applySerialAdminStatus(out, port);
    pushSerialLog(`Running firmware: ${out.fw_version || 'unknown'}`);
  } catch (e) {
    const state = serialDeviceState(port);
    if (state) state.adminSupported = false;
    pushSerialLog(serialFeatureError('Running firmware read', e));
  }
}

async function refreshFlashPortAfterFirmwareUpdate(port: string): Promise<void> {
  const state = serialDeviceState(port);
  if (state) {
    state.status = null;
    state.config = null;
    state.deviceInfo = null;
    state.adminSupported = false;
    state.adminPassword = '';
  }
  pushSerialLog('Waiting for flashed device to restart...');
  await waitForSerialAdminHello(port, 18000);
  pushSerialLog('Reloading device information after firmware update...');
  const loaded = await readDeviceInfoForPort(port, 'serial');
  if (!loaded) throw new Error('Unable to reload device information after flashing');
}

async function startFlash() {
  if (flashDisabled.value) return;
  if (!selectedPort.value || !selectedVersion.value) return;
  const flashPort = selectedPort.value;

  isFlashing.value = true;
  noteMonitorReleasedForPort(flashPort, 'firmware flash needs this port');
  pushSerialLog('--- Preparing Firmware ---');

  try {
    if (!hasActiveDeviceInfo.value) {
      pushSerialLog('Loading device information before flash...');
      const loaded = await readDeviceInfo();
      if (!loaded) throw new Error('Unable to read device information before flashing');
    }

    const isLocal = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX);
    const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;

    if (isLocal && !firmwarePath) throw new Error('Local file path missing');

    const result = await invoke('flash_firmware', {
      port: flashPort,
      firmwarePath,
      region: isLocal ? null : region.value,
      eraseFirst: eraseBeforeFlash.value
    });
    pushSerialLog(result as string);
    await refreshFlashPortAfterFirmwareUpdate(flashPort);

    if (monitorAfterFlash.value) {
      await startSerialMonitor(flashPort, false);
    }
  } catch (e) {
    pushSerialLog('Flash failed: ' + e);
    notify('Flash failed: ' + e);
  } finally {
    isFlashing.value = false;
  }
}

async function startBulkFlash() {
  if (isBulkFlashing.value) return;
  if (bulkSelectedPorts.value.length === 0) {
    notify('Select at least one USB port to flash.');
    return;
  }
  if (!selectedVersion.value) {
    notify('Select a firmware version to flash.');
    return;
  }

  isBulkFlashing.value = true;
  const selectedPorts = [...bulkSelectedPorts.value];
  const isLocal = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX);
  const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;

  if (isLocal && !firmwarePath) {
    notify('Local file path missing.');
    isBulkFlashing.value = false;
    return;
  }

  try {
    await Promise.all(
      selectedPorts.map(async (port) => {
        const state = serialDeviceState(port);
        if (!state) return;

        state.isFlashing = true;
        state.flashProgress = 0;
        state.flashStatus = 'Queued';
        state.flashPhase = 'idle';
        state.flashLogs = [];

        try {
          noteMonitorReleasedForPort(port, 'firmware flash needs this port');

          state.flashStatus = 'Reading Info...';
          if (!state.deviceInfo) {
            await readDeviceInfoForPort(port, 'serial');
          }

          if (eraseBeforeFlash.value) {
            state.flashPhase = 'erase';
            state.flashStatus = 'Erasing...';
          } else {
            state.flashPhase = 'write';
          }
          state.flashStatus = eraseBeforeFlash.value ? 'Erasing...' : 'Flashing...';
          const result = await invoke<string>('flash_firmware', {
            port,
            firmwarePath,
            region: isLocal ? null : region.value,
            eraseFirst: eraseBeforeFlash.value
          });

          state.flashPhase = 'verify';
          state.flashStatus = 'Completed';
          state.flashProgress = 100;
          state.flashLogs.push(result);
          
          await refreshFlashPortAfterFirmwareUpdate(port);

          if (monitorAfterFlash.value) {
            await startSerialMonitor(port, false);
          }
        } catch (e) {
          state.flashStatus = 'Failed';
          state.flashLogs.push(`Flash failed: ${e}`);
        } finally {
          state.isFlashing = false;
        }
      })
    );
  } finally {
    isBulkFlashing.value = false;
  }
}

async function startBulkFactoryReset() {
  if (isBulkResetting.value) return;
  if (bulkSelectedPorts.value.length === 0) {
    notify('Select at least one USB port to factory reset.');
    return;
  }

  const confirmed = await confirmOperatorAction(
    `Factory reset all ${bulkSelectedPorts.value.length} selected USB devices?\n\nThis will restore factory defaults and reboot them.`,
    { confirmText: 'Factory Reset All', danger: true }
  );
  if (!confirmed) return;

  isBulkResetting.value = true;
  const selectedPorts = [...bulkSelectedPorts.value];

  try {
    await Promise.all(
      selectedPorts.map(async (port) => {
        const state = serialDeviceState(port);
        if (!state) return;

        state.isResetting = true;
        state.resetStatus = 'Reading Info...';

        try {
          noteMonitorReleasedForPort(port, 'factory reset needs this port');
          
          if (!state.deviceInfo) {
            await readDeviceInfoForPort(port, 'serial');
          }

          const password = state.adminPassword || state.deviceInfo?.password || '';
          if (!password) throw new Error('derived password unavailable');

          state.resetStatus = 'Resetting...';
          await invoke('serial_admin_command', {
            port,
            request: {
              cmd: 'factory_reset',
              admin_password: password,
              keep_shared_fleet_key: serialFactoryKeepFleet.value,
              keep_wifi_credentials: serialFactoryKeepWifi.value
            },
            timeoutMs: 8000
          });

          state.resetStatus = 'Success';
          state.status = null;
          state.config = null;
        } catch (e) {
          state.resetStatus = 'Failed';
        } finally {
          state.isResetting = false;
        }
      })
    );
  } finally {
    isBulkResetting.value = false;
  }
}

async function startSerialMonitor(port: string, readInfoFirst = true) {
  if (readInfoFirst && !hasActiveDeviceInfo.value) {
    await readDeviceInfo();
  }

  await invoke('toggle_serial_monitor', {
    port,
    baud: 115200,
    enable: true
  });
  isMonitoring.value = true;
  activeMonitorPort.value = port;
  activeMonitorSsid.value = deviceInfo.value?.ssid?.trim() || '';
  pushSerialLog(`Serial monitor started for ${monitorContextLabel.value}`);
}

async function stopSerialMonitorForModeChange() {
  if (!isMonitoring.value) return;
  const port = activeMonitorPort.value || selectedPort.value;
  if (!port) return;
  try {
    await invoke('toggle_serial_monitor', {
      port,
      baud: 115200,
      enable: false
    });
  } catch (e) {
    pushSerialLog('Monitor stop error: ' + e);
  } finally {
    isMonitoring.value = false;
    pushSerialLog(`Serial monitor stopped for ${monitorContextLabel.value}`);
    activeMonitorPort.value = '';
    activeMonitorSsid.value = '';
  }
}

async function toggleMonitor() {
  const targetState = !isMonitoring.value;
  const port = targetState ? selectedPort.value : (activeMonitorPort.value || selectedPort.value);
  if (!port) return;
  try {
    if (targetState) {
      await startSerialMonitor(port, true);
    } else {
      await stopSerialMonitorForModeChange();
    }
  } catch (e) {
    pushSerialLog('Monitor error: ' + e);
  }
}

function scrollToBottom() {
  if (logContainer.value) {
    logContainer.value.scrollTop = logContainer.value.scrollHeight;
  }
}

function scrollNetworkUdpToBottom() {
  if (networkUdpLogContainer.value) {
    networkUdpLogContainer.value.scrollTop = networkUdpLogContainer.value.scrollHeight;
  }
}

function handleLogScroll() {
  if (!logContainer.value) return;
  const { scrollTop, clientHeight, scrollHeight } = logContainer.value;
  stickLogToBottom.value = scrollHeight - (scrollTop + clientHeight) < 24;
}

watch(serialLogs, () => {
  if (stickLogToBottom.value) {
    nextTick(() => scrollToBottom());
  }
}, { deep: true });

watch(networkLogs, () => {
  if (stickLogToBottom.value) {
    nextTick(() => scrollToBottom());
  }
  if (isNetworkUdpMonitoring.value) {
    nextTick(() => scrollNetworkUdpToBottom());
  }
}, { deep: true });

watch([pairWifiSsid, pairWifiPassword], ([nextSsid, nextPassword], [prevSsid, prevPassword]) => {
  if (nextSsid === prevSsid && nextPassword === prevPassword) return;
  cancelGatewayWifiConnect('WiFi credentials changed');
});

watch(pairWifiSsid, (nextSsid) => {
  if (nextSsid) {
    const cached = getCachedWifiPassword(nextSsid);
    if (cached) {
      pairWifiPassword.value = cached;
    }
  }
});

watch(pairWifiPassword, (nextPassword) => {
  const ssid = pairWifiSsid.value;
  if (ssid) {
    saveCachedWifiPassword(ssid, nextPassword);
  }
});

watch(() => settingsDeviceModal.value?.wifi_ssid, (nextSsid) => {
  if (nextSsid && settingsDeviceModal.value) {
    const cached = getCachedWifiPassword(nextSsid);
    if (cached) {
      settingsDeviceModal.value.wifi_password = cached;
    }
  }
});

watch(() => settingsDeviceModal.value?.wifi_password, (nextPassword) => {
  if (settingsDeviceModal.value && settingsDeviceModal.value.wifi_ssid) {
    saveCachedWifiPassword(settingsDeviceModal.value.wifi_ssid, nextPassword || '');
  }
});

watch(() => serialAdminConfig.value?.wifi_sta_ssid, (nextSsid) => {
  if (nextSsid && serialAdminConfig.value) {
    const cached = getCachedWifiPassword(nextSsid);
    if (cached) {
      serialAdminConfig.value.wifi_sta_password = cached;
    }
  }
});

watch(() => serialAdminConfig.value?.wifi_sta_password, (nextPassword) => {
  if (serialAdminConfig.value && serialAdminConfig.value.wifi_sta_ssid) {
    saveCachedWifiPassword(serialAdminConfig.value.wifi_sta_ssid, nextPassword || '');
  }
});

watch(activeMode, (mode) => {
  nextTick(() => scrollToBottom());
  if (mode === 'network') {
    nextTick(() => scrollNetworkUdpToBottom());
  }
  syncDeviceInfoForSelectedPort();
  // Note: readDeviceInfo() is NOT called here — the watch(selectedPort) watcher handles it
  // when the computed selectedPort changes due to the mode switch.
  if (mode !== 'monitor') stopMonitorPolling();
  if (mode !== 'network') {
    stopLoraInventoryPolling(false);
  } else if (portGatewayReady(gatewaySelectedPort.value)) {
    // Gateway was already loaded (e.g. switching back to Fleet tab) — just refresh inventory
    refreshLoraInventoryStatus(false).finally(() => startFleetCachePolling());
  } else {
    // Gateway loading is handled reactively by watch(gatewayPortDeviceInfo)
    loadNetworkGateway();
  }
});

watch(selectedPort, (port) => {
  if (port) {
    deviceInfoReadSeqByPort.value = {
      ...deviceInfoReadSeqByPort.value,
      [port]: (deviceInfoReadSeqByPort.value[port] || 0) + 1
    };
  }
  syncDeviceInfoForSelectedPort();
  serialUptimeMs.value = activeSerialDevice.value?.status?.uptime_ms ?? null;
  if (port && activeMode.value !== 'pair' && !isSelectedPortMonitoring.value && !hasActiveDeviceInfo.value) {
    readDeviceInfo();
  }
  if (activeMode.value === 'monitor') {
    monitorFleetRows.value = [];
    stopMonitorPolling();
  }
});

watch(gatewaySelectedPort, (port) => {
  // Provisioning side-effects
  pairStatus.value = null;
  saveTabPort('pair', port);
  // Fleet side-effects
  saveTabPort('network', port);
  loraInventory.value = [];
  loraInventoryScan.value = null;
  isLoraInventoryScanning.value = false;
  stopLoraInventoryPolling(false);
  // Note: gateway loading is triggered reactively by watch(gatewayPortDeviceInfo)
  // once the port-detection flow populates deviceInfo — no direct call here.
  if (port) loadNetworkGateway();
});

// Reactive gateway loader: fires when device info becomes available on the gateway port,
// ensuring gateway admin steps only run AFTER the port-detection flow has succeeded.
const gatewayPortDeviceInfo = computed(() => {
  const port = gatewaySelectedPort.value;
  if (!port) return null;
  return serialDeviceState(port)?.deviceInfo ?? null;
});
watch(gatewayPortDeviceInfo, (info) => {
  if (!info) return;
  const port = gatewaySelectedPort.value;
  if (!port || portGatewayReady(port)) return;
  if (activeMode.value === 'network') {
    loadNetworkGateway();
  } else if (activeMode.value === 'pair') {
    loadEasyPairGateway(true);
  }
});

watch(flashSelectedPort, port => saveTabPort('serial', port));
watch(monitorSelectedPort, port => saveTabPort('monitor', port));
watch(settingsSelectedPort, port => saveTabPort('settings', port));

const handleWindowClick = () => {
  activeDropdownAddress.value = null;
};

onMounted(async () => {
  window.addEventListener('click', handleWindowClick);
  generatePairFleetKey(false);
  fleetClockTimer.value = window.setInterval(() => {
    fleetClockMs.value = Date.now();
    checkOtaProgressWatchdog();
  }, 1000);
  loadSavedTabPorts();
  try {
    flasherAppVersion.value = await invoke<string>('get_app_version');
  } catch {
    flasherAppVersion.value = '';
  }
  try {
    flasherInterfaces.value = await invoke<NetworkInterface[]>('get_network_interfaces');
  } catch {
    flasherInterfaces.value = [];
  }

  // Poll network interfaces every 5 seconds to dynamically adapt to network configuration changes
  window.setInterval(async () => {
    try {
      const nextInterfaces = await invoke<NetworkInterface[]>('get_network_interfaces');
      const prevStr = JSON.stringify(flasherInterfaces.value.map(i => i.ip).sort());
      const nextStr = JSON.stringify(nextInterfaces.map(i => i.ip).sort());
      if (prevStr !== nextStr) {
        pushNetworkLog('Host network interfaces changed. Updating subnets...');
        flasherInterfaces.value = nextInterfaces;
        
        if (firmwareServerInfo.value) {
          const currentUrls = firmwareServerInfo.value.urls;
          const stillValid = currentUrls.some(url => {
            try {
              const parsed = new URL(url);
              return nextInterfaces.some(i => i.ip === parsed.hostname);
            } catch (_) {
              return false;
            }
          });
          
          if (!stillValid) {
            pushNetworkLog('Firmware server is no longer reachable on this network. Restarting...');
            await stopFirmwareServer();
            const firmwareOptions = networkOtaFirmwareOptions();
            if (firmwareOptions) {
              await startFirmwareServerWithOptions(firmwareOptions);
            }
          }
        }
      }
    } catch (e) {
      console.error('Failed to poll network interfaces:', e);
    }
  }, 5000);
  try {
    const savedMonitor = localStorage.getItem(MONITOR_AFTER_FLASH_STORAGE_KEY);
    if (savedMonitor === 'true' || savedMonitor === 'false') {
      monitorAfterFlash.value = savedMonitor === 'true';
    }
    const savedErase = localStorage.getItem(ERASE_BEFORE_FLASH_STORAGE_KEY);
    if (savedErase === 'true' || savedErase === 'false') {
      eraseBeforeFlash.value = savedErase === 'true';
    }
    const savedMonitorAutoRefresh = localStorage.getItem(MONITOR_AUTO_REFRESH_STORAGE_KEY);
    if (savedMonitorAutoRefresh === 'true' || savedMonitorAutoRefresh === 'false') {
      monitorAutoRefresh.value = savedMonitorAutoRefresh === 'true';
    }
  } catch (_) {
    // Ignore storage failures; checkbox defaults still work.
  }
  const rememberedRegion = (() => {
    try {
      const saved = localStorage.getItem(REGION_STORAGE_KEY);
      return saved === 'ZA' || saved === 'EU' || saved === 'US' ? (saved as RegionCode) : null;
    } catch (_) {
      return null;
    }
  })();
  const detected = detectRegionFromSystem();
  if (detected.region && detected.reliable) {
    region.value = detected.region;
    pushSerialLog(`Region auto-detected: ${detected.region} (${detected.reason})`);
  } else if (rememberedRegion) {
    region.value = rememberedRegion;
    pushSerialLog(`Region auto-detect not confident; using last selected region: ${rememberedRegion} (${detected.reason})`);
  } else if (detected.region) {
    region.value = detected.region;
    pushSerialLog(`Region auto-detect weak signal; using best guess: ${detected.region} (${detected.reason})`);
  } else {
    pushSerialLog(`Region auto-detection unavailable; using default: ${region.value}`);
  }

  refreshPorts(false);
  fetchFirmware();

  // Background cache the top 5 firmware releases to speed up future OTA updates
  invoke('cache_recent_firmware_releases', { limit: 5 }).catch(e => {
    console.warn('Failed to pre-cache recent firmware releases:', e);
  });

  unlistenFlash = await listen<LogEvent>('flash-log', (event) => {
    const rawMsg = event.payload.message;
    const port = event.payload.port;
    // Split on both newlines and carriage returns to ensure progress updates
    // from esptool appear as fresh lines in our Activity Log.
    const lines = rawMsg.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
      if (trimmed) pushSerialLogForPort(port, trimmed);
    });
  });

  unlistenMonitor = await listen<MonitorEvent>('monitor-log', (event) => {
    const rawLine = event.payload.line;
    const port = event.payload.port;
    const lines = rawLine.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
       if (trimmed) pushMonitorLogForPort(port, trimmed);
    });
  });

  unlistenNetworkMonitor = await listen<MonitorEvent>('network-monitor-log', (event) => {
    const rawLine = event.payload.line;
    const lines = rawLine.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
      if (trimmed) {
        pushNetworkLog(trimmed);
        
        // Find matching device by chip ID from [lrs-xxxxxx] in log content first, falling back to IP match
        let dev: LoraInventoryDevice | undefined = undefined;
        const chipMatch = trimmed.match(/\[lrs-([0-9a-fA-F]+)\]/);
        if (chipMatch) {
          const chipId = chipMatch[1].toLowerCase();
          dev = loraInventory.value.find(d => 
            String(d.chip_id || '').trim().replace(/^0x/i, '').toLowerCase() === chipId
          );
        }
        
        if (!dev) {
          const ipMatch = trimmed.match(/^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})\s+/);
          if (ipMatch) {
            const ip = ipMatch[1];
            dev = loraInventory.value.find(d => d.ip === ip);
          }
        }

        if (dev) {
          // Track active OTA packet transmissions for progress watchdog
          if (trimmed.includes('event=ota_pull_control_start_rx') || trimmed.includes('event=ota_pull_control_rx')) {
            if (!fleetRowHistory.value[dev.address]) {
              fleetRowHistory.value[dev.address] = {};
            }
            fleetRowHistory.value[dev.address].lastOtaActivityMs = Date.now();
          }

          // Handle OTA completed downloading & waiting to apply/reboot
          if (trimmed.includes('event=ota_pull_control_apply')) {
            if (dev.row_state === 'ota_downloading' || dev.row_state === 'ota_pending' || dev.row_state === 'ota_retrying') {
              fleetRowHistory.value[dev.address] = {
                ...(fleetRowHistory.value[dev.address] || {}),
                rowState: 'ota_apply_wait',
                rowStateUntilMs: Date.now() + 120000,
                rebootExpectedUntilMs: Date.now() + 240000
              };
              loraInventory.value = loraInventory.value.map(row => 
                row.address === dev.address ? { ...row, row_state: 'ota_apply_wait', row_state_until_ms: Date.now() + 120000 } : row
              );
            }
          }

          // Handle OTA failures
          if (trimmed.includes('event=ota_pull_control_failed') ||
              trimmed.includes('event=ota_pull_control_incomplete') ||
              trimmed.includes('event=ota_pull_control_bad_hash') ||
              trimmed.includes('event=ota_pull_control_orphan')) {
            const activeStates = ['ota_pending', 'ota_downloading', 'ota_apply_wait', 'ota_retrying'];
            if (activeStates.includes(dev.row_state || '')) {
              triggerOtaFailureOrRetry(dev);
            }
          }
        }
      }
    });
  });

  unlistenPortsChanged = await listen<PortsChangedEvent>('serial-ports-changed', () => {
    if (!isFlashing.value) {
      refreshPorts(true);
    }
  });
});

watch(region, (next) => {
  try {
    localStorage.setItem(REGION_STORAGE_KEY, next);
  } catch (_) {
    // Ignore storage failures and continue with in-memory value.
  }
});

watch(monitorAfterFlash, (next) => {
  try {
    localStorage.setItem(MONITOR_AFTER_FLASH_STORAGE_KEY, next ? 'true' : 'false');
  } catch (_) {
    // Ignore storage failures; current checkbox value still applies.
  }
});

watch(eraseBeforeFlash, (next) => {
  try {
    localStorage.setItem(ERASE_BEFORE_FLASH_STORAGE_KEY, next ? 'true' : 'false');
  } catch (_) {
    // Ignore storage failures; current checkbox value still applies.
  }
});

watch(monitorAutoRefresh, (enabled) => {
  try {
    localStorage.setItem(MONITOR_AUTO_REFRESH_STORAGE_KEY, enabled ? 'true' : 'false');
  } catch (_) {
    // Ignore storage failures; current checkbox value still applies.
  }
  if (!isMonitorLoopRunning.value) return;
  if (enabled && activeMode.value === 'monitor' && selectedPort.value) {
    startMonitorPolling();
    refreshMonitorData(true);
  } else {
    stopMonitorPolling();
  }
});

onUnmounted(() => {
  window.removeEventListener('click', handleWindowClick);
  stopEasyPairStatusPolling();
  stopLoraInventoryPolling();
  stopMonitorPolling();
  if (fleetClockTimer.value) window.clearInterval(fleetClockTimer.value);
  Object.values(fleetOtaFollowupTimers.value).forEach(timer => window.clearTimeout(timer));
  fleetOtaFollowupTimers.value = {};
  if (identifyTimer.value) window.clearTimeout(identifyTimer.value);
  if (unlistenFlash) unlistenFlash();
  if (unlistenMonitor) unlistenMonitor();
  if (unlistenNetworkMonitor) unlistenNetworkMonitor();
  if (unlistenPortsChanged) unlistenPortsChanged();
  if (isNetworkUdpMonitoring.value) {
    invoke('stop_network_udp_monitor').catch(() => {});
  }
  if (firmwareServerInfo.value) {
    invoke('stop_firmware_file_server').catch(() => {});
  }
});

function formatLabel(key: string) {
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

function isCrashStart(line: string): boolean {
  return [
    'User exception (panic/abort/assert)',
    'Soft WDT reset',
    'wdt reset',
    'Fatal exception',
    'Exception (',
    'Unhandled C++ exception:',
  ].some(marker => line.includes(marker));
}

function countCrashEvents(entries: string[]): number {
  let count = 0;
  let inCrashBlock = false;
  for (const entry of entries) {
    const line = entry.trim();
    if (!line) continue;
    if (isCrashStart(line)) {
      if (!inCrashBlock) {
        count += 1;
        inCrashBlock = true;
      }
      continue;
    }
    if (inCrashBlock && line.startsWith('[INFO][SYS]') && line.includes('event=boot_banner')) {
      inCrashBlock = false;
    }
  }
  return count;
}

function toggleSelectAllBulkPorts() {
  if (bulkSelectedPorts.value.length === ports.value.length) {
    bulkSelectedPorts.value = [];
  } else {
    bulkSelectedPorts.value = ports.value.map(p => p.port_name);
  }
}
</script>

<template>
  <div class="relative h-full flex flex-col">
    <div :class="['grid gap-3 flex-1 min-h-0 transition-all duration-500', activityFullscreen || activeMode === 'network' || activeMode === 'monitor' ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
      <!-- Log Panel -->
      <div v-if="activeMode !== 'network' && activeMode !== 'monitor'" :class="['glass-card p-3 flex flex-col gap-2 text-left overflow-hidden h-full']">
        <!-- New Bulk Operations Status Grid -->
        <div v-if="activeMode === 'serial' && bulkMode" class="flex flex-col gap-3 h-full min-h-0 overflow-hidden">
          <div class="flex items-center justify-between border-b border-slate-700/80 pb-2">
            <h2 class="text-sm font-semibold text-slate-300 flex items-center gap-2">
              <span :class="['w-2 h-2 rounded-full', isBulkFlashing || isBulkResetting ? 'bg-cyan-500 animate-pulse' : 'bg-slate-600']"></span>
              Bulk Operations Status
            </h2>
            <div class="text-xs text-slate-500 font-mono">
              Selected: {{ bulkSelectedPorts.length }} ports
            </div>
          </div>
          
          <div class="flex-1 overflow-auto custom-scrollbar pr-1 space-y-3">
            <div v-for="port in bulkSelectedPorts" :key="port" class="glass-card p-3 flex flex-col gap-2 border border-slate-800 bg-slate-900/40 hover:border-slate-700 transition-all rounded-lg text-left">
              <div class="flex items-center justify-between gap-3 text-xs">
                <span class="font-bold text-slate-200 font-mono">{{ port }}</span>
                <div class="flex items-center gap-2">
                  <span v-if="serialDeviceState(port)?.deviceInfo?.ssid" class="font-mono text-[10px] text-slate-500">
                    SSID: {{ serialDeviceState(port)?.deviceInfo?.ssid }}
                  </span>
                  <span :class="[
                    'px-2 py-0.5 rounded-[4px] text-[10px] font-bold border uppercase',
                    serialDeviceState(port)?.isFlashing ? 'bg-cyan-500/10 border-cyan-500/30 text-cyan-300' :
                    serialDeviceState(port)?.isResetting ? 'bg-amber-500/10 border-amber-500/30 text-amber-300' :
                    serialDeviceState(port)?.flashStatus === 'Completed' || serialDeviceState(port)?.resetStatus === 'Success' ? 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300' :
                    serialDeviceState(port)?.flashStatus === 'Failed' || serialDeviceState(port)?.resetStatus === 'Failed' ? 'bg-red-500/10 border-red-500/30 text-red-300' :
                    'bg-slate-800 border-slate-700 text-slate-400'
                  ]">
                    {{ serialDeviceState(port)?.isFlashing ? serialDeviceState(port)?.flashStatus :
                       serialDeviceState(port)?.isResetting ? serialDeviceState(port)?.resetStatus :
                       serialDeviceState(port)?.flashStatus !== 'Idle' ? serialDeviceState(port)?.flashStatus :
                       serialDeviceState(port)?.resetStatus !== 'Idle' ? serialDeviceState(port)?.resetStatus : 'Queued' }}
                  </span>
                </div>
              </div>
              
              <!-- Progress bar -->
              <div v-if="serialDeviceState(port)?.isFlashing || serialDeviceState(port)?.flashStatus === 'Completed' || serialDeviceState(port)?.flashStatus === 'Failed'" class="w-full flex items-center gap-3 mt-1">
                <div class="flex-1 h-1.5 rounded-full bg-slate-800 overflow-hidden border border-slate-700/50">
                  <div :style="{ width: `${serialDeviceState(port)?.flashProgress || 0}%` }" class="h-full bg-cyan-500 rounded-full transition-all duration-300"></div>
                </div>
                <span class="text-[10px] font-bold text-slate-400 font-mono">{{ serialDeviceState(port)?.flashProgress || 0 }}%</span>
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
                <div v-for="(log, idx) in serialDeviceState(port)?.flashLogs" :key="idx" class="border-l border-slate-800 pl-1.5 py-0.5">
                  {{ log }}
                </div>
                <div v-if="!serialDeviceState(port)?.flashLogs?.length" class="text-slate-600 italic text-center">
                  No console output yet.
                </div>
              </div>
            </div>

            <div v-if="bulkSelectedPorts.length === 0" class="h-full flex items-center justify-center text-slate-600 italic text-xs">
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
                v-if="activeMode === 'serial' && isMonitoring"
                class="flex items-center gap-1 pl-4 text-xs text-slate-500"
              >
                <span>Monitoring</span>
                <span class="font-mono text-slate-300">{{ monitorDeviceLabel }}</span>
                <span class="text-slate-500">on</span>
                <span class="font-mono text-slate-400">{{ activeMonitorPort || selectedPort }}</span>
                <template v-if="serialUptimeLabel">
                  <span class="text-slate-600">·</span>
                  <span>Uptime</span>
                  <span class="font-mono text-slate-400">{{ serialUptimeLabel }}</span>
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
                @click="copyActivityLog"
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
                v-if="activeMode === 'serial' && identifyAvailable"
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="[
                  'p-1 rounded border transition-all disabled:opacity-50 disabled:cursor-not-allowed',
                  isIdentifying
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
                v-if="activeMode === 'serial' && isMonitoring"
                @click="copyActivePassword"
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
                @click="toggleMonitor"
                :class="[
                  'rounded border transition-all',
                  isMonitoring
                    ? 'p-1 border-cyan-500 bg-cyan-500/20 text-cyan-300 hover:text-cyan-100'
                    : 'p-1 border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                ]"
                :title="isMonitoring ? 'Stop monitor' : 'Start monitor'"
                :aria-label="isMonitoring ? 'Stop monitor' : 'Start monitor'"
              >
                <svg v-if="isMonitoring" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" aria-hidden="true">
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
              <button @click="clearActivityLog" class="text-xs text-slate-500 hover:text-slate-300">Clear</button>
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

      <div v-if="activeMode === 'pair'" class="flex flex-col gap-3 h-full overflow-hidden">
        <!-- Gateway Device Validation Warning Callout -->
        <div v-if="gatewaySelectedPort && serialDeviceState(gatewaySelectedPort)?.status && !serialDeviceState(gatewaySelectedPort)?.status?.role_tx" class="rounded-lg border border-amber-500/30 bg-amber-500/10 p-3 text-amber-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
          <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-amber-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
            <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
          <div>
            <span class="font-bold">Gateway Device Required:</span> The device currently connected on <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewaySelectedPort }}</span> is configured as a <span class="font-bold text-amber-200">Remote</span>. Please connect a gateway device instead.
          </div>
        </div>

        <!-- Gateway Uncommissioned/Factory State Warning Callout -->
        <div v-if="gatewaySelectedPort && serialDeviceState(gatewaySelectedPort)?.status && serialDeviceState(gatewaySelectedPort)?.status?.role_tx && (!serialDeviceState(gatewaySelectedPort)?.status?.commissioned || serialDeviceState(gatewaySelectedPort)?.status?.fleet_passphrase_default)" class="rounded-lg border border-cyan-500/30 bg-cyan-500/10 p-3 text-cyan-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
          <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-cyan-400 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor" stroke-width="2">
            <circle cx="12" cy="12" r="10"></circle>
            <line x1="12" y1="8" x2="12" y2="12"></line>
            <line x1="12" y1="16" x2="12.01" y2="16"></line>
          </svg>
          <div>
            <span class="font-bold">Uncommissioned Gateway:</span> The gateway connected on <span class="font-mono text-white bg-slate-900/60 px-1 py-0.5 rounded border border-slate-700/50">{{ gatewaySelectedPort }}</span> is in a <span class="font-bold text-cyan-200">Factory / Uncommissioned State</span>. Provisioning it now will assign the new fleet key and commission it.
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
          <div class="flex items-start justify-between gap-3">
            <div>
              <h2 class="text-base font-bold text-cyan-300">
                Provision
              </h2>
              <p class="mt-1 text-xs text-slate-400">Selected USB device becomes the LoRa gateway.</p>
            </div>
            <div class="flex items-center gap-2">
              <button
                v-if="identifyAvailable"
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="['glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed', { 'identify-led-active': isIdentifying }]"
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
                @click="() => loadEasyPairGateway(false)"
                :disabled="isGatewayLoading || isPairBusy || !selectedPort"
                class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
              >
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-cyan-300': isGatewayLoading }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
                <span>{{ isGatewayLoading ? 'Loading...' : 'Load Gateway' }}</span>
              </button>
            </div>
          </div>

          <div class="grid grid-cols-2 rounded border border-slate-800 bg-slate-950/30 text-xs font-bold">
            <button
              @click="pairPanelTab = 'pair'"
              :class="['m-0 h-8 rounded-none px-3 transition-all', pairPanelTab === 'pair' ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
            >
              Pair
            </button>
            <button
              @click="openPairWifiTab"
              :class="['m-0 h-8 rounded-none px-3 transition-all', pairPanelTab === 'wifi' ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
            >
              WiFi
            </button>
          </div>

          <div v-if="pairPanelTab === 'pair'" class="flex flex-col gap-3">
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">USB gateway</label>
              <div class="flex gap-2">
                <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                  <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                    {{ port.port_name }}
                  </option>
                  <option v-if="ports.length === 0" disabled>Scanning...</option>
                </select>
                <button @click="refreshPorts" :disabled="isRefreshingPorts || serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                </button>
              </div>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Scan count</label>
              <input v-model.number="pairExpectedCount" class="glass-input h-10" type="number" min="1" max="12" />
              <div class="text-[10px] text-slate-500">Powered factory/unprovisioned remotes to listen for in this scan.</div>
            </div>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Fleet key</label>
              <div class="grid grid-cols-[3rem_minmax(0,1fr)_3rem_3rem] gap-2">
                <button @click="generatePairFleetKey(true)" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" title="Generate fleet key" aria-label="Generate fleet key">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
                </button>
                <input v-model="pairFleetKey" class="glass-input h-10 flex-1 font-mono" :type="showPairFleetKey ? 'text' : 'password'" autocomplete="new-password" />
                <button @click="showPairFleetKey = !showPairFleetKey" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="showPairFleetKey ? 'Hide fleet key' : 'Show fleet key'" :aria-label="showPairFleetKey ? 'Hide fleet key' : 'Show fleet key'">
                  <svg v-if="!showPairFleetKey" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
                <button @click="copyPairFleetKey" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" title="Copy fleet key" aria-label="Copy fleet key">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Gateway admin password</label>
              <div class="grid grid-cols-[minmax(0,1fr)_3rem_3rem] gap-2">
                <input v-model="pairAdminPassword" class="glass-input h-10 min-w-0 font-mono" :type="showPairAdminPassword ? 'text' : 'password'" autocomplete="current-password" />
                <button @click="showPairAdminPassword = !showPairAdminPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="showPairAdminPassword ? 'Hide password' : 'Show password'" :aria-label="showPairAdminPassword ? 'Hide password' : 'Show password'">
                  <svg v-if="!showPairAdminPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
                <button @click="copyPairAdminPassword" :disabled="!pairAdminPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50" title="Copy gateway password" aria-label="Copy gateway password">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="grid grid-cols-[1fr_auto] gap-3">
            <button @click="isPairBusy ? cancelEasyPair() : runEasyPair()" :disabled="!isPairBusy && pairPrimaryDisabled" :class="['h-9 flex items-center justify-center gap-3 text-sm font-bold transition-colors', isPairBusy ? 'rounded-md border border-amber-500/40 bg-amber-500/20 text-amber-100 hover:bg-amber-500/30' : 'primary-btn']">
              <svg v-if="isPairBusy" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><rect x="7" y="7" width="10" height="10" rx="1.5"></rect></svg>
              <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
              <span>{{ isPairBusy ? 'Cancel' : 'Provision' }}</span>
            </button>
            <button @click="startEasyPairDiscovery" :disabled="isPairBusy || isGatewayLoading || !selectedPort" class="glass-input h-9 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Discover powered remotes over LoRa without provisioning">
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
                {{ gatewayWifiReady ? 'Gateway WiFi is already connected. Enter the WiFi password if you need to send it to remotes.' : 'Read gateway status, scan if needed, then send the same credentials to remotes.' }}
              </p>
            </div>
            <button
              @click="scanGatewayWifi"
              :disabled="isWifiScanning || isGatewayLoading || !selectedPort"
              class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold"
            >
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-cyan-300': isWifiScanning || isGatewayLoading }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
              <span>{{ isGatewayLoading ? 'Loading...' : isWifiScanning ? 'Scanning...' : gatewayReady ? 'Scan WiFi' : 'Load & Scan' }}</span>
            </button>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">WiFi network</label>
              <select v-model="pairWifiSsid" @change="clearGatewayWifiReady" class="glass-input h-10 appearance-none">
                <option v-for="network in wifiNetworks" :key="`${network.ssid}-${network.bssid}`" :value="network.ssid">
                  {{ network.ssid }} · {{ wifiSignalLabel(network.rssi) }} · ch {{ network.channel }}
                </option>
                <option v-if="pairWifiSsid && !pairWifiSsidInScan" :value="pairWifiSsid">
                  {{ pairWifiSsid }} · {{ gatewayWifiReady ? 'already connected' : 'selected' }}
                </option>
                <option v-if="wifiNetworks.length === 0" disabled>Scan to choose a network</option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">WiFi password</label>
              <div class="flex gap-2">
                <input v-model="pairWifiPassword" class="glass-input h-10 flex-1" :type="showPairWifiPassword ? 'text' : 'password'" autocomplete="new-password" />
                <button @click="showPairWifiPassword = !showPairWifiPassword" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center" :title="showPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'" :aria-label="showPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'">
                  <svg v-if="!showPairWifiPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="flex flex-col gap-2">
            <p :class="['text-xs', gatewayWifiReady ? 'text-emerald-300' : 'text-slate-400']">{{ gatewayWifiStatusText }}</p>
            <button
              v-if="!gatewayWifiReady"
              @click="connectGatewayWifi"
              :disabled="isWifiApplying || !gatewayReady || !pairWifiSsid"
              class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
            >
              {{ isWifiApplying ? 'Connecting Gateway...' : 'Connect Gateway' }}
            </button>
            <button
              v-else
              @click="sendWifiToRemotes"
              :disabled="isFleetWifiSending || !gatewayReady || !pairWifiSsid"
              class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
            >
              {{ isFleetWifiSending ? 'Sending...' : 'Send to Remotes' }}
            </button>
          </div>
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-3 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between">
            <h2 class="text-base font-bold text-slate-300">Discovered devices</h2>
            <button @click="refreshEasyPairStatus(true)" :disabled="isPairBusy || !gatewayReady" class="text-xs text-slate-500 hover:text-cyan-300">Refresh</button>
          </div>
          <div v-if="pairStatus?.session" class="grid grid-cols-4 gap-3 text-xs">
            <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
              <div class="text-slate-500">State</div>
              <div class="font-mono text-slate-300 truncate">{{ pairStatus.session.state }}</div>
            </div>
            <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
              <div class="text-slate-500">Found</div>
              <div class="font-mono text-slate-300">{{ pairDiscoveredDeviceCount }} / {{ pairStatus.session.estimated_count }}</div>
            </div>
            <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
              <div class="text-slate-500">Verified</div>
              <div class="font-mono text-slate-300">{{ pairStatus.session.verified_count }}</div>
            </div>
            <div class="rounded-md border border-slate-800 bg-slate-900/30 p-3">
              <div class="text-slate-500">Failed</div>
              <div class="font-mono text-slate-300">{{ pairStatus.session.failed_count }}</div>
            </div>
          </div>

          <div v-if="!pairStatus?.devices?.length" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
            Power the remote devices, then scan from the selected USB gateway.
          </div>

          <div v-else class="flex-1 min-h-0 overflow-auto custom-scrollbar pr-1">
            <div
              v-for="device in pairStatus.devices"
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
                  <div class="font-mono text-slate-300">{{ compactFirmwareVersion(device.fw_major, device.fw_minor, device.fw_patch, device.fw_build) }}</div>
                </div>
                <div class="text-right">
                  <span :class="['rounded border px-2 py-1 text-[10px] font-bold', device.address_conflict ? 'border-amber-500/40 bg-amber-500/10 text-amber-300' : 'border-slate-700 bg-slate-800/40 text-slate-300']">
                    {{ device.address_conflict ? 'Conflict' : device.state }}
                  </span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Right Panel (Controls + Details) - Hidden in Monitor Mode -->
      <div v-if="activeMode === 'serial' && !isMonitoring" class="flex flex-col gap-4 h-full min-h-0 overflow-auto custom-scrollbar pr-1 transition-opacity duration-300" :class="{ 'opacity-0 pointer-events-none': isMonitoring }">
        <!-- Segment Control (Single vs Bulk) -->
        <div class="grid grid-cols-2 rounded-lg border border-slate-800 bg-slate-950/40 p-1 text-xs font-bold shrink-0">
          <button
            @click="bulkMode = false"
            :class="['m-0 h-9 rounded-md px-3 transition-all flex items-center justify-center gap-1.5 shadow-none border-0', !bulkMode ? 'bg-cyan-600/80 text-white shadow-lg shadow-cyan-500/10' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
          >
            <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
              <rect x="3" y="3" width="18" height="18" rx="2" ry="2"></rect>
              <line x1="9" y1="3" x2="9" y2="21"></line>
            </svg>
            <span>Single Device</span>
          </button>
          <button
            @click="bulkMode = true"
            :class="['m-0 h-9 rounded-md px-3 transition-all flex items-center justify-center gap-1.5 shadow-none border-0', bulkMode ? 'bg-cyan-600/80 text-white shadow-lg shadow-cyan-500/10' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
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
        <template v-if="!bulkMode">
          <!-- Device Configuration Panel -->
          <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
            <div class="flex items-start justify-between gap-3">
              <h2 class="text-base font-bold text-cyan-300">
                Device configuration
              </h2>
              <button
                v-if="identifyAvailable"
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="['glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed', { 'identify-led-active': isIdentifying }]"
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
                <select v-model="region" class="glass-input h-10 appearance-none">
                  <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
                </select>
              </div>

              <div class="flex flex-col gap-1.5 text-xs">
                <label class="font-medium text-slate-400">Serial port</label>
                <div class="flex gap-2">
                  <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                    <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                      {{ port.port_name }}
                    </option>
                    <option v-if="ports.length === 0" disabled>Scanning...</option>
                  </select>
                  <button @click="refreshPorts" :disabled="isRefreshingPorts || serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                    <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                  </button>
                </div>
              </div>
            </div>

            <div :class="['rounded-md border p-3', flashRunningFirmware ? 'border-cyan-500/30 bg-cyan-500/10' : 'border-slate-800 bg-slate-950/30']">
              <div class="flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
                <div>
                  <div class="text-[10px] font-bold uppercase tracking-wide text-slate-500">Running firmware</div>
                  <div :class="['mt-1 font-mono text-xl font-bold', flashRunningFirmware ? 'text-cyan-100' : 'text-slate-500']">
                    {{ flashRunningFirmware || '-' }}
                  </div>
                </div>
                <div class="text-xs text-slate-400 sm:text-right">
                  {{ flashRunningFirmwareSummary }}
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
                <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
                </button>
              </div>
            </div>

            <div class="grid grid-cols-2 gap-3 mt-2">
              <div class="flex flex-col gap-3">
                <button @click="startFlash" :disabled="flashDisabled" class="primary-btn h-9 flex items-center justify-center gap-2 text-xs font-bold w-full active:scale-95 transition-all disabled:opacity-60 disabled:cursor-not-allowed">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
                  <span>{{ isFlashing ? 'Flashing...' : 'Flash firmware' }}</span>
                </button>
                <div class="flex flex-wrap items-center gap-3 px-1">
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="eraseBeforeFlash" class="peer hidden" />
                      <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-amber-500 peer-checked:border-amber-500 transition-all"></div>
                      <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                    </div>
                    <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Erase flash before write</span>
                  </label>
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="monitorAfterFlash" class="peer hidden" />
                      <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                      <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                    </div>
                    <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor when flash complete</span>
                  </label>
                </div>
              </div>
              <button @click="readDeviceInfo" :disabled="isFlashing || isLoadingInfo" class="glass-input h-9 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs transition-all active:scale-95">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400', { 'animate-spin text-cyan-300': isLoadingInfo }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
                <span>{{ isLoadingInfo ? 'Reading...' : 'Get device info' }}</span>
              </button>
            </div>
          </div>

          <!-- Device Details Panel -->
          <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
            <div class="flex items-center justify-between">
              <h2 class="text-base font-bold text-slate-300">
                Device details
              </h2>
              <button v-if="deviceInfo" @click="copyAllDeviceInfo" class="text-slate-500 hover:text-cyan-300 transition-colors" title="Copy all">
                <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
              </button>
            </div>

            <div class="space-y-1">
              <div v-for="[key, val] in orderedDeviceInfoEntries" :key="key" class="group flex items-center justify-between text-xs border-b border-white/5 py-1.5 hover:bg-white/5 px-2 -mx-2 rounded transition-colors">
                <span class="text-slate-500">{{ formatLabel(key) }}</span>
                <div class="flex items-center gap-3">
                  <span class="font-mono text-slate-300">{{ val }}</span>
                  <button @click="copyToClipboard(val.toString(), formatLabel(key).toLowerCase())" class="opacity-0 group-hover:opacity-100 text-slate-600 hover:text-cyan-300 transition-all">
                    <svg xmlns="http://www.w3.org/2000/svg" class="w-3.5 h-3.5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                  </button>
                </div>
              </div>
              <div v-if="!deviceInfo && !isLoadingInfo" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
                Connect a device and click <br/> "Get device info"
              </div>
              <div v-if="isLoadingInfo" class="h-32 flex flex-col items-center justify-center text-cyan-300 italic text-sm gap-2">
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
                <button @click="toggleSelectAllBulkPorts" class="text-xs text-slate-400 hover:text-slate-200 shadow-none bg-transparent border border-slate-700 rounded px-2 py-0.5 transition-all">
                  {{ bulkSelectedPorts.length === ports.length ? 'Deselect All' : 'Select All' }}
                </button>
                <button @click="refreshPorts" :disabled="isRefreshingPorts" class="glass-input m-0 h-7 w-7 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                </button>
              </div>
            </div>

            <!-- Ports checklist -->
            <div class="flex flex-col gap-2 max-h-48 overflow-auto pr-1 custom-scrollbar">
              <div v-for="port in ports" :key="port.port_name" class="flex items-center justify-between p-2 rounded-md border border-slate-800 bg-slate-900/30 hover:border-slate-700/80 transition-all">
                <label class="flex items-center gap-3 cursor-pointer group flex-1">
                  <div class="relative flex items-center">
                    <input type="checkbox" :value="port.port_name" v-model="bulkSelectedPorts" class="peer hidden" />
                    <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                    <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                  </div>
                  <span class="text-xs font-semibold font-mono text-slate-300 group-hover:text-cyan-300 transition-colors">{{ port.port_name }}</span>
                </label>
                <span v-if="serialDeviceState(port.port_name)?.deviceInfo?.chip_id" class="text-[10px] font-mono text-slate-500 bg-slate-800/60 px-1.5 py-0.5 rounded border border-slate-700/40">
                  {{ serialDeviceState(port.port_name)?.deviceInfo?.chip_id }}
                </span>
              </div>
              <div v-if="ports.length === 0" class="h-16 flex items-center justify-center text-slate-500 italic text-xs">
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
                <select v-model="region" class="glass-input h-10 appearance-none">
                  <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
                </select>
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
                  <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input h-10 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0">
                    <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
                  </button>
                </div>
              </div>
            </div>

            <!-- Action execution CTAs & Options -->
            <div class="grid grid-cols-2 gap-4 mt-3 pt-3 border-t border-slate-800">
              <!-- Flash CTA & Options -->
              <div class="flex flex-col gap-3">
                <button
                  @click="startBulkFlash"
                  :disabled="bulkFlashDisabled"
                  class="primary-btn h-10 flex items-center justify-center gap-2 text-xs font-bold w-full active:scale-95 transition-all disabled:opacity-50 disabled:cursor-not-allowed"
                >
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin': isBulkFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
                  <span>{{ isBulkFlashing ? 'Flashing...' : 'Bulk Flash' }}</span>
                </button>
                <div class="flex flex-col gap-2 px-1">
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="eraseBeforeFlash" class="peer hidden" />
                      <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-amber-500 peer-checked:border-amber-500 transition-all"></div>
                      <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                    </div>
                    <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Erase flash before write</span>
                  </label>
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="monitorAfterFlash" class="peer hidden" />
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
                  @click="startBulkFactoryReset"
                  :disabled="bulkResetDisabled"
                  class="glass-input m-0 h-10 hover:bg-slate-700/70 border-amber-500/30 hover:border-amber-500/60 bg-amber-500/5 text-amber-300 flex items-center justify-center gap-2 text-xs transition-all active:scale-95 disabled:opacity-50 disabled:cursor-not-allowed disabled:border-slate-800 disabled:bg-slate-900/10 w-full"
                >
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin': isBulkResetting }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/></svg>
                  <span>{{ isBulkResetting ? 'Resetting...' : 'Bulk Reset' }}</span>
                </button>
                <div class="flex flex-col gap-2 px-1">
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="serialFactoryKeepFleet" class="peer hidden" />
                      <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-cyan-600 peer-checked:border-cyan-500 transition-all"></div>
                      <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                    </div>
                    <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Keep shared fleet key</span>
                  </label>
                  <label class="flex items-center gap-2 cursor-pointer group">
                    <div class="relative flex items-center">
                      <input type="checkbox" v-model="serialFactoryKeepWifi" class="peer hidden" />
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

      <div v-if="activeMode === 'settings'" class="flex flex-col h-full min-h-0 overflow-hidden gap-3 text-left">
        <div class="glass-card flex flex-col gap-3 p-3 shrink-0">
          <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
            <div class="min-w-0">
              <h2 class="text-base font-bold text-cyan-300">Settings</h2>
              <p class="mt-1 text-xs text-slate-400">{{ serialStatusSummary }}</p>
            </div>
            <div class="flex flex-wrap items-center gap-2">
              <button
                v-if="identifyAvailable"
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="[
                  'glass-input m-0 h-8 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed',
                  { 'identify-led-active text-cyan-300': isIdentifying }
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
              <button @click="readDeviceInfo" :disabled="isFlashing || isLoadingInfo" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                {{ isLoadingInfo ? 'Reading...' : 'Read identity' }}
              </button>
              <button @click="refreshSerialAdminStatus" :disabled="serialAdminDisabled" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminLoading ? 'Loading...' : 'Refresh status' }}
              </button>
              <button @click="fetchSerialDeviceSettings" :disabled="!selectedPort || isFlashing || isLoadingInfo || serialAdminBusy" class="primary-btn m-0 h-8 px-3 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminLoading ? 'Fetching...' : 'Fetch settings' }}
              </button>
            </div>
          </div>

          <div class="grid grid-cols-1 gap-2 md:grid-cols-[minmax(0,1fr)_10rem_10rem]">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Device</label>
              <div class="flex gap-2">
                <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-9 flex-1 appearance-none disabled:opacity-60">
                  <option value="" disabled>Select USB device</option>
                  <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                    {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
                  </option>
                  <option v-if="ports.length === 0" disabled>Scanning...</option>
                </select>
                <button @click="refreshPorts" :disabled="isRefreshingPorts || serialPortSelectorDisabled" class="glass-input h-9 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                </button>
              </div>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Transport</label>
              <select v-model="settingsTransport" class="glass-input h-9 appearance-none">
                <option value="serial">Serial</option>
                <option value="mqtt" disabled>MQTT later</option>
                <option value="lora" disabled>LoRa gateway later</option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Admin path</label>
              <div class="glass-input h-9 flex items-center text-slate-400">
                {{ settingsTransport === 'serial' ? 'USB serial admin' : 'Not available yet' }}
              </div>
            </div>
          </div>
        </div>

        <div class="glass-card flex min-h-0 flex-1 flex-col overflow-hidden">
          <div class="flex shrink-0 overflow-x-auto border-b border-slate-800 bg-slate-900/50 text-xs font-bold">
            <button
              v-for="tab in SETTINGS_TABS"
              :key="tab.key"
              @click="settingsTab = tab.key"
              :class="['m-0 h-9 rounded-none border-r border-slate-800 px-4 transition-colors', settingsTab === tab.key ? 'bg-cyan-700 text-white' : 'text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
            >
              {{ tab.label }}
            </button>
          </div>

          <div class="min-h-0 flex-1 overflow-auto custom-scrollbar p-3">
            <div v-if="serialAdminIsFactoryDefault && settingsTab !== 'remote'" class="mb-3 rounded border border-amber-500/30 bg-amber-500/10 p-2 text-xs text-amber-100">
              Factory default: this device is not commissioned yet. Use Provision before treating it as an operational transmitter or receiver.
            </div>

            <div v-if="!hasActiveDeviceInfo && settingsTab !== 'remote'" class="rounded border border-slate-800 bg-slate-950/30 p-3 text-xs text-slate-500">
              Select a USB device and read device info before loading or saving settings.
            </div>

            <div v-if="settingsTab === 'general'" class="flex flex-col gap-3">
              <div v-if="serialAdminStatus" class="grid grid-cols-2 gap-2 text-xs xl:grid-cols-5">
                <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
                  <div class="text-slate-500">Firmware</div>
                  <div class="font-mono text-slate-200">{{ serialAdminStatus.fw_version || 'unknown' }}</div>
                </div>
                <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
                  <div class="text-slate-500">Uptime</div>
                  <div class="font-mono text-slate-200">{{ formatUptime(serialAdminStatus.uptime_ms || 0) }}</div>
                </div>
                <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
                  <div class="text-slate-500">Heap</div>
                  <div class="font-mono text-slate-200">{{ formatBytes(serialAdminStatus.heap_free) }}</div>
                </div>
                <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
                  <div class="text-slate-500">Mode</div>
                  <div class="font-mono text-slate-200">{{ serialAdminStatus.mode || '-' }}</div>
                </div>
                <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
                  <div class="text-slate-500">State</div>
                  <div class="font-mono text-slate-200">{{ serialAdminIsFactoryDefault ? 'factory' : 'commissioned' }}</div>
                </div>
              </div>

              <div v-if="serialAdminConfig" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
                <label class="self-center text-right font-semibold text-slate-300">Role</label>
                <select v-model="serialAdminConfig.role_tx" class="glass-input h-9 appearance-none">
                  <option :value="true">Gateway / transmitter</option>
                  <option :value="false">Remote / receiver</option>
                </select>
                <label class="self-center text-right font-semibold text-slate-300">Local addr</label>
                <input v-model.number="serialAdminConfig.local_address" type="number" min="1" max="254" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">Remote addr</label>
                <input v-model.number="serialAdminConfig.remote_address" type="number" min="1" max="254" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">Fleet key</label>
                <div class="flex gap-2">
                  <input v-model="serialAdminConfig.fleet_passphrase" :type="showSerialFleetKey ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Fleet key passphrase" />
                  <button @click="showSerialFleetKey = !showSerialFleetKey" class="glass-input h-9 px-3 hover:bg-slate-700/70" type="button">
                    {{ showSerialFleetKey ? 'Hide' : 'Show' }}
                  </button>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">Heartbeat sec</label>
                <div class="flex items-center gap-2">
                  <input :value="Math.round((serialAdminConfig.heartbeat_ms || 60000) / 1000)" @input="serialAdminConfig.heartbeat_ms = Number(($event.target as HTMLInputElement).value || 60) * 1000" type="number" min="60" max="3600" class="glass-input h-9 flex-1" :disabled="!serialAdminConfig.heartbeat_enabled" />
                  <label class="flex items-center gap-1.5 text-xs text-slate-400 cursor-pointer select-none whitespace-nowrap">
                    <input v-model="serialAdminConfig.heartbeat_enabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
                    Enabled
                  </label>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">Retry sec</label>
                <input :value="Math.round((serialAdminConfig.tx_command_retry_timeout_ms || 180000) / 1000)" @input="serialAdminConfig.tx_command_retry_timeout_ms = Number(($event.target as HTMLInputElement).value || 180) * 1000" type="number" min="5" max="3600" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">RX failsafe</label>
                <select v-model="serialAdminConfig.rx_failsafe_mode" class="glass-input h-9 appearance-none">
                  <option value="hold_last">Hold last</option>
                  <option value="force_off">Force off</option>
                  <option value="force_on">Force on</option>
                </select>
                <label class="self-center text-right font-semibold text-slate-300">Failsafe sec</label>
                <input :value="Math.round((serialAdminConfig.rx_failsafe_timeout_ms || 180000) / 1000)" @input="serialAdminConfig.rx_failsafe_timeout_ms = Number(($event.target as HTMLInputElement).value || 180) * 1000" type="number" min="5" max="3600" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">Debug telemetry</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.maintenance_debug_telemetry_enabled" type="checkbox" />
                  Enabled
                </label>
                <label class="self-center text-right font-semibold text-slate-300">Input control</label>
                <label class="flex items-center gap-2 text-slate-300" title="When enabled, closing the gateway's input terminals will command paired remotes to close their relays">
                  <input v-model="serialAdminConfig.input_control_paired_lora_enabled" type="checkbox" />
                  Enabled via gateway input
                </label>
              </div>
            </div>

            <div v-if="settingsTab === 'network'" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
              <template v-if="serialAdminConfig">
                <div class="col-span-2 rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
                  <div class="flex flex-wrap items-center justify-between gap-2">
                    <span>WiFi passwords are redacted on fetch. Leave password fields blank to keep the stored value.</span>
                    <button @click="scanSettingsWifi" :disabled="!selectedPort || isFlashing || isLoadingInfo || serialAdminBusy || isWifiScanning" class="glass-input h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                      {{ isWifiScanning ? 'Scanning...' : 'Scan WiFi' }}
                    </button>
                  </div>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">WiFi SSID</label>
                <div class="flex gap-2">
                  <input v-model="serialAdminConfig.wifi_sta_ssid" class="glass-input h-9 flex-1" placeholder="Leave blank for no WiFi" list="settings-wifi-networks" />
                  <datalist id="settings-wifi-networks">
                    <option v-for="network in settingsWifiNetworks" :key="`${network.ssid}-${network.bssid}`" :value="network.ssid">
                      {{ network.ssid }} · {{ wifiSignalLabel(network.rssi) }} · ch {{ network.channel }}{{ network.secure ? ' · secured' : ' · open' }}
                    </option>
                  </datalist>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">New WiFi password</label>
                <div class="flex gap-2">
                  <input v-model="serialAdminConfig.wifi_sta_password" :type="showSerialWifiPassword ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Blank keeps existing password" />
                  <button @click="showSerialWifiPassword = !showSerialWifiPassword" class="glass-input h-9 px-3 hover:bg-slate-700/70">{{ showSerialWifiPassword ? 'Hide' : 'Show' }}</button>
                </div>
                <label class="self-center text-right font-semibold text-slate-300">WiFi admin</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.wifi_admin_enabled" type="checkbox" />
                  Enabled
                </label>
                <label class="self-center text-right font-semibold text-slate-300">Hostname</label>
                <input v-model="serialAdminConfig.lan_hostname" class="glass-input h-9" placeholder="Blank uses lrs-chipid" />
                <label class="self-center text-right font-semibold text-slate-300">Fallback AP</label>
                <select v-model="serialAdminConfig.wifi_ap_fallback_policy" class="glass-input h-9 appearance-none">
                  <option value="fallback_on_disconnect">Enable when disconnected</option>
                  <option value="secure_sta_only">Keep disabled</option>
                </select>
                <label class="self-center text-right font-semibold text-slate-300">Soft AP</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.ap_always_on" type="checkbox" />
                  Always on while disconnected
                </label>
                <label class="self-center text-right font-semibold text-slate-300">PHY mode</label>
                <select v-model="serialAdminConfig.wifi_phy_mode" class="glass-input h-9 appearance-none">
                  <option value="11b">11b range</option>
                  <option value="11g">11g</option>
                  <option value="11n">11n</option>
                </select>
                <label class="self-center text-right font-semibold text-slate-300">TX power</label>
                <input v-model.number="serialAdminConfig.wifi_tx_power_dbm" type="number" min="0" max="20.5" step="0.25" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">Channel</label>
                <input v-model.number="serialAdminConfig.wifi_channel_override" type="number" min="0" max="13" class="glass-input h-9" />
                <label class="self-center text-right font-semibold text-slate-300">WiFi sleep</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.wifi_sleep_enabled" type="checkbox" />
                  Allow sleep
                </label>
                <label class="self-center text-right font-semibold text-slate-300">Static IP</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.wifi_static_ip_enabled" type="checkbox" />
                  Enabled
                </label>
                <label class="self-center text-right font-semibold text-slate-300">IP address</label>
                <input v-model="serialAdminConfig.wifi_static_ip" class="glass-input h-9" placeholder="192.168.1.50" />
                <label class="self-center text-right font-semibold text-slate-300">Gateway</label>
                <input v-model="serialAdminConfig.wifi_static_gateway" class="glass-input h-9" placeholder="192.168.1.1" />
                <label class="self-center text-right font-semibold text-slate-300">Subnet</label>
                <input v-model="serialAdminConfig.wifi_static_subnet" class="glass-input h-9" placeholder="255.255.255.0" />
              </template>
            </div>

            <div v-if="settingsTab === 'mqtt'" class="flex flex-col gap-3 text-xs">
              <template v-if="serialAdminConfig">
                <!-- If it's a remote/receiver unit -->
                <div v-if="!serialAdminConfig.role_tx" class="rounded border border-cyan-500/20 bg-cyan-950/15 p-3 text-cyan-200 leading-relaxed shadow-[inset_0_1px_0_rgba(6,182,212,0.15)] select-text">
                  <div class="font-bold text-sm text-cyan-100 mb-1 flex items-center gap-1.5">
                    <span class="inline-block w-2.5 h-2.5 rounded-full bg-cyan-500 shadow-[0_0_8px_rgba(6,182,212,0.6)]"></span>
                    🌐 Remote MQTT Routing Bridge Active
                  </div>
                  This device is configured with the <span class="font-bold text-cyan-100">Remote / Receiver</span> role.
                  <p class="mt-2 text-slate-300">
                    Remote units do not run local MQTT clients to conserve power, memory, and local WiFi network capacity. Instead, they communicate securely over LoRa to your central Gateway.
                  </p>
                  <p class="mt-2 text-slate-300">
                    The Gateway automatically connects to the MQTT broker and bridges all sensor telemetry and command topics to the broker on behalf of this remote node.
                  </p>
                  <p class="mt-3 text-cyan-300 font-semibold border-t border-cyan-500/20 pt-2 flex items-center gap-2">
                    💡 Remote configuration (like WiFi provisioning, sensor toggles, or reboots) happens over LoRa from the Gateway's MQTT peer command interface.
                  </p>
                </div>

                <!-- If it's a gateway/transmitter unit -->
                <div v-else class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2">
                  <label class="self-center text-right font-semibold text-slate-300">MQTT host</label>
                  <input v-model="serialAdminConfig.mqtt_host" class="glass-input h-9" placeholder="venus.local" />
                  <label class="self-center text-right font-semibold text-slate-300">MQTT port</label>
                  <input v-model.number="serialAdminConfig.mqtt_port" type="number" min="1" max="65535" class="glass-input h-9" />
                  <label class="self-center text-right font-semibold text-slate-300">Topic root</label>
                  <input v-model="serialAdminConfig.mqtt_topic_root" class="glass-input h-9" />
                  <label class="self-center text-right font-semibold text-slate-300">MQTT client</label>
                  <label class="flex items-center gap-2 text-slate-300">
                    <input v-model="serialAdminConfig.mqtt_client_enabled" type="checkbox" />
                    Enabled
                  </label>
                  <label class="self-center text-right font-semibold text-slate-300">MQTT control</label>
                  <label class="flex items-center gap-2 text-slate-300">
                    <input v-model="serialAdminConfig.mqtt_control_enabled" type="checkbox" />
                    Enabled
                  </label>
                  <label class="self-center text-right font-semibold text-slate-300">MQTT user</label>
                  <input v-model="serialAdminConfig.mqtt_user" class="glass-input h-9" />
                  <label class="self-center text-right font-semibold text-slate-300">New MQTT password</label>
                  <div class="flex gap-2">
                    <input v-model="serialAdminConfig.mqtt_password" :type="showSerialMqttPassword ? 'text' : 'password'" class="glass-input h-9 flex-1" placeholder="Blank keeps existing password" />
                    <button @click="showSerialMqttPassword = !showSerialMqttPassword" class="glass-input h-9 px-3 hover:bg-slate-700/70">{{ showSerialMqttPassword ? 'Hide' : 'Show' }}</button>
                  </div>
                  <label class="self-center text-right font-semibold text-slate-300">Controllers</label>
                  <input v-model="serialAdminConfig.mqtt_controller_addresses" class="glass-input h-9" placeholder="1,84" />
                </div>
              </template>
            </div>

            <div v-if="settingsTab === 'sensors'" class="grid grid-cols-[9rem_minmax(0,1fr)] gap-x-3 gap-y-2 text-xs">
              <template v-if="serialAdminConfig">
                <div class="col-span-2 rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
                  Sensor wiring is fixed in firmware for this board: DS18B20 uses the configured one-wire pin, and the 4-20 mA tank input uses A0 with the 3553 mV board calibration.
                </div>
                <label class="self-center text-right font-semibold text-slate-300">DS18B20</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.sensor_temp_enabled" type="checkbox" />
                  Temperature sensor enabled
                </label>
                <label class="self-center text-right font-semibold text-slate-300">Tank level</label>
                <label class="flex items-center gap-2 text-slate-300">
                  <input v-model="serialAdminConfig.sensor_tank_enabled" type="checkbox" />
                  4-20 mA tank sensor enabled
                </label>
                <div class="col-start-2 text-slate-500">
                  5000 mm water range, 120 ohm sense resistor, sampled every {{ serialAdminConfig.sensor_tank_interval_s }}s.
                </div>
              </template>
            </div>

            <div v-if="settingsTab === 'system'" class="flex flex-col gap-3 text-xs">
              <div class="rounded border border-slate-800 bg-slate-950/30 p-3 text-slate-400">
                Save applies the loaded settings over USB serial admin. Reboot clears cached live status. Factory reset clears loaded settings in Flasher because the device reboots into a new/default configuration.
              </div>
              <div class="rounded border border-amber-500/30 bg-amber-500/10 p-3 text-amber-100">
                Keep fleet key preserves pairing material. Keep WiFi preserves STA credentials. Clearing either one returns that part of the device to factory-default setup.
              </div>
              <div class="flex flex-wrap items-center gap-3">
                <button @click="saveSerialAdminConfig" :disabled="serialAdminDisabled || isSerialAdminSaving || !serialAdminConfig" class="primary-btn h-9 px-4 text-xs font-bold disabled:opacity-60">
                  {{ isSerialAdminSaving ? 'Saving...' : 'Save config' }}
                </button>
                <button @click="rebootSerialDevice" :disabled="serialAdminDisabled" class="glass-input h-9 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                  Reboot
                </button>
                <label class="flex items-center gap-2 text-slate-400">
                  <input v-model="serialFactoryKeepFleet" type="checkbox" />
                  Keep fleet key
                </label>
                <label class="flex items-center gap-2 text-slate-400">
                  <input v-model="serialFactoryKeepWifi" type="checkbox" />
                  Keep WiFi
                </label>
                <button @click="factoryResetSerialDevice" :disabled="serialAdminDisabled" class="glass-input h-9 px-4 hover:bg-red-500/15 text-xs font-bold text-red-200 disabled:opacity-60">
                  Factory reset
                </button>
              </div>
            </div>

            <!-- Remote Config Help Reference Tab -->
            <div v-if="settingsTab === 'remote'" class="flex flex-col gap-4 text-xs select-text">
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
                  @click="remoteSubTab = 'serial'"
                  type="button"
                  :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', remoteSubTab === 'serial' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
                >
                  <span class="font-mono text-[10px]">🔌</span> Serial Admin API
                </button>
                <button
                  @click="remoteSubTab = 'mqtt'"
                  type="button"
                  :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', remoteSubTab === 'mqtt' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
                >
                  <span class="font-mono text-[10px]">🌐</span> MQTT Bridge
                </button>
                <button
                  @click="remoteSubTab = 'lora'"
                  type="button"
                  :class="['m-0 h-8 rounded px-4 font-bold transition-all flex items-center gap-1.5', remoteSubTab === 'lora' ? 'bg-cyan-700 text-white shadow-md' : 'text-slate-400 hover:text-slate-200 hover:bg-slate-800/40']"
                >
                  <span class="font-mono text-[10px]">📡</span> LoRa OTA Protocol
                </button>
              </div>

              <!-- Content Cards -->
              <div class="flex flex-col gap-4">
                
                <!-- ================== SERIAL PANEL ================== -->
                <div v-if="remoteSubTab === 'serial'" class="flex flex-col gap-3">
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
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;hello\&quot;}', 'hello command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                          </div>
                          <span class="text-slate-400">Verifies serial communications and fetches API limits.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <div class="flex justify-between items-center">
                            <span class="font-mono font-bold text-cyan-300">"identity"</span>
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;identity\&quot;}', 'identity command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                          </div>
                          <span class="text-slate-400">Returns core device hardware serial, MAC, assigned Addresses, mode, and active firmware version.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <div class="flex justify-between items-center">
                            <span class="font-mono font-bold text-cyan-300">"status"</span>
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;status\&quot;}', 'status command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
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
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;get_config\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;include_secrets\&quot;:true}', 'get_config command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                          </div>
                          <span class="text-slate-400">Reads currently stored configuration store. Set <code class="font-mono text-[10px] text-slate-300">include_secrets</code> to true to request raw WiFi credentials and keys.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <div class="flex justify-between items-center">
                            <span class="font-mono font-bold text-cyan-300">"set_config"</span>
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;set_config\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;config\&quot;:{\&quot;wifi_sta_ssid\&quot;:\&quot;YourSSID\&quot;,\&quot;wifi_sta_password\&quot;:\&quot;Password\&quot;}}', 'set_config command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                          </div>
                          <span class="text-slate-400">Applies a configuration patch. Supports changing WiFi client setups, MQTT properties, sensor configs, timing parameters, and addresses.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <div class="flex justify-between items-center">
                            <span class="font-mono font-bold text-cyan-300">"configure_gateway"</span>
                            <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;configure_gateway\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;fleet_passphrase\&quot;:\&quot;YourKey\&quot;}', 'configure_gateway command')" class="text-[10px] text-slate-500 hover:text-cyan-300 transition-colors">Copy Payload</button>
                          </div>
                          <span class="text-slate-400">Provisions a factory default node into an operational Gateway, updating security key material and starting administration mode.</span>
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
                          <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;start_lora_inventory\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;start_addr\&quot;:1,\&quot;end_addr\&quot;:12}', 'start_lora_inventory')" class="text-[10px] text-slate-500 hover:text-cyan-300">Copy</button>
                        </div>
                        Commands gateway to query remote nodes via LoRa:
                        <code class="text-[10px] text-slate-500 mt-1">cmd: "start_lora_inventory"<br>cmd: "lora_inventory_status"</code>
                      </div>

                      <div class="bg-slate-950/20 border border-slate-800 p-2.5 rounded flex flex-col gap-1">
                        <div class="flex justify-between items-center">
                          <span class="font-mono text-cyan-300 font-semibold">Remote UDP Diagnostic Logs</span>
                          <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;remote_udp_log_control\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;addr\&quot;:1,\&quot;enabled\&quot;:true,\&quot;host\&quot;:\&quot;192.168.1.50\&quot;,\&quot;port\&quot;:5514}', 'remote_udp_log_control')" class="text-[10px] text-slate-500 hover:text-cyan-300">Copy</button>
                        </div>
                        Instructs a remote node over LoRa to redirect its diagnostic event logging to a specified local UDP server.
                      </div>

                      <div class="bg-slate-950/20 border border-slate-800 p-2.5 rounded flex flex-col gap-1">
                        <div class="flex justify-between items-center">
                          <span class="font-mono text-cyan-300 font-semibold">Remote OTA Firmware Pull</span>
                          <button @click="copyToClipboard('LRS:{\&quot;cmd\&quot;:\&quot;remote_ota_pull\&quot;,\&quot;password\&quot;:\&quot;admin_pwd\&quot;,\&quot;addr\&quot;:1,\&quot;url\&quot;:\&quot;http://192.168.1.100/fw.bin\&quot;,\&quot;sha256\&quot;:\&quot;YOUR_SHA256_HEX\&quot;}', 'remote_ota_pull')" class="text-[10px] text-slate-500 hover:text-cyan-300">Copy</button>
                        </div>
                        Signals a remote node over LoRa carrying an HTTP URL and SHA256 checksum to trigger it to download a firmware update over WiFi.
                      </div>
                    </div>
                  </div>
                </div>

                <!-- ================== MQTT PANEL ================== -->
                <div v-if="remoteSubTab === 'mqtt'" class="flex flex-col gap-3">
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
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/relay</code>
                          <span class="text-slate-400">Payload: <code class="text-slate-200">1</code> (ON) or <code class="text-slate-200">0</code> (OFF).</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300">Control Remote Node Relay</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/control</code>
                          <span class="text-slate-400">Gateway accepts JSON payload to trigger OTA command to a remote:</span>
                          <div class="flex items-center justify-between bg-slate-950/60 p-1.5 rounded mt-1">
                            <code class="font-mono text-cyan-400 text-[10px]">{"addr": 1, "relay": 1}</code>
                            <button @click="copyToClipboard('{\&quot;addr\&quot;: 1, \&quot;relay\&quot;: 1}', 'MQTT control payload')" class="text-[10px] text-slate-500 hover:text-cyan-200">Copy JSON</button>
                          </div>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300">Trigger Gateway OTA Update</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/ota_pull</code>
                          <div class="flex items-center justify-between bg-slate-950/60 p-1.5 rounded mt-1">
                            <code class="font-mono text-cyan-400 text-[10px]">{"url": "...", "sha256": "..."}</code>
                            <button @click="copyToClipboard('{\&quot;url\&quot;: \&quot;http://192.168.1.100/fw.bin\&quot;, \&quot;sha256\&quot;: \&quot;\&quot;}', 'MQTT OTA pull')" class="text-[10px] text-slate-500 hover:text-cyan-200">Copy JSON</button>
                          </div>
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
                          <span class="font-semibold text-slate-300">Peer Polling Interval (in seconds)</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peer/&lt;address&gt;/poll_interval_s</code>
                          <span class="text-slate-400">Payload: integer seconds (e.g. <code class="text-slate-200">300</code>). Set 0 to disable regular telemetry polling.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300">Force Peer Polling (Immediate)</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peer/&lt;address&gt;/poll_now</code>
                          <span class="text-slate-400">Payload: any. Forces the gateway to emit a secured LoRa status query reports probe.</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300">Peer WiFi Hardware Power Setup</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peer/&lt;address&gt;/wifi</code>
                          <span class="text-slate-400">Payload: <code class="text-slate-200">1</code> (Enable WiFi chip) or <code class="text-slate-200">0</code> (Power off WiFi to conserve energy).</span>
                        </div>

                        <div class="flex flex-col gap-1 border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300">Peer Remote Diagnostics Control</span>
                          <code class="font-mono text-cyan-300">lora/lrs-&lt;chipid&gt;/peer/&lt;address&gt;/udp_log_control</code>
                          <div class="flex items-center justify-between bg-slate-950/60 p-1.5 rounded mt-1">
                            <code class="font-mono text-cyan-400 text-[10px]">{"enabled": true, "host": "...", "port": 5514}</code>
                            <button @click="copyToClipboard('{\&quot;enabled\&quot;:true,\&quot;host\&quot;:\&quot;192.168.1.50\&quot;,\&quot;port\&quot;:5514,\&quot;ttl_s\&quot;:300}', 'Peer UDP config')" class="text-[10px] text-slate-500 hover:text-cyan-200">Copy JSON</button>
                          </div>
                        </div>
                      </div>
                    </div>
                  </div>

                  <!-- Telemetry Publishing Details -->
                  <div class="glass-card p-3 flex flex-col gap-2">
                    <div class="font-bold text-slate-200 border-b border-slate-800 pb-1">📈 Telemetry & Status Publishing Map</div>
                    <span class="text-slate-400">The gateway automatically publishes status reports to these topics when updates are heard over LoRa or changed locally. Telemetry values are published as **retained** plain-text strings:</span>
                    <div class="grid gap-4 sm:grid-cols-2 mt-1">
                      <div>
                        <div class="font-bold text-slate-300 text-[11px] mb-1">Local Gateway Status Topics:</div>
                        <ul class="list-disc pl-4 space-y-1 text-slate-400 font-mono text-[10px]">
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/input</span> (or <span class="text-slate-300">/dry_contact</span>): Dry contact state (<code class="text-cyan-400">1</code> or <code class="text-cyan-400">0</code>)</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/relay_feedback</span>: Live physical relay feedback sense state</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/temp_c</span> / <span class="text-slate-300">/remote_temp_c</span>: DS18B20 temperatures</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/tank_depth_mm</span>: Calibrated water level measurement</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/heap_free</span> / <span class="text-slate-300">/heap_frag_pct</span>: Controller health</li>
                        </ul>
                      </div>
                      <div>
                        <div class="font-bold text-slate-300 text-[11px] mb-1">Remote Peer Telemetry (Forwarded):</div>
                        <ul class="list-disc pl-4 space-y-1 text-slate-400 font-mono text-[10px]">
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/relay</span>: Remote unit relay feedback</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/input</span>: Remote unit dry contact state</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/ack_state</span>: OTA ACK status (<code class="text-emerald-400">Ok</code>, <code class="text-amber-400">Pending</code>, <code class="text-rose-400">Timeout</code>)</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/uplink_rssi_dbm</span>: Reception signal level</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/tank_status</span>: Tank telemetry state</li>
                          <li><span class="text-slate-300">lora/lrs-&lt;chipid&gt;/peer/&lt;addr&gt;/tank_depth_mm</span>: Tank depth level</li>
                        </ul>
                      </div>
                    </div>
                  </div>
                </div>

                <!-- ================== LORA PANEL ================== -->
                <div v-if="remoteSubTab === 'lora'" class="flex flex-col gap-3">
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
                          <p class="mt-0.5">Emitted by remote receivers periodically or immediately when a dry contact input changes. Carries logical state, active flags, current temperature code, and receiver-side diagnostics.</p>
                        </div>
                        <div class="border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::MaintenanceStatus</span>
                          <p class="mt-0.5">Delivered in high-density segmented paging packets, pulling deep operational metrics to the gateway:</p>
                          <ul class="list-disc pl-4 mt-1 space-y-1 text-slate-400 font-mono text-[10px]">
                            <li><span class="text-slate-300">Version Page:</span> Major, minor, patch, and build version code</li>
                            <li><span class="text-slate-300">Sensors Page:</span> 4-20mA current (mA), voltage (mV), and calibrated water level measurement (mm)</li>
                            <li><span class="text-slate-300">Debug Page:</span> Node uptime in minutes, free heap memory blocks, and free block fragmentation percentage</li>
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
                          <p class="mt-0.5">The gateway broadcasts chunked network credentials packets over the air. Receivers assemble the chunks in memory, verify the full string FNV-1a hash, and permanently commit the SSID & Password configuration store.</p>
                        </div>
                        <div class="border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::WifiControl</span>
                          <p class="mt-0.5">Directly manages remote WiFi chip state. Allows toggling WiFi on or off to optimize deep sleep profiles or query current client connection IP and RSSI levels.</p>
                        </div>
                        <div class="border-t border-slate-800/50 pt-2">
                          <span class="font-semibold text-slate-300 font-mono text-cyan-300 font-bold">MessageType::OtaPullControl</span>
                          <p class="mt-0.5">Tails remote nodes to fetch new firmware binaries from a local staging web server over WiFi. Payload contains the temporary update URL and SHA-256 file signature for local validation.</p>
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <p v-if="!serialAdminStatus && !serialAdminConfig && !isSerialAdminLoading && settingsTab !== 'remote'" class="mt-3 text-xs text-slate-500">
              {{ settingsEmptyMessage }}
            </p>
          </div>

          <!-- Sticky action footer for saving settings globally -->
          <div v-if="serialAdminConfig && settingsTab !== 'remote'" class="flex shrink-0 items-center justify-between border-t border-slate-800 bg-slate-900/60 p-3 text-xs">
            <span class="text-slate-400">
              💡 Changes must be saved to apply to the device.
            </span>
            <div class="flex items-center gap-2">
              <button
                @click="saveSerialAdminConfig"
                :disabled="serialAdminDisabled || isSerialAdminSaving || !serialAdminConfig"
                class="primary-btn m-0 h-9 px-4 text-xs font-bold disabled:opacity-60 flex items-center justify-center gap-1.5"
              >
                <svg v-if="isSerialAdminSaving" class="animate-spin h-3.5 w-3.5 text-white" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
                  <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                  <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8V0C5.373 0 0 5.373 0 12h4zm2 5.291A7.962 7.962 0 014 12H0c0 3.042 1.135 5.824 3 7.938l3-2.647z"></path>
                </svg>
                <span>{{ isSerialAdminSaving ? 'Saving...' : 'Save config' }}</span>
              </button>
              <button
                @click="rebootSerialDevice"
                :disabled="serialAdminDisabled || isSerialAdminSaving || !serialAdminConfig"
                class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60"
              >
                Reboot
              </button>
            </div>
          </div>
        </div>
      </div>

      <div v-if="activeMode === 'monitor'" class="flex flex-col h-full overflow-hidden gap-3">
        <!-- Gateway Device Validation Warning Callout -->
        <div v-if="monitorSelectedPort && serialDeviceState(monitorSelectedPort)?.status && !serialDeviceState(monitorSelectedPort)?.status?.role_tx" class="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-red-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
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
                {{ monitorHealthSummary }} · {{ monitorStatusMessage }}
              </p>
            </div>
            <div class="flex flex-wrap items-center justify-end gap-2">
              <button
                v-if="identifyAvailable"
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="[
                  'glass-input m-0 h-8 w-10 hover:bg-slate-700/70 flex items-center justify-center transition-all disabled:opacity-50 disabled:cursor-not-allowed',
                  { 'identify-led-active text-cyan-300': isIdentifying }
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
              <label class="flex items-center gap-2 text-xs text-slate-400">
                <input v-model="monitorAutoRefresh" type="checkbox" />
                Auto refresh
              </label>
              <button
                @click="toggleMonitorLoop"
                :disabled="!selectedPort"
                class="primary-btn m-0 h-8 px-3 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
              >
                {{ isMonitorLoopRunning ? 'Stop' : 'Monitor' }}
              </button>
            </div>
          </div>

          <div class="grid grid-cols-1 gap-2 md:grid-cols-[minmax(0,1.5fr)_12rem_auto_auto]">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">USB gateway</label>
              <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-9 flex-1 appearance-none disabled:opacity-60">
                <option value="" disabled>Select USB gateway</option>
                <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                  {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
                </option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Transport</label>
              <select v-model="monitorTransport" class="glass-input h-9 appearance-none">
                <option value="serial">Serial</option>
                <option value="mqtt">MQTT</option>
              </select>
            </div>
            <div class="flex flex-col justify-end">
              <button
                @click="openMonitorMqttSettings"
                class="glass-input m-0 h-9 px-3 hover:bg-slate-700/70 text-xs font-bold"
              >
                MQTT settings
              </button>
            </div>
            <div class="flex flex-col justify-end">
              <span :class="['inline-flex h-9 min-w-28 items-center justify-center rounded border px-2 text-[10px] font-bold whitespace-nowrap', monitorMqttConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
                MQTT {{ monitorMqttConnected ? 'configured' : 'not active' }}
              </span>
            </div>
          </div>
        </div>

        <div class="grid grid-cols-1 gap-3 xl:grid-cols-[minmax(0,1.35fr)_22rem] shrink-0">
          <div class="glass-card p-3 text-left">
            <div class="grid grid-cols-1 gap-3 text-xs md:grid-cols-2 xl:grid-cols-4">
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Gateway</div>
                <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ lrsDeviceName(monitorGatewayStatus?.chip_id) }}</div>
                <div class="mt-1 text-slate-400">{{ monitorGatewayStatus?.role || '-' }} · addr {{ monitorGatewayStatus?.local_address ?? '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Firmware</div>
                <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ displayFirmwareVersion(monitorGatewayStatus?.fw_version) }}</div>
                <div class="mt-1 text-slate-400">uptime {{ monitorGatewayStatus?.uptime_ms ? formatUptime(monitorGatewayStatus.uptime_ms) : '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Memory</div>
                <div class="mt-2 font-mono text-lg font-bold text-slate-100">{{ formatBytes(monitorGatewayStatus?.heap_free) }}</div>
                <div class="mt-1 text-slate-400">max {{ formatBytes(monitorGatewayStatus?.heap_max_block) }} · frag {{ monitorGatewayStatus?.heap_frag_pct ?? '-' }}%</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Fleet</div>
                <div class="mt-2 text-lg font-bold text-slate-100">{{ monitorFleetLiveCount }} live · {{ monitorFleetStaleCount }} stale</div>
                <div class="mt-1 text-slate-400">{{ monitorFleetOfflineCount }} offline · {{ monitorFleetRows.length }} total</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">WiFi</div>
                <div class="mt-2 text-lg font-bold text-slate-100">{{ monitorGatewayStatus?.wifi?.sta_connected ? 'Connected' : (monitorGatewayStatus?.wifi?.status || '-') }}</div>
                <div class="mt-1 font-mono text-slate-400">{{ monitorGatewayStatus?.wifi?.ip || '-' }} · {{ monitorGatewayStatus?.wifi?.rssi ?? '-' }} dBm</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">MQTT</div>
                <div class="mt-2 text-lg font-bold text-slate-100">{{ monitorGatewayStatus?.mqtt?.client_enabled ? 'Enabled' : '-' }}</div>
                <div class="mt-1 text-slate-400">{{ monitorGatewayStatus?.mqtt?.host || '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Link</div>
                <div class="mt-2 text-lg font-bold text-slate-100">{{ monitorGatewayStatus?.link_state || '-' }}</div>
                <div class="mt-1 text-slate-400">peer {{ monitorGatewayStatus?.peer_count ?? '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-3">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Temperature</div>
                <div class="mt-2 text-lg font-bold text-slate-100">{{ monitorGatewayStatus?.local_temp_valid ? `${monitorGatewayStatus.local_temp_c} °C` : '-' }}</div>
                <div class="mt-1 text-slate-400">local sensor</div>
              </div>
            </div>
          </div>

          <div class="glass-card flex flex-col items-center justify-center gap-3 p-5 text-center">
            <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Gateway Relay</div>
            <div :class="['flex h-36 w-36 items-center justify-center rounded-full border text-lg font-black tracking-widest transition-all', monitorRelayBadgeClass]">
              {{ monitorRelayLabel }}
            </div>
            <div class="grid w-full grid-cols-3 gap-2 text-xs">
              <div class="rounded border border-slate-800 bg-slate-950/25 p-2">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Cmd</div>
                <div class="mt-1 font-mono text-slate-200">{{ monitorGatewayStatus?.relay_state ?? '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-2">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">FB</div>
                <div class="mt-1 font-mono text-slate-200">{{ monitorGatewayStatus?.relay_feedback ?? '-' }}</div>
              </div>
              <div class="rounded border border-slate-800 bg-slate-950/25 p-2">
                <div class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Input</div>
                <div class="mt-1 font-mono text-slate-200">{{ monitorGatewayStatus?.input_state ?? '-' }}</div>
              </div>
            </div>
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-2 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between gap-3">
            <div>
              <h2 class="text-sm font-bold text-slate-300">Gateway Peer Cache</h2>
              <div class="mt-1 text-xs text-slate-500">Read-only serial view of the selected gateway's runtime state.</div>
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
                  <th class="px-2 py-1.5 text-left font-semibold">Heap</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Frag</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Poll</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="monitorFleetRows.length === 0">
                  <td colspan="15" class="px-3 py-8 text-center text-slate-600">Start Monitor to read the gateway peer cache.</td>
                </tr>
                <tr v-for="device in monitorFleetRows" :key="device.address" class="border-b border-slate-900/80 hover:bg-white/5 transition-colors">
                  <td class="px-2 py-1.5 font-mono text-slate-200">{{ device.address }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ lrsDeviceName(device.chip_id) }}</td>
                  <td class="px-2 py-1.5">
                    <span :class="['rounded border px-2 py-1 text-[10px] font-bold', monitorFreshnessClass(device)]">{{ monitorFreshnessLabel(device) }}</span>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ displayFirmwareVersion(device.fw_version) }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip || '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">ack {{ device.relay_state ?? '-' }} · fb {{ device.relay_feedback ?? '-' }}</td>
                  <td class="px-2 py-1.5 text-slate-300">{{ remoteInputLabel(device) }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ remoteTempLabel(device) }}</td>
                  <td class="px-2 py-1.5">
                    <div class="font-mono text-slate-300">{{ tankLabel(device) }}</div>
                    <div v-if="tankDetailLabel(device)" class="mt-0.5 font-mono text-[10px] text-slate-500">{{ tankDetailLabel(device) }}</div>
                  </td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.wifi_connected_known ? (device.wifi_connected ? 'Connected' : 'Offline') : 'Unknown' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">up {{ device.rssi ?? '-' }} / down {{ device.downlink_rssi_known ? device.downlink_rssi : '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ monitorHeapLabel(device) }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ monitorFragLabel(device) }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ monitorUptimeLabel(device) }}</td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.poll_pending ? 'Pending' : 'Idle' }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <div
        v-if="showMonitorMqttSettings"
        class="fixed inset-0 z-40 flex items-center justify-center bg-black/45 p-4"
        @click.self="closeMonitorMqttSettings"
      >
        <div class="glass-card w-full max-w-2xl overflow-hidden text-left">
          <div class="flex items-center justify-between border-b border-slate-800 bg-slate-900/50 px-3 py-2">
            <div>
              <h2 class="text-sm font-bold text-cyan-300">Monitor MQTT Settings</h2>
              <p class="mt-0.5 text-xs text-slate-500">Used when Monitor transport is set to MQTT.</p>
            </div>
            <button
              @click="closeMonitorMqttSettings"
              class="glass-input h-8 w-8 p-0 hover:bg-slate-700/70"
              title="Close MQTT settings"
              aria-label="Close MQTT settings"
            >
              ×
            </button>
          </div>

          <div class="grid grid-cols-[8rem_minmax(0,1fr)] gap-x-3 gap-y-2 p-3 text-xs">
            <label class="self-center text-right font-semibold text-slate-300">Broker host</label>
            <input v-model="monitorMqttDraftHost" class="glass-input h-9" placeholder="venus.local" />

            <label class="self-center text-right font-semibold text-slate-300">Broker port</label>
            <input v-model.number="monitorMqttDraftPort" class="glass-input h-9" type="number" min="1" max="65535" />

            <label class="self-center text-right font-semibold text-slate-300">Topic root</label>
            <input v-model="monitorMqttDraftTopicRoot" class="glass-input h-9" />

            <label class="self-center text-right font-semibold text-slate-300">MQTT user</label>
            <input v-model="monitorMqttDraftUser" class="glass-input h-9" />

            <label class="self-center text-right font-semibold text-slate-300">MQTT password</label>
            <div class="flex gap-2">
              <input v-model="monitorMqttDraftPassword" :type="showMonitorMqttPassword ? 'text' : 'password'" class="glass-input h-9 min-w-0 flex-1" />
              <button @click="showMonitorMqttPassword = !showMonitorMqttPassword" class="glass-input h-9 w-14 hover:bg-slate-700/70 text-xs font-bold">{{ showMonitorMqttPassword ? 'Hide' : 'Show' }}</button>
            </div>
          </div>

          <div class="flex items-center justify-between border-t border-slate-800 bg-slate-950/30 px-3 py-2">
            <span :class="['inline-flex h-8 items-center rounded border px-2 text-[10px] font-bold', monitorMqttConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
              MQTT {{ monitorMqttConnected ? 'configured' : 'not active' }}
            </span>
            <div class="flex gap-2">
              <button @click="closeMonitorMqttSettings" class="glass-input h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
              <button
                @click="toggleMonitorMqttConnection"
                :disabled="!monitorMqttDraftHost"
                class="primary-btn h-8 px-3 text-xs font-bold disabled:opacity-50"
              >
                {{ monitorMqttConnected ? 'Disconnect MQTT' : 'Save MQTT' }}
              </button>
            </div>
          </div>
        </div>
      </div>

      <div v-if="activeMode === 'network'" class="flex flex-col h-full overflow-hidden gap-3">
        <!-- Gateway Device Validation Warning Callout -->
        <div v-if="gatewaySelectedPort && serialDeviceState(gatewaySelectedPort)?.status && !serialDeviceState(gatewaySelectedPort)?.status?.role_tx" class="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-red-300 text-xs flex items-center gap-2.5 shrink-0 select-text">
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
                {{ fleetGatewayStatusLabel }} · Gateway + {{ loraInventory.length }} remote{{ loraInventory.length === 1 ? '' : 's' }} · {{ loraInventoryProgressLabel }}
              </p>
            </div>
            <div class="flex flex-wrap items-center justify-end gap-3">
              <span :class="['rounded border px-2 py-1 text-[10px] font-bold', firmwareServerInfo ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
                Firmware server {{ firmwareServerInfo ? 'on' : 'off' }}
              </span>
              <button
                @click="isLoraInventoryScanning ? cancelLoraInventoryScan() : startLoraInventoryScan()"
                :disabled="fleetScanDisabled"
                class="primary-btn m-0 h-10 px-4 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
              >
                {{ fleetForceScanLabel }}
              </button>
              <button
                @click="firmwareServerInfo ? stopFirmwareServer() : startFirmwareServer()"
                :disabled="isFirmwareServerStarting"
                class="glass-input m-0 h-10 px-4 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
              >
                {{ firmwareServerInfo ? 'Stop server' : (isFirmwareServerStarting ? 'Starting...' : 'Start server') }}
              </button>
            </div>
          </div>

          <div class="grid grid-cols-1 lg:grid-cols-4 gap-3">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">USB gateway</label>
              <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                <option value="" disabled>Select USB gateway</option>
                <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                  {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
                </option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Gateway admin password</label>
              <input v-model="pairAdminPassword" class="glass-input h-10 min-w-0 font-mono" :type="showPairAdminPassword ? 'text' : 'password'" autocomplete="current-password" />
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Region</label>
              <select v-model="region" class="glass-input h-10 appearance-none">
                <option v-for="r in ['ZA', 'EU', 'US']" :key="r" :value="r">{{ r }}</option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Firmware version</label>
              <div class="flex gap-2">
                <select v-model="selectedVersion" class="glass-input h-10 flex-1 appearance-none">
                  <option v-for="v in firmwareVersions" :key="v" :value="v">
                    {{ v === LOCAL_OPTION ? 'Choose a file' : v }}
                  </option>
                </select>
                <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input m-0 h-10 w-12 hover:bg-slate-700/70 flex items-center justify-center group/btn shrink-0">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-cyan-300 transition-colors', { 'animate-spin text-cyan-400': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="flex flex-wrap items-center justify-between gap-3 text-xs text-slate-400">
            <span>{{ networkStatusMessage }}</span>
            <span v-if="firmwareServerInfo" class="font-mono text-slate-500 truncate">{{ firmwareServerInfo.filename }} · {{ firmwareServerInfo.urls[0] }}</span>
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
          <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
            <div class="min-w-0">
              <div class="flex items-center gap-2">
                <h2 class="text-lg font-bold text-slate-300">Gateway</h2>
                <span :class="['rounded border px-2 py-1 text-[10px] font-bold', fleetGatewayBadgeClass]">
                  {{ fleetGatewayBadgeLabel }}
                </span>
              </div>
              <div class="mt-1 text-xs text-slate-500">{{ fleetGatewaySummary }}</div>
            </div>
            <div class="flex flex-wrap items-center gap-2">
              <button
                @click="loadNetworkGateway"
                :disabled="isNetworkGatewayLoading || !selectedPort"
                class="glass-input m-0 h-9 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60"
              >
                {{ isNetworkGatewayLoading ? 'Loading...' : 'Load gateway' }}
              </button>
              <button
                @click="triggerIdentify"
                :disabled="identifyDisabled"
                :class="['glass-input m-0 h-9 w-11 hover:bg-slate-700/70 flex items-center justify-center disabled:opacity-50', { 'identify-led-active': isIdentifying }]"
                title="Identify selected USB gateway"
              >
                <svg xmlns="http://www.w3.org/2000/svg" class="w-6 h-6 identify-led-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18h6"></path><path d="M10 22h4"></path><path d="M8.5 14.5a6 6 0 1 1 7 0c-.8.7-1.5 1.6-1.5 2.5h-4c0-.9-.7-1.8-1.5-2.5Z"></path><path d="M12 2v2"></path><path d="m4.9 4.9 1.4 1.4"></path><path d="M2 12h2"></path><path d="m19.1 4.9-1.4 1.4"></path><path d="M20 12h2"></path></svg>
              </button>
              <button
                v-if="isGatewayUpgradeAvailable || isFlashing"
                @click="flashFleetGateway"
                :disabled="fleetGatewayFlashDisabled"
                :title="fleetGatewayFlashUnavailableReason()"
                class="primary-btn m-0 h-9 px-4 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
              >
                {{ isFlashing ? 'Flashing...' : 'Upgrade gateway' }}
              </button>
            </div>
          </div>
          <div class="grid grid-cols-2 md:grid-cols-4 xl:grid-cols-7 gap-2 text-xs">
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Port</div>
              <div class="mt-1 truncate font-mono text-slate-300">{{ selectedPort || '-' }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Name</div>
              <div class="mt-1 truncate font-mono text-slate-300">{{ lrsDeviceName(fleetGatewayStatus?.chip_id || fleetGatewayIdentity?.chip_id) }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Firmware</div>
              <div class="mt-1 truncate font-mono text-slate-300">{{ displayFirmwareVersion(fleetGatewayStatus?.fw_version) }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Role</div>
              <div class="mt-1 truncate text-slate-300">{{ fleetGatewayStatus?.role || '-' }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Address</div>
              <div class="mt-1 truncate font-mono text-slate-300">{{ fleetGatewayStatus ? `${fleetGatewayStatus.local_address}->${fleetGatewayStatus.remote_address}` : fleetGatewayIdentity ? `${fleetGatewayIdentity.local_addr}->${fleetGatewayIdentity.remote_addr}` : '-' }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">WiFi</div>
              <div class="mt-1 truncate text-slate-300">{{ fleetGatewayStatus?.wifi?.sta_connected ? (fleetGatewayStatus.wifi.ip || 'connected') : (fleetGatewayStatus?.wifi?.status || '-') }}</div>
            </div>
            <div class="rounded border border-slate-800 bg-slate-950/30 p-2">
              <div class="text-[10px] uppercase tracking-wide text-slate-600">Uptime</div>
              <div class="mt-1 truncate font-mono text-slate-300">{{ fleetGatewayStatus?.uptime_ms ? formatUptime(fleetGatewayStatus.uptime_ms) : '-' }}</div>
            </div>
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-3 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between gap-3">
            <div>
              <h2 class="text-lg font-bold text-slate-300">Remotes</h2>
              <div class="mt-1 text-xs text-slate-500">Gateway-owned peer cache; Force Scan asks the gateway to refresh LoRa state.</div>
            </div>
            <div class="flex items-center gap-3">
              <div class="text-xs text-slate-500">{{ loraInventory.length }} remote{{ loraInventory.length === 1 ? '' : 's' }} cached · {{ selectedLoraInventoryCount }} selected</div>
            </div>
          </div>
          <div class="min-h-0 flex-1 overflow-auto custom-scrollbar rounded-md border border-slate-800">
            <table class="w-full min-w-[1120px] border-collapse text-xs">
              <thead class="sticky top-0 bg-slate-950/95 text-slate-500">
                <tr class="border-b border-slate-800">
                  <th class="w-10 px-2 py-1.5 text-left"></th>
                  <th class="px-2 py-1.5 text-left font-semibold">Addr</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Device</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Role</th>
                  <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Power Save</th>
                  <th v-if="hasAnyRemoteIp" class="px-2 py-1.5 text-left font-semibold">IP</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Sensors</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
                  <th class="px-2 py-1.5 text-left font-semibold">RSSI</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Age</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="loraInventory.length === 0">
                  <td :colspan="hasAnyRemoteIp ? 13 : 12" class="px-3 py-8 text-center text-slate-600">Select a USB gateway to read its peer cache, or Force Scan to probe remotes.</td>
                </tr>
                <tr
                  v-for="device in loraInventory"
                  :key="device.address"
                  :class="['border-b border-slate-900/80 hover:bg-white/5 transition-colors', fleetRowClass(device)]"
                >
                  <td class="px-2 py-1.5"><input v-model="device.selected" type="checkbox" /></td>
                  <td class="px-2 py-1.5 font-mono">
                    <span :class="['inline-flex min-w-8 items-center justify-center rounded border px-2 py-1 text-[10px] font-bold', fleetFreshnessClass(device)]">
                      {{ device.address }}
                    </span>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.chip_id ? lrsDeviceName(device.chip_id) : '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.fw_version ? displayFirmwareVersion(device.fw_version) : '-' }}</td>
                  <td class="px-2 py-1.5 text-slate-300">{{ device.role || '-' }} / {{ device.mode || '-' }}</td>
                  <td class="px-2 py-1.5">
                    <span 
                      v-if="device.wifi_pending_offline"
                      class="rounded border px-2 py-1 text-[10px] font-bold border-orange-500/30 bg-orange-500/10 text-orange-300 animate-pulse"
                      title="PowerSave is active. Waiting for WiFi connection to drop."
                    >
                      ...
                    </span>
                    <span v-else :class="['rounded border px-2 py-1 text-[10px] font-bold', device.wifi_connected_known ? (device.wifi_connected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-600 bg-slate-800/50 text-slate-400') : 'border-slate-800 bg-slate-900/50 text-slate-500']">
                      {{ device.wifi_connected_known ? (device.wifi_connected ? 'OK' : 'Offline') : '-' }}
                    </span>
                  </td>
                  <td class="px-2 py-1.5">
                    <span 
                      v-if="device.pending_power_save_listen_only !== undefined"
                      class="rounded border px-2 py-1 text-[10px] font-bold border-orange-500/30 bg-orange-500/10 text-orange-300 animate-pulse"
                      title="Command transmitted. Waiting for remote node to check in over LoRa to confirm."
                    >
                      Pending...
                    </span>
                    <template v-else-if="device.power_save_listen_only">
                      <span 
                        class="rounded border px-2 py-1 text-[10px] font-bold border-cyan-500/30 bg-cyan-500/10 text-cyan-300"
                        title="Power save active: node is running in LoRa-only low-power mode"
                      >
                        PowerSave
                      </span>
                    </template>
                    <span v-else class="text-slate-500">-</span>
                  </td>
                  <td v-if="hasAnyRemoteIp" class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip || '-' }}</td>
                  <td class="px-2 py-1.5">
                    <div class="text-slate-300 flex items-center gap-1 flex-wrap">
                      <template v-if="device.age_ms !== undefined && device.age_ms !== null">
                        <span>in <span :class="remoteInputLabel(device) === 'Closed' ? 'text-emerald-400 font-semibold' : remoteInputLabel(device) === 'Open' ? 'text-orange-400 font-semibold' : 'text-slate-400'">{{ remoteInputLabel(device) }}</span></span>
                        <span v-if="remoteTempLabel(device) !== '-'" class="text-slate-500">·</span>
                        <span v-if="remoteTempLabel(device) !== '-'">{{ remoteTempLabel(device) }}</span>
                        <span v-if="tankLabel(device) !== '-'" class="text-slate-500">·</span>
                        <span v-if="tankLabel(device) !== '-'">tank {{ tankLabel(device) }}</span>
                      </template>
                      <template v-else>-</template>
                    </div>
                    <div v-if="tankDetailLabel(device)" class="mt-1 font-mono text-[10px] text-slate-500">{{ tankDetailLabel(device) }}</div>
                  </td>
                  <td class="px-2 py-1.5 font-mono">
                    <div class="text-slate-300">{{ device.uptime_ms ? formatUptime(device.uptime_ms) : '-' }}</div>
                    <div v-if="fleetRowStatusLabel(device)" :class="['mt-1 text-[10px] font-bold inline-flex items-center gap-1 cursor-pointer select-none', (device.row_state === 'unexpected_reboot' || device.row_state === 'ota_failed') ? 'text-rose-300' : device.row_state === 'ota_updated' ? 'text-emerald-300' : device.row_state === 'ota_rebooted' ? 'text-orange-400' : 'text-sky-300']" :title="device.row_state === 'unexpected_reboot' ? 'Spontaneous restart detected: Device uptime rolled back (rebooted) without a requested OTA command. Typically caused by power cycles, brownouts, or watchdog resets.' : device.row_state === 'ota_failed' ? 'Download failed: The remote device failed to download the firmware binary from the server.' : device.row_state === 'ota_rebooted' ? 'Normal post-upgrade restart: Device rebooted successfully to boot into the newly written firmware version.' : device.row_state === 'ota_no_reboot' ? 'Upgrade timeout: The firmware binary was served, but the remote did not reboot to apply it within the expected window.' : undefined">
                      {{ fleetRowStatusLabel(device) }}
                      <span v-if="device.row_state === 'unexpected_reboot' || device.row_state === 'ota_failed' || device.row_state === 'ota_rebooted' || device.row_state === 'ota_no_reboot'" class="opacity-60 text-[9px]">ⓘ</span>
                    </div>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.rssi ?? '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.age_ms != null ? `${Math.round(device.age_ms / 1000)}s` : '-' }}</td>
                  <td class="px-2 py-1.5 overflow-visible">
                    <div class="relative inline-block text-left">
                      <button
                        @click.stop="toggleFleetDropdown(device.address)"
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
                          @click="flashLoraRemote(device); activeDropdownAddress = null"
                          :disabled="isFirmwareServerStarting || ['ota_queued', 'ota_downloading', 'ota_apply_wait', 'ota_retrying'].includes(device.row_state || '') || !fleetFlashAvailable(device)"
                          class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 disabled:opacity-40 transition-colors flex items-center gap-2 select-none"
                          :title="fleetFlashUnavailableReason(device)"
                        >
                          ⚡ Flash
                        </button>
                        <button
                          @click="startFleetUdpLogs(device); activeDropdownAddress = null"
                          :disabled="remoteUdpBusyAddress !== null || !fleetLogsAvailable(device)"
                          class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 disabled:opacity-40 transition-colors flex items-center gap-2 select-none"
                        >
                          📋 Logs
                        </button>
                        <button
                          @click="openSettingsModal(device)"
                          class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none"
                        >
                          🛠️ Commands
                        </button>
                        <button
                          @click="executeRemoteReboot(device)"
                          class="w-full text-left px-3 py-1.5 hover:bg-white/5 text-[11px] font-bold text-slate-300 transition-colors flex items-center gap-2 select-none"
                        >
                          🔄 Reboot
                        </button>
                        <button
                          @click="executeForgetRemote(device); activeDropdownAddress = null"
                          class="w-full text-left px-3 py-1.5 hover:bg-rose-500/20 hover:text-rose-200 text-[11px] font-bold text-rose-300/80 transition-colors flex items-center gap-2 select-none"
                        >
                          🗑️ Forget Device
                        </button>
                        <div class="h-[1px] bg-slate-800/80 my-1"></div>
                        <button
                          @click="openFactoryResetModal(device)"
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
            v-if="isNetworkUdpMonitoring"
            :class="networkUdpLogsExpanded ? 'fixed inset-4 z-40 flex flex-col rounded-md border border-slate-700 bg-slate-950 p-4 shadow-2xl' : 'shrink-0 rounded-md border border-slate-800 bg-slate-950/40 p-3'"
          >
            <div class="mb-2 flex items-center justify-between gap-3">
              <div class="min-w-0">
                <div class="truncate text-xs font-bold text-slate-300">UDP logs · {{ networkUdpTarget || 'Fleet' }}</div>
                <div class="mt-0.5 text-[10px] text-slate-600">{{ filteredNetworkLogs.length }} lines · following latest</div>
              </div>
              <div class="flex shrink-0 items-center gap-2">
                <button @click="copyNetworkUdpLog" :disabled="filteredNetworkLogs.length === 0" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-50">Copy</button>
                <button @click="networkUdpLogsExpanded = !networkUdpLogsExpanded; nextTick(() => scrollNetworkUdpToBottom())" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">
                  {{ networkUdpLogsExpanded ? 'Collapse' : 'Full screen' }}
                </button>
                <button @click="stopNetworkUdpMonitor" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">Stop logs</button>
              </div>
            </div>
            <div ref="networkUdpLogContainer" :class="['overflow-auto custom-scrollbar font-mono text-[10px] leading-tight text-slate-400', networkUdpLogsExpanded ? 'min-h-0 flex-1 rounded border border-slate-800 bg-slate-950/60 p-2' : 'max-h-44']">
              <div v-for="(log, i) in filteredNetworkLogs.slice(-200)" :key="i">{{ log }}</div>
              <div v-if="filteredNetworkLogs.length === 0" class="text-slate-600">Waiting for UDP log lines...</div>
            </div>
          </div>
        </div>

      </div>
    </div>

    <!-- Operator confirmation -->
    <Transition name="toast">
      <div v-if="confirmDialog" class="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 px-4">
        <div class="w-full max-w-md rounded-lg border border-slate-700 bg-slate-900 p-4 shadow-2xl">
          <p class="whitespace-pre-line text-sm leading-6 text-slate-200">{{ confirmDialog.message }}</p>
          <div class="mt-4 flex justify-end gap-2">
            <button
              @click="resolveConfirmDialog(false)"
              class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold"
            >
              {{ confirmDialog.cancelText }}
            </button>
            <button
              @click="resolveConfirmDialog(true)"
              :class="['m-0 h-9 rounded-md border px-4 text-xs font-bold transition-colors', confirmDialog.danger ? 'border-rose-500/40 bg-rose-500/20 text-rose-100 hover:bg-rose-500/30' : 'primary-btn']"
            >
              {{ confirmDialog.confirmText }}
            </button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- Toast Notification -->
    <Transition name="toast">
      <div v-if="showToast" class="fixed bottom-4 left-1/2 -translate-x-1/2 z-50 glass-card px-3 py-2 border border-cyan-500/50 text-xs font-medium text-slate-200 flex items-center gap-3">
        <span class="w-2 h-2 rounded-full bg-cyan-500"></span>
        {{ toastMessage }}
      </div>
    </Transition>

    <!-- Targeted Factory Reset Dialog -->
    <Transition name="toast">
      <div v-if="factoryResetTargetModal" class="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 px-4">
        <div class="w-full max-w-md rounded-lg border border-slate-700 bg-slate-900 p-5 shadow-2xl flex flex-col gap-4">
          <div>
            <h3 class="text-base font-bold text-rose-400">⚠️ Factory Reset Remote Node {{ factoryResetTargetModal.device.address }}</h3>
            <p class="mt-1 text-xs text-slate-500">Decommissions the receiver node over LoRa, formatting its state and triggering a reboot.</p>
          </div>
          
          <div class="rounded border border-amber-500/20 bg-amber-500/5 p-3 text-xs text-amber-200 leading-relaxed">
            💡 Select which parts of the remote configuration to preserve during reset. Clearing both options returns the node to uncommissioned factory-default state.
          </div>

          <div class="flex flex-col gap-3 py-1">
            <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-2.5 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
              <input v-model="factoryResetTargetModal.keep_shared_fleet_key" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-rose-500 focus:ring-0 focus:ring-offset-0" />
              <div>
                <div class="font-semibold text-slate-200">Keep Fleet Key</div>
                <div class="text-[10px] text-slate-500 mt-0.5">Preserves pairing encryption keys to stay in this gateway's secure fleet.</div>
              </div>
            </label>
            <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-2.5 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
              <input v-model="factoryResetTargetModal.keep_wifi_credentials" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-rose-500 focus:ring-0 focus:ring-offset-0" />
              <div>
                <div class="font-semibold text-slate-200">Keep WiFi Credentials</div>
                <div class="text-[10px] text-slate-500 mt-0.5">Preserves local WiFi SSID and password settings.</div>
              </div>
            </label>
          </div>
          
          <div class="mt-2 flex justify-end gap-2">
            <button
              @click="factoryResetTargetModal = null"
              class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold"
            >
              Cancel
            </button>
            <button
              @click="executeRemoteFactoryReset(factoryResetTargetModal.device, factoryResetTargetModal.keep_shared_fleet_key, factoryResetTargetModal.keep_wifi_credentials)"
              class="m-0 h-9 rounded-md border border-rose-500/40 bg-rose-500/20 text-rose-100 hover:bg-rose-500/30 px-4 text-xs font-bold transition-colors"
            >
              Factory Reset Node
            </button>
          </div>
        </div>
      </div>
    </Transition>

    <!-- Device Settings Modal -->
    <Transition name="toast">
      <div v-if="settingsDeviceModal" class="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 px-4">
        <div class="w-full max-w-lg rounded-lg border border-slate-700 bg-slate-900 p-5 shadow-2xl flex flex-col gap-4">
          <!-- Header with device info -->
          <div>
            <h3 class="text-base font-bold text-slate-200">🛠️ Command Console — Node {{ settingsDeviceModal.device.address }}</h3>
            <p class="mt-1 text-xs text-slate-500">
              <span v-if="settingsDeviceModal.device.chip_id">{{ settingsDeviceModal.device.chip_id }}</span>
              <span v-if="settingsDeviceModal.device.fw_version"> · v{{ settingsDeviceModal.device.fw_version }}</span>
              <span v-if="settingsDeviceModal.device.role"> · {{ settingsDeviceModal.device.role }}</span>
            </p>
          </div>

          <!-- Tab bar -->
          <div class="flex gap-0 border-b border-slate-800">
            <button
              v-for="tab in ([{key:'sensors',label:'🛠️ Sensors'},{key:'power',label:'⚡ Power'},{key:'wifi',label:'📶 WiFi'},{key:'security',label:'🔑 Security'}] as const)"
              :key="tab.key"
              @click="settingsDeviceModal.activeTab = tab.key"
              class="px-4 py-2 text-[11px] font-bold transition-colors"
              :class="settingsDeviceModal.activeTab === tab.key
                ? 'text-cyan-300 border-b-2 border-cyan-400 -mb-px'
                : 'text-slate-500 hover:text-slate-300'"
            >
              {{ tab.label }}
            </button>
          </div>

          <!-- Sensors tab content -->
          <div v-if="settingsDeviceModal.activeTab === 'sensors'" class="flex flex-col gap-3">
            <p class="text-xs text-slate-500">Enable or disable hardware sensors over LoRa. Changes persist to remote device flash memory.</p>
            <div class="flex flex-col gap-3 py-1">
              <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-3 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
                <input v-model="settingsDeviceModal.sensor_temp_enabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
                <div>
                  <div class="font-semibold text-slate-200">DS18B20 Temperature Sensor</div>
                  <div class="text-[10px] text-slate-500 mt-0.5">Enables digital temperature probes on the device.</div>
                </div>
              </label>
              <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-3 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
                <input v-model="settingsDeviceModal.sensor_tank_enabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
                <div>
                  <div class="font-semibold text-slate-200">4-20mA Pressure Tank Level Sensor</div>
                  <div class="text-[10px] text-slate-500 mt-0.5">Enables analog pressure sensor mappings for tank level tracking.</div>
                </div>
              </label>
            </div>
            <div class="mt-2 flex justify-end gap-2">
              <button @click="settingsDeviceModal = null" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
              <button
                @click="executeRemoteSensors(settingsDeviceModal.device, settingsDeviceModal.sensor_temp_enabled, settingsDeviceModal.sensor_tank_enabled, settingsDeviceModal.power_save_listen_only, true)"
                class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold transition-colors"
              >
                Apply Sensor Configuration
              </button>
            </div>
          </div>

          <!-- Power tab content (Stateless Commands Console) -->
          <div v-if="settingsDeviceModal.activeTab === 'power'" class="flex flex-col gap-3">
            <p class="text-xs text-slate-500">PowerSave turns off WiFi, Serial Admin, OTA, MQTT, UDP logging, LEDs, and background services. LoRa command handling remains active so the node can be returned to Full Power remotely.</p>
            
            <div class="flex flex-col gap-3 py-1">
              <div class="flex flex-col md:flex-row md:items-center justify-between gap-3 border border-slate-800 bg-slate-950/20 rounded p-4">
                <div class="flex-1">
                  <div class="font-semibold text-slate-200 text-xs flex items-center gap-1.5">
                    <span :class="['w-2 h-2 rounded-full', settingsDeviceModal.power_save_listen_only ? 'bg-cyan-400 animate-pulse' : 'bg-emerald-500']"></span>
                    {{ settingsDeviceModal.power_save_listen_only ? 'Power Save Mode Enabled' : 'Full Power Mode Active' }}
                  </div>
                  <div class="text-[10px] text-slate-400 mt-2 select-text leading-relaxed">
                    <template v-if="settingsDeviceModal.power_save_listen_only">
                      The node is configured for deep power saving. Local Wi-Fi, Serial Admin, and background services are completely shut down to preserve battery life. LoRa receiver remains active.
                    </template>
                    <template v-else>
                      Keeps the remote node continuously awake. WiFi, Serial Admin, OTA, and active sensor polling remain fully operational at all times.
                    </template>
                  </div>
                </div>
                <button
                  v-if="settingsDeviceModal.power_save_listen_only"
                  @click="executeRemoteSensors(settingsDeviceModal.device, settingsDeviceModal.sensor_temp_enabled, settingsDeviceModal.sensor_tank_enabled, false, true)"
                  class="m-0 h-9 self-center rounded-md border border-emerald-500/40 bg-emerald-500/10 text-emerald-300 hover:bg-emerald-500/25 px-4 text-xs font-bold transition-colors whitespace-nowrap"
                >
                  Disable Power Save
                </button>
                <button
                  v-else
                  @click="executeRemoteSensors(settingsDeviceModal.device, settingsDeviceModal.sensor_temp_enabled, settingsDeviceModal.sensor_tank_enabled, true, true)"
                  class="m-0 h-9 self-center rounded-md border border-cyan-500/40 bg-cyan-500/10 text-cyan-300 hover:bg-cyan-500/25 px-4 text-xs font-bold transition-colors whitespace-nowrap"
                >
                  Enable Power Save
                </button>
              </div>
            </div>

            <div class="mt-2 flex justify-end">
              <button @click="settingsDeviceModal = null" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Close Console</button>
            </div>
          </div>

          <!-- WiFi tab content -->
          <div v-if="settingsDeviceModal.activeTab === 'wifi'" class="flex flex-col gap-3">
            <p class="text-xs text-slate-500">Securely transmit targeted WiFi credentials over LoRa.</p>
            <div class="flex flex-col gap-1">
              <label class="text-[11px] font-semibold text-slate-400">SSID</label>
              <input v-model="settingsDeviceModal.wifi_ssid" type="text" class="glass-input h-9 px-3 text-xs w-full" placeholder="WiFi Network Name" />
            </div>
            <div class="flex flex-col gap-1">
              <label class="text-[11px] font-semibold text-slate-400">STA Password</label>
              <input v-model="settingsDeviceModal.wifi_password" type="password" class="glass-input h-9 px-3 text-xs w-full" placeholder="Leave blank to clear credentials" />
            </div>
            <div class="mt-2 flex justify-end gap-2">
              <button @click="settingsDeviceModal = null" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
              <button
                @click="executeRemoteWifi(settingsDeviceModal.device, settingsDeviceModal.wifi_ssid, settingsDeviceModal.wifi_password)"
                class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold transition-colors"
              >
                Send Credentials
              </button>
            </div>
          </div>

          <!-- Security tab content -->
          <div v-if="settingsDeviceModal.activeTab === 'security'" class="flex flex-col gap-3">
            <p class="text-xs text-slate-500">Update the node's shared fleet passphrase over LoRa.</p>
            <div class="rounded-lg border border-rose-500/30 bg-rose-500/10 p-3 text-xs text-rose-300 flex flex-col gap-1.5 select-text leading-relaxed">
              <div class="flex items-center gap-1.5 font-bold">
                <span class="text-sm">⚠️</span> CRITICAL OPERATIONAL WARNING
              </div>
              <div>
                Changing the remote's Fleet Key will make it <span class="font-bold text-rose-200">immediately unreachable</span> by this Gateway once the remote reboots.
                You must update this Gateway's Fleet Key to match, or the remote node will be permanently orphaned until manually retrieved!
              </div>
            </div>
            <div class="flex flex-col gap-3.5 py-1">
              <label class="flex items-center gap-3 text-xs text-slate-300 cursor-pointer select-none">
                <input v-model="settingsDeviceModal.fleet_key_confirmed" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-600 focus:ring-0 focus:ring-offset-0" />
                <span class="font-semibold text-slate-200">I understand that the node will become unreachable until Gateway keys are matched.</span>
              </label>
              <div class="flex flex-col gap-1.5 text-xs">
                <label class="font-semibold text-slate-300">New Fleet Passphrase</label>
                <div class="flex gap-2">
                  <input
                    v-model="settingsDeviceModal.fleet_key"
                    :type="settingsDeviceModal.show_fleet_key ? 'text' : 'password'"
                    class="glass-input h-9 flex-1"
                    placeholder="Minimum 8 characters"
                    :disabled="!settingsDeviceModal.fleet_key_confirmed"
                  />
                  <button
                    @click="settingsDeviceModal.show_fleet_key = !settingsDeviceModal.show_fleet_key"
                    class="glass-input h-9 px-3 hover:bg-slate-700/70"
                    type="button"
                    :disabled="!settingsDeviceModal.fleet_key_confirmed"
                  >
                    {{ settingsDeviceModal.show_fleet_key ? 'Hide' : 'Show' }}
                  </button>
                </div>
              </div>
            </div>
            <div class="mt-2 flex justify-end gap-2">
              <button @click="settingsDeviceModal = null" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
              <button
                @click="executeRemoteFleetKeyChange(settingsDeviceModal.device, settingsDeviceModal.fleet_key)"
                :disabled="!settingsDeviceModal.fleet_key_confirmed || !settingsDeviceModal.fleet_key || settingsDeviceModal.fleet_key.length < 8"
                class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
              >
                Update Key over LoRa
              </button>
            </div>
          </div>
        </div>
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
  transition: opacity 0.15s ease, transform 0.15s ease;
}
.toast-enter-from, .toast-leave-to {
  opacity: 0;
  transform: translate(-50%, 20px);
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
