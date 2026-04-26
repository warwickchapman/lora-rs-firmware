<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { open } from '@tauri-apps/plugin-dialog';
import { openUrl } from '@tauri-apps/plugin-opener';

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
}

type RegionCode = 'ZA' | 'EU' | 'US';

const ports = ref<SerialPort[]>([]);
const selectedPort = ref('');
const firmwareVersions = ref<string[]>(['__local_browse__']);
const selectedVersion = ref('');
const selectedLocalPath = ref('');
const region = ref<RegionCode>('ZA');
const activeMode = ref<'serial' | 'network'>('serial');
const isFlashing = ref(false);
const isMonitoring = ref(false);
const isNetworkDiscovering = ref(false);
const isNetworkOta = ref(false);
const isNetworkUdpMonitoring = ref(false);
const logs = ref<string[]>([]);
const deviceInfo = ref<DeviceInfo | null>(null);
const deviceInfoPort = ref('');
const isLoadingInfo = ref(false);
const isRefreshingPorts = ref(false);
const isFetchingFirmware = ref(false);
const showToast = ref(false);
const toastMessage = ref('');
const logContainer = ref<HTMLElement | null>(null);
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
const networkStatusMessage = ref('Ready to scan the current LAN.');
const networkUdpTarget = ref('');

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
const crashCount = computed(() => countCrashEvents(logs.value));
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
const networkSubnetLabel = computed(() => {
  if (networkSubnets.value.length === 0) return 'No LAN subnet detected yet';
  return networkSubnets.value
    .map(s => `${s.interface_name} ${s.network}/${s.cidr}`)
    .join(', ');
});
const networkLogActive = computed(() => activeMode.value === 'network' && isNetworkUdpMonitoring.value);
const activityBusy = computed(() => isMonitoring.value || isFlashing.value || isNetworkDiscovering.value || isNetworkOta.value || isNetworkUdpMonitoring.value);

let unlistenFlash: UnlistenFn | null = null;
let unlistenMonitor: UnlistenFn | null = null;
let unlistenPortsChanged: UnlistenFn | null = null;
let unlistenNetworkMonitor: UnlistenFn | null = null;

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
    const hasActiveOperation = isFlashing.value || isLoadingInfo.value || isMonitoring.value;

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
  if (logs.value.length === 0) {
    notify('No activity logs to copy');
    return;
  }
  copyToClipboard(logs.value.join('\n'), 'activity log');
}

function copyActivePassword() {
  const password = deviceInfo.value?.password?.trim();
  if (!password) {
    notify('Load device info first to copy password');
    return;
  }
  copyToClipboard(password, 'password');
}

function passwordForNetworkDevice(device: NetworkDevice): string {
  const stored = networkPasswords.value[device.ip];
  return stored !== undefined ? stored : device.derived_password;
}

function setNetworkPassword(ip: string, value: string) {
  networkPasswords.value = { ...networkPasswords.value, [ip]: value };
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
    setNetworkPassword(updated.ip, updated.derived_password || '');
  }
}

function ipSortKey(ip: string): number {
  return ip.split('.').reduce((acc, part) => (acc * 256) + Number(part || 0), 0);
}

function networkAuthLabel(status: string): string {
  if (status === 'derived_ok') return 'Default OK';
  if (status === 'custom_ok') return 'Password OK';
  if (status === 'auth_error') return 'Auth error';
  return 'Password needed';
}

function networkAuthClass(status: string): string {
  if (status === 'derived_ok' || status === 'custom_ok') return 'text-emerald-300 border-emerald-500/30 bg-emerald-500/10';
  if (status === 'auth_error') return 'text-amber-300 border-amber-500/30 bg-amber-500/10';
  return 'text-slate-300 border-slate-700 bg-slate-800/50';
}

