<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import { openUrl } from '@tauri-apps/plugin-opener';

type ActiveMode = 'pair' | 'serial' | 'network' | 'monitor';

const activeMode = defineModel<ActiveMode>('activeMode', { default: 'pair' });

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

interface LoraInventoryDevice {
  address: number;
  chip_id?: string;
  fw_version?: string;
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
  maintenance_debug_known?: boolean;
  heap_free?: number;
  heap_max_block?: number;
  heap_frag_pct?: number;
  relay_state?: number;
  relay_feedback?: number;
  input_state?: number;
  input_feedback?: number;
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
  row_state?: 'ota_pending' | 'ota_rebooted' | 'ota_updated' | 'ota_no_reboot' | 'unexpected_reboot';
  row_state_until_ms?: number;
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
}

interface SerialAdminConfig {
  mode: string;
  role_tx: boolean;
  local_address: number;
  remote_address: number;
  wifi_sta_ssid: string;
  wifi_sta_password: string;
  wifi_admin_enabled: boolean;
  mqtt_client_enabled: boolean;
  mqtt_control_enabled: boolean;
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  mqtt_topic_root: string;
  sensor_temp_enabled: boolean;
  sensor_temp_pin: number;
  sensor_temp_interval_s: number;
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
}

type RegionCode = 'ZA' | 'EU' | 'US';

const READABLE_KEY_CONSONANTS = 'bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS = 'aeiou';
const NETWORK_FIRMWARE_PATH = '/firmware.bin';
const LRS_REMOTE_SCAN_CAP = 12;

const ports = ref<SerialPort[]>([]);
const flashSelectedPort = ref('');
const provisionSelectedPort = ref('');
const fleetSelectedPort = ref('');
const monitorSelectedPort = ref('');
const selectedPort = computed<string>({
  get() {
    if (activeMode.value === 'pair') return provisionSelectedPort.value;
    if (activeMode.value === 'network') return fleetSelectedPort.value;
    if (activeMode.value === 'monitor') return monitorSelectedPort.value;
    return flashSelectedPort.value;
  },
  set(port) {
    if (activeMode.value === 'pair') {
      provisionSelectedPort.value = port;
    } else if (activeMode.value === 'network') {
      fleetSelectedPort.value = port;
    } else if (activeMode.value === 'monitor') {
      monitorSelectedPort.value = port;
    } else {
      flashSelectedPort.value = port;
    }
  }
});
const firmwareVersions = ref<string[]>(['__local_browse__']);
const selectedVersion = ref('');
const selectedLocalPath = ref('');
const region = ref<RegionCode>('ZA');
const isFlashing = ref(false);
const isMonitoring = ref(false);
const isNetworkUdpMonitoring = ref(false);
const isFirmwareServerStarting = ref(false);
const remoteOtaBusyAddress = ref<number | null>(null);
const remoteUdpBusyAddress = ref<number | null>(null);
const firmwareServerInfo = ref<FirmwareServerInfo | null>(null);
const serialLogs = ref<string[]>([]);
const networkLogs = ref<string[]>([]);
const pairLogs = ref<string[]>([]);
const serialUptimeMs = ref<number | null>(null);
const networkUptimeMs = ref<number | null>(null);
const serialDevicesByPort = ref<Record<string, SerialDeviceState>>({});
const isLoadingInfo = ref(false);
const isRefreshingPorts = ref(false);
const isFetchingFirmware = ref(false);
const showToast = ref(false);
const toastMessage = ref('');
const logContainer = ref<HTMLElement | null>(null);
const deviceInfoReadSeq = ref(0);
const monitorAfterFlash = ref(true);
const eraseBeforeFlash = ref(false);
const lastPortSnapshot = ref<string[]>([]);
const portSeenSequence = ref<Record<string, number>>({});
const portSeenCounter = ref(0);
const stickLogToBottom = ref(true);
const activeMonitorPort = ref('');
const activeMonitorSsid = ref('');
const networkStatusMessage = ref('Ready to scan the fleet.');
const monitorStatusMessage = ref('Select a USB gateway and refresh monitor data.');
const monitorTransport = ref<'serial' | 'mqtt'>('serial');
const monitorMqttHost = ref('');
const monitorMqttPort = ref(1883);
const monitorMqttUser = ref('');
const monitorMqttPassword = ref('');
const monitorMqttTopicRoot = ref('lora');
const monitorMqttConnected = ref(false);
const showMonitorMqttPassword = ref(false);
const monitorFleetRows = ref<LoraInventoryDevice[]>([]);
const isMonitorRefreshing = ref(false);
const monitorPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const monitorAutoRefresh = ref(false);
const networkUdpTarget = ref('');
const loraInventory = ref<LoraInventoryDevice[]>([]);
const loraInventoryScan = ref<LoraInventoryStatus['scan'] | null>(null);
const isLoraInventoryScanning = ref(false);
const isNetworkGatewayLoading = ref(false);
const networkInventoryPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const fleetOtaFollowupTimers = ref<Record<number, ReturnType<typeof window.setTimeout>>>({});
const fleetRowHistory = ref<Record<number, { uptimeMs?: number; fwVersion?: string; otaExpectedUntilMs?: number; rowState?: LoraInventoryDevice['row_state']; rowStateUntilMs?: number }>>({});
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
const serialFactoryKeepFleet = ref(true);
const serialFactoryKeepWifi = ref(true);

