<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import { openUrl } from '@tauri-apps/plugin-opener';

type ActiveMode = 'pair' | 'serial' | 'network';

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

interface LanSubnet {
  interface_name: string;
  address: string;
  netmask: string;
  cidr: number;
  network: string;
  broadcast: string;
  host_count: number;
}

interface NetworkDevice {
  ip: string;
  identity: string;
  chip_id: string;
  derived_password: string;
  auth_status: string;
  fw_version: string;
  fw_display: string;
  role: string;
  mode: string;
  lan_hostname: string;
  message: string;
}

interface NetworkDiscoveryResult {
  subnets: LanSubnet[];
  devices: NetworkDevice[];
  scanned_hosts: number;
  duration_ms: number;
}

interface NetworkScanProgressEvent {
  scanned_hosts: number;
  total_hosts: number;
  found_devices: number;
  subnet_label: string;
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
    topic_root: string;
  };
  link_state?: string;
  relay_state?: number;
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

type RegionCode = 'ZA' | 'EU' | 'US';

const SAVED_NETWORK_PASSWORDS_KEY = 'lrs_flasher_network_passwords';
const READABLE_KEY_CONSONANTS = 'bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS = 'aeiou';

const ports = ref<SerialPort[]>([]);
const selectedPort = ref('');
const firmwareVersions = ref<string[]>(['__local_browse__']);
const selectedVersion = ref('');
const selectedLocalPath = ref('');
const region = ref<RegionCode>('ZA');
const isFlashing = ref(false);
const isMonitoring = ref(false);
const isNetworkDiscovering = ref(false);
const isNetworkOta = ref(false);
const isNetworkUdpMonitoring = ref(false);
const serialLogs = ref<string[]>([]);
const networkLogs = ref<string[]>([]);
const pairLogs = ref<string[]>([]);
const serialUptimeMs = ref<number | null>(null);
const networkUptimeMs = ref<number | null>(null);
const deviceInfo = ref<DeviceInfo | null>(null);
const deviceInfoPort = ref('');
const deviceInfoByPort = ref<Record<string, DeviceInfo>>({});
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
const networkDevices = ref<NetworkDevice[]>([]);
const networkSubnets = ref<LanSubnet[]>([]);
const scannedNetworkHosts = ref(0);
const selectedNetworkIp = ref('');
const networkPasswords = ref<Record<string, string>>({});
const savedNetworkPasswords = ref<Record<string, string>>(loadSavedNetworkPasswords());
const networkStatusMessage = ref('Ready to scan the current LAN.');
const networkUdpTarget = ref('');
const networkScanProgress = ref<NetworkScanProgressEvent | null>(null);
const networkPasswordCheckSeq = ref<Record<string, number>>({});
const pairExpectedCount = ref(12);
const pairPanelTab = ref<'pair' | 'wifi'>('pair');
const pairFleetKey = ref('');
const pairAdminPassword = ref('');
const showPairFleetKey = ref(false);
const showPairAdminPassword = ref(false);
const pairStatus = ref<EasyPairStatus | null>(null);
const isPairBusy = ref(false);
const isGatewayLoading = ref(false);
const gatewayLoadedPort = ref('');
const serialAdminSupportedPorts = ref<Record<string, boolean>>({});
const pairStatusPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const wifiNetworks = ref<WifiNetwork[]>([]);
const pairWifiSsid = ref('');
const pairWifiPassword = ref('');
const showPairWifiPassword = ref(false);
const isWifiScanning = ref(false);
const isWifiApplying = ref(false);
const isFleetWifiSending = ref(false);
const isIdentifying = ref(false);
const identifyTimer = ref<ReturnType<typeof window.setTimeout> | null>(null);
const serialAdminStatus = ref<SerialAdminStatus | null>(null);
const serialAdminConfig = ref<SerialAdminConfig | null>(null);
const isSerialAdminLoading = ref(false);
const isSerialAdminSaving = ref(false);
const isSerialSystemAction = ref(false);
const showSerialWifiPassword = ref(false);
const showSerialMqttPassword = ref(false);
const serialFactoryKeepFleet = ref(true);
const serialFactoryKeepWifi = ref(true);

const LOCAL_OPTION = '__local_browse__';
const NETWORK_UDP_LOG_TTL_S = 1800;
const NETWORK_UDP_LOG_RENEW_MS = 5 * 60 * 1000;
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

const hasActiveDeviceInfo = computed(() =>
  !!deviceInfo.value && deviceInfoPort.value === selectedPort.value
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
const selectedNetworkDevice = computed(() =>
  networkDevices.value.find(d => d.ip === selectedNetworkIp.value) || null
);
const activeNetworkDevice = computed(() => {
  if (networkUdpTarget.value) {
    return networkDevices.value.find(d => d.ip === networkUdpTarget.value) || selectedNetworkDevice.value;
  }
  return selectedNetworkDevice.value;
});
const activeNetworkPassword = computed(() =>
  activeNetworkDevice.value ? passwordForNetworkDevice(activeNetworkDevice.value) : ''
);
const networkSubnetLabel = computed(() => {
  if (networkSubnets.value.length === 0) return 'No LAN subnet detected yet';
  return networkSubnets.value
    .map(s => `${s.interface_name} ${s.network}/${s.cidr}`)
    .join(', ');
});
const networkLogActive = computed(() => activeMode.value === 'network' && isNetworkUdpMonitoring.value);
const gatewayReady = computed(() => !!selectedPort.value && gatewayLoadedPort.value === selectedPort.value && !!pairAdminPassword.value.trim());
const pairPrimaryDisabled = computed(() => isPairBusy.value || !selectedPort.value);
const pairControlsDisabled = computed(() => isPairBusy.value || isGatewayLoading.value || !gatewayReady.value);
const identifyAvailable = computed(() => hasActiveDeviceInfo.value && !!serialAdminSupportedPorts.value[selectedPort.value]);
const identifyDisabled = computed(() => !selectedPort.value || !identifyAvailable.value || isFlashing.value || isLoadingInfo.value || isGatewayLoading.value || isPairBusy.value || isMonitoring.value);
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
const serialAdminAvailable = computed(() => hasActiveDeviceInfo.value && !!serialAdminSupportedPorts.value[selectedPort.value]);
const serialAdminPassword = computed(() => deviceInfo.value?.password?.trim() || '');
const serialAdminBusy = computed(() => isSerialAdminLoading.value || isSerialAdminSaving.value || isSerialSystemAction.value);
const serialAdminDisabled = computed(() => !selectedPort.value || !hasActiveDeviceInfo.value || isFlashing.value || isMonitoring.value || isLoadingInfo.value || serialAdminBusy.value);
const serialStatusSummary = computed(() => {
  const st = serialAdminStatus.value;
  if (!st) return 'Load local status to inspect firmware health.';
  const wifi = st.wifi?.sta_connected ? `WiFi ${st.wifi.ip || 'connected'}` : `WiFi ${st.wifi?.status || 'offline'}`;
  return `${st.role || 'unknown'} ${st.local_address}->${st.remote_address} · ${wifi} · heap ${formatBytes(st.heap_free)} free`;
});
const activityBusy = computed(() => isMonitoring.value || isFlashing.value || isNetworkDiscovering.value || isNetworkOta.value || isNetworkUdpMonitoring.value || isPairBusy.value || isGatewayLoading.value || isWifiScanning.value || isWifiApplying.value || isFleetWifiSending.value || isIdentifying.value || serialAdminBusy.value);
const activityFullscreen = computed(() =>
  (activeMode.value === 'serial' && isMonitoring.value) ||
  (activeMode.value === 'network' && isNetworkUdpMonitoring.value)
);
const serialUptimeLabel = computed(() => formatUptime(serialUptimeMs.value));
const networkUptimeLabel = computed(() => formatUptime(networkUptimeMs.value));

let unlistenFlash: UnlistenFn | null = null;
let unlistenMonitor: UnlistenFn | null = null;
let unlistenPortsChanged: UnlistenFn | null = null;
let unlistenNetworkMonitor: UnlistenFn | null = null;
let unlistenNetworkScanProgress: UnlistenFn | null = null;
let unlistenNetworkDeviceFound: UnlistenFn | null = null;
let networkUdpRenewalTimer: ReturnType<typeof window.setInterval> | null = null;
const networkPasswordCheckTimers: Record<string, ReturnType<typeof setTimeout>> = {};

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
    const selectedExists = currentNames.includes(selectedPort.value);
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
    for (const known of Object.keys(deviceInfoByPort.value)) {
      if (!currentNames.includes(known)) {
        delete deviceInfoByPort.value[known];
      }
    }
    for (const known of Object.keys(serialAdminSupportedPorts.value)) {
      if (!currentNames.includes(known)) {
        delete serialAdminSupportedPorts.value[known];
      }
    }

    if (currentNames.length === 0) {
      selectedPort.value = '';
    } else if (!selectedPort.value) {
      selectedPort.value = chooseDefaultPort(currentNames);
    } else if (newPorts.length > 0 && !hasActiveOperation) {
      // Cross-platform policy: only auto-switch when a new device appears and no operation is active.
      // Prefer the most recently connected device.
      selectedPort.value = chooseMostRecentPort(newPorts) ?? chooseDefaultPort(currentNames);
    } else if (!selectedExists) {
      // Selected port vanished (device removed/reset); choose a valid fallback.
      if (!hasActiveOperation) {
        selectedPort.value = chooseDefaultPort(currentNames);
      }
    }

    lastPortSnapshot.value = currentNames;
    syncDeviceInfoForSelectedPort();
    // Artificial delay to ensure the spin is satisfyingly visible
    await new Promise(resolve => setTimeout(resolve, 300));
  } finally {
    isRefreshingPorts.value = false;
  }
}