async function discoverNetworkDevices() {
  if (isNetworkDiscovering.value) return;
  activeMode.value = 'network';
  isNetworkDiscovering.value = true;
  networkStatusMessage.value = 'Scanning current LAN adapters...';
  logs.value.push('--- Network discovery ---');
  try {
    const result = await invoke<NetworkDiscoveryResult>('discover_network_devices');
    networkSubnets.value = result.subnets || [];
    scannedNetworkHosts.value = result.scanned_hosts || 0;
    networkDevices.value = (result.devices || []).sort((a, b) => ipSortKey(a.ip) - ipSortKey(b.ip));
    const nextPasswords: Record<string, string> = { ...networkPasswords.value };
    for (const device of networkDevices.value) {
      if (!nextPasswords[device.ip]) nextPasswords[device.ip] = device.derived_password || '';
    }
    networkPasswords.value = nextPasswords;
    if (!selectedNetworkIp.value && networkDevices.value.length > 0) {
      selectedNetworkIp.value = networkDevices.value[0].ip;
    }
    networkStatusMessage.value = `Found ${networkDevices.value.length} device${networkDevices.value.length === 1 ? '' : 's'} across ${scannedNetworkHosts.value} hosts.`;
    logs.value.push(`${networkStatusMessage.value} ${networkSubnetLabel.value}`);
  } catch (e) {
    networkStatusMessage.value = 'Network discovery failed: ' + e;
    logs.value.push(networkStatusMessage.value);
    notify(networkStatusMessage.value);
  } finally {
    isNetworkDiscovering.value = false;
  }
}

async function authenticateSelectedNetworkDevice() {
  const device = selectedNetworkDevice.value;
  if (!device) return;
  const password = passwordForNetworkDevice(device).trim();
  if (!password) {
    notify('Enter an admin password for this device');
    return;
  }
  logs.value.push(`Authenticating ${device.identity || device.ip} at ${device.ip}...`);
  try {
    const updated = await invoke<NetworkDevice>('authenticate_network_device', {
      ip: device.ip,
      password
    });
    mergeNetworkDevice(updated);
    networkStatusMessage.value = `${updated.identity || updated.ip} authenticated.`;
    logs.value.push(networkStatusMessage.value);
  } catch (e) {
    networkStatusMessage.value = `Authentication failed for ${device.ip}: ${e}`;
    logs.value.push(networkStatusMessage.value);
    notify(networkStatusMessage.value);
  }
}

async function openSelectedNetworkDeviceConsole() {
  const device = selectedNetworkDevice.value;
  if (!device) return;
  const url = `http://${device.ip}`;
  try {
    await openUrl(url);
    logs.value.push(`Opened ${url}`);
  } catch (e) {
    notify('Failed to open device URL: ' + e);
  }
}

async function startNetworkUdpMonitorForSelected() {
  const device = selectedNetworkDevice.value;
  if (!device) return;
  const password = passwordForNetworkDevice(device).trim();
  if (!password) {
    notify('Enter an admin password for this device');
    return;
  }
  try {
    const started = await invoke<string>('start_network_udp_monitor');
    logs.value.push(started);
    const enabled = await invoke<string>('enable_network_udp_logging', {
      ip: device.ip,
      options: { password, ttl_s: 300 }
    });
    isNetworkUdpMonitoring.value = true;
    networkUdpTarget.value = device.ip;
    logs.value.push(enabled);
  } catch (e) {
    logs.value.push('UDP monitor error: ' + e);
    notify('UDP monitor error: ' + e);
  }
}

async function stopNetworkUdpMonitor() {
  try {
    const stopped = await invoke<string>('stop_network_udp_monitor');
    logs.value.push(stopped);
  } catch (e) {
    logs.value.push('UDP monitor stop error: ' + e);
  } finally {
    isNetworkUdpMonitoring.value = false;
    networkUdpTarget.value = '';
  }
}

async function startNetworkOta() {
  const device = selectedNetworkDevice.value;
  if (!device || !selectedVersion.value) return;
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
  logs.value.push(`--- Network OTA ${device.identity || device.ip} (${device.ip}) ---`);
  try {
    const result = await invoke<string>('ota_network_device', {
      ip: device.ip,
      options: {
        password,
        firmware_path: firmwarePath,
        region: isLocal ? null : region.value
      }
    });
    logs.value.push(result);
    logs.value.push('OTA upload complete. Waiting for reboot and WiFi reconnect before UDP logging...');
    await waitForNetworkDevice(device.ip, password, 90000);
    await startNetworkUdpMonitorForSelected();
  } catch (e) {
    logs.value.push('Network OTA failed: ' + e);
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
      logs.value.push(`${updated.identity || ip} is back online.`);
      return;
    } catch {
      logs.value.push(`Waiting for ${ip} to return...`);
      await new Promise(resolve => setTimeout(resolve, 5000));
    }
  }
  throw new Error('Device did not return before the 90s wait expired');
}