const LOCAL_OPTION = '__local_browse__';
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
const REGION_CONFIDENT_MIN_SCORE = 5;
const REGION_CONFIDENT_MIN_GAP = 2;

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
    gatewayWifiReadyIp: ''
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
    const state = serialDeviceState();
    if (state) state.deviceInfo = info;
  }
});
const pairAdminPassword = computed<string>({
  get: () => activeSerialDevice.value?.adminPassword || activeSerialDevice.value?.deviceInfo?.password?.trim() || '',
  set: (password) => {
    const state = serialDeviceState();
    if (state) state.adminPassword = password;
  }
});
const wifiNetworks = computed<WifiNetwork[]>({
  get: () => activeSerialDevice.value?.wifiNetworks || [],
  set: (networks) => {
    const state = serialDeviceState();
    if (state) state.wifiNetworks = networks;
  }
});
const pairWifiSsid = computed<string>({
  get: () => activeSerialDevice.value?.wifiSsid || '',
  set: (ssid) => {
    const state = serialDeviceState();
    if (state) state.wifiSsid = ssid;
  }
});
const serialAdminStatus = computed<SerialAdminStatus | null>({
  get: () => activeSerialDevice.value?.status || null,
  set: (status) => {
    const state = serialDeviceState();
    if (state) state.status = status;
  }
});
const serialAdminConfig = computed<SerialAdminConfig | null>({
  get: () => activeSerialDevice.value?.config || null,
  set: (config) => {
    const state = serialDeviceState();
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
const loraInventoryProgressLabel = computed(() => {
  const scan = loraInventoryScan.value;
  if (!scan) return 'Idle';
  if (!scan.active) return `Complete, ${scan.sent || 0} probes sent`;
  const next = scan.next_address || scan.start_address || 1;
  return `Scanning ${next}-${scan.end_address || LRS_REMOTE_SCAN_CAP}, ${scan.sent || 0} probes sent`;
});
const gatewayReady = computed(() =>
  !!selectedPort.value &&
  hasActiveDeviceInfo.value &&
  !!activeSerialDevice.value?.adminSupported &&
  !!pairPassword()
);
const pairPrimaryDisabled = computed(() => isPairBusy.value || !selectedPort.value);
const pairControlsDisabled = computed(() => isPairBusy.value || isGatewayLoading.value || !gatewayReady.value);
const identifyAvailable = computed(() => hasActiveDeviceInfo.value && !!activeSerialDevice.value?.adminSupported);
const isSelectedPortMonitoring = computed(() => isMonitoring.value && !!selectedPort.value && activeMonitorPort.value === selectedPort.value);
const identifyDisabled = computed(() => !selectedPort.value || !identifyAvailable.value || isFlashing.value || isLoadingInfo.value || isGatewayLoading.value || isPairBusy.value || isSelectedPortMonitoring.value);
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
const serialAdminPassword = computed(() => deviceInfo.value?.password?.trim() || '');
const serialAdminBusy = computed(() => isSerialAdminLoading.value || isSerialAdminSaving.value || isSerialSystemAction.value);
const serialAdminDisabled = computed(() => !selectedPort.value || !hasActiveDeviceInfo.value || isFlashing.value || isSelectedPortMonitoring.value || isLoadingInfo.value || serialAdminBusy.value);
const serialStatusSummary = computed(() => {
  const st = serialAdminStatus.value;
  if (!st) return 'Load local status to inspect firmware health.';
  if (!st.commissioned || st.fleet_passphrase_default) {
    return `Factory default · awaiting commissioning · addr ${st.local_address}->${st.remote_address} · heap ${formatBytes(st.heap_free)} free`;
  }
  const wifi = st.wifi?.sta_connected ? `WiFi ${st.wifi.ip || 'connected'}` : `WiFi ${st.wifi?.status || 'offline'}`;
  return `${st.role || 'unknown'} ${st.local_address}->${st.remote_address} · ${wifi} · heap ${formatBytes(st.heap_free)} free`;
});
const serialAdminIsFactoryDefault = computed(() => {
  const st = serialAdminStatus.value;
  return !!st && (!st.commissioned || !!st.fleet_passphrase_default);
});
const fleetGatewayStatusLabel = computed(() => {
  if (serialAdminIsFactoryDefault.value) return 'gateway factory default';
  if (gatewayReady.value) return 'gateway ready';
  return 'gateway not loaded';
});
const fleetScanDisabled = computed(() =>
  isNetworkGatewayLoading.value ||
  !selectedPort.value
);
const gatewayWifiReady = computed(() =>
  !!selectedPort.value &&
  activeSerialDevice.value?.gatewayWifiReadySsid === pairWifiSsid.value.trim() &&
  !!activeSerialDevice.value?.gatewayWifiReadyIp
);
const pairWifiSsidInScan = computed(() =>
  !!pairWifiSsid.value.trim() &&
  wifiNetworks.value.some(n => n.ssid === pairWifiSsid.value.trim())
);
const gatewayWifiStatusText = computed(() => {
  if (gatewayWifiReady.value) return `Gateway connected at ${activeSerialDevice.value?.gatewayWifiReadyIp}`;
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
const monitorFleetLiveCount = computed(() => monitorFleetRows.value.filter(row => monitorRowFreshness(row) === 'live').length);
const monitorFleetStaleCount = computed(() => monitorFleetRows.value.filter(row => monitorRowFreshness(row) === 'stale').length);
const monitorFleetOfflineCount = computed(() => monitorFleetRows.value.filter(row => monitorRowFreshness(row) === 'offline').length);

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
      pushSerialLog(`Local firmware selected: ${selected}`);
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

async function refreshPorts() {
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
    }
    for (const known of Object.keys(portSeenSequence.value)) {
      if (!currentNames.includes(known)) {
        delete portSeenSequence.value[known];
      }
    }
    for (const known of Object.keys(serialDevicesByPort.value)) {
      if (!currentNames.includes(known)) {
        delete serialDevicesByPort.value[known];
      }
    }

    reconcileTabPortSelections(currentNames, newPorts, !hasActiveOperation);

    lastPortSnapshot.value = currentNames;
    syncDeviceInfoForSelectedPort();
    // Artificial delay to ensure the spin is satisfyingly visible
    await new Promise(resolve => setTimeout(resolve, 300));
  } finally {
    isRefreshingPorts.value = false;
  }
}

function reconcileTabPortSelections(currentNames: string[], newPorts: string[], allowAutoSwitch: boolean) {
  const defaultPort = chooseDefaultPort(currentNames);
  const preferredNewPort = chooseMostRecentPort(newPorts) ?? defaultPort;
  const activeReplacement = allowAutoSwitch && preferredNewPort ? preferredNewPort : defaultPort;

  const ensureSelection = (port: string, active: boolean): string => {
    if (currentNames.length === 0) return '';
    if (!port || !currentNames.includes(port)) return activeReplacement;
    if (active && allowAutoSwitch && preferredNewPort) return preferredNewPort;
    return port;
  };

  flashSelectedPort.value = ensureSelection(flashSelectedPort.value, activeMode.value === 'serial');
  provisionSelectedPort.value = ensureSelection(provisionSelectedPort.value, activeMode.value === 'pair');
  fleetSelectedPort.value = ensureSelection(fleetSelectedPort.value, activeMode.value === 'network');
  monitorSelectedPort.value = ensureSelection(monitorSelectedPort.value, activeMode.value === 'monitor');
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
    // Keep local-flash path available even when network release fetch fails.
    const localEntry = firmwareVersions.value.find(v => v.startsWith('Local: '));
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
  const isLocal = selectedVersion.value.startsWith('Local: ');
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
    pushNetworkLog('Enable UDP logging over USB serial, MQTT, or gateway-mediated LoRa admin.');
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
  return pairAdminPassword.value.trim() ||
    activeSerialDevice.value?.adminPassword?.trim() ||
    (hasActiveDeviceInfo.value ? deviceInfo.value?.password?.trim() || '' : '');
}

function noteMonitorReleasedForPort(port: string, reason: string) {
  if (!isMonitoring.value || activeMonitorPort.value !== port) return;
  isMonitoring.value = false;
  pushSerialLog(`Serial monitor released for ${port}: ${reason}.`);
  activeMonitorPort.value = '';
  activeMonitorSsid.value = '';
}

async function sendEasyPairCommand<T = any>(cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000): Promise<T> {
  if (!selectedPort.value) throw new Error('Select the USB gateway first');
  noteMonitorReleasedForPort(selectedPort.value, 'serial admin command needs this port');
  return await invoke<T>('serial_admin_command', {
    port: selectedPort.value,
    request: { cmd, ...payload },
    timeoutMs
  });
}

async function loadNetworkGateway() {
  if (!selectedPort.value || isNetworkGatewayLoading.value) return;
  const port = selectedPort.value;
  isNetworkGatewayLoading.value = true;
  try {
    syncDeviceInfoForSelectedPort();
    if (!deviceInfo.value?.password?.trim()) {
      networkStatusMessage.value = `Reading gateway identity on ${port}...`;
      const ok = await readDeviceInfo();
      if (!ok || selectedPort.value !== port) throw new Error('Unable to read gateway factory details');
    }
    const hello = await waitForSerialAdminHello(port, 6000);
    const state = serialDeviceState(port);
    if (state) {
      state.adminSupported = true;
      state.adminPassword = state.adminPassword || pairAdminPassword.value || deviceInfo.value?.password || '';
    }
    await ensureFleetGatewayStatus(true);
    networkStatusMessage.value = `Gateway loaded on ${port}; firmware ${hello.fw_version || 'unknown'}.`;
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
  const otaExpected = Number(history.otaExpectedUntilMs || 0) > now;

  if (previousUptime > 0 && uptime > 0 && uptime + 30000 < previousUptime) {
    rowState = otaExpected ? 'ota_rebooted' : 'unexpected_reboot';
    rowStateUntilMs = now + (otaExpected ? 20000 : 60000);
  }
  if (otaExpected && history.fwVersion && row.fw_version && history.fwVersion !== row.fw_version) {
    rowState = 'ota_updated';
    rowStateUntilMs = now + 30000;
  }
  if (rowStateUntilMs && rowStateUntilMs <= now) {
    rowState = otaExpected ? 'ota_pending' : undefined;
    rowStateUntilMs = otaExpected ? history.otaExpectedUntilMs : undefined;
  }
  if (history.otaExpectedUntilMs && history.otaExpectedUntilMs <= now && rowState === 'ota_pending') {
    rowState = 'ota_no_reboot';
    rowStateUntilMs = now + 60000;
  }

  fleetRowHistory.value[row.address] = {
    ...history,
    uptimeMs: uptime || history.uptimeMs,
    fwVersion: row.fw_version || history.fwVersion,
    rowState,
    rowStateUntilMs
  };
  return { ...row, row_state: rowState, row_state_until_ms: rowStateUntilMs };
}

function fleetRowClass(device: LoraInventoryDevice): string {
  if (device.row_state === 'unexpected_reboot') return 'bg-rose-950/50 ring-1 ring-rose-500/50';
  if (device.row_state === 'ota_updated') return 'bg-emerald-950/40 ring-1 ring-emerald-500/40';
  if (device.row_state === 'ota_rebooted') return 'bg-sky-950/40 ring-1 ring-sky-500/40';
  if (device.row_state === 'ota_no_reboot') return 'bg-amber-950/40 ring-1 ring-amber-500/40';
  if (device.row_state === 'ota_pending') return 'bg-cyan-950/30';
  return 'bg-slate-950/20';
}

function fleetRowStatusLabel(device: LoraInventoryDevice): string {
  if (device.row_state === 'unexpected_reboot') return 'Unexpected reboot';
  if (device.row_state === 'ota_updated') return 'Updated';
  if (device.row_state === 'ota_rebooted') return 'Rebooted';
  if (device.row_state === 'ota_no_reboot') return 'No reboot seen';
  if (device.row_state === 'ota_pending') return 'Waiting for reboot';
  return '';
}

function mergeLoraInventoryRows(rows: LoraInventoryDevice[]) {
  const selected = new Set(loraInventory.value.filter(d => d.selected).map(d => d.address));
  const now = Date.now();
  loraInventory.value = rows
    .slice()
    .sort((a, b) => a.address - b.address)
    .map(row => classifyFleetRow({ ...row, selected: selected.has(row.address) }, now));
}

async function refreshLoraInventoryStatus() {
  try {
    const out = await sendEasyPairCommand<LoraInventoryStatus>('lora_inventory_status', {}, 5000);
    loraInventoryScan.value = out.scan || null;
    mergeLoraInventoryRows(out.devices || []);
    isLoraInventoryScanning.value = !!out.scan?.active;
    networkStatusMessage.value = `${loraInventoryProgressLabel.value}; ${loraInventory.value.length} device${loraInventory.value.length === 1 ? '' : 's'} visible.`;
    if (!out.scan?.active) stopLoraInventoryPolling(false);
  } catch (e) {
    networkStatusMessage.value = serialFeatureError('LoRa inventory status', e);
    stopLoraInventoryPolling(false);
  }
}

function startLoraInventoryPolling() {
  stopLoraInventoryPolling(false);
  networkInventoryPollTimer.value = window.setInterval(() => {
    refreshLoraInventoryStatus();
  }, 1200);
}

function stopLoraInventoryPolling(markIdle = true) {
  if (networkInventoryPollTimer.value) {
    window.clearInterval(networkInventoryPollTimer.value);
    networkInventoryPollTimer.value = null;
  }
  if (markIdle) isLoraInventoryScanning.value = false;
}

function monitorRowFreshness(row: LoraInventoryDevice): 'live' | 'stale' | 'offline' | 'unknown' {
  const age = Number(row.age_ms || 0);
  if (!row.age_ms && row.age_ms !== 0) return 'unknown';
  if (age <= 15000) return 'live';
  if (age <= 120000) return 'stale';
  return 'offline';
}

function monitorFreshnessClass(row: LoraInventoryDevice): string {
  const state = monitorRowFreshness(row);
  if (state === 'live') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (state === 'stale') return 'border-amber-500/30 bg-amber-500/10 text-amber-300';
  if (state === 'offline') return 'border-rose-500/30 bg-rose-500/10 text-rose-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
}

function monitorFreshnessLabel(row: LoraInventoryDevice): string {
  const state = monitorRowFreshness(row);
  if (state === 'unknown') return 'Unknown';
  return state.charAt(0).toUpperCase() + state.slice(1);
}

function adoptMonitorMqttFromStatus(st: SerialAdminStatus | null) {
  if (!st?.mqtt) return;
  monitorMqttHost.value = st.mqtt.host || monitorMqttHost.value;
  monitorMqttPort.value = Number(st.mqtt.port || monitorMqttPort.value || 1883);
  monitorMqttTopicRoot.value = st.mqtt.topic_root || monitorMqttTopicRoot.value || 'lora';
}

async function refreshMonitorData() {
  if (!selectedPort.value || isMonitorRefreshing.value) return;
  isMonitorRefreshing.value = true;
  try {
    const status = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    applySerialAdminStatus(status);
    adoptMonitorMqttFromStatus(status);
    const inventory = await sendEasyPairCommand<LoraInventoryStatus>('lora_inventory_status', {}, 5000);
    monitorFleetRows.value = (inventory.devices || []).slice().sort((a, b) => a.address - b.address);
    monitorStatusMessage.value = `Updated ${new Date().toLocaleTimeString()} · ${monitorFleetRows.value.length} peer${monitorFleetRows.value.length === 1 ? '' : 's'} visible.`;
  } catch (e) {
    monitorStatusMessage.value = serialFeatureError('Monitor refresh', e);
    notify(monitorStatusMessage.value);
  } finally {
    isMonitorRefreshing.value = false;
  }
}

function startMonitorPolling() {
  stopMonitorPolling();
  monitorPollTimer.value = window.setInterval(() => {
    refreshMonitorData();
  }, 5000);
}

function stopMonitorPolling() {
  if (!monitorPollTimer.value) return;
  window.clearInterval(monitorPollTimer.value);
  monitorPollTimer.value = null;
}

function toggleMonitorMqttConnection() {
  monitorMqttConnected.value = !monitorMqttConnected.value;
  monitorStatusMessage.value = monitorMqttConnected.value
    ? 'MQTT monitor configuration saved locally. Subscription backend is not active yet.'
    : 'MQTT monitor disconnected.';
}

async function ensureFleetGatewayStatus(force = false): Promise<SerialAdminStatus | null> {
  if (!force && serialAdminStatus.value) return serialAdminStatus.value;
  try {
    const out = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    applySerialAdminStatus(out);
    return out;
  } catch {
    return null;
  }
}

function fleetScanBlockedMessage(st: SerialAdminStatus | null): string | null {
  if (!st) return null;
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
  return serialFeatureError('LoRa inventory scan', err);
}

async function startLoraInventoryScan() {
  if (!selectedPort.value) {
    notify('Select the USB gateway first');
    return;
  }
  if (!gatewayReady.value) await loadNetworkGateway();
  const password = pairPassword();
  if (!password) {
    notify('Unable to read the gateway admin password from device details');
    return;
  }
  const gatewayStatus = await ensureFleetGatewayStatus(true);
  const blockedMessage = fleetScanBlockedMessage(gatewayStatus);
  if (blockedMessage) {
    isLoraInventoryScanning.value = false;
    networkStatusMessage.value = blockedMessage;
    notify(blockedMessage);
    return;
  }
  try {
    isLoraInventoryScanning.value = true;
    await sendEasyPairCommand('start_lora_inventory', {
      admin_password: password,
      start_address: 1,
      end_address: LRS_REMOTE_SCAN_CAP,
      interval_ms: 250
    }, 8000);
    networkStatusMessage.value = 'LoRa inventory scan started.';
    await refreshLoraInventoryStatus();
    startLoraInventoryPolling();
  } catch (e) {
    isLoraInventoryScanning.value = false;
    networkStatusMessage.value = fleetScanErrorMessage(e);
    notify(networkStatusMessage.value);
  }
}

async function cancelLoraInventoryScan() {
  const password = pairPassword();
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    await sendEasyPairCommand('cancel_lora_inventory', { admin_password: password }, 5000);
    stopLoraInventoryPolling();
    await refreshLoraInventoryStatus();
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

function fleetFlashAvailable(device: LoraInventoryDevice): boolean {
  return !!device.wifi_connected_known && !!device.wifi_connected && !!device.ip;
}

function fleetFlashUnavailableReason(device: LoraInventoryDevice): string {
  if (!device.wifi_connected_known) return 'Needs confirmed WiFi status from Fleet scan';
  if (!device.wifi_connected) return 'Device WiFi is offline';
  if (!device.ip) return 'Device has no IP address in Fleet status';
  return 'Ready to trigger OTA pull';
}

function markFleetOtaPending(device: LoraInventoryDevice) {
  const now = Date.now();
  fleetRowHistory.value[device.address] = {
    ...(fleetRowHistory.value[device.address] || {}),
    uptimeMs: device.uptime_ms || fleetRowHistory.value[device.address]?.uptimeMs,
    fwVersion: device.fw_version || fleetRowHistory.value[device.address]?.fwVersion,
    otaExpectedUntilMs: now + 180000,
    rowState: 'ota_pending',
    rowStateUntilMs: now + 180000
  };
  loraInventory.value = loraInventory.value.map(row =>
    row.address === device.address
      ? { ...row, row_state: 'ota_pending', row_state_until_ms: now + 180000 }
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
    const password = pairPassword();
    if (!password) throw new Error('missing gateway password');
    await sendEasyPairCommand('start_lora_inventory', {
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
  const password = pairPassword();
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!fleetLogsAvailable(device)) {
    notify('UDP logs need confirmed WiFi connection and IP from fleet status');
    return;
  }
  try {
    remoteUdpBusyAddress.value = device.address;
    if (!gatewayReady.value) await loadNetworkGateway();
    const hosts = await invoke<string[]>('local_udp_log_hosts');
    const host = hosts[0];
    if (!host) throw new Error('No reachable Flasher LAN address found');
    if (!isNetworkUdpMonitoring.value) {
      await startNetworkUdpListener();
    }
    await sendEasyPairCommand('remote_udp_log_control', {
      admin_password: password,
      address: device.address,
      enabled: true,
      host,
      port: 5514,
      ttl_s: 300
    }, 8000);
    networkUdpTarget.value = `LoRa ${device.address}`;
    networkStatusMessage.value = `UDP logging enabled for LoRa ${device.address} to ${host}:5514.`;
    notify(`UDP logs enabled for LoRa ${device.address}`);
  } catch (e) {
    const msg = serialFeatureError(`UDP logs ${device.address}`, e);
    networkStatusMessage.value = msg;
    pushNetworkLog(msg);
    notify(msg);
  } finally {
    remoteUdpBusyAddress.value = null;
  }
}

async function flashLoraRemote(device: LoraInventoryDevice) {
  if (remoteOtaBusyAddress.value != null) return;
  const password = pairPassword();
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  if (!fleetFlashAvailable(device)) {
    notify(fleetFlashUnavailableReason(device));
    return;
  }
  try {
    remoteOtaBusyAddress.value = device.address;
    if (!gatewayReady.value) await loadNetworkGateway();
    const info = await ensureRemoteFlashFirmwareServer();
    const target = firmwareServerTarget(info);
    const out = await sendEasyPairCommand<any>('remote_ota_pull', {
      admin_password: password,
      address: device.address,
      host: target.host,
      port: target.port
    }, 8000);
    markFleetOtaPending(device);
    startFleetOtaFollowup(device);
    networkStatusMessage.value = `Remote OTA pull triggered for LoRa ${device.address} from ${target.host}:${target.port}.`;
    pushNetworkLog(`Remote OTA pull: addr ${device.address} -> http://${target.host}:${target.port}${NETWORK_FIRMWARE_PATH} (${out.path || NETWORK_FIRMWARE_PATH})`);
    notify(`Flash triggered for LoRa ${device.address}`);
  } catch (e) {
    const msg = serialFeatureError(`Remote flash ${device.address}`, e);
    networkStatusMessage.value = msg;
    pushNetworkLog(msg);
    notify(msg);
  } finally {
    remoteOtaBusyAddress.value = null;
  }
}

async function probeSerialAdminSupport(port = selectedPort.value): Promise<boolean> {
  if (!port) return false;
  const state = serialDeviceState(port);
  try {
    await invoke<any>('serial_admin_command', {
      port,
      request: { cmd: 'hello' },
      timeoutMs: 1200
    });
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
      const hello = await invoke<any>('serial_admin_command', {
        port,
        request: { cmd: 'hello' },
        timeoutMs: 1800
      });
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
    }, 5000);
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
  isSerialAdminLoading.value = true;
  pushSerialLog('Refreshing local admin status...');
  try {
    if (!activeSerialDevice.value?.adminSupported) {
      await probeSerialAdminSupport(selectedPort.value);
    }
    const out = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    applySerialAdminStatus(out);
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
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  isSerialAdminLoading.value = true;
  pushSerialLog('Loading local device configuration...');
  try {
    if (!activeSerialDevice.value?.adminSupported) {
      await probeSerialAdminSupport(selectedPort.value);
    }
    const out = await sendEasyPairCommand<{ ok: boolean; cmd: string; config: SerialAdminConfig }>('get_config', {
      admin_password: password
    }, 8000);
    serialAdminConfig.value = {
      ...out.config,
      wifi_sta_password: '',
      mqtt_password: ''
    };
    pushSerialLog('Local configuration loaded. Password fields stay blank unless you enter new values.');
  } catch (e) {
    const msg = serialFeatureError('Config load', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialAdminLoading.value = false;
  }
}

function serialConfigPatch(): Record<string, any> {
  const cfg = serialAdminConfig.value;
  if (!cfg) return {};
  const patch: Record<string, any> = {
    mode: cfg.mode || 'paired',
    role_tx: !!cfg.role_tx,
    local_address: Number(cfg.local_address || 1),
    remote_address: Number(cfg.remote_address || 254),
    wifi_sta_ssid: cfg.wifi_sta_ssid || '',
    wifi_admin_enabled: !!cfg.wifi_admin_enabled,
    mqtt_client_enabled: !!cfg.mqtt_client_enabled,
    mqtt_control_enabled: !!cfg.mqtt_control_enabled,
    mqtt_host: cfg.mqtt_host || '',
    mqtt_port: Number(cfg.mqtt_port || 1883),
    mqtt_user: cfg.mqtt_user || '',
    mqtt_topic_root: cfg.mqtt_topic_root || 'lora',
    sensor_temp_enabled: !!cfg.sensor_temp_enabled,
    sensor_temp_pin: Number(cfg.sensor_temp_pin || 0),
    sensor_temp_interval_s: Number(cfg.sensor_temp_interval_s || 10)
  };
  if (cfg.wifi_sta_password) patch.wifi_sta_password = cfg.wifi_sta_password;
  if (cfg.mqtt_password) patch.mqtt_password = cfg.mqtt_password;
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
    }, 12000);
    pushSerialLog(`Configuration saved${out.network_restarted ? '; networking restarted' : ''}.`);
    await refreshSerialAdminStatus();
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
  if (!confirm('Reboot the selected USB device now?')) return;
  isSerialSystemAction.value = true;
  pushSerialLog('Sending reboot command...');
  try {
    await sendEasyPairCommand('reboot', { admin_password: password }, 5000);
    pushSerialLog('Reboot command accepted.');
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
  if (!confirm(`Factory reset the selected USB device (${summary})?`)) return;
  isSerialSystemAction.value = true;
  pushSerialLog(`Sending factory reset command (${summary})...`);
  try {
    await sendEasyPairCommand('factory_reset', {
      admin_password: password,
      keep_shared_fleet_key: serialFactoryKeepFleet.value,
      keep_wifi_credentials: serialFactoryKeepWifi.value
    }, 6000);
    pushSerialLog('Factory reset command accepted; device is rebooting.');
  } catch (e) {
    const msg = serialFeatureError('Factory reset', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialSystemAction.value = false;
  }
}

async function loadEasyPairGateway() {
  if (!selectedPort.value) return;
  const port = selectedPort.value;
  syncDeviceInfoForSelectedPort();
  if (gatewayReady.value) {
    pushPairLog(`Gateway already ready on ${port}.`);
    return;
  }
  isGatewayLoading.value = true;
  pushPairLog('Reading USB gateway identity...');
  try {
    const ok = await readDeviceInfo();
    if (!ok || !deviceInfo.value) throw new Error('Unable to read gateway factory details');
    if (selectedPort.value !== port) return;
    pairAdminPassword.value = deviceInfo.value.password || '';
    pushPairLog('Waiting for serial admin to become ready...');
    const hello = await waitForSerialAdminHello(port);
    if (selectedPort.value !== port) return;
    pushPairLog(`Gateway ready on ${selectedPort.value}; firmware ${hello.fw_version || 'unknown'}, max remotes ${hello.max_remotes || 12}.`);
    await refreshGatewayStatusForPair();
  } catch (e) {
    if (selectedPort.value === port) {
      pairAdminPassword.value = '';
    }
    pushPairLog('Gateway check failed: ' + e);
    notify('Gateway check failed: ' + e);
  } finally {
    isGatewayLoading.value = false;
  }
}

async function configureEasyPairGateway() {
  const fleetKey = pairFleetKey.value.trim();
  const password = pairPassword();
  if (!fleetKey) {
    notify('Enter the fleet key before pairing');
    return;
  }
  if (!password) {
    notify('Load the gateway factory password first');
    return;
  }
  isPairBusy.value = true;
  pushPairLog('Configuring selected USB device as gateway...');
  try {
    await sendEasyPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      expected_remotes: pairExpectedCount.value
    }, 10000);
    pushPairLog(`Gateway configured for up to ${pairExpectedCount.value} remote device${pairExpectedCount.value === 1 ? '' : 's'}.`);
    await refreshGatewayStatusForPair();
  } catch (e) {
    pushPairLog('Gateway configuration failed: ' + e);
    notify('Gateway configuration failed: ' + e);
  } finally {
    isPairBusy.value = false;
  }
}

async function runEasyPair() {
  const expected = Math.max(1, Math.min(12, Number(pairExpectedCount.value) || 12));
  pairExpectedCount.value = expected;
  const fleetKey = pairFleetKey.value.trim();
  if (!fleetKey) {
    notify('Enter the fleet key before pairing');
    return;
  }
  isPairBusy.value = true;
  pushPairLog('--- EasyPair ---');
  try {
    if (!gatewayReady.value) {
      await loadEasyPairGateway();
      if (!gatewayReady.value) throw new Error('Unable to load gateway');
    }
    const password = pairPassword();
    await sendEasyPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      expected_remotes: expected
    }, 10000);
    pushPairLog(`Gateway prepared. Scanning for ${expected} remote device${expected === 1 ? '' : 's'}...`);
    await sendEasyPairCommand('start_discovery', {
      admin_password: password,
      expected_remotes: expected
    }, 10000);
    startEasyPairStatusPolling();
    await waitForEasyPairState(['ready', 'error'], 130000);
    if (pairStatus.value?.session?.state === 'error') throw new Error('Discovery ended with an error');
    const found = pairStatus.value?.session?.discovered_count || 0;
    if (found === 0) throw new Error('No remote devices found');
    pushPairLog(`Found ${found} remote device${found === 1 ? '' : 's'}. Provisioning...`);
    await sendEasyPairCommand('provision_all', { admin_password: password }, 10000);
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
    pairStatus.value = await sendEasyPairCommand<EasyPairStatus>('provisioning_status', {}, 5000);
    if (log && pairStatus.value.session) {
      const s = pairStatus.value.session;
      pushPairLog(`Status: ${s.state}, found ${s.discovered_count}, verified ${s.verified_count}, failed ${s.failed_count}.`);
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
  const password = pairPassword();
  if (!password) {
    notify('Load the gateway factory password first');
    return;
  }
  isPairBusy.value = true;
  pushPairLog(`Scanning for up to ${pairExpectedCount.value} powered remote devices...`);
  try {
    await sendEasyPairCommand('start_discovery', {
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

async function provisionEasyPairDevices() {
  const password = pairPassword();
  if (!password) {
    notify('Load the gateway factory password first');
    return;
  }
  isPairBusy.value = true;
  pushPairLog('Provisioning discovered remotes...');
  try {
    await sendEasyPairCommand('provision_all', { admin_password: password }, 10000);
    startEasyPairStatusPolling();
    await refreshEasyPairStatus(true);
  } catch (e) {
    pushPairLog('Provisioning failed: ' + e);
    notify('Provisioning failed: ' + e);
  } finally {
    isPairBusy.value = false;
  }
}

async function saveEasyPairTargets() {
  const password = pairPassword();
  const devices = (pairStatus.value?.devices || [])
    .filter(d =>
      d.selected &&
      !d.address_conflict &&
      d.state === 'verified' &&
      d.assigned_address > 0 &&
      d.assigned_address < 255
    );
  const addresses = devices.map(d => d.assigned_address);
  if (!password || addresses.length === 0) {
    notify('No provisioned target addresses to save yet');
    return;
  }
  const seen = new Set<number>();
  const duplicate = addresses.find(addr => {
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
  isPairBusy.value = true;
  devices.forEach(d => pushPairLog(`Address allocation: addr ${d.assigned_address} chip ${d.chip_id_hex || 'unknown'}`));
  pushPairLog(`Saving gateway target list: ${addresses.join(', ')}`);
  try {
    await sendEasyPairCommand('set_gateway_targets', { admin_password: password, addresses }, 10000);
    pushPairLog('Provisioning complete.');
    await refreshGatewayStatusForPair();
    await refreshEasyPairStatus(false);
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
    await sendEasyPairCommand('cancel_provisioning', { admin_password: password }, 5000);
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
    const out = await sendEasyPairCommand<WifiScanResponse>('wifi_scan', {
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

async function openPairWifiTab() {
  pairPanelTab.value = 'wifi';
  if (!selectedPort.value || isGatewayLoading.value || isWifiScanning.value) {
    return;
  }
  if (gatewayWifiReady.value) {
    return;
  }
  if (activeSerialDevice.value?.wifiScanned && wifiNetworks.value.length > 0) {
    return;
  }
  if (gatewayReady.value && await refreshGatewayStatusForPair()) {
    return;
  }
  await scanGatewayWifi();
}

function clearGatewayWifiReady() {
  const state = serialDeviceState();
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
  if (!port || port !== selectedPort.value) return false;
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

  pairWifiSsid.value = ssid;
  const state = serialDeviceState(port);
  if (!state) return false;
  state.gatewayWifiReadySsid = ssid;
  state.gatewayWifiReadyIp = ip;
  return true;
}

async function refreshGatewayStatusForPair(): Promise<boolean> {
  if (!selectedPort.value) return false;
  const port = selectedPort.value;
  try {
    const status = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    if (selectedPort.value !== port) return false;
    applySerialAdminStatus(status, port);
    if (gatewayWifiReady.value) {
      pushPairLog(`Gateway already connected to ${activeSerialDevice.value?.gatewayWifiReadySsid} at ${activeSerialDevice.value?.gatewayWifiReadyIp}.`);
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
    const out = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    assertGatewayWifiAttemptActive(attemptId);
    lastStatus = out;
    const wifi = out.wifi;
    const wifiStatus = wifi?.status?.trim().toLowerCase() || '';
    const wifiIp = wifi?.ip?.trim() || '';
    const wifiSsid = wifi?.sta_ssid?.trim() || '';
    if ((wifi?.sta_connected || wifiStatus === 'connected') && wifiSsid === ssid && wifiIp && wifiIp !== '0.0.0.0') {
      applySerialAdminStatus(out);
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
    await sendEasyPairCommand('configure_wifi', {
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
    pushPairLog(`Gateway connected to ${ssid} at ${activeSerialDevice.value?.gatewayWifiReadyIp || status.wifi?.ip || 'unknown IP'}. You can now send WiFi to remotes.`);
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
  const password = pairPassword();
  const ssid = pairWifiSsid.value.trim();
  if (!password || !ssid) {
    notify('Select a WiFi network and load the gateway password first');
    return;
  }
  if (!gatewayWifiReady.value) {
    notify('Connect the gateway to WiFi first');
    return;
  }
  isFleetWifiSending.value = true;
  pushPairLog(`Sending WiFi credentials to remotes over LoRa for ${ssid}...`);
  try {
    const out = await sendEasyPairCommand<any>('provision_fleet_wifi', {
      admin_password: password,
      wifi_sta_ssid: ssid,
      wifi_sta_password: pairWifiPassword.value
    }, 20000);
    pushPairLog(`LoRa WiFi provisioning sent (${out.packets || '?'} packets).`);
  } catch (e) {
    const msg = serialFeatureError('WiFi provisioning', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    isFleetWifiSending.value = false;
  }
}

function latestStaIpFromLogs(): string | null {
  for (let i = serialLogs.value.length - 1; i >= 0; i--) {
    const line = String(serialLogs.value[i] || '');
    const match = line.match(/\bevent=sta_connected\b.*\bip=((?:\d{1,3}\.){3}\d{1,3})\b/);
    if (match && match[1]) {
      return match[1];
    }
  }
  return null;
}

async function openActiveDeviceConsole() {
  if (!deviceInfo.value) {
    notify('Load device info first to open device URL');
    return;
  }
  const staIp = latestStaIpFromLogs();
  const url = staIp ? `http://${staIp}` : 'http://192.168.4.1';
  try {
    await openUrl(url);
    pushSerialLog(`Opened ${url}`);
  } catch (e) {
    notify('Failed to open device URL: ' + e);
  }
}

async function readDeviceInfo() {
  if (!selectedPort.value) return;
  const port = selectedPort.value;
  const seq = deviceInfoReadSeq.value + 1;
  deviceInfoReadSeq.value = seq;
  isLoadingInfo.value = true;
  noteMonitorReleasedForPort(port, 'device info read needs this port');
  syncDeviceInfoForSelectedPort();
  pushSerialLog(`Reading device information from ${port}...`);
  try {
    const info = await invoke<DeviceInfo>('get_device_info', { port });
    if (deviceInfoReadSeq.value !== seq || selectedPort.value !== port) {
      pushSerialLog(`Ignored stale device info from ${port}`);
      return false;
    }
    const state = serialDeviceState(port);
    if (state) {
      state.deviceInfo = info;
      if (!state.adminPassword.trim()) {
        state.adminPassword = info.password || '';
      }
    }
    pushSerialLog('Device info read successfully');
    if (activeMode.value === 'serial') {
      await probeSerialAdminSupport(port);
    }
    return true;
  } catch (e) {
    if (deviceInfoReadSeq.value !== seq || selectedPort.value !== port) {
      pushSerialLog(`Ignored stale device info error from ${port}`);
      return false;
    }
    pushSerialLog('Failed to read device info: ' + e);
    return false;
  } finally {
    if (deviceInfoReadSeq.value === seq) {
      isLoadingInfo.value = false;
    }
  }
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

    const isLocal = selectedVersion.value.startsWith('Local: ');
    const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
    
    if (isLocal && !firmwarePath) throw new Error('Local file path missing');

    const result = await invoke('flash_firmware', { 
      port: flashPort,
      firmwarePath,
      region: isLocal ? null : region.value,
      eraseFirst: eraseBeforeFlash.value
    });
    pushSerialLog(result as string);
    
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
}, { deep: true });

watch([pairWifiSsid, pairWifiPassword], ([nextSsid, nextPassword], [prevSsid, prevPassword]) => {
  if (nextSsid === prevSsid && nextPassword === prevPassword) return;
  cancelGatewayWifiConnect('WiFi credentials changed');
});

watch(activeMode, (mode) => {
  nextTick(() => scrollToBottom());
  syncDeviceInfoForSelectedPort();
  if (mode === 'serial' && selectedPort.value && !hasActiveDeviceInfo.value) {
    readDeviceInfo();
  }
  if (mode !== 'monitor') {
    stopMonitorPolling();
  }
});

watch(selectedPort, (port) => {
  deviceInfoReadSeq.value += 1;
  isLoadingInfo.value = false;
  syncDeviceInfoForSelectedPort();
  serialUptimeMs.value = activeSerialDevice.value?.status?.uptime_ms ?? null;
  if (port && activeMode.value === 'serial') {
    readDeviceInfo();
  }
  if (activeMode.value === 'monitor') {
    monitorFleetRows.value = [];
    if (monitorAutoRefresh.value) {
      refreshMonitorData();
    }
  }
});

watch(provisionSelectedPort, () => {
  pairStatus.value = null;
});

onMounted(async () => {
  generatePairFleetKey(false);
  try {
    const savedMonitor = localStorage.getItem(MONITOR_AFTER_FLASH_STORAGE_KEY);
    if (savedMonitor === 'true' || savedMonitor === 'false') {
      monitorAfterFlash.value = savedMonitor === 'true';
    }
    const savedErase = localStorage.getItem(ERASE_BEFORE_FLASH_STORAGE_KEY);
    if (savedErase === 'true' || savedErase === 'false') {
      eraseBeforeFlash.value = savedErase === 'true';
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

  refreshPorts();
  fetchFirmware();
  
  unlistenFlash = await listen<LogEvent>('flash-log', (event) => {
    const rawMsg = event.payload.message;
    // Split on both newlines and carriage returns to ensure progress updates 
    // from esptool appear as fresh lines in our Activity Log.
    const lines = rawMsg.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
      if (trimmed) pushSerialLog(trimmed);
    });
  });
  
  unlistenMonitor = await listen<MonitorEvent>('monitor-log', (event) => {
    const rawLine = event.payload.line;
    const lines = rawLine.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
       if (trimmed) pushSerialLog(trimmed);
    });
  });

  unlistenNetworkMonitor = await listen<MonitorEvent>('network-monitor-log', (event) => {
    const rawLine = event.payload.line;
    const lines = rawLine.split(/[\r\n]+/);
    lines.forEach(line => {
      const trimmed = line.trim();
      if (trimmed) pushNetworkLog(trimmed);
    });
  });

  unlistenPortsChanged = await listen<PortsChangedEvent>('serial-ports-changed', () => {
    if (!isFlashing.value) {
      refreshPorts();
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
  if (enabled) {
    startMonitorPolling();
    refreshMonitorData();
  } else {
    stopMonitorPolling();
  }
});

onUnmounted(() => {
  stopEasyPairStatusPolling();
  stopLoraInventoryPolling();
  stopMonitorPolling();
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
</script>

<template>
  <div class="relative h-full flex flex-col">
    <div :class="['grid gap-3 flex-1 min-h-0 transition-all duration-500', activityFullscreen || activeMode === 'network' ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
      <!-- Log Panel -->
      <div v-if="activeMode !== 'network'" :class="['glass-card p-3 flex flex-col gap-2 text-left overflow-hidden h-full']">
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
              v-if="activeMode === 'serial' && isMonitoring"
              @click="openActiveDeviceConsole"
              :disabled="!hasActiveDeviceInfo"
              :class="[
                'p-1 rounded border transition-all',
                hasActiveDeviceInfo
                  ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                  : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
              ]"
              title="Open device web console"
              aria-label="Open device web console"
            >
              <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <circle cx="12" cy="12" r="9"></circle>
                <path d="M3 12h18"></path>
                <path d="M12 3a14 14 0 0 1 0 18"></path>
                <path d="M12 3a14 14 0 0 0 0 18"></path>
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

      <div v-if="activeMode === 'pair'" class="flex flex-col gap-3 h-full overflow-hidden">
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
                @click="loadEasyPairGateway"
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
              <label class="font-medium text-slate-400">Remote count</label>
              <input v-model.number="pairExpectedCount" class="glass-input h-10" type="number" min="1" max="12" />
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

          <button @click="runEasyPair" :disabled="pairPrimaryDisabled" class="primary-btn h-9 flex items-center justify-center gap-3 text-sm font-bold">
            <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isPairBusy }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
            <span>{{ isPairBusy ? 'Provisioning...' : 'Provision' }}</span>
          </button>

          <details class="rounded-md border border-slate-800 bg-slate-900/30 px-2 py-1.5">
            <summary class="cursor-pointer select-none text-xs font-semibold text-slate-500 hover:text-slate-300">Advanced steps</summary>
            <div class="mt-3 grid grid-cols-2 xl:grid-cols-5 gap-3">
              <button @click="configureEasyPairGateway" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Configure the USB device as gateway">Prepare</button>
              <button @click="startEasyPairDiscovery" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Discover powered remotes over LoRa">Scan</button>
              <button @click="provisionEasyPairDevices" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Provision all discovered remotes">Provision All</button>
              <button @click="saveEasyPairTargets" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Save discovered remote addresses on the gateway">Finish</button>
              <button @click="cancelEasyPair" :disabled="!gatewayReady" class="glass-input h-10 hover:bg-slate-700/70 flex items-center justify-center gap-2 text-xs font-bold" title="Stop the current discovery or provisioning session">Stop</button>
            </div>
          </details>
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
              <div class="font-mono text-slate-300">{{ pairStatus.session.discovered_count }} / {{ pairStatus.session.estimated_count }}</div>
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
                  <div class="font-mono text-slate-300">{{ device.fw_major }}.{{ device.fw_minor }}.{{ device.fw_patch }}</div>
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
      <div v-if="activeMode === 'serial' && !isMonitoring" class="flex flex-col gap-6 h-full min-h-0 overflow-auto custom-scrollbar pr-1 transition-opacity duration-300" :class="{ 'opacity-0 pointer-events-none': isMonitoring }">
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

        <!-- Local Admin Panel -->
        <div class="glass-card p-3 flex flex-col gap-3 text-left shrink-0">
          <div class="flex items-start justify-between gap-3">
            <div>
              <h2 class="text-base font-bold text-slate-300">Local admin</h2>
              <p class="mt-1 text-xs text-slate-400">{{ serialStatusSummary }}</p>
            </div>
            <div class="flex gap-2">
              <button @click="refreshSerialAdminStatus" :disabled="serialAdminDisabled" class="glass-input m-0 h-10 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminLoading ? 'Loading...' : 'Status' }}
              </button>
              <button @click="loadSerialAdminConfig" :disabled="serialAdminDisabled" class="glass-input m-0 h-10 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
                Load config
              </button>
            </div>
          </div>

          <div v-if="serialAdminIsFactoryDefault" class="rounded-md border border-amber-500/30 bg-amber-500/10 p-3 text-xs text-amber-100">
            Factory default: this device is not commissioned yet. Use Provision to assign its fleet key, role, address, and WiFi before treating it as an operational transmitter or receiver.
          </div>

          <div v-if="serialAdminStatus" class="grid grid-cols-2 sm:grid-cols-4 gap-2 text-xs">
            <div class="rounded border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Firmware</div>
              <div class="font-mono text-slate-200">{{ serialAdminStatus.fw_version || 'unknown' }}</div>
            </div>
            <div class="rounded border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Uptime</div>
              <div class="font-mono text-slate-200">{{ formatUptime(serialAdminStatus.uptime_ms || 0) }}</div>
            </div>
            <div class="rounded border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Heap</div>
              <div class="font-mono text-slate-200">{{ formatBytes(serialAdminStatus.heap_free) }}</div>
            </div>
            <div class="rounded border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">MQTT</div>
              <div class="font-mono text-slate-200">{{ serialAdminStatus.mqtt?.client_enabled ? 'enabled' : 'disabled' }}</div>
            </div>
            <div class="rounded border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">State</div>
              <div class="font-mono text-slate-200">{{ serialAdminIsFactoryDefault ? 'factory' : 'commissioned' }}</div>
            </div>
          </div>

          <div v-if="serialAdminConfig" class="grid grid-cols-1 sm:grid-cols-2 gap-3 text-xs">
            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">Role</label>
              <select v-model="serialAdminConfig.role_tx" class="glass-input h-10 appearance-none">
                <option :value="true">Gateway / transmitter</option>
                <option :value="false">Remote / receiver</option>
              </select>
            </div>
            <div class="grid grid-cols-2 gap-2">
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">Local addr</label>
                <input v-model.number="serialAdminConfig.local_address" type="number" min="1" max="254" class="glass-input h-10" />
              </div>
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">Remote addr</label>
                <input v-model.number="serialAdminConfig.remote_address" type="number" min="1" max="254" class="glass-input h-10" />
              </div>
            </div>

            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">WiFi SSID</label>
              <input v-model="serialAdminConfig.wifi_sta_ssid" class="glass-input h-10" placeholder="Leave blank for no WiFi" />
            </div>
            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">New WiFi password</label>
              <div class="flex gap-2">
                <input v-model="serialAdminConfig.wifi_sta_password" :type="showSerialWifiPassword ? 'text' : 'password'" class="glass-input h-10 flex-1" placeholder="Blank keeps existing password" />
                <button @click="showSerialWifiPassword = !showSerialWifiPassword" class="glass-input h-10 px-3 hover:bg-slate-700/70">{{ showSerialWifiPassword ? 'Hide' : 'Show' }}</button>
              </div>
            </div>

            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="serialAdminConfig.wifi_admin_enabled" type="checkbox" />
              WiFi admin enabled
            </label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="serialAdminConfig.sensor_temp_enabled" type="checkbox" />
              DS18B20 temperature sensor enabled
            </label>

            <div class="grid grid-cols-2 gap-2">
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">Sensor pin</label>
                <input v-model.number="serialAdminConfig.sensor_temp_pin" type="number" min="0" max="16" class="glass-input h-10" />
              </div>
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">Report seconds</label>
                <input v-model.number="serialAdminConfig.sensor_temp_interval_s" type="number" min="5" max="3600" class="glass-input h-10" />
              </div>
            </div>

            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">MQTT host</label>
              <input v-model="serialAdminConfig.mqtt_host" class="glass-input h-10" placeholder="venus.local" />
            </div>
            <div class="grid grid-cols-2 gap-2">
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">MQTT port</label>
                <input v-model.number="serialAdminConfig.mqtt_port" type="number" min="1" max="65535" class="glass-input h-10" />
              </div>
              <div class="flex flex-col gap-1.5">
                <label class="font-medium text-slate-400">Topic root</label>
                <input v-model="serialAdminConfig.mqtt_topic_root" class="glass-input h-10" />
              </div>
            </div>

            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="serialAdminConfig.mqtt_client_enabled" type="checkbox" />
              MQTT client enabled
            </label>
            <label class="flex items-center gap-2 text-slate-300">
              <input v-model="serialAdminConfig.mqtt_control_enabled" type="checkbox" />
              MQTT control enabled
            </label>

            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">MQTT user</label>
              <input v-model="serialAdminConfig.mqtt_user" class="glass-input h-10" />
            </div>
            <div class="flex flex-col gap-1.5">
              <label class="font-medium text-slate-400">New MQTT password</label>
              <div class="flex gap-2">
                <input v-model="serialAdminConfig.mqtt_password" :type="showSerialMqttPassword ? 'text' : 'password'" class="glass-input h-10 flex-1" placeholder="Blank keeps existing password" />
                <button @click="showSerialMqttPassword = !showSerialMqttPassword" class="glass-input h-10 px-3 hover:bg-slate-700/70">{{ showSerialMqttPassword ? 'Hide' : 'Show' }}</button>
              </div>
            </div>

            <div class="sm:col-span-2 flex flex-wrap items-center gap-3 pt-1">
              <button @click="saveSerialAdminConfig" :disabled="serialAdminDisabled || isSerialAdminSaving" class="primary-btn h-10 px-5 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminSaving ? 'Saving...' : 'Save config' }}
              </button>
              <button @click="rebootSerialDevice" :disabled="serialAdminDisabled" class="glass-input h-10 px-4 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-60">
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
              <button @click="factoryResetSerialDevice" :disabled="serialAdminDisabled" class="glass-input h-10 px-4 hover:bg-red-500/15 text-xs font-bold text-red-200 disabled:opacity-60">
                Factory reset
              </button>
            </div>
          </div>

          <p v-if="hasActiveDeviceInfo && !serialAdminStatus && !serialAdminConfig && !isSerialAdminLoading" class="text-xs text-slate-500">
            Load status or config to inspect this device over USB serial admin.
          </p>
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
      </div>

      <div v-if="activeMode === 'monitor'" class="flex flex-col h-full overflow-hidden gap-3">
        <div class="glass-card flex flex-col text-left shrink-0 p-3 gap-3">
          <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
            <div class="min-w-0">
              <h2 class="text-base font-bold text-cyan-300">
                Monitor
              </h2>
              <p class="mt-1 text-xs text-slate-400 max-w-3xl">
                {{ monitorHealthSummary }} · {{ monitorStatusMessage }}
              </p>
            </div>
            <div class="flex flex-wrap items-center justify-end gap-2">
              <label class="flex items-center gap-2 text-xs text-slate-400">
                <input v-model="monitorAutoRefresh" type="checkbox" />
                Auto refresh
              </label>
              <button
                @click="refreshMonitorData"
                :disabled="isMonitorRefreshing || !selectedPort"
                class="primary-btn m-0 h-8 px-3 flex items-center justify-center gap-2 text-xs font-bold disabled:opacity-60"
              >
                {{ isMonitorRefreshing ? 'Refreshing...' : 'Refresh' }}
              </button>
            </div>
          </div>

          <div class="grid grid-cols-1 md:grid-cols-3 xl:grid-cols-5 gap-2">
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
              <label class="font-medium text-slate-400">Transport</label>
              <select v-model="monitorTransport" class="glass-input h-10 appearance-none">
                <option value="serial">Serial</option>
                <option value="mqtt">MQTT</option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Broker host</label>
              <input v-model="monitorMqttHost" class="glass-input h-10" placeholder="venus.local" />
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Broker port</label>
              <input v-model.number="monitorMqttPort" class="glass-input h-10" type="number" min="1" max="65535" />
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Topic root</label>
              <input v-model="monitorMqttTopicRoot" class="glass-input h-10" />
            </div>
          </div>

          <div class="grid grid-cols-1 md:grid-cols-2 xl:grid-cols-[minmax(0,1fr)_minmax(0,2fr)_auto_auto] gap-2 items-end">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">MQTT user</label>
              <input v-model="monitorMqttUser" class="glass-input h-10" />
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">MQTT password</label>
              <div class="flex gap-2">
                <input v-model="monitorMqttPassword" :type="showMonitorMqttPassword ? 'text' : 'password'" class="glass-input h-10 min-w-0 flex-1" />
                <button @click="showMonitorMqttPassword = !showMonitorMqttPassword" class="glass-input m-0 h-10 w-14 shrink-0 hover:bg-slate-700/70 text-xs font-bold">{{ showMonitorMqttPassword ? 'Hide' : 'Show' }}</button>
              </div>
            </div>
            <div class="flex">
              <button
                @click="toggleMonitorMqttConnection"
                :disabled="monitorTransport !== 'mqtt' || !monitorMqttHost"
                class="glass-input m-0 h-10 min-w-24 px-3 hover:bg-slate-700/70 text-xs font-bold disabled:opacity-50"
              >
                {{ monitorMqttConnected ? 'Disconnect MQTT' : 'Save MQTT' }}
              </button>
            </div>
            <div class="flex">
              <span :class="['inline-flex h-10 min-w-28 items-center justify-center rounded border px-2 text-[10px] font-bold whitespace-nowrap', monitorMqttConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
                MQTT {{ monitorMqttConnected ? 'configured' : 'not active' }}
              </span>
            </div>
          </div>
        </div>

        <div class="glass-card grid grid-cols-1 gap-0 overflow-hidden text-left text-xs md:grid-cols-2 xl:grid-cols-4 shrink-0">
          <div class="border-b border-slate-800 p-2 md:border-r xl:border-b-0">
            <span class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Gateway</span>
            <span class="ml-2 font-mono font-bold text-slate-200">{{ monitorGatewayStatus?.chip_id || '-' }}</span>
            <span class="ml-2 text-slate-400">{{ monitorGatewayStatus?.fw_version || '-' }} · {{ monitorGatewayStatus?.role || '-' }} · addr {{ monitorGatewayStatus?.local_address ?? '-' }}</span>
          </div>
          <div class="border-b border-slate-800 p-2 md:border-b-0 xl:border-r">
            <span class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Memory</span>
            <span class="ml-2 font-mono font-bold text-slate-200">{{ formatBytes(monitorGatewayStatus?.heap_free) }}</span>
            <span class="ml-2 text-slate-400">max {{ formatBytes(monitorGatewayStatus?.heap_max_block) }} · frag {{ monitorGatewayStatus?.heap_frag_pct ?? '-' }}%</span>
          </div>
          <div class="border-b border-slate-800 p-2 md:border-r md:border-b-0 xl:border-r">
            <span class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Relay</span>
            <span class="ml-2 font-mono font-bold text-slate-200">cmd {{ monitorGatewayStatus?.relay_state ?? '-' }} · fb {{ monitorGatewayStatus?.relay_feedback ?? '-' }}</span>
            <span class="ml-2 text-slate-400">input {{ monitorGatewayStatus?.input_state ?? '-' }} · link {{ monitorGatewayStatus?.link_state || '-' }}</span>
          </div>
          <div class="p-2">
            <span class="text-[10px] uppercase tracking-wider text-slate-500 font-bold">Fleet</span>
            <span class="ml-2 font-bold text-slate-200">{{ monitorFleetLiveCount }} live · {{ monitorFleetStaleCount }} stale</span>
            <span class="ml-2 text-slate-400">{{ monitorFleetOfflineCount }} offline · {{ monitorFleetRows.length }} total</span>
          </div>
        </div>

        <div class="glass-card p-3 flex flex-col gap-2 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between gap-3">
            <div>
              <h2 class="text-sm font-bold text-slate-300">Fleet Diagnostics</h2>
              <div class="mt-1 text-xs text-slate-500">Serial-backed monitor data from the selected gateway.</div>
            </div>
          </div>
          <div class="min-h-0 flex-1 overflow-auto custom-scrollbar rounded border border-slate-800">
            <table class="w-full min-w-[1180px] border-collapse text-xs">
              <thead class="sticky top-0 bg-slate-950/95 text-slate-500">
                <tr class="border-b border-slate-800">
                  <th class="px-2 py-1.5 text-left font-semibold">Addr</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Freshness</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
                  <th class="px-2 py-1.5 text-left font-semibold">IP</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Relay</th>
                  <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
                  <th class="px-2 py-1.5 text-left font-semibold">MQTT</th>
                  <th class="px-2 py-1.5 text-left font-semibold">RSSI</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Heap</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Frag</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Poll</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="monitorFleetRows.length === 0">
                  <td colspan="12" class="px-3 py-8 text-center text-slate-600">Refresh monitor data to load gateway and fleet diagnostics.</td>
                </tr>
                <tr v-for="device in monitorFleetRows" :key="device.address" class="border-b border-slate-900/80 hover:bg-white/5 transition-colors">
                  <td class="px-2 py-1.5 font-mono text-slate-200">{{ device.address }}</td>
                  <td class="px-2 py-1.5">
                    <span :class="['rounded border px-2 py-1 text-[10px] font-bold', monitorFreshnessClass(device)]">{{ monitorFreshnessLabel(device) }}</span>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.fw_version || '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip || '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">ack {{ device.relay_state ?? '-' }} · fb {{ device.relay_feedback ?? '-' }}</td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.wifi_connected_known ? (device.wifi_connected ? 'Connected' : 'Offline') : 'Unknown' }}</td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.mqtt_known ? (device.mqtt_connected ? 'Connected' : 'Offline') : 'Unknown' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">up {{ device.rssi ?? '-' }} / down {{ device.downlink_rssi_known ? device.downlink_rssi : '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.maintenance_debug_known ? `${formatBytes(device.heap_free)} / ${formatBytes(device.heap_max_block)}` : '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.maintenance_debug_known ? `${device.heap_frag_pct ?? '-'}%` : '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.uptime_ms ? formatUptime(device.uptime_ms) : '-' }}</td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.poll_pending ? 'Pending' : 'Idle' }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </div>

      <div v-if="activeMode === 'network'" class="flex flex-col h-full overflow-hidden gap-3">
        <div class="glass-card flex flex-col text-left shrink-0 p-3 gap-3">
          <div class="flex flex-col gap-3 xl:flex-row xl:items-start xl:justify-between">
            <div class="min-w-0">
              <h2 class="text-base font-bold text-cyan-300">
                Fleet
              </h2>
              <p class="mt-1 text-xs text-slate-400 max-w-3xl">
                {{ fleetGatewayStatusLabel }} · {{ selectedPort || 'no USB gateway selected' }} · {{ loraInventoryProgressLabel }}
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
                {{ isLoraInventoryScanning ? 'Stop scan' : 'Scan Fleet' }}
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

        <div class="glass-card p-3 flex flex-col gap-3 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between gap-3">
            <div>
              <h2 class="text-lg font-bold text-slate-300">Devices</h2>
              <div class="mt-1 text-xs text-slate-500">Fleet rows are expanded by default; actions are per device.</div>
            </div>
            <div class="flex items-center gap-3">
              <div class="text-xs text-slate-500">{{ loraInventory.length }} found · {{ selectedLoraInventoryCount }} selected</div>
            </div>
          </div>
          <div class="min-h-0 flex-1 overflow-auto custom-scrollbar rounded-md border border-slate-800">
            <table class="w-full min-w-[980px] border-collapse text-xs">
              <thead class="sticky top-0 bg-slate-950/95 text-slate-500">
                <tr class="border-b border-slate-800">
                  <th class="w-10 px-2 py-1.5 text-left"></th>
                  <th class="px-2 py-1.5 text-left font-semibold">LoRa</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Chip / Host</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Firmware</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Role</th>
                  <th class="px-2 py-1.5 text-left font-semibold">WiFi</th>
                  <th class="px-2 py-1.5 text-left font-semibold">IP</th>
                  <th class="px-2 py-1.5 text-left font-semibold">MQTT</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Uptime</th>
                  <th class="px-2 py-1.5 text-left font-semibold">RSSI</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Age</th>
                  <th class="px-2 py-1.5 text-left font-semibold">Actions</th>
                </tr>
              </thead>
              <tbody>
                <tr v-if="loraInventory.length === 0">
                  <td colspan="12" class="px-3 py-8 text-center text-slate-600">Select a USB gateway and scan the fleet to discover remotes.</td>
                </tr>
                <tr
                  v-for="device in loraInventory"
                  :key="device.address"
                  :class="['border-b border-slate-900/80 hover:bg-white/5 transition-colors', fleetRowClass(device)]"
                >
                  <td class="px-2 py-1.5"><input v-model="device.selected" type="checkbox" /></td>
                  <td class="px-2 py-1.5 font-mono text-slate-200">{{ device.address }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.chip_id || '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.fw_version || 'unsupported' }}</td>
                  <td class="px-2 py-1.5 text-slate-300">{{ device.role || '-' }} / {{ device.mode || '-' }}</td>
                  <td class="px-2 py-1.5">
                    <span :class="['rounded border px-2 py-1 text-[10px] font-bold', device.wifi_enabled_known ? (device.wifi_enabled ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-amber-500/30 bg-amber-500/10 text-amber-300') : 'border-slate-700 bg-slate-800/50 text-slate-400']">
                      {{ device.wifi_enabled_known ? (device.wifi_enabled ? 'Enabled' : 'Disabled') : 'Unknown' }}
                    </span>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.ip || '-' }}</td>
                  <td class="px-2 py-1.5 text-slate-400">{{ device.mqtt_known ? (device.mqtt_connected ? 'Connected' : 'Offline') : 'Unknown' }}</td>
                  <td class="px-2 py-1.5 font-mono">
                    <div class="text-slate-300">{{ device.uptime_ms ? formatUptime(device.uptime_ms) : '-' }}</div>
                    <div v-if="fleetRowStatusLabel(device)" :class="['mt-1 text-[10px] font-bold', device.row_state === 'unexpected_reboot' ? 'text-rose-300' : device.row_state === 'ota_updated' ? 'text-emerald-300' : 'text-sky-300']">
                      {{ fleetRowStatusLabel(device) }}
                    </div>
                  </td>
                  <td class="px-2 py-1.5 font-mono text-slate-300">{{ device.rssi ?? '-' }}</td>
                  <td class="px-2 py-1.5 font-mono text-slate-400">{{ device.age_ms != null ? `${Math.round(device.age_ms / 1000)}s` : '-' }}</td>
                  <td class="px-2 py-1.5">
                    <div class="flex items-center gap-2">
                    <button
                      @click="flashLoraRemote(device)"
                      :disabled="remoteOtaBusyAddress !== null || isFirmwareServerStarting || !fleetFlashAvailable(device)"
                      class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold disabled:opacity-50"
                      :title="fleetFlashUnavailableReason(device)"
                    >
                      {{ remoteOtaBusyAddress === device.address ? 'Flashing...' : 'Flash' }}
                    </button>
                    <button
                      @click="startFleetUdpLogs(device)"
                      :disabled="remoteUdpBusyAddress !== null || !fleetLogsAvailable(device)"
                      class="glass-input m-0 h-7 px-3 hover:bg-slate-700/70 text-[10px] font-bold disabled:opacity-50"
                      :title="fleetLogsAvailable(device) ? 'Enable and show UDP logs' : 'Needs confirmed WiFi connection and IP from fleet status'"
                    >
                      {{ remoteUdpBusyAddress === device.address ? 'Starting...' : 'Logs' }}
                    </button>
                    </div>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
          <div v-if="isNetworkUdpMonitoring" class="shrink-0 rounded-md border border-slate-800 bg-slate-950/40 p-3">
            <div class="mb-2 flex items-center justify-between gap-3">
              <div class="text-xs font-bold text-slate-300">UDP logs · {{ networkUdpTarget || 'Fleet' }}</div>
              <button @click="stopNetworkUdpMonitor" class="glass-input m-0 h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">Stop logs</button>
            </div>
            <div class="max-h-44 overflow-auto custom-scrollbar font-mono text-[10px] leading-tight text-slate-400">
              <div v-for="(log, i) in networkLogs.slice(-200)" :key="i">{{ log }}</div>
              <div v-if="networkLogs.length === 0" class="text-slate-600">Waiting for UDP log lines...</div>
            </div>
          </div>
        </div>

      </div>
    </div>

    <!-- Toast Notification -->
    <Transition name="toast">
      <div v-if="showToast" class="fixed bottom-4 left-1/2 -translate-x-1/2 z-50 glass-card px-3 py-2 border border-cyan-500/50 text-xs font-medium text-slate-200 flex items-center gap-3">
        <span class="w-2 h-2 rounded-full bg-cyan-500"></span>
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