function chooseMostRecentPort(candidates: string[]): string | null {
  const ranked = candidates
    .map(name => ({ name, seq: portSeenSequence.value[name] ?? -1 }))
    .sort((a, b) => b.seq - a.seq);
  return ranked.length > 0 ? ranked[0].name : null;
}

function chooseDefaultPort(portNames: string[]): string {
  // Backend already returns score-sorted ports; default to top-ranked candidate.
  return portNames[0];
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
  const port = selectedPort.value;
  if (!port) {
    deviceInfo.value = null;
    deviceInfoPort.value = '';
    return;
  }
  const cached = deviceInfoByPort.value[port];
  if (cached) {
    deviceInfo.value = cached;
    deviceInfoPort.value = port;
    if (activeMode.value === 'pair' && !pairAdminPassword.value.trim()) {
      pairAdminPassword.value = cached.password || '';
    }
  } else if (deviceInfoPort.value !== port) {
    deviceInfo.value = null;
    deviceInfoPort.value = '';
  }
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

function passwordForNetworkDevice(device: NetworkDevice): string {
  const stored = networkPasswords.value[device.ip];
  return stored !== undefined ? stored : device.derived_password;
}

function networkDeviceUsesDerivedPassword(device: NetworkDevice): boolean {
  const current = passwordForNetworkDevice(device).trim();
  return !!device.derived_password && current === device.derived_password;
}

function copyNetworkPassword(device: NetworkDevice | null) {
  if (!device) {
    notify('Select a network device first');
    return;
  }
  const password = passwordForNetworkDevice(device).trim();
  if (!password) {
    notify('Enter an admin password first');
    return;
  }
  copyToClipboard(password, 'network password');
}

function copyNetworkIdentity(device: NetworkDevice) {
  copyToClipboard(device.identity || device.ip, 'device name');
}

function loadSavedNetworkPasswords(): Record<string, string> {
  try {
    const raw = localStorage.getItem(SAVED_NETWORK_PASSWORDS_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw);
    return parsed && typeof parsed === 'object' ? parsed as Record<string, string> : {};
  } catch {
    return {};
  }
}

function saveNetworkPasswordStore() {
  try {
    localStorage.setItem(SAVED_NETWORK_PASSWORDS_KEY, JSON.stringify(savedNetworkPasswords.value));
  } catch {
    // Ignore storage failures; the current row password still works in memory.
  }
}

function networkPasswordKeys(device: NetworkDevice): string[] {
  const keys = new Set<string>();
  const identity = (device.identity || '').trim().toLowerCase();
  const chipId = (device.chip_id || '').trim().toLowerCase();
  if (identity && identity !== 'lrs device') keys.add(identity);
  if (chipId) keys.add(chipId.startsWith('lrs-') ? chipId : `lrs-${chipId.padStart(8, '0')}`);
  return [...keys];
}

function savedPasswordForNetworkDevice(device: NetworkDevice): string {
  for (const key of networkPasswordKeys(device)) {
    const saved = savedNetworkPasswords.value[key];
    if (saved) return saved;
  }
  return '';
}

function rememberNetworkPassword(device: NetworkDevice, password: string) {
  const trimmed = password.trim();
  if (!trimmed || (device.derived_password && trimmed === device.derived_password)) {
    return;
  }
  const keys = networkPasswordKeys(device);
  if (keys.length === 0) return;
  const next = { ...savedNetworkPasswords.value };
  for (const key of keys) {
    next[key] = trimmed;
  }
  savedNetworkPasswords.value = next;
  saveNetworkPasswordStore();
}

function setNetworkPassword(ip: string, value: string, verify = true) {
  networkPasswords.value = { ...networkPasswords.value, [ip]: value };
  if (verify) {
    scheduleNetworkPasswordCheck(ip, value);
  }
}

function updateNetworkDevice(ip: string, patch: Partial<NetworkDevice>) {
  const idx = networkDevices.value.findIndex(d => d.ip === ip);
  if (idx < 0) return;
  const next = [...networkDevices.value];
  next[idx] = { ...next[idx], ...patch };
  networkDevices.value = next;
}

function scheduleNetworkPasswordCheck(ip: string, value: string) {
  if (networkPasswordCheckTimers[ip]) {
    clearTimeout(networkPasswordCheckTimers[ip]);
    delete networkPasswordCheckTimers[ip];
  }

  const password = value.trim();
  const seq = (networkPasswordCheckSeq.value[ip] || 0) + 1;
  networkPasswordCheckSeq.value = { ...networkPasswordCheckSeq.value, [ip]: seq };

  if (!password) {
    updateNetworkDevice(ip, {
      auth_status: 'auth_needed',
      message: 'Enter admin password for version and OTA.',
    });
    return;
  }

  updateNetworkDevice(ip, {
    auth_status: 'auth_testing',
    message: 'Testing password...',
  });

  networkPasswordCheckTimers[ip] = setTimeout(() => {
    verifyNetworkPassword(ip, password, seq);
  }, 2500);
}

function clearNetworkPasswordChecks() {
  for (const timer of Object.values(networkPasswordCheckTimers)) {
    clearTimeout(timer);
  }
  for (const ip of Object.keys(networkPasswordCheckTimers)) {
    delete networkPasswordCheckTimers[ip];
  }
  networkPasswordCheckSeq.value = {};
}

async function verifyNetworkPassword(ip: string, password: string, seq: number) {
  try {
    const updated = await invoke<NetworkDevice>('authenticate_network_device', { ip, password });
    if (networkPasswordCheckSeq.value[ip] !== seq || (networkPasswords.value[ip] || '').trim() !== password) {
      return;
    }
    rememberNetworkPassword(updated, password);
    mergeNetworkDevice(updated);
  } catch (e) {
    if (networkPasswordCheckSeq.value[ip] !== seq) {
      return;
    }
    updateNetworkDevice(ip, {
      auth_status: 'auth_error',
      message: String(e),
    });
  } finally {
    if (networkPasswordCheckSeq.value[ip] === seq) {
      delete networkPasswordCheckTimers[ip];
    }
  }
}

function mergeNetworkDevice(updated: NetworkDevice) {
  const idx = networkDevices.value.findIndex(d => d.ip === updated.ip);
  if (idx >= 0) {
    const next = [...networkDevices.value];
    next[idx] = { ...next[idx], ...updated };
    networkDevices.value = next;
  } else {
    networkDevices.value = [...networkDevices.value, updated].sort((a, b) => ipSortKey(a.ip) - ipSortKey(b.ip));
  }
  if (!networkPasswords.value[updated.ip]) {
    const saved = savedPasswordForNetworkDevice(updated);
    const preferred = saved || updated.derived_password || '';
    setNetworkPassword(updated.ip, preferred, false);
    if (saved && saved !== updated.derived_password) {
      scheduleNetworkPasswordCheck(updated.ip, saved);
    }
  }
  if (!selectedNetworkIp.value) {
    selectedNetworkIp.value = updated.ip;
  }
}

function ipSortKey(ip: string): number {
  return ip.split('.').reduce((acc, part) => (acc * 256) + Number(part || 0), 0);
}

function networkAuthLabel(status: string): string {
  if (status === 'derived_ok') return 'Password OK';
  if (status === 'custom_ok') return 'Password OK';
  if (status === 'auth_testing') return 'Test';
  if (status === 'auth_error') return 'Auth error';
  return 'Password needed';
}

function networkAuthClass(status: string): string {
  if (status === 'derived_ok' || status === 'custom_ok') return 'text-emerald-300 border-emerald-500/30 bg-emerald-500/10';
  if (status === 'auth_testing') return 'text-indigo-300 border-indigo-500/30 bg-indigo-500/10';
  if (status === 'auth_error') return 'text-amber-300 border-amber-500/30 bg-amber-500/10';
  return 'text-slate-300 border-slate-700 bg-slate-800/50';
}

function networkVersionTag(device: NetworkDevice): string {
  return device.fw_version || (device.fw_display.match(/^[^\s(]+/)?.[0] ?? '');
}

function networkVersionMeta(device: NetworkDevice): string {
  const display = device.fw_display || '';
  const match = display.match(/\(([^)]+)\)/);
  return match?.[1] || '';
}

function networkDeviceModeLabel(device: NetworkDevice): string {
  return [device.mode, device.role].filter(Boolean).join(' ') || '-';
}

async function discoverNetworkDevices() {
  if (isNetworkDiscovering.value) return;
  activeMode.value = 'network';
  isNetworkDiscovering.value = true;
  networkScanProgress.value = null;
  clearNetworkPasswordChecks();
  networkDevices.value = [];
  selectedNetworkIp.value = '';
  networkStatusMessage.value = 'Scanning current LAN adapters...';
  pushNetworkLog('--- Network discovery ---');
  try {
    const result = await invoke<NetworkDiscoveryResult>('discover_network_devices');
    networkSubnets.value = result.subnets || [];
    scannedNetworkHosts.value = result.scanned_hosts || 0;
    networkDevices.value = (result.devices || []).sort((a, b) => ipSortKey(a.ip) - ipSortKey(b.ip));
    const nextPasswords: Record<string, string> = { ...networkPasswords.value };
    for (const device of networkDevices.value) {
      const saved = savedPasswordForNetworkDevice(device);
      const preferred = saved || device.derived_password || '';
      if (!nextPasswords[device.ip]) nextPasswords[device.ip] = preferred;
      if (saved && saved !== device.derived_password) {
        scheduleNetworkPasswordCheck(device.ip, saved);
      }
    }
    networkPasswords.value = nextPasswords;
    if (!selectedNetworkIp.value && networkDevices.value.length > 0) {
      selectedNetworkIp.value = networkDevices.value[0].ip;
    }
    const seconds = (Number(result.duration_ms || 0) / 1000).toFixed(1);
    networkStatusMessage.value = `Found ${networkDevices.value.length} device${networkDevices.value.length === 1 ? '' : 's'} across ${scannedNetworkHosts.value} hosts in ${seconds}s.`;
    pushNetworkLog(`${networkStatusMessage.value} ${networkSubnetLabel.value}`);
  } catch (e) {
    networkStatusMessage.value = 'Network discovery failed: ' + e;
    pushNetworkLog(networkStatusMessage.value);
    notify(networkStatusMessage.value);
  } finally {
    isNetworkDiscovering.value = false;
  }
}

async function openNetworkDeviceConsole(device: NetworkDevice | null) {
  if (!device) return;
  selectedNetworkIp.value = device.ip;
  const url = `http://${device.ip}`;
  try {
    await openUrl(url);
    pushNetworkLog(`Opened ${url}`);
  } catch (e) {
    notify('Failed to open device URL: ' + e);
  }
}

async function startNetworkUdpMonitor(device: NetworkDevice | null) {
  if (!device) return;
  clearNetworkUdpRenewal();
  selectedNetworkIp.value = device.ip;
  const password = passwordForNetworkDevice(device).trim();
  if (!password) {
    notify('Enter an admin password for this device');
    return;
  }
  try {
    const started = await invoke<string>('start_network_udp_monitor');
    pushNetworkLog(started);
    const enabled = await enableNetworkUdpLoggingLease(device.ip, password);
    isNetworkUdpMonitoring.value = true;
    networkUdpTarget.value = device.ip;
    pushNetworkLog(enabled);
    pushNetworkLog(`Flasher will renew UDP logging every ${Math.round(NETWORK_UDP_LOG_RENEW_MS / 60000)} minutes while monitoring.`);
    startNetworkUdpRenewal();
  } catch (e) {
    pushNetworkLog('UDP monitor error: ' + e);
    notify('UDP monitor error: ' + e);
    clearNetworkUdpRenewal();
  }
}

async function stopNetworkUdpMonitor() {
  clearNetworkUdpRenewal();
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

async function enableNetworkUdpLoggingLease(ip: string, password: string): Promise<string> {
  return await invoke<string>('enable_network_udp_logging', {
    ip,
    options: { password, ttl_s: NETWORK_UDP_LOG_TTL_S }
  });
}

function startNetworkUdpRenewal() {
  clearNetworkUdpRenewal();
  networkUdpRenewalTimer = window.setInterval(async () => {
    if (!isNetworkUdpMonitoring.value || !networkUdpTarget.value) {
      clearNetworkUdpRenewal();
      return;
    }
    const device = networkDevices.value.find(d => d.ip === networkUdpTarget.value) || activeNetworkDevice.value;
    const password = device ? passwordForNetworkDevice(device).trim() : '';
    if (!password) {
      pushNetworkLog('UDP logging renewal skipped: missing admin password');
      return;
    }
    try {
      await enableNetworkUdpLoggingLease(networkUdpTarget.value, password);
    } catch (e) {
      pushNetworkLog('UDP logging renewal failed: ' + e);
    }
  }, NETWORK_UDP_LOG_RENEW_MS);
}

function clearNetworkUdpRenewal() {
  if (networkUdpRenewalTimer) {
    window.clearInterval(networkUdpRenewalTimer);
    networkUdpRenewalTimer = null;
  }
}

async function startNetworkOtaForDevice(device: NetworkDevice | null) {
  if (!device || !selectedVersion.value) return;
  selectedNetworkIp.value = device.ip;
  const password = passwordForNetworkDevice(device).trim();
  if (!password) {
    notify('Enter an admin password for this device');
    return;
  }
  const isLocal = selectedVersion.value.startsWith('Local: ');
  const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
  if (isLocal && !firmwarePath) {
    notify('Local file path missing');
    return;
  }

  isNetworkOta.value = true;
  pushNetworkLog(`--- Network OTA ${device.identity || device.ip} (${device.ip}) ---`);
  try {
    const result = await invoke<string>('ota_network_device', {
      ip: device.ip,
      options: {
        password,
        firmware_path: firmwarePath,
        region: isLocal ? null : region.value
      }
    });
    pushNetworkLog(result);
    pushNetworkLog('OTA upload complete. Waiting for reboot and WiFi reconnect before UDP logging...');
    await waitForNetworkDevice(device.ip, password, 90000);
    await startNetworkUdpMonitor(device);
  } catch (e) {
    pushNetworkLog('Network OTA failed: ' + e);
    notify('Network OTA failed: ' + e);
  } finally {
    isNetworkOta.value = false;
  }
}

async function waitForNetworkDevice(ip: string, password: string, timeoutMs: number) {
  const started = Date.now();
  await new Promise(resolve => setTimeout(resolve, 7000));
  while (Date.now() - started < timeoutMs) {
    try {
      const updated = await invoke<NetworkDevice>('authenticate_network_device', { ip, password });
      mergeNetworkDevice(updated);
      pushNetworkLog(`${updated.identity || ip} is back online.`);
      return;
    } catch {
      pushNetworkLog(`Waiting for ${ip} to return...`);
      await new Promise(resolve => setTimeout(resolve, 5000));
    }
  }
  throw new Error('Device did not return before the 90s wait expired');
}

function pairPassword(): string {
  return pairAdminPassword.value.trim() || (hasActiveDeviceInfo.value ? deviceInfo.value?.password?.trim() || '' : '');
}

async function sendEasyPairCommand<T = any>(cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000): Promise<T> {
  if (!selectedPort.value) throw new Error('Select the USB gateway first');
  return await invoke<T>('serial_admin_command', {
    port: selectedPort.value,
    request: { cmd, ...payload },
    timeoutMs
  });
}

async function probeSerialAdminSupport(port = selectedPort.value): Promise<boolean> {
  if (!port) return false;
  try {
    await invoke<any>('serial_admin_command', {
      port,
      request: { cmd: 'hello' },
      timeoutMs: 1200
    });
    serialAdminSupportedPorts.value[port] = true;
    return true;
  } catch {
    delete serialAdminSupportedPorts.value[port];
    return false;
  }
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
    if (!serialAdminSupportedPorts.value[selectedPort.value]) {
      await probeSerialAdminSupport(selectedPort.value);
    }
    const out = await sendEasyPairCommand<SerialAdminStatus>('status', {}, 5000);
    serialAdminStatus.value = out;
    serialUptimeMs.value = Number(out.uptime_ms || 0);
    pushSerialLog(`Status loaded: ${out.role || 'unknown'} ${out.local_address}->${out.remote_address}, heap ${formatBytes(out.heap_free)} free.`);
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
    if (!serialAdminSupportedPorts.value[selectedPort.value]) {
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
  isGatewayLoading.value = true;
  gatewayLoadedPort.value = '';
  pushPairLog('Reading USB gateway identity...');
  try {
    const ok = await readDeviceInfo();
    if (!ok || !deviceInfo.value) throw new Error('Unable to read gateway factory details');
    if (selectedPort.value !== port) return;
    pairAdminPassword.value = deviceInfo.value.password || '';
    const hello = await sendEasyPairCommand<any>('hello', {}, 4000);
    if (selectedPort.value !== port) return;
    serialAdminSupportedPorts.value[port] = true;
    gatewayLoadedPort.value = port;
    pushPairLog(`Gateway ready on ${selectedPort.value}; firmware ${hello.fw_version || 'unknown'}, max remotes ${hello.max_remotes || 12}.`);
  } catch (e) {
    if (selectedPort.value === port) {
      pairAdminPassword.value = '';
      gatewayLoadedPort.value = '';
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
  const addresses = (pairStatus.value?.devices || [])
    .filter(d =>
      d.selected &&
      !d.address_conflict &&
      d.state === 'verified' &&
      d.assigned_address > 0 &&
      d.assigned_address < 255
    )
    .map(d => d.assigned_address);
  if (!password || addresses.length === 0) {
    notify('No provisioned target addresses to save yet');
    return;
  }
  isPairBusy.value = true;
  pushPairLog(`Saving gateway target list: ${addresses.join(', ')}`);
  try {
    await sendEasyPairCommand('set_gateway_targets', { admin_password: password, addresses }, 10000);
    pushPairLog('Pairing complete.');
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
    if (!pairWifiSsid.value && networks.length > 0) {
      pairWifiSsid.value = networks[0].ssid;
    }
    pushPairLog(`Found ${networks.length} WiFi network${networks.length === 1 ? '' : 's'}.`);
  } catch (e) {
    const msg = serialFeatureError('WiFi scan', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    isWifiScanning.value = false;
  }
}

async function connectGatewayWifi() {
  const password = pairPassword();
  const ssid = pairWifiSsid.value.trim();
  if (!password || !ssid) {
    notify('Select a WiFi network and load the gateway password first');
    return;
  }
  isWifiApplying.value = true;
  pushPairLog(`Saving WiFi credentials on gateway for ${ssid}...`);
  try {
    await sendEasyPairCommand('configure_wifi', {
      admin_password: password,
      wifi_sta_ssid: ssid,
      wifi_sta_password: pairWifiPassword.value
    }, 10000);
    pushPairLog('Gateway WiFi saved. It will connect as normal firmware networking runs.');
  } catch (e) {
    const msg = serialFeatureError('WiFi save', e);
    pushPairLog(msg);
    notify(msg);
  } finally {
    isWifiApplying.value = false;
  }
}

async function sendWifiToRemotes() {
  const password = pairPassword();
  const ssid = pairWifiSsid.value.trim();
  if (!password || !ssid) {
    notify('Select a WiFi network and load the gateway password first');
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
  syncDeviceInfoForSelectedPort();
  pushSerialLog(`Reading device information from ${port}...`);
  try {
    const info = await invoke<DeviceInfo>('get_device_info', { port });
    if (deviceInfoReadSeq.value !== seq || selectedPort.value !== port) {
      pushSerialLog(`Ignored stale device info from ${port}`);
      return false;
    }
    deviceInfoByPort.value[port] = info;
    deviceInfo.value = info;
    deviceInfoPort.value = port;
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

watch(activeMode, (mode, previousMode) => {
  nextTick(() => scrollToBottom());
  if (previousMode === 'serial' && mode !== 'serial') {
    stopSerialMonitorForModeChange();
  }
  syncDeviceInfoForSelectedPort();
  if (mode === 'serial' && selectedPort.value && !hasActiveDeviceInfo.value) {
    readDeviceInfo();
  }
});

watch(selectedPort, (port) => {
  deviceInfoReadSeq.value += 1;
  isLoadingInfo.value = false;
  pairStatus.value = null;
  wifiNetworks.value = [];
  pairWifiSsid.value = '';
  gatewayLoadedPort.value = '';
  pairAdminPassword.value = deviceInfoByPort.value[port]?.password || '';
  syncDeviceInfoForSelectedPort();
  if (port && activeMode.value === 'serial') {
    readDeviceInfo();
  }
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

  unlistenNetworkScanProgress = await listen<NetworkScanProgressEvent>('network-scan-progress', (event) => {
    networkScanProgress.value = event.payload;
    const scanned = Number(event.payload.scanned_hosts || 0);
    const total = Number(event.payload.total_hosts || 0);
    const found = Number(event.payload.found_devices || 0);
    networkStatusMessage.value = `Scanning ${event.payload.subnet_label}: ${scanned}/${total} hosts, ${found} found.`;
  });

  unlistenNetworkDeviceFound = await listen<NetworkDevice>('network-device-found', (event) => {
    mergeNetworkDevice(event.payload);
    pushNetworkLog(`Found ${event.payload.identity || event.payload.ip} at ${event.payload.ip}`);
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

onUnmounted(() => {
  stopEasyPairStatusPolling();
  if (identifyTimer.value) window.clearTimeout(identifyTimer.value);
  clearNetworkPasswordChecks();
  clearNetworkUdpRenewal();
  if (unlistenFlash) unlistenFlash();
  if (unlistenMonitor) unlistenMonitor();
  if (unlistenNetworkMonitor) unlistenNetworkMonitor();
  if (unlistenNetworkScanProgress) unlistenNetworkScanProgress();
  if (unlistenNetworkDeviceFound) unlistenNetworkDeviceFound();
  if (unlistenPortsChanged) unlistenPortsChanged();
  if (isNetworkUdpMonitoring.value) {
    invoke('stop_network_udp_monitor').catch(() => {});
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
    <div :class="['grid gap-8 flex-1 min-h-0 transition-all duration-500', activityFullscreen ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
      <!-- Log Panel -->
      <div :class="['glass-card p-6 flex flex-col gap-4 text-left overflow-hidden h-full']">
        <div class="flex items-center justify-between border-b border-white/5 pb-4">
          <div class="flex flex-col gap-1">
            <h2 class="text-lg font-semibold text-slate-300 flex items-center gap-2">
              <span :class="['w-2 h-2 rounded-full', activityBusy ? 'bg-indigo-500 animate-pulse' : 'bg-slate-600']"></span>
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
            <div
              v-if="networkLogActive"
              class="flex items-center gap-1 pl-4 text-xs text-slate-500"
            >
              <span>UDP logs</span>
              <span class="font-mono text-slate-300">{{ networkUdpTarget }}</span>
              <span class="text-slate-500">on</span>
              <span class="font-mono text-slate-400">5514</span>
              <template v-if="networkUptimeLabel">
                <span class="text-slate-600">·</span>
                <span>Uptime</span>
                <span class="font-mono text-slate-400">{{ networkUptimeLabel }}</span>
              </template>
            </div>
          </div>
          <div class="flex items-center gap-4">
            <div
              v-if="crashCount > 0"
              class="flex h-10 items-center gap-2 rounded-md border border-amber-500/40 bg-amber-500/10 px-3 text-xs font-semibold text-amber-300"
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
                'p-1.5 rounded-md border transition-all',
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
                'p-1.5 rounded-md border transition-all',
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
              v-if="activeMode === 'network' && isNetworkUdpMonitoring"
              @click="copyNetworkPassword(activeNetworkDevice)"
              :disabled="!activeNetworkPassword"
              :class="[
                'p-1.5 rounded-md border transition-all',
                activeNetworkPassword
                  ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                  : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
              ]"
              title="Copy network device password"
              aria-label="Copy network device password"
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
                'p-1.5 rounded-md border transition-all',
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
              v-if="activeMode === 'network' && isNetworkUdpMonitoring"
              @click="openNetworkDeviceConsole(activeNetworkDevice)"
              :disabled="!activeNetworkDevice"
              :class="[
                'p-1.5 rounded-md border transition-all',
                activeNetworkDevice
                  ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                  : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
              ]"
              title="Open network device web console"
              aria-label="Open network device web console"
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
                'rounded-md border transition-all',
                isMonitoring
                  ? 'p-1.5 border-indigo-500 bg-indigo-500/20 text-indigo-400 hover:text-indigo-200'
                  : 'p-1.5 border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
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
            <button
              v-if="activeMode === 'network' && isNetworkUdpMonitoring"
              @click="stopNetworkUdpMonitor"
              class="p-1.5 rounded-md border transition-all border-indigo-500 bg-indigo-500/20 text-indigo-400 hover:text-indigo-200"
              title="Stop monitor"
              aria-label="Stop monitor"
            >
              <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" aria-hidden="true">
                <rect x="7" y="7" width="10" height="10" rx="1.5" fill="currentColor"></rect>
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

      <div v-if="activeMode === 'pair'" class="flex flex-col gap-6 h-full overflow-hidden">
        <div class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <div class="flex items-start justify-between gap-4">
            <div>
              <h2 class="text-xl font-bold bg-gradient-to-r from-indigo-400 to-purple-400 bg-clip-text text-transparent">
                Pair Devices
              </h2>
              <p class="mt-1 text-xs text-slate-400">Selected USB device becomes the LoRa gateway.</p>
            </div>
            <div class="flex items-center gap-2">
              <button
                v-if="identifyAvailable && !identifyDisabled"
                @click="triggerIdentify"
                :class="['glass-input m-0 h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all', { 'identify-led-active': isIdentifying }]"
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
                class="glass-input m-0 h-10 px-4 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold"
              >
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-indigo-400': isGatewayLoading }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
                <span>{{ isGatewayLoading ? 'Loading...' : 'Load Gateway' }}</span>
              </button>
            </div>
          </div>

          <div class="grid grid-cols-2 rounded-md border border-slate-800 bg-slate-950/30 p-1 text-xs font-bold">
            <button
              @click="pairPanelTab = 'pair'"
              :class="['m-0 h-9 rounded px-3 transition-all', pairPanelTab === 'pair' ? 'bg-indigo-500 text-white shadow-lg shadow-indigo-500/20' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
            >
              Pair
            </button>
            <button
              @click="pairPanelTab = 'wifi'"
              :class="['m-0 h-9 rounded px-3 transition-all', pairPanelTab === 'wifi' ? 'bg-indigo-500 text-white shadow-lg shadow-indigo-500/20' : 'text-slate-400 hover:text-slate-200 hover:bg-white/5']"
            >
              WiFi
            </button>
          </div>

          <div v-if="pairPanelTab === 'pair'" class="flex flex-col gap-4">
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">USB gateway</label>
              <div class="flex gap-2">
                <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                  <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                    {{ port.port_name }}
                  </option>
                  <option v-if="ports.length === 0" disabled>Scanning...</option>
                </select>
                <button @click="refreshPorts" :disabled="isRefreshingPorts || serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-6 h-6 text-slate-400 group-hover/btn:text-indigo-400 transition-colors', { 'animate-spin text-indigo-500': isRefreshingPorts }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
                </button>
              </div>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Remote count</label>
              <input v-model.number="pairExpectedCount" class="glass-input h-10" type="number" min="1" max="12" />
            </div>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Fleet key</label>
              <div class="grid grid-cols-[3rem_minmax(0,1fr)_3rem_3rem] gap-2">
                <button @click="generatePairFleetKey(true)" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center" title="Generate fleet key" aria-label="Generate fleet key">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
                </button>
                <input v-model="pairFleetKey" class="glass-input h-10 flex-1 font-mono" :type="showPairFleetKey ? 'text' : 'password'" autocomplete="new-password" />
                <button @click="showPairFleetKey = !showPairFleetKey" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center" :title="showPairFleetKey ? 'Hide fleet key' : 'Show fleet key'" :aria-label="showPairFleetKey ? 'Hide fleet key' : 'Show fleet key'">
                  <svg v-if="!showPairFleetKey" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
                <button @click="copyPairFleetKey" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center" title="Copy fleet key" aria-label="Copy fleet key">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">Gateway admin password</label>
              <div class="grid grid-cols-[minmax(0,1fr)_3rem_3rem] gap-2">
                <input v-model="pairAdminPassword" class="glass-input h-10 min-w-0 font-mono" :type="showPairAdminPassword ? 'text' : 'password'" autocomplete="current-password" />
                <button @click="showPairAdminPassword = !showPairAdminPassword" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center" :title="showPairAdminPassword ? 'Hide password' : 'Show password'" :aria-label="showPairAdminPassword ? 'Hide password' : 'Show password'">
                  <svg v-if="!showPairAdminPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
                <button @click="copyPairAdminPassword" :disabled="!pairAdminPassword" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center disabled:opacity-50" title="Copy gateway password" aria-label="Copy gateway password">
                  <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <button @click="runEasyPair" :disabled="pairPrimaryDisabled" class="primary-btn h-12 flex items-center justify-center gap-3 text-sm font-bold">
            <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isPairBusy }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M16 3h5v5"></path><path d="M4 20 21 3"></path><path d="M21 16v5h-5"></path><path d="M15 15l6 6"></path><path d="M4 4l5 5"></path></svg>
            <span>{{ isPairBusy ? 'Pairing...' : 'Pair Devices' }}</span>
          </button>

          <details class="rounded-md border border-slate-800 bg-slate-900/30 px-3 py-2">
            <summary class="cursor-pointer select-none text-xs font-semibold text-slate-500 hover:text-slate-300">Advanced steps</summary>
            <div class="mt-3 grid grid-cols-2 xl:grid-cols-5 gap-3">
              <button @click="configureEasyPairGateway" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold" title="Configure the USB device as gateway">Prepare</button>
              <button @click="startEasyPairDiscovery" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold" title="Discover powered remotes over LoRa">Scan</button>
              <button @click="provisionEasyPairDevices" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold" title="Provision all discovered remotes">Pair All</button>
              <button @click="saveEasyPairTargets" :disabled="pairControlsDisabled" class="glass-input h-10 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold" title="Save discovered remote addresses on the gateway">Finish</button>
              <button @click="cancelEasyPair" :disabled="!gatewayReady" class="glass-input h-10 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold" title="Stop the current discovery or provisioning session">Stop</button>
            </div>
          </details>
          </div>

          <div v-else class="flex flex-col gap-4">
          <div class="flex items-start justify-between gap-4">
            <div>
              <h2 class="text-xl font-bold text-slate-300">WiFi</h2>
              <p class="mt-1 text-xs text-slate-400">Scan from the USB gateway, connect it, then send the same credentials to remotes.</p>
            </div>
            <button
              @click="scanGatewayWifi"
              :disabled="isWifiScanning || !gatewayReady"
              class="glass-input m-0 h-10 px-4 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold"
            >
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-indigo-400': isWifiScanning }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
              <span>{{ isWifiScanning ? 'Scanning...' : 'Scan WiFi' }}</span>
            </button>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">WiFi network</label>
              <select v-model="pairWifiSsid" class="glass-input h-10 appearance-none">
                <option v-for="network in wifiNetworks" :key="`${network.ssid}-${network.bssid}`" :value="network.ssid">
                  {{ network.ssid }} · {{ wifiSignalLabel(network.rssi) }} · ch {{ network.channel }}
                </option>
                <option v-if="wifiNetworks.length === 0" disabled>Scan to choose a network</option>
              </select>
            </div>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-medium text-slate-400">WiFi password</label>
              <div class="flex gap-2">
                <input v-model="pairWifiPassword" class="glass-input h-10 flex-1" :type="showPairWifiPassword ? 'text' : 'password'" autocomplete="new-password" />
                <button @click="showPairWifiPassword = !showPairWifiPassword" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center" :title="showPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'" :aria-label="showPairWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'">
                  <svg v-if="!showPairWifiPassword" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0"></path><circle cx="12" cy="12" r="3"></circle></svg>
                  <svg v-else xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 text-slate-400" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round"><path d="m15 18-.722-3.25"></path><path d="M2 8a10.645 10.645 0 0 0 20 0"></path><path d="m20 15-1.726-2.05"></path><path d="m4 15 1.726-2.05"></path><path d="m9 18 .722-3.25"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <button @click="connectGatewayWifi" :disabled="isWifiApplying || !gatewayReady || !pairWifiSsid" class="glass-input h-11 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold">Connect Gateway</button>
            <button @click="sendWifiToRemotes" :disabled="isFleetWifiSending || !gatewayReady || !pairWifiSsid" class="primary-btn h-11 flex items-center justify-center gap-2 text-xs font-bold">Send to Remotes</button>
          </div>
          </div>
        </div>

        <div class="glass-card p-5 flex flex-col gap-4 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between">
            <h2 class="text-xl font-bold text-slate-300">Discovered devices</h2>
            <button @click="refreshEasyPairStatus(true)" :disabled="isPairBusy || !gatewayReady" class="text-xs text-slate-500 hover:text-indigo-400">Refresh</button>
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
      <div v-if="activeMode === 'serial' && !isMonitoring" class="flex flex-col gap-6 h-full overflow-hidden transition-opacity duration-300" :class="{ 'opacity-0 pointer-events-none': isMonitoring }">
        <!-- Device Configuration Panel -->
        <div class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <div class="flex items-start justify-between gap-4">
            <h2 class="text-xl font-bold bg-gradient-to-r from-indigo-400 to-purple-400 bg-clip-text text-transparent">
              Device configuration
            </h2>
            <button
              v-if="identifyAvailable && !identifyDisabled"
              @click="triggerIdentify"
              :class="['glass-input m-0 h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all', { 'identify-led-active': isIdentifying }]"
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
                <select v-model="selectedPort" :disabled="serialPortSelectorDisabled" class="glass-input h-10 flex-1 appearance-none disabled:opacity-60">
                  <option v-for="port in ports" :key="port.port_name" :value="port.port_name">
                    {{ port.port_name }}
                  </option>
                  <option v-if="ports.length === 0" disabled>Scanning...</option>
                </select>
                <button @click="refreshPorts" :disabled="isRefreshingPorts || serialPortSelectorDisabled" class="glass-input h-10 w-12 hover:bg-white/10 flex items-center justify-center transition-all group/btn shrink-0 disabled:opacity-60">
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
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-indigo-400 transition-colors', { 'animate-bounce text-indigo-500': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
              </button>
            </div>
          </div>

          <div class="grid grid-cols-2 gap-4 mt-2">
            <div class="flex flex-col gap-3">
              <button @click="startFlash" :disabled="flashDisabled" class="primary-btn h-12 flex items-center justify-center gap-3 text-sm tracking-wider font-bold w-full active:scale-95 transition-all disabled:opacity-60 disabled:cursor-not-allowed">
                <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isFlashing }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
                <span>{{ isFlashing ? 'Flashing...' : 'Flash firmware' }}</span>
              </button>
              <div class="flex flex-wrap items-center gap-4 px-1">
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
                    <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-indigo-500 peer-checked:border-indigo-500 transition-all"></div>
                    <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                  </div>
                  <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor when flash complete</span>
                </label>
              </div>
            </div>
            <button @click="readDeviceInfo" :disabled="isFlashing || isLoadingInfo" class="glass-input h-12 hover:bg-white/10 flex items-center justify-center gap-3 text-sm tracking-wider transition-all active:scale-95">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400', { 'animate-spin text-indigo-400': isLoadingInfo }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"></circle><path d="M12 16v-4"></path><path d="M12 8h.01"></path></svg>
              <span>{{ isLoadingInfo ? 'Reading...' : 'Get device info' }}</span>
            </button>
          </div>
        </div>

        <!-- Local Admin Panel -->
        <div class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <div class="flex items-start justify-between gap-4">
            <div>
              <h2 class="text-xl font-bold text-slate-300">Local admin</h2>
              <p class="mt-1 text-xs text-slate-400">{{ serialStatusSummary }}</p>
            </div>
            <div class="flex gap-2">
              <button @click="refreshSerialAdminStatus" :disabled="serialAdminDisabled" class="glass-input m-0 h-10 px-3 hover:bg-white/10 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminLoading ? 'Loading...' : 'Status' }}
              </button>
              <button @click="loadSerialAdminConfig" :disabled="serialAdminDisabled" class="glass-input m-0 h-10 px-3 hover:bg-white/10 text-xs font-bold disabled:opacity-60">
                Load config
              </button>
            </div>
          </div>

          <div v-if="serialAdminStatus" class="grid grid-cols-2 sm:grid-cols-4 gap-2 text-xs">
            <div class="rounded-xl border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Firmware</div>
              <div class="font-mono text-slate-200">{{ serialAdminStatus.fw_version || 'unknown' }}</div>
            </div>
            <div class="rounded-xl border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Uptime</div>
              <div class="font-mono text-slate-200">{{ formatUptime(serialAdminStatus.uptime_ms || 0) }}</div>
            </div>
            <div class="rounded-xl border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">Heap</div>
              <div class="font-mono text-slate-200">{{ formatBytes(serialAdminStatus.heap_free) }}</div>
            </div>
            <div class="rounded-xl border border-white/10 bg-black/15 p-3">
              <div class="text-slate-500">MQTT</div>
              <div class="font-mono text-slate-200">{{ serialAdminStatus.mqtt?.client_enabled ? 'enabled' : 'disabled' }}</div>
            </div>
          </div>

          <div v-if="serialAdminConfig" class="grid grid-cols-1 sm:grid-cols-2 gap-4 text-xs">
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
                <button @click="showSerialWifiPassword = !showSerialWifiPassword" class="glass-input h-10 px-3 hover:bg-white/10">{{ showSerialWifiPassword ? 'Hide' : 'Show' }}</button>
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
                <button @click="showSerialMqttPassword = !showSerialMqttPassword" class="glass-input h-10 px-3 hover:bg-white/10">{{ showSerialMqttPassword ? 'Hide' : 'Show' }}</button>
              </div>
            </div>

            <div class="sm:col-span-2 flex flex-wrap items-center gap-3 pt-1">
              <button @click="saveSerialAdminConfig" :disabled="serialAdminDisabled || isSerialAdminSaving" class="primary-btn h-10 px-5 text-xs font-bold disabled:opacity-60">
                {{ isSerialAdminSaving ? 'Saving...' : 'Save config' }}
              </button>
              <button @click="rebootSerialDevice" :disabled="serialAdminDisabled" class="glass-input h-10 px-4 hover:bg-white/10 text-xs font-bold disabled:opacity-60">
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

          <p v-if="hasActiveDeviceInfo && !serialAdminAvailable" class="text-xs text-amber-300">
            This firmware does not answer the serial admin probe yet. Flash a Phase 1A build, then load status again.
          </p>
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
            <div v-for="[key, val] in orderedDeviceInfoEntries" :key="key" class="group flex items-center justify-between text-xs border-b border-white/5 py-1.5 hover:bg-white/5 px-2 -mx-2 rounded transition-colors">
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

      <div v-if="activeMode === 'network' && !isNetworkUdpMonitoring" class="flex flex-col gap-6 h-full overflow-hidden">
        <div class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <div class="flex items-start justify-between gap-4">
            <div>
              <h2 class="text-xl font-bold bg-gradient-to-r from-indigo-400 to-purple-400 bg-clip-text text-transparent">
                Network OTA
              </h2>
              <p class="mt-1 text-xs text-slate-400">{{ networkSubnetLabel }}</p>
            </div>
            <button
              @click="discoverNetworkDevices"
              :disabled="isNetworkDiscovering || isNetworkOta"
              class="glass-input m-0 h-10 px-4 hover:bg-white/10 flex items-center justify-center gap-2 text-xs font-bold"
            >
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin text-indigo-400': isNetworkDiscovering }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8"></path><path d="M21 3v5h-5"></path></svg>
              <span>{{ isNetworkDiscovering ? 'Scanning...' : 'Scan' }}</span>
            </button>
          </div>

          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
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
                <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input m-0 h-10 w-12 hover:bg-white/10 flex items-center justify-center group/btn shrink-0">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-7 h-7 text-slate-400 group-hover/btn:text-indigo-400 transition-colors', { 'animate-bounce text-indigo-500': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.35" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="text-xs text-slate-400">{{ networkStatusMessage }}</div>
          <div v-if="networkScanProgress" class="h-2 overflow-hidden rounded bg-slate-800">
            <div
              class="h-full bg-indigo-500 transition-all"
              :style="{ width: `${Math.min(100, Math.round((networkScanProgress.scanned_hosts / Math.max(1, networkScanProgress.total_hosts)) * 100))}%` }"
            ></div>
          </div>
        </div>

        <div class="glass-card p-5 flex flex-col gap-4 text-left flex-1 min-h-0 overflow-hidden">
          <div class="flex items-center justify-between">
            <h2 class="text-xl font-bold text-slate-300">Discovered devices</h2>
            <span class="text-xs text-slate-500">{{ networkDevices.length }} found</span>
          </div>

          <div v-if="networkDevices.length === 0" class="h-32 flex items-center justify-center text-slate-600 italic text-sm text-center">
            Scan the current LAN to find LRS devices.
          </div>

          <div v-else class="flex-1 min-h-0 overflow-auto custom-scrollbar pr-1">
            <div
              v-for="device in networkDevices"
              :key="device.ip"
              @click="selectedNetworkIp = device.ip"
              :class="['rounded-md border p-3 mb-3 cursor-pointer transition-all', selectedNetworkIp === device.ip ? 'border-indigo-500/60 bg-indigo-500/10' : 'border-slate-800 bg-slate-900/30 hover:border-slate-600']"
            >
              <div class="flex flex-col gap-3">
                <div class="flex items-start justify-between gap-3">
                  <div class="min-w-0 flex-1">
                    <button
                      @click.stop="copyNetworkIdentity(device)"
                      class="m-0 block max-w-full bg-transparent p-0 text-left font-mono text-sm text-slate-200 shadow-none transition-colors hover:text-indigo-300"
                      title="Copy device name"
                      aria-label="Copy device name"
                    >
                      <span class="block truncate">{{ device.identity || device.ip }}</span>
                    </button>
                    <div class="font-mono text-xs text-slate-500">{{ device.ip }}</div>
                  </div>
                  <div class="hidden min-w-0 flex-1 sm:block">
                    <div class="text-[11px] text-slate-500">Version</div>
                    <div class="font-mono text-xs text-slate-300 truncate">{{ networkVersionTag(device) || '-' }}</div>
                    <div v-if="networkVersionMeta(device)" class="font-mono text-[10px] text-slate-600 truncate">
                      {{ networkVersionMeta(device) }}
                    </div>
                  </div>
                  <div class="hidden min-w-0 flex-1 sm:block">
                    <div class="text-[11px] text-slate-500">Mode</div>
                    <div class="font-mono text-xs text-slate-300 truncate">{{ networkDeviceModeLabel(device) }}</div>
                  </div>
                  <div class="flex shrink-0 items-center gap-2">
                    <span
                      v-if="networkDeviceUsesDerivedPassword(device)"
                      class="rounded border border-cyan-500/30 bg-cyan-500/10 px-2 py-1 text-[10px] font-bold text-cyan-300"
                      title="This row is using the factory-derived admin password"
                    >
                      Factory pwd
                    </span>
                    <span :class="['rounded border px-2 py-1 text-[10px] font-bold', networkAuthClass(device.auth_status)]">
                      {{ networkAuthLabel(device.auth_status) }}
                    </span>
                  </div>
                </div>

                <div class="grid grid-cols-2 gap-2 text-xs sm:hidden">
                  <div>
                    <span class="text-slate-500">Version</span>
                    <div class="font-mono text-slate-300 truncate">{{ networkVersionTag(device) || '-' }}</div>
                    <div v-if="networkVersionMeta(device)" class="font-mono text-[10px] text-slate-600 truncate">
                      {{ networkVersionMeta(device) }}
                    </div>
                  </div>
                  <div>
                    <span class="text-slate-500">Mode</span>
                    <div class="font-mono text-slate-300">{{ networkDeviceModeLabel(device) }}</div>
                  </div>
                </div>

                <div class="grid grid-cols-1 xl:grid-cols-[minmax(13rem,1fr)_auto] gap-3 items-end">
                  <div class="flex flex-col gap-1.5 text-xs">
                    <label class="font-medium text-slate-400">Admin password</label>
                    <input
                      :value="passwordForNetworkDevice(device)"
                      @click.stop
                      @input="setNetworkPassword(device.ip, ($event.target as HTMLInputElement).value)"
                      class="glass-input h-10"
                      type="password"
                      autocomplete="current-password"
                    />
                  </div>
                  <div class="flex flex-wrap items-center gap-3 self-end">
                    <button
                      @click.stop="openNetworkDeviceConsole(device)"
                      class="m-0 p-1.5 rounded-md border transition-all border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500"
                      title="Open Web UI"
                      aria-label="Open Web UI"
                    >
                      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <circle cx="12" cy="12" r="9"></circle>
                        <path d="M3 12h18"></path>
                        <path d="M12 3a14 14 0 0 1 0 18"></path>
                        <path d="M12 3a14 14 0 0 0 0 18"></path>
                      </svg>
                    </button>
                    <button
                      @click.stop="copyNetworkPassword(device)"
                      :disabled="!passwordForNetworkDevice(device)"
                      :class="[
                        'm-0 p-1.5 rounded-md border transition-all',
                        passwordForNetworkDevice(device)
                          ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                          : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
                      ]"
                      title="Copy admin password"
                      aria-label="Copy admin password"
                    >
                      <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <circle cx="7.5" cy="14.5" r="3.5"></circle>
                        <path d="M10 12 20 2"></path>
                        <path d="m16 6 2 2"></path>
                        <path d="m14 8 2 2"></path>
                      </svg>
                    </button>
                    <button
                      @click.stop="isNetworkUdpMonitoring && networkUdpTarget === device.ip ? stopNetworkUdpMonitor() : startNetworkUdpMonitor(device)"
                      :disabled="isNetworkOta"
                      :class="[
                        'm-0 p-1.5 rounded-md border transition-all',
                        !isNetworkOta
                          ? 'border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500'
                          : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
                      ]"
                      :title="isNetworkUdpMonitoring && networkUdpTarget === device.ip ? 'Stop monitor' : 'Start monitor'"
                      :aria-label="isNetworkUdpMonitoring && networkUdpTarget === device.ip ? 'Stop monitor' : 'Start monitor'"
                    >
                      <svg v-if="isNetworkUdpMonitoring && networkUdpTarget === device.ip" xmlns="http://www.w3.org/2000/svg" class="w-5 h-5" viewBox="0 0 24 24" fill="none" aria-hidden="true">
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
                    <button
                      @click.stop="startNetworkOtaForDevice(device)"
                      :disabled="isNetworkOta || isNetworkDiscovering"
                      :class="[
                        'm-0 p-1.5 rounded-md border transition-all',
                        !(isNetworkOta || isNetworkDiscovering)
                          ? 'border-indigo-500 bg-indigo-500/20 text-indigo-300 hover:text-indigo-100 hover:border-indigo-400'
                          : 'border-slate-800 text-slate-600 opacity-50 cursor-not-allowed'
                      ]"
                      title="Update firmware over OTA"
                      aria-label="Update firmware over OTA"
                    >
                      <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5', { 'animate-spin': isNetworkOta && selectedNetworkIp === device.ip }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                        <path d="M4 16.899A7 7 0 1 1 15.71 10h1.79a4.5 4.5 0 0 1 2.5 8.242"></path>
                        <path d="M12 19V8"></path>
                        <path d="m8 12 4-4 4 4"></path>
                      </svg>
                    </button>
                  </div>
                </div>
              </div>
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
    color: rgb(129 140 248);
    filter: drop-shadow(0 0 8px rgba(129, 140, 248, 0.9));
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