function latestStaIpFromLogs(): string | null {
  for (let i = logs.value.length - 1; i >= 0; i--) {
    const line = String(logs.value[i] || '');
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
    logs.value.push(`Opened ${url}`);
  } catch (e) {
    notify('Failed to open device URL: ' + e);
  }
}

async function readDeviceInfo() {
  if (!selectedPort.value) return;
  const port = selectedPort.value;
  isLoadingInfo.value = true;
  deviceInfo.value = null;
  deviceInfoPort.value = '';
  logs.value.push('Reading device information...');
  try {
    deviceInfo.value = await invoke('get_device_info', { port });
    deviceInfoPort.value = port;
    logs.value.push('Device info read successfully');
    return true;
  } catch (e) {
    logs.value.push('Failed to read device info: ' + e);
    return false;
  } finally {
    isLoadingInfo.value = false;
  }
}

async function startFlash() {
  if (!selectedPort.value || !selectedVersion.value) return;
  
  isFlashing.value = true;
  logs.value.push('--- Preparing Firmware ---');
  
  try {
    if (!hasActiveDeviceInfo.value) {
      logs.value.push('Loading device information before flash...');
      const loaded = await readDeviceInfo();
      if (!loaded) throw new Error('Unable to read device information before flashing');
    }

    const isLocal = selectedVersion.value.startsWith('Local: ');
    const firmwarePath = isLocal ? selectedLocalPath.value : selectedVersion.value;
    
    if (isLocal && !firmwarePath) throw new Error('Local file path missing');

    const result = await invoke('flash_firmware', { 
      port: selectedPort.value,
      firmwarePath,
      region: isLocal ? null : region.value,
      eraseFirst: eraseBeforeFlash.value
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
  const port = selectedPort.value;
  try {
    if (targetState && !hasActiveDeviceInfo.value) {
      await readDeviceInfo();
    }

    await invoke('toggle_serial_monitor', { 
      port, 
      baud: 115200, 
      enable: targetState 
    });
    isMonitoring.value = targetState;
    if (targetState) {
      activeMonitorPort.value = port;
      activeMonitorSsid.value = deviceInfo.value?.ssid?.trim() || '';
      logs.value.push(`Serial monitor started for ${monitorContextLabel.value}`);
    } else {
      logs.value.push(`Serial monitor stopped for ${monitorContextLabel.value}`);
      activeMonitorPort.value = '';
      activeMonitorSsid.value = '';
    }
  } catch (e) {
    logs.value.push('Monitor error: ' + e);
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

watch(logs, () => {
  if (stickLogToBottom.value) {
    nextTick(() => scrollToBottom());
  }
}, { deep: true });

onMounted(async () => {
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
    logs.value.push(`Region auto-detected: ${detected.region} (${detected.reason})`);
  } else if (rememberedRegion) {
    region.value = rememberedRegion;
    logs.value.push(`Region auto-detect not confident; using last selected region: ${rememberedRegion} (${detected.reason})`);
  } else if (detected.region) {
    region.value = detected.region;
    logs.value.push(`Region auto-detect weak signal; using best guess: ${detected.region} (${detected.reason})`);
  } else {
    logs.value.push(`Region auto-detection unavailable; using default: ${region.value}`);
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

  unlistenNetworkMonitor = await listen<MonitorEvent>('network-monitor-log', (event) => {
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

onUnmounted(() => {
  if (unlistenFlash) unlistenFlash();
  if (unlistenMonitor) unlistenMonitor();
  if (unlistenNetworkMonitor) unlistenNetworkMonitor();
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
    'password': 'Password'
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
    <div class="mb-4 flex w-full max-w-md rounded-md border border-slate-700 bg-slate-900/60 p-1">
      <button
        @click="activeMode = 'serial'"
        :class="['m-0 flex-1 rounded px-4 py-2 text-sm font-semibold transition-all shadow-none', activeMode === 'serial' ? 'bg-indigo-500 text-white' : 'bg-transparent text-slate-400 hover:text-slate-100']"
      >
        Serial
      </button>
      <button
        @click="activeMode = 'network'"
        :class="['m-0 flex-1 rounded px-4 py-2 text-sm font-semibold transition-all shadow-none', activeMode === 'network' ? 'bg-indigo-500 text-white' : 'bg-transparent text-slate-400 hover:text-slate-100']"
      >
        Network
      </button>
    </div>

    <div :class="['grid gap-8 flex-1 min-h-0 transition-all duration-500', activeMode === 'serial' && isMonitoring ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
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
            </div>
            <div
              v-if="networkLogActive"
              class="flex items-center gap-1 pl-4 text-xs text-slate-500"
            >
              <span>UDP logs</span>
              <span class="font-mono text-slate-300">{{ networkUdpTarget }}</span>
              <span class="text-slate-500">on</span>
              <span class="font-mono text-slate-400">5514</span>
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
              :disabled="logs.length === 0"
              :class="[
                'p-1.5 rounded-md border transition-all',
                logs.length > 0
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
            <button v-if="activeMode === 'serial'" @click="toggleMonitor" :class="['px-3 py-1 rounded-md text-xs font-bold transition-all border', isMonitoring ? 'bg-indigo-500/20 border-indigo-500 text-indigo-400' : 'bg-slate-800 border-slate-700 text-slate-400 hover:text-slate-200 hover:border-slate-500']">
              {{ isMonitoring ? 'Stop monitor' : 'Start monitor' }}
            </button>
            <button v-if="activeMode === 'network' && isNetworkUdpMonitoring" @click="stopNetworkUdpMonitor" class="px-3 py-1 rounded-md text-xs font-bold transition-all border bg-indigo-500/20 border-indigo-500 text-indigo-400">
              Stop UDP
            </button>
            <button @click="logs = []" class="text-xs text-slate-500 hover:text-slate-300">Clear</button>
          </div>
        </div>
        
        <div ref="logContainer" @scroll="handleLogScroll" class="flex-1 overflow-auto font-mono text-[9px] sm:text-[10px] pr-2 custom-scrollbar space-y-0.5 leading-tight tracking-tight whitespace-pre">
          <div v-for="(log, i) in logs" :key="i" class="text-slate-400 border-l border-slate-700/50 pl-2 opacity-90">
            {{ log }}
          </div>
          <div v-if="logs.length === 0" class="h-full flex items-center justify-center text-slate-600 italic text-xs">
            No activity logs to show
          </div>
        </div>
      </div>

      <!-- Right Panel (Controls + Details) - Hidden in Monitor Mode -->
      <div v-if="activeMode === 'serial' && !isMonitoring" class="flex flex-col gap-6 h-full overflow-hidden transition-opacity duration-300" :class="{ 'opacity-0 pointer-events-none': isMonitoring }">
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
              <div class="flex flex-wrap items-center gap-4 px-1">
                <label class="flex items-center gap-2 cursor-pointer group">
                  <div class="relative flex items-center">
                    <input type="checkbox" v-model="monitorAfterFlash" class="peer hidden" />
                    <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-indigo-500 peer-checked:border-indigo-500 transition-all"></div>
                    <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                  </div>
                  <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Start monitor when flash complete</span>
                </label>
                <label class="flex items-center gap-2 cursor-pointer group">
                  <div class="relative flex items-center">
                    <input type="checkbox" v-model="eraseBeforeFlash" class="peer hidden" />
                    <div class="w-4 h-4 border border-slate-600 rounded bg-slate-800/50 peer-checked:bg-amber-500 peer-checked:border-amber-500 transition-all"></div>
                    <svg class="absolute w-3 h-3 text-white opacity-0 peer-checked:opacity-100 left-0.5 transition-opacity" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                  </div>
                  <span class="text-[10px] text-slate-400 group-hover:text-slate-300 transition-colors">Erase flash before write</span>
                </label>
              </div>
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

      <div v-if="activeMode === 'network'" class="flex flex-col gap-6 h-full overflow-hidden">
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
              <span>{{ isNetworkDiscovering ? 'Scanning...' : 'Scan LAN' }}</span>
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
                <button @click="fetchFirmware" :disabled="isFetchingFirmware" class="glass-input m-0 h-10 w-12 hover:bg-white/10 flex items-center justify-center shrink-0">
                  <svg xmlns="http://www.w3.org/2000/svg" :class="['w-5 h-5 text-slate-400', { 'animate-bounce text-indigo-500': isFetchingFirmware }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 14.899A7 7 0 1 1 15.71 8h1.79a4.5 4.5 0 0 1 2.5 8.242"></path><path d="M12 12v9"></path><path d="m8 17 4 4 4-4"></path></svg>
                </button>
              </div>
            </div>
          </div>

          <div class="text-xs text-slate-400">{{ networkStatusMessage }}</div>
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
              <div class="flex items-start justify-between gap-3">
                <div class="min-w-0">
                  <div class="font-mono text-sm text-slate-200 truncate">{{ device.identity || device.ip }}</div>
                  <div class="font-mono text-xs text-slate-500">{{ device.ip }}</div>
                </div>
                <span :class="['shrink-0 rounded border px-2 py-1 text-[10px] font-bold', networkAuthClass(device.auth_status)]">
                  {{ networkAuthLabel(device.auth_status) }}
                </span>
              </div>
              <div class="mt-2 grid grid-cols-2 gap-2 text-xs">
                <div>
                  <span class="text-slate-500">Version</span>
                  <div class="font-mono text-slate-300 truncate">{{ device.fw_display || device.fw_version || '-' }}</div>
                </div>
                <div>
                  <span class="text-slate-500">Mode</span>
                  <div class="font-mono text-slate-300">{{ device.mode || '-' }} {{ device.role || '' }}</div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div v-if="selectedNetworkDevice" class="glass-card p-5 flex flex-col gap-4 text-left shrink-0">
          <div class="flex items-center justify-between gap-3">
            <div>
              <h2 class="text-lg font-bold text-slate-300">{{ selectedNetworkDevice.identity || selectedNetworkDevice.ip }}</h2>
              <div class="font-mono text-xs text-slate-500">{{ selectedNetworkDevice.ip }}</div>
            </div>
            <button @click="openSelectedNetworkDeviceConsole" class="glass-input m-0 h-10 px-3 text-xs hover:bg-white/10">
              Open Web UI
            </button>
          </div>

          <div class="flex flex-col gap-1.5 text-xs">
            <label class="font-medium text-slate-400">Admin password</label>
            <input
              :value="passwordForNetworkDevice(selectedNetworkDevice)"
              @input="setNetworkPassword(selectedNetworkDevice.ip, ($event.target as HTMLInputElement).value)"
              class="glass-input h-10"
              type="password"
              autocomplete="current-password"
            />
          </div>

          <div class="grid grid-cols-3 gap-3">
            <button @click="authenticateSelectedNetworkDevice" :disabled="isNetworkOta" class="glass-input h-11 hover:bg-white/10 flex items-center justify-center text-xs font-bold">
              Check
            </button>
            <button @click="startNetworkOta" :disabled="isNetworkOta || isNetworkDiscovering" class="primary-btn h-11 flex items-center justify-center gap-2 text-xs font-bold">
              <svg xmlns="http://www.w3.org/2000/svg" :class="['w-4 h-4', { 'animate-spin': isNetworkOta }]" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"></path></svg>
              <span>{{ isNetworkOta ? 'Updating...' : 'OTA' }}</span>
            </button>
            <button
              @click="isNetworkUdpMonitoring && networkUdpTarget === selectedNetworkDevice.ip ? stopNetworkUdpMonitor() : startNetworkUdpMonitorForSelected()"
              :disabled="isNetworkOta"
              class="glass-input h-11 hover:bg-white/10 flex items-center justify-center text-xs font-bold"
            >
              {{ isNetworkUdpMonitoring && networkUdpTarget === selectedNetworkDevice.ip ? 'Stop UDP' : 'UDP logs' }}
            </button>
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
