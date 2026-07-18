<script setup lang="ts">
import { ref, Ref, computed, onMounted, onUnmounted, nextTick, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { listen, UnlistenFn } from '@tauri-apps/api/event';
import { useMqttAdmin } from '../composables/useMqttAdmin';
import { useSerialAdmin, SerialJobOptions } from '../composables/useSerialAdmin';
import { useMqttConfigBuffer } from '../composables/useMqttConfigBuffer';
import type { DeviceMqttConfig } from '../composables/useMqttConfigBuffer';
import { useFleetInventory, CANDIDATE_RECENT_IDENTITY_MS, deriveCandidateLocalTimestamp, calculateDynamicAgeMs, calculateCandidateAgeMs, fleetDeviceWithDisplayState } from '../composables/useFleetInventory';
import { parseVersion, compareParsedVersions } from '../utils/versionHelper';
import { useFirmwareManager, LOCAL_OPTION, LOCAL_LABEL_PREFIX } from '../composables/useFirmwareManager';
import { useMqttConnection } from '../composables/useMqttConnection';
import FleetMode from './flasher/FleetMode.vue';
import type {
  FleetConfig,
  FleetGatewayStatus,
  FleetServerStatus,
  FleetUdpLogs,
  FleetTransportState,
  FleetInventorySummary,
  FleetCandidateSummary,
  FleetDisplayRow,
  FleetCandidateDisplayRow,
  FleetCandidateActionPayload,
  MqttGatewayOption
} from './flasher/FleetMode.vue';
import { LoraInventoryDevice, LoraAdoptionCandidate, LoraAdoptionStatus, LoraInventoryStatus, LoraInventoryPeerStatus, SensorReading } from '../types/fleet';
import { useFleetInventoryPolling, shouldClearScanStateOnTimeout } from '../composables/useFleetInventoryPolling';
import { useFleetOta } from '../composables/useFleetOta';
import { useFirmwareServer, NetworkInterface } from '../composables/useFirmwareServer';
import ActivityPanel from './flasher/ActivityPanel.vue';
import SessionMqttBanner from './flasher/SessionMqttBanner.vue';
import MonitorMqttSettingsModal from './flasher/MonitorMqttSettingsModal.vue';
import FlashMode from './flasher/FlashMode.vue';
import RemoteSettingsModal from './flasher/RemoteSettingsModal.vue';
import RemoteFactoryResetModal from './flasher/RemoteFactoryResetModal.vue';
import type { RemoteFactoryResetDraft } from './flasher/RemoteFactoryResetModal.vue';
import ProvisionMode from './flasher/ProvisionMode.vue';
import type {
  ProvisionConfig,
  ProvisionUiState,
  ProvisionGatewayState,
  ProvisionTransportState,
  ProvisionSessionSummary,
  ProvisionDiscoveredDeviceRow,
  ProvisionPairState,
  ProvisionWifiState,
  ProvisionIdentifyState
} from './flasher/ProvisionMode.vue';
import SettingsMode from './flasher/SettingsMode.vue';
import type {
  SettingsForm,
  SettingsHeaderState,
  SettingsTransportState,
  SettingsAdminStatusState,
  SettingsSecretState,
  SettingsWifiState
} from './flasher/SettingsMode.vue';
import MonitorMode from './flasher/MonitorMode.vue';
import type {
  MonitorForm,
  MonitorHeaderState,
  MonitorTransportState,
  MonitorGatewayWarningState,
  MonitorGatewaySummaryState,
  MonitorFleetSummaryState,
  MonitorDisplayRow
} from './flasher/MonitorMode.vue';

type ActiveMode = 'pair' | 'serial' | 'network' | 'monitor' | 'settings';

const activeMode = defineModel<ActiveMode>('activeMode', { default: 'pair' });
const sessionConnectionState = defineModel<'active' | 'partial' | 'offline'>('sessionConnectionState', { default: 'offline' });

type SettingsTab = 'general' | 'network' | 'mqtt' | 'sensors' | 'remote' | 'system';
type FleetGatewayFlashPhase = 'idle' | 'flashing' | 'rebooting' | 'waiting' | 'updated' | 'failed' | 'unknown';

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
  password: string;
  local_addr: number;
  remote_addr: number;
  ssid: string;
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
    max_remotes: number;
    discovered_count: number;
    selected_count: number;
    verified_count: number;
    failed_count: number;
    conflict_count: number;
    debug_events?: string[];
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
  input_state?: number;
  peer_count?: number;
  sensors?: SensorReading[];
}

interface GatewayEventRecord {
  port: string;
  ms: number;
  event: string;
  rssi: number;
  counter: number;
  state: number;
  raw: string;
  level: 'info' | 'warn' | 'error' | 'crash' | 'reset' | 'log_line' | 'raw';
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

function sensorTelemetryStateEnabled(state: unknown): boolean {
  return ['ok', 'missing', 'fault', 'overrange', 'waiting'].includes(String(state || '').toLowerCase());
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
  isFlashing: boolean;
  flashProgress: number;
  flashStatus: string;
  flashPhase: 'idle' | 'erase' | 'write' | 'verify';
  isResetting: boolean;
  resetStatus: string;
}


const READABLE_KEY_CONSONANTS = 'bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS = 'aeiou';
const NETWORK_FIRMWARE_PATH = '/firmware.bin';
const LRS_REMOTE_SCAN_CAP = 12;
const DISCONNECTED_PORT_CACHE_GRACE_MS = 120000;

const ports = ref<SerialPort[]>([]);
const flashSelectedPort = ref('');
const gatewaySelectedPort = ref('');
const monitorSelectedPort = ref('');
const settingsSelectedPort = ref('');
interface InFlightRead {
  seq: number;
  promise: Promise<boolean>;
}
const inFlightDeviceInfoReads = ref(new Map<string, InFlightRead>());
const selectedPort = computed<string>({
  get() {
    if (activeMode.value === 'pair') return pairGatewayKey.value;
    if (activeMode.value === 'network') {
      return fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
    }
    if (activeMode.value === 'monitor') {
      return monitorTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : monitorSelectedPort.value;
    }
    if (activeMode.value === 'settings') {
      return settingsTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : settingsSelectedPort.value;
    }
    return flashSelectedPort.value;
  },
  set(port) {
    if (activeMode.value === 'pair') {
      if (pairTransport.value === 'mqtt') {
        selectedMqttGatewayChipId.value = port;
      } else {
        gatewaySelectedPort.value = port;
      }
    } else if (activeMode.value === 'network') {
      if (fleetTransport.value === 'mqtt') {
        selectedMqttGatewayChipId.value = port;
      } else {
        gatewaySelectedPort.value = port;
      }
    } else if (activeMode.value === 'monitor') {
      if (monitorTransport.value === 'mqtt') {
        selectedMqttGatewayChipId.value = port;
      } else {
        monitorSelectedPort.value = port;
      }
    } else if (activeMode.value === 'settings') {
      if (settingsTransport.value === 'mqtt') {
        selectedMqttGatewayChipId.value = port;
      } else {
        settingsSelectedPort.value = port;
      }
    } else {
      flashSelectedPort.value = port;
    }
  }
});
const flasherAppVersion = ref('');

const {
  firmwareVersions,
  selectedVersion,
  selectedLocalPath,
  isFetchingFirmware,
  region,
  selectedFirmwareCandidateVersion,
  networkOtaFirmwareOptions,
  fetchFirmware,
  initializeRegion
} = useFirmwareManager({
  flasherAppVersion,
  pushLog: pushSerialLog,
  notify
});
const isFlashing = ref(false);
const isBulkFlashing = ref(false);
const isBulkResetting = ref(false);
const bulkSelectedPorts = ref<string[]>([]);
const bulkMode = ref(false);
const isMonitoring = ref(false);
const isNetworkUdpMonitoring = ref(false);
const otaQueue = ref<LoraInventoryDevice[]>([]);
const networkInterfaceInterval = ref<ReturnType<typeof window.setInterval> | null>(null);

const {
  isFirmwareServerStarting,
  firmwareServerInfo,
  startFirmwareServer,
  ensureFirmwareServer,
  stopFirmwareServer,
  firmwareServerTarget,
  handleNetworkInterfacesChanged,
  revalidateFirmwareServerAfterNetworkChange,
  cleanupFirmwareServer
} = useFirmwareServer({
  resolveFirmwareOptions: networkOtaFirmwareOptions,
  onStarting: () => {
    activeMode.value = 'network';
  },
  onStarted: (_info, statusMsg) => {
    networkStatusMessage.value = statusMsg;
  },
  onStopped: (statusMsg) => {
    networkStatusMessage.value = statusMsg;
  },
  pushNetworkLog,
  notify
});

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
const FLEET_CACHE_POLL_INTERVAL_MS = 5000;
const FLEET_SCAN_POLL_INTERVAL_MS = 1200;
const FLEET_SCAN_SETTLE_REFRESH_MS = 1800;
const FLEET_PEER_REFRESH_SETTLE_MS = 900;
const FLEET_FORCE_SCAN_COOLDOWN_MS = 60000;
const FLEET_INVENTORY_STATUS_TIMEOUT_MS = 15000;
const provisionCacheRefreshedChips = new Set<string>();
const isLoadingInfo = computed(() => inFlightDeviceInfoReads.value.has(selectedPort.value));
const isRefreshingPorts = ref(false);
const fleetGatewayFlashPhase = ref<FleetGatewayFlashPhase>('idle');
const mqttOtaStatus = ref<Record<string, { status: string; timestamp: number }>>({});
let unlistenMqttOtaStatus: (() => void) | null = null;
let unlistenMqttConfig: (() => void) | null = null;
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

const settingsDeviceModalState = computed({
  get: () => {
    if (!settingsDeviceModal.value) return {
      activeTab: 'sensors' as const,
      wifi_ssid: '',
      wifi_password: '',
      sensor_temp_enabled: false,
      sensor_tank_enabled: false,
      power_save_listen_only: false,
      fleet_key: '',
      fleet_key_confirmed: false,
      show_fleet_key: false
    };
    return {
      activeTab: settingsDeviceModal.value.activeTab,
      wifi_ssid: settingsDeviceModal.value.wifi_ssid,
      wifi_password: settingsDeviceModal.value.wifi_password,
      sensor_temp_enabled: settingsDeviceModal.value.sensor_temp_enabled,
      sensor_tank_enabled: settingsDeviceModal.value.sensor_tank_enabled,
      power_save_listen_only: settingsDeviceModal.value.power_save_listen_only,
      fleet_key: settingsDeviceModal.value.fleet_key,
      fleet_key_confirmed: settingsDeviceModal.value.fleet_key_confirmed,
      show_fleet_key: settingsDeviceModal.value.show_fleet_key
    };
  },
  set: (val) => {
    if (settingsDeviceModal.value) {
      settingsDeviceModal.value.activeTab = val.activeTab;
      settingsDeviceModal.value.wifi_ssid = val.wifi_ssid;
      settingsDeviceModal.value.wifi_password = val.wifi_password;
      settingsDeviceModal.value.sensor_temp_enabled = val.sensor_temp_enabled;
      settingsDeviceModal.value.sensor_tank_enabled = val.sensor_tank_enabled;
      settingsDeviceModal.value.power_save_listen_only = val.power_save_listen_only;
      settingsDeviceModal.value.fleet_key = val.fleet_key;
      settingsDeviceModal.value.fleet_key_confirmed = val.fleet_key_confirmed;
      settingsDeviceModal.value.show_fleet_key = val.show_fleet_key;
    }
  }
});

const isSettingsDeviceModalOpen = computed({
  get: () => !!settingsDeviceModal.value,
  set: (val) => {
    if (!val) {
      settingsDeviceModal.value = null;
    }
  }
});

function handleRemoteSettingsSensors(temp: boolean, tank: boolean, listen: boolean) {
  const modal = settingsDeviceModal.value;
  if (!modal) return;
  executeRemoteSensors(modal.device, temp, tank, listen);
}

function handleRemoteSettingsWifi(ssid: string, pass: string) {
  const modal = settingsDeviceModal.value;
  if (!modal) return;
  executeRemoteWifi(modal.device, ssid, pass);
}

function handleRemoteSettingsFleetKey(key: string) {
  const modal = settingsDeviceModal.value;
  if (!modal) return;
  executeRemoteFleetKeyChange(modal.device, key);
}

interface FactoryResetModalState {
  device: LoraInventoryDevice;
  keep_shared_fleet_key: boolean;
  keep_wifi_credentials: boolean;
}
const factoryResetTargetModal = ref<FactoryResetModalState | null>(null);

const isFactoryResetTargetModalOpen = computed({
  get: () => !!factoryResetTargetModal.value,
  set: (open) => {
    if (!open) factoryResetTargetModal.value = null;
  }
});

const factoryResetTargetDraftComputed = computed<RemoteFactoryResetDraft>({
  get: () => ({
    keep_shared_fleet_key: factoryResetTargetModal.value?.keep_shared_fleet_key ?? false,
    keep_wifi_credentials: factoryResetTargetModal.value?.keep_wifi_credentials ?? false,
  }),
  set: (draft) => {
    if (!factoryResetTargetModal.value) return;
    factoryResetTargetModal.value = {
      ...factoryResetTargetModal.value,
      keep_shared_fleet_key: draft.keep_shared_fleet_key,
      keep_wifi_credentials: draft.keep_wifi_credentials,
    };
  }
});

const factoryResetTargetAddressComputed = computed<number | string>(() => {
  return factoryResetTargetModal.value?.device.address ?? '';
});

function confirmRemoteFactoryReset() {
  const modal = factoryResetTargetModal.value;
  if (!modal) return;
  executeRemoteFactoryReset(
    modal.device,
    modal.keep_shared_fleet_key,
    modal.keep_wifi_credentials
  );
}

const networkUdpLogContainer = ref<HTMLElement | null>(null);
const deviceInfoReadSeqByPort = ref<Record<string, number>>({});
const monitorAfterFlash = ref(true);
const eraseBeforeFlash = ref(false);
const lastPortSnapshot = ref<string[]>([]);
const portSeenSequence = ref<Record<string, number>>({});
const portSeenCounter = ref(0);
const networkUdpLogsExpanded = ref(false);
const fleetGatewayEventsExpanded = ref(false);
const fleetGatewayEventsIncludeLogLines = ref(false);
const gatewayEvents = ref<GatewayEventRecord[]>([]);
const gatewayEventsStatus = ref('Waiting for firmware log events.');
const gatewayEventLastMsByPort = ref<Record<string, number>>({});
const activeMonitorPort = ref('');
const activeMonitorSsid = ref('');
const networkStatusMessage = ref('Select a USB gateway to read its fleet cache.');
const monitorStatusMessage = ref('Select a USB gateway and refresh monitor data.');
const monitorTransport = computed<'serial' | 'mqtt'>(() => {
  return sessionConnectionType.value === 'serial' ? 'serial' : 'mqtt';
});

// Unified session gateway connection state variables
const isSerialSessionConnected = computed(() => portGatewayReady(gatewaySelectedPort.value));
const serialSessionConnectionState = computed<'active' | 'partial' | 'offline'>(() => {
  if (portGatewayReady(gatewaySelectedPort.value)) {
    return 'active';
  }
  if (gatewaySelectedPort.value) {
    return 'partial';
  }
  return 'offline';
});

const {
  sessionConnectionType,
  localBrokerPort,
  showSessionConfigPanel,
  monitorMqttTopicRoot,
  monitorMqttConnected,
  showMonitorMqttSettings,
  isSessionConnected,
  computedSessionConnectionState,
  monitorMqttDraftState,
  localBrokerState,
  mqttSettingsState,
  toggleMonitorMqttConnection,
  startAndConnectLocalBroker,
  adoptMonitorMqttFromStatus,
  openMonitorMqttSettings,
  closeMonitorMqttSettings,
  applyMqttStateChanged,
  hydrateMqttState,
  hydrateLocalBrokerStatus
} = useMqttConnection({
  notify,
  setMonitorStatusMessage: (msg) => { monitorStatusMessage.value = msg; },
  isSerialSessionConnected,
  serialSessionConnectionState
});

watch(computedSessionConnectionState, (newVal) => {
  sessionConnectionState.value = newVal;
}, { immediate: true });
const monitorFleetRows = ref<LoraInventoryDevice[]>([]);
const selectedMonitorDeviceAddress = ref<number | null>(null);
const hasDiagnosticsData = computed(() => {
  return monitorFleetRows.value.some(device => device.heap_free !== undefined && device.heap_free !== null && device.heap_free !== 0);
});
const fleetTransport = computed<'serial' | 'mqtt'>(() => {
  return sessionConnectionType.value === 'serial' ? 'serial' : 'mqtt';
});
const pairTransport = ref<'serial' | 'mqtt'>('serial');
const pairGatewayLoaded = ref(false);
const mqttGateways = ref<Record<string, any>>({});
const lastMqttDiscoveryMs = ref<Record<string, number>>({});
const selectedMqttGatewayChipId = ref('');
const selectedMqttManualChipId = ref('');
const manualMqttGatewayError = ref('');

const { mqttConfigBuffers, handleConfigUpdate } = useMqttConfigBuffer();

async function ensureMqttConfigLoaded(rawChipId: string, timeoutMs = 5000): Promise<DeviceMqttConfig> {
  const canonical = normalizeChipId(rawChipId);
  let bufState = mqttConfigBuffers.value[canonical];
  if (bufState?.complete) return bufState;

  let elapsed = 0;
  while (elapsed < timeoutMs) {
    await new Promise(resolve => setTimeout(resolve, 200));
    elapsed += 200;
    bufState = mqttConfigBuffers.value[canonical];
    if (bufState?.complete) return bufState;
  }

  throw new Error('MQTT config topics not loaded');
}

function isMqttSecretConfigured(secretKey: string): boolean {
  if (settingsTransport.value !== 'mqtt' || !selectedPort.value) return false;
  const canonical = canonicalChipId(selectedPort.value);
  const bufState = mqttConfigBuffers.value[canonical];
  return !!bufState?.secretsMetadata?.[secretKey];
}

function isMqttFleetPassphraseDefault(): boolean {
  if (settingsTransport.value !== 'mqtt' || !selectedPort.value) return false;
  const canonical = canonicalChipId(selectedPort.value);
  const bufState = mqttConfigBuffers.value[canonical];
  return !!bufState?.secretsMetadata?.['fleet_passphrase_default'];
}


// Computed helper to check if the selected chip is actually in the discovered map
const isSelectedMqttGatewayDiscovered = computed(() => {
  return !!selectedMqttGatewayChipId.value && selectedMqttGatewayChipId.value in mqttGateways.value;
});

// Watcher to keep the manual input field synchronized if a discovered option is selected
watch(selectedMqttGatewayChipId, (newVal) => {
  if (newVal && newVal in mqttGateways.value) {
    selectedMqttManualChipId.value = newVal;
    manualMqttGatewayError.value = '';
  }
  if (newVal) {
    ensureMqttGatewayDefaultState(newVal);
  }
});

async function ensureMqttGatewayDefaultState(rawChipId: string | undefined | null) {
  const chipId = normalizeChipId(rawChipId);
  if (!/^[0-9a-f]{6,8}$/.test(chipId)) return;

  const state = serialDeviceState(chipId);
  if (!state) return;

  const gw = mqttGateways.value[chipId];
  const derived = await invoke<DeviceInfo>('derive_device_info_from_chip_id', { chipId });
  const password = state.adminPassword || state.deviceInfo?.password || derived.password;

  state.adminSupported = true;
  state.adminPassword = state.adminPassword || password;
  state.deviceInfo = {
    ...derived,
    mac: gw?.mac || state.deviceInfo?.mac || derived.mac,
    password,
    ssid: gw?.sta_ssid || state.deviceInfo?.ssid || derived.ssid
  };
}

// Normalization and validation function for manual entry
function handleManualMqttGatewayInput(val: string) {
  const trimmed = val.trim();
  if (!trimmed) {
    manualMqttGatewayError.value = '';
    return;
  }

  // Normalize by stripping "lrs-" prefix
  let clean = trimmed.toLowerCase();
  if (clean.startsWith('lrs-')) {
    clean = clean.substring(4);
  }

  // Validate format (6 to 8 hex chars)
  if (/^[0-9a-f]{6,8}$/.test(clean)) {
    selectedMqttGatewayChipId.value = clean;
    selectedPort.value = clean;
    selectedMqttManualChipId.value = clean;
    ensureMqttGatewayDefaultState(clean);
    manualMqttGatewayError.value = '';
  } else {
    manualMqttGatewayError.value = 'Must be of format "lrs-<6-8 hex>" or "<6-8 hex>"';
  }
}

const pairGatewayKey = computed(() => {
  return pairTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
});
watch([pairGatewayKey, pairTransport], () => {
  pairGatewayLoaded.value = false;
});
const isMonitorRefreshing = ref(false);
const isMonitorLoopRunning = ref(false);
const monitorPollTimer = ref<ReturnType<typeof window.setInterval> | null>(null);
const monitorAutoRefresh = ref(true);
const gatewaySnapshotPauseCount = ref(0);
const settingsTransport = computed<'serial' | 'mqtt'>(() => {
  return sessionConnectionType.value === 'serial' ? 'serial' : 'mqtt';
});
const settingsTab = ref<SettingsTab>('general');
const remoteSubTab = ref<'serial' | 'mqtt' | 'lora'>('serial');
const networkUdpTarget = ref('');
const activeRemoteUdpAddress = ref<number | null>(null);
const fleetClockMs = ref(Date.now());
const isLoraInventoryScanning = ref(false);
const isLoraInventoryScanStarting = ref(false);
const fleetScanActiveSinceMs = ref(0);
const fleetScanSettleTimer = ref<ReturnType<typeof window.setTimeout> | null>(null);
const GATEWAY_REBOOT_ALERT_MS = 120000;
const GATEWAY_UPTIME_ROLLBACK_GRACE_MS = 30000;

interface GatewayUnexpectedRebootAlert {
  detectedAtMs: number;
  previousUptimeMs: number;
  currentUptimeMs: number;
}

const {
  loraInventory,
  loraInventoryScan,
  loraCandidates,
  fleetRowHistory,
  fleetForceScanCooldownUntilMs,
  rowFreshness,
  candidateStateText,
  candidateStateClass,
  fleetDeviceUdpLabel,
  fleetRowStatusLabel,
  loraInventoryProgressLabel,
  fleetForceScanCooldownRemainingMs,
  fleetForceScanLabel,
  remotesAndCandidatesStatusLine,
  selectedLoraInventoryCount,
  hasAnyRemoteIp,
  mergeInventoryRows,
  mergeMonitorRows,
  applyTelemetryUpdate,
  clearFleetGatewayCache: clearFleetGatewayCacheComposable
} = useFleetInventory({
  otaQueue,
  selectedFirmwareCandidateVersion: selectedFirmwareCandidateVersion,
  fleetClockMs,
  isLoraInventoryScanning,
  canonicalChipId,
  normalizeRole,
  parseVersion,
  compareParsedVersions,
  activeGatewayId: () => selectedMqttGatewayChipId.value
});
const candidateTotal = ref(0);
const candidateTruncated = ref(false);
const loraAdoptionStatus = ref<LoraAdoptionStatus | null>(null);
const lastLoraInventoryStatus = ref<LoraInventoryStatus | null>(null);
const lastLoraInventoryStatusAtMs = ref(0);
const lastLoraInventoryStatusPort = ref('');
const fleetInventoryHydrateCursor = ref(0);
const activeGatewaySessionKey = ref('');
const gatewayUptimeHistory = ref<Record<string, number>>({});
const gatewayUnexpectedReboots = ref<Record<string, GatewayUnexpectedRebootAlert>>({});
const processedEasyPairLogLines = ref<Set<string>>(new Set());
const isNetworkGatewayLoading = ref(false);
const {
  networkInventoryPollMode,
  refreshLoraInventoryStatus,
  startLoraInventoryPolling,
  startFleetCachePolling,
  stopLoraInventoryPolling
} = useFleetInventoryPolling({
  activeMode: activeMode as Ref<any>,
  fleetTransport,
  gatewaySelectedPort,
  selectedMqttGatewayChipId,
  isLoraInventoryScanning,
  refreshGatewaySnapshot,
  fleetScanPollIntervalMs: FLEET_SCAN_POLL_INTERVAL_MS,
  fleetCachePollIntervalMs: FLEET_CACHE_POLL_INTERVAL_MS
});
// fleetForceScanCooldownUntilMs is managed by useFleetInventory
const fleetClockTimer = ref<ReturnType<typeof window.setInterval> | null>(null);

const {
  hasActiveRemoteOtaPulls,
  fleetFlashAvailable,
  fleetFlashUnavailableReason,
  flashLoraRemote,
  handleOtaLogLine,
  cleanupFleetOtaTimers
} = useFleetOta({
  loraInventory,
  fleetRowHistory,
  otaQueue,
  networkFirmwarePath: NETWORK_FIRMWARE_PATH,
  checkPreflight: () => {
    const { port, password } = fleetGatewayCommandTarget();
    if (!port) {
      notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
      return false;
    }
    if (!password) {
      notify('Enter the gateway admin password');
      return false;
    }
    return true;
  },
  triggerOtaCommand: async (device) => {
    const { port, password } = fleetGatewayCommandTarget();
    if (!port || !password) {
      throw new Error('Select a gateway and enter the admin password first');
    }
    if (!portGatewayReady(port)) await loadNetworkGateway();
    const info = await ensureFirmwareServer();
    const target = firmwareServerTarget(info);
    const out = await sendEasyPairCommandOnPort<any>(port, 'remote_ota_pull', {
      admin_password: password,
      address: device.address,
      host: target.host,
      port: target.port,
      sha256: info.sha256
    }, 8000);
    return { out, target, sha256: info.sha256 };
  },
  runFollowupInventoryScan: async (address: number) => {
    const { port, password } = fleetGatewayCommandTarget();
    if (!password) throw new Error('missing gateway password');
    await sendEasyPairCommandOnPort(port, 'start_lora_inventory', {
      admin_password: password,
      start_address: address,
      end_address: address,
      interval_ms: 250
    }, 8000);
    await new Promise(resolve => setTimeout(resolve, 900));
    await refreshLoraInventoryStatus();
  },
  notify,
  pushNetworkLog,
  setNetworkStatusMessage: (msg) => { networkStatusMessage.value = msg; },
  serialFeatureError
});
// fleetRowHistory is managed by useFleetInventory
const pairExpectedCount = ref(12);
const pairPanelTab = ref<'pair' | 'wifi'>('pair');
const pairFleetKey = ref('');
const pairFleetKeySource = ref<'none' | 'gateway' | 'factory_generated' | 'manual'>('none');
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
const showSettingsAdminPassword = ref(false);
const serialFactoryKeepFleet = ref(false);
const serialFactoryKeepWifi = ref(true);

const DEVICE_INFO_ORDER: Array<keyof DeviceInfo> = [
  'ssid',
  'password',
  'local_addr',
  'remote_addr',
  'mac',
  'chip_id',
];
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
const targetGatewayKey = computed(() => {
  if (activeMode.value === 'network') {
    return fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
  }
  if (activeMode.value === 'settings') {
    return settingsTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : settingsSelectedPort.value;
  }
  if (activeMode.value === 'monitor') {
    return monitorTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : monitorSelectedPort.value;
  }
  if (activeMode.value === 'pair') {
    return pairGatewayKey.value;
  }
  return flashSelectedPort.value;
});

const pairAdminPassword = computed<string>({
  get: () => {
    const state = serialDeviceState(targetGatewayKey.value);
    return state?.adminPassword || state?.deviceInfo?.password?.trim() || '';
  },
  set: (password) => {
    const state = serialDeviceState(targetGatewayKey.value);
    if (state) state.adminPassword = password;
  }
});
watch(pairAdminPassword, () => {
  pairGatewayLoaded.value = false;
});
const settingsAdminPassword = computed<string>({
  get: () => {
    const state = serialDeviceState(selectedPort.value);
    return state?.adminPassword || state?.deviceInfo?.password?.trim() || '';
  },
  set: (password) => {
    const state = serialDeviceState(selectedPort.value);
    if (state) state.adminPassword = password;
  }
});
const wifiNetworks = computed<WifiNetwork[]>({
  get: () => serialDeviceState(targetGatewayKey.value)?.wifiNetworks || [],
  set: (networks) => {
    const state = serialDeviceState(targetGatewayKey.value);
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
  get: () => serialDeviceState(targetGatewayKey.value)?.wifiSsid || '',
  set: (ssid) => {
    const state = serialDeviceState(targetGatewayKey.value);
    if (state) state.wifiSsid = ssid;
  }
});

function selectedProvisionWifiSsid(): string {
  return pairWifiSsid.value.trim();
}

function selectedProvisionAdminPassword(): string {
  return adminPasswordForPort(pairGatewayKey.value);
}

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
const pairDiscoveredDeviceCount = computed(() => pairStatus.value?.devices?.length || 0);
const fleetGatewayDevice = computed(() => serialDeviceState(targetGatewayKey.value));
const fleetGatewayIdentity = computed(() => fleetGatewayDevice.value?.deviceInfo || null);
const fleetGatewayStatus = computed(() => fleetGatewayDevice.value?.status || null);



const isGatewayUpgradeAvailable = computed(() => {
  const currentFw = fleetGatewayStatus.value?.fw_version;
  if (!currentFw || !selectedVersion.value) return false;

  if (selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX)) {
    return true;
  }

  const p1 = parseVersion(selectedVersion.value);
  const p2 = parseVersion(currentFw);
  if (!p1 || !p2) return false;

  return compareParsedVersions(p1, p2) > 0;
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
const fleetGatewayFlashDisabled = computed(() => {
  const { port, password, isMqtt } = fleetGatewayCommandTarget();
  if (!port) return true;
  if (isMqtt && !monitorMqttConnected.value) return true;
  if (isMqtt && !password) return true;
  return fleetGatewayFlashPhase.value !== 'idle' ||
    isNetworkGatewayLoading.value ||
    isLoraInventoryScanning.value ||
    hasActiveRemoteOtaPulls.value ||
    isFirmwareServerStarting.value ||
    isPairBusy.value;
});
const gatewayReady = computed(() => {
  const key = pairGatewayKey.value;
  if (!key || !pairGatewayLoaded.value || !pairPassword()) return false;
  if (pairTransport.value === 'mqtt') {
    return true;
  }
  const state = serialDeviceState(key);
  return !!state?.deviceInfo && !!state?.adminSupported;
});
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
const serialAdminDisabled = computed(() => {
  if (settingsTransport.value === 'mqtt') {
    return !selectedPort.value || isFlashing.value || serialAdminBusy.value;
  }
  return !selectedPort.value || !hasActiveDeviceInfo.value || isFlashing.value || isSelectedPortMonitoring.value || isLoadingInfo.value || serialAdminBusy.value;
});
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

const flashFormDraftState = computed({
  get: () => ({
    bulkMode: bulkMode.value,
    region: region.value,
    selectedPort: selectedPort.value,
    selectedVersion: selectedVersion.value,
    eraseBeforeFlash: eraseBeforeFlash.value,
    monitorAfterFlash: monitorAfterFlash.value,
    serialFactoryKeepFleet: serialFactoryKeepFleet.value,
    serialFactoryKeepWifi: serialFactoryKeepWifi.value,
    bulkSelectedPorts: bulkSelectedPorts.value
  }),
  set: (val) => {
    bulkMode.value = val.bulkMode;
    region.value = val.region as any;
    selectedPort.value = val.selectedPort;
    selectedVersion.value = val.selectedVersion;
    eraseBeforeFlash.value = val.eraseBeforeFlash;
    monitorAfterFlash.value = val.monitorAfterFlash;
    serialFactoryKeepFleet.value = val.serialFactoryKeepFleet;
    serialFactoryKeepWifi.value = val.serialFactoryKeepWifi;
    bulkSelectedPorts.value = val.bulkSelectedPorts;
  }
});

const flashDeviceState = computed(() => ({
  runningFirmware: flashRunningFirmware.value,
  runningFirmwareSummary: flashRunningFirmwareSummary.value,
  deviceInfo: deviceInfo.value,
  orderedEntries: orderedDeviceInfoEntries.value,
  isLoadingInfo: isLoadingInfo.value,
  isIdentifying: isIdentifying.value,
  identifyAvailable: identifyAvailable.value,
  identifyDisabled: identifyDisabled.value,
  flashDisabled: flashDisabled.value,
  isFlashing: isFlashing.value
}));

const flashBulkState = computed(() => ({
  bulkFlashDisabled: bulkFlashDisabled.value,
  bulkResetDisabled: bulkResetDisabled.value,
  isBulkFlashing: isBulkFlashing.value,
  isBulkResetting: isBulkResetting.value
}));

const systemConfigState = computed(() => ({
  ports: ports.value,
  isRefreshingPorts: isRefreshingPorts.value,
  serialPortSelectorDisabled: serialPortSelectorDisabled.value,
  portChipIds: Object.fromEntries(
    ports.value
      .map(p => [p.port_name, serialDeviceState(p.port_name)?.deviceInfo?.chip_id])
      .filter(([_, chip_id]) => !!chip_id)
  ) as Record<string, string>,
  firmwareVersions: firmwareVersions.value,
  isFetchingFirmware: isFetchingFirmware.value,
  localOptionConstant: LOCAL_OPTION
}));

const settingsFormComputed = computed<SettingsForm>({
  get: () => ({
    selectedPort: selectedPort.value,
    sessionConnectionType: sessionConnectionType.value,
    settingsAdminPassword: settingsAdminPassword.value,
    selectedMqttManualChipId: selectedMqttManualChipId.value,
    selectedMqttGatewayChipId: selectedMqttGatewayChipId.value,
    settingsTab: settingsTab.value,
    remoteSubTab: remoteSubTab.value,
    showSettingsAdminPassword: showSettingsAdminPassword.value,
    showSerialWifiPassword: showSerialWifiPassword.value,
    showSerialMqttPassword: showSerialMqttPassword.value,
    showSerialFleetKey: showSerialFleetKey.value,
    serialFactoryKeepFleet: serialFactoryKeepFleet.value,
    serialFactoryKeepWifi: serialFactoryKeepWifi.value,
  }),
  set: (val) => {
    selectedPort.value = val.selectedPort;
    sessionConnectionType.value = val.sessionConnectionType;
    settingsAdminPassword.value = val.settingsAdminPassword;
    selectedMqttManualChipId.value = val.selectedMqttManualChipId;
    selectedMqttGatewayChipId.value = val.selectedMqttGatewayChipId;
    settingsTab.value = val.settingsTab;
    remoteSubTab.value = val.remoteSubTab;
    showSettingsAdminPassword.value = val.showSettingsAdminPassword;
    showSerialWifiPassword.value = val.showSerialWifiPassword;
    showSerialMqttPassword.value = val.showSerialMqttPassword;
    showSerialFleetKey.value = val.showSerialFleetKey;
    serialFactoryKeepFleet.value = val.serialFactoryKeepFleet;
    serialFactoryKeepWifi.value = val.serialFactoryKeepWifi;
  }
});

const settingsHeaderStateComputed = computed<SettingsHeaderState>(() => ({
  serialStatusSummary: serialStatusSummary.value,
  identifyAvailable: identifyAvailable.value,
  identifyDisabled: identifyDisabled.value,
  isIdentifying: isIdentifying.value,
  isFlashing: isFlashing.value,
  isLoadingInfo: isLoadingInfo.value,
  isSerialAdminLoading: isSerialAdminLoading.value,
  isSerialAdminSaving: isSerialAdminSaving.value,
  serialAdminDisabled: serialAdminDisabled.value,
  serialAdminBusy: serialAdminBusy.value,
  serialAdminConfigExists: !!serialAdminConfig.value,
}));

const settingsTransportStateComputed = computed<SettingsTransportState>(() => ({
  settingsTransport: settingsTransport.value,
  ports: ports.value.map(p => ({ port_name: p.port_name, description: p.description || undefined })),
  serialPortSelectorDisabled: serialPortSelectorDisabled.value,
  isRefreshingPorts: isRefreshingPorts.value,
  mqttGateways: Object.fromEntries(
    Object.entries(mqttGateways.value).map(([k, v]) => [k, { chip_id: v.chip_id }])
  ),
  manualMqttGatewayError: manualMqttGatewayError.value,
  isSelectedMqttGatewayDiscovered: isSelectedMqttGatewayDiscovered.value,
  monitorMqttConnected: monitorMqttConnected.value,
}));

const settingsAdminStatusStateComputed = computed<SettingsAdminStatusState | null>(() => {
  const status = serialAdminStatus.value;
  if (!status) return null;
  return {
    fw_version: status.fw_version,
    uptime_ms: status.uptime_ms,
    heap_free: status.heap_free,
    mode: status.mode,
  };
});

const settingsSecretStateComputed = computed<SettingsSecretState>(() => ({
  isWifiStaPasswordConfigured: isMqttSecretConfigured('wifi_sta_password'),
  isMqttPasswordConfigured: isMqttSecretConfigured('mqtt_password'),
  isFleetPassphraseConfigured: isMqttSecretConfigured('fleet_passphrase'),
  isMqttFleetPassphraseDefault: isMqttFleetPassphraseDefault(),
}));

const settingsWifiStateComputed = computed<SettingsWifiState>(() => ({
  isWifiScanning: isWifiScanning.value,
  settingsWifiNetworks: settingsWifiNetworks.value.map(n => ({
    ssid: n.ssid,
    rssi: n.rssi,
    channel: n.channel,
    secure: n.secure,
    bssid: n.bssid,
  })),
}));

const monitorFormComputed = computed<MonitorForm>({
  get: () => ({
    selectedPort: selectedPort.value,
    sessionConnectionType: sessionConnectionType.value,
    monitorAutoRefresh: monitorAutoRefresh.value,
    selectedMonitorDeviceAddress: selectedMonitorDeviceAddress.value,
  }),
  set: (val) => {
    selectedPort.value = val.selectedPort;
    sessionConnectionType.value = val.sessionConnectionType;
    monitorAutoRefresh.value = val.monitorAutoRefresh;
    selectedMonitorDeviceAddress.value = val.selectedMonitorDeviceAddress;
  }
});

const monitorHeaderStateComputed = computed<MonitorHeaderState>(() => ({
  monitorHealthSummary: monitorHealthSummary.value,
  monitorStatusMessage: monitorStatusMessage.value,
  identifyAvailable: identifyAvailable.value,
  identifyDisabled: identifyDisabled.value,
  isIdentifying: isIdentifying.value,
  isMonitorLoopRunning: isMonitorLoopRunning.value,
}));

const monitorTransportStateComputed = computed<MonitorTransportState>(() => ({
  ports: ports.value.map(p => ({ port_name: p.port_name, description: p.description || undefined })),
  serialPortSelectorDisabled: serialPortSelectorDisabled.value,
  monitorMqttConnected: monitorMqttConnected.value,
}));

const monitorGatewayWarningStateComputed = computed<MonitorGatewayWarningState>(() => ({
  showWarning: !!(
    selectedPort.value &&
    serialDeviceState(selectedPort.value)?.status &&
    !serialDeviceState(selectedPort.value)?.status?.role_tx
  ),
}));

const monitorGatewaySummaryStateComputed = computed<MonitorGatewaySummaryState>(() => {
  const gs = monitorGatewayStatus.value;
  return {
    gatewayName: gs ? lrsDeviceName(gs.chip_id) : '-',
    role: gs?.role || '-',
    localAddress: gs?.local_address ?? '-',
    firmwareVersion: gs ? displayFirmwareVersion(gs.fw_version) : '-',
    uptime: gs?.uptime_ms ? formatUptime(gs.uptime_ms) : '-',
    memory: gs ? formatBytes(gs.heap_free) : '0 B',
    memoryMax: gs ? formatBytes(gs.heap_max_block) : '0 B',
    memoryFrag: gs?.heap_frag_pct !== undefined ? String(gs.heap_frag_pct) : '-',
    wifiStatus: gs?.wifi?.sta_connected ? 'Connected' : (gs?.wifi?.status || '-'),
    wifiIp: gs?.wifi?.ip || '-',
    wifiRssi: gs?.wifi?.rssi ?? '-',
    mqttStatus: gs?.mqtt?.client_enabled ? 'Enabled' : '-',
    mqttHost: gs?.mqtt?.host || '-',
    linkState: gs?.link_state || '-',
    linkPeerCount: gs?.peer_count ?? '-',
    sensors: (gs?.sensors || []).map(s => ({
      kind: s.kind,
      instance: s.instance,
      displayKind: s.kind === 'temperature' ? 'Temperature' : (s.kind === 'tank_level' ? 'Tank Level' : (s.kind === 'input' ? 'Input' : s.kind)),
      displayValue: s.kind === 'input'
        ? (Number(s.value) === 1 ? 'Closed' : 'Open')
        : (s.state === 'ok' ? `${s.value} ${s.unit === 'c' ? '°C' : (s.unit || '')}` : (s.state === 'overrange' ? 'Overrange' : s.state))
    })),
    relayBadgeClass: monitorRelayBadgeClass.value,
    relayBadgeLabel: monitorRelayLabel.value,
    relayStateLabel: gs?.relay_state !== undefined ? (Number(gs.relay_state) === 1 ? 'ON' : 'OFF') : '-',
    inputStateLabel: gs?.input_state !== undefined ? (Number(gs.input_state) === 1 ? 'Closed' : 'Open') : '-',
  };
});

const monitorFleetSummaryStateComputed = computed<MonitorFleetSummaryState>(() => ({
  liveCount: monitorFleetLiveCount.value,
  staleCount: monitorFleetStaleCount.value,
  offlineCount: monitorFleetOfflineCount.value,
  totalCount: monitorFleetRows.value.length,
  hasDiagnosticsData: hasDiagnosticsData.value,
}));

const monitorDisplayRowsComputed = computed<MonitorDisplayRow[]>(() => {
  return monitorFleetRows.value.map(row => ({
    address: row.address,
    chip_id: row.chip_id || '',
    deviceName: lrsDeviceName(row.chip_id),
    freshnessClass: monitorFreshnessClass(row),
    freshnessLabel: monitorFreshnessLabel(row),
    firmwareVersion: displayFirmwareVersion(row.fw_version),
    ip: row.ip || '-',
    relayLabel: remoteRelayLabel(row),
    inputLabel: remoteInputLabel(row),
    tempLabel: remoteTempLabel(row),
    tankLabel: tankLabel(row),
    tankDetailLabel: tankDetailLabel(row),
    wifiConnectedLabel: row.wifi_connected_known ? (row.wifi_connected ? 'Connected' : 'Offline') : 'Unknown',
    rssiLabel: row.rssi !== undefined && row.rssi !== null ? `${row.rssi} dBm` : '-',
    heapLabel: monitorHeapLabel(row),
    fragLabel: monitorFragLabel(row),
    uptimeLabel: monitorUptimeLabel(row),
    poll_pending: !!row.poll_pending,
  }));
});

const serialAdminIsFactoryDefault = computed(() => {
  const st = serialAdminStatus.value;
  return !!st && (!st.commissioned || !!st.fleet_passphrase_default);
});
function gatewayStatusAlertKey(port: string, status?: Pick<SerialAdminStatus, 'chip_id'> | null): string {
  return `${port || 'unknown'}:${status?.chip_id || 'unknown'}`;
}

function isExpectedGatewayRebootPhase(): boolean {
  return ['rebooting', 'waiting', 'updated'].includes(fleetGatewayFlashPhase.value);
}

function noteGatewayStatusForRebootDetection(out: SerialAdminStatus, port: string) {
  if (!port) return;
  const uptimeMs = Number(out.uptime_ms || 0);
  if (!Number.isFinite(uptimeMs) || uptimeMs <= 0) return;

  const key = gatewayStatusAlertKey(port, out);
  const previousUptimeMs = gatewayUptimeHistory.value[key];
  if (
    previousUptimeMs !== undefined &&
    uptimeMs + GATEWAY_UPTIME_ROLLBACK_GRACE_MS < previousUptimeMs &&
    !isExpectedGatewayRebootPhase()
  ) {
    gatewayUnexpectedReboots.value = {
      ...gatewayUnexpectedReboots.value,
      [key]: {
        detectedAtMs: Date.now(),
        previousUptimeMs,
        currentUptimeMs: uptimeMs
      }
    };
  }

  gatewayUptimeHistory.value = {
    ...gatewayUptimeHistory.value,
    [key]: uptimeMs
  };
}

const fleetGatewayUnexpectedReboot = computed<GatewayUnexpectedRebootAlert | null>(() => {
  const status = fleetGatewayStatus.value;
  const key = gatewayStatusAlertKey(targetGatewayKey.value, status);
  const alert = gatewayUnexpectedReboots.value[key];
  if (!alert) return null;
  if (fleetClockMs.value - alert.detectedAtMs > GATEWAY_REBOOT_ALERT_MS) return null;
  return alert;
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
  if (fleetGatewayFlashPhase.value === 'unknown') return 'network status unknown';
  if (fleetGatewayUnexpectedReboot.value) return 'gateway unexpected reboot';
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
  if (fleetGatewayFlashPhase.value === 'unknown') return 'status unknown';
  if (fleetGatewayUnexpectedReboot.value) return 'unexpected reboot';
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
  if (fleetGatewayFlashPhase.value === 'unknown') return 'border-amber-500/40 bg-amber-500/15 text-amber-200';
  if (fleetGatewayUnexpectedReboot.value) return 'border-rose-500/40 bg-rose-500/10 text-rose-300';
  if (fleetGatewayStatus.value) return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (fleetGatewayIdentity.value) return 'border-sky-500/30 bg-sky-500/10 text-sky-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
});
const fleetGatewaySummary = computed(() => {
  const isMqtt = fleetTransport.value === 'mqtt';
  if (fleetGatewayFlashPhase.value === 'flashing') return isMqtt ? 'Serving firmware binary for OTA pull...' : 'Writing firmware over USB serial.';
  if (fleetGatewayFlashPhase.value === 'rebooting') return isMqtt ? 'OTA pull triggered; gateway is resetting.' : 'Flash completed; gateway is resetting.';
  if (fleetGatewayFlashPhase.value === 'waiting') return isMqtt ? 'Network reconnecting...' : 'Waiting for serial admin to return after reboot.';
  if (fleetGatewayFlashPhase.value === 'updated') return isMqtt ? 'Gateway upgraded and reconnected over MQTT.' : 'Gateway responded after flash; status refreshed.';
  if (fleetGatewayFlashPhase.value === 'failed') return isMqtt ? 'Gateway OTA upgrade did not complete; check activity log.' : 'Gateway flash did not complete; check activity log.';
  if (fleetGatewayFlashPhase.value === 'unknown') return 'Gateway rebooted, but network reconnect timed out; upgrade status unknown.';
  const rebootAlert = fleetGatewayUnexpectedReboot.value;
  if (rebootAlert) {
    return `Unexpected gateway reboot detected: uptime rolled back from ${formatUptime(rebootAlert.previousUptimeMs)} to ${formatUptime(rebootAlert.currentUptimeMs)}.`;
  }
  const status = fleetGatewayStatus.value;
  if (status) {
    const wifi = status.wifi?.sta_connected ? `WiFi ${status.wifi.ip || 'connected'}` : `WiFi ${status.wifi?.status || 'offline'}`;
    return `${status.role || 'gateway'} addr ${status.local_address} · ${wifi} · heap ${formatBytes(status.heap_free)} free`;
  }
  if (fleetGatewayIdentity.value) {
    return `Identity loaded · addr ${fleetGatewayIdentity.value.local_addr}->${fleetGatewayIdentity.value.remote_addr}`;
  }
  return isMqtt ? 'Select or load the MQTT gateway to inspect and upgrade it.' : 'Select or load the USB gateway to inspect and flash it.';
});
const fleetScanDisabled = computed(() =>
  isNetworkGatewayLoading.value ||
  isLoraInventoryScanStarting.value ||
  !selectedPort.value ||
  (!isLoraInventoryScanning.value && fleetForceScanCooldownRemainingMs.value > 0)
);

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
  const state = serialDeviceState(gatewaySelectedPort.value);
  const ssid = state?.gatewayWifiReadySsid;
  const ip = state?.gatewayWifiReadyIp;
  if (ssid && ip) {
    return `Gateway WiFi: connected to ${ssid} at ${ip}.`;
  }
  return 'Gateway WiFi: not connected. You can still send credentials to remotes over LoRa.';
});
const gatewayWifiHelpText = computed(() => {
  const state = serialDeviceState(gatewaySelectedPort.value);
  const ssid = state?.gatewayWifiReadySsid;
  const ip = state?.gatewayWifiReadyIp;
  if (ssid && ip) {
    if (ssid === pairWifiSsid.value.trim()) {
      return 'Gateway WiFi is already connected to this network. Enter the WiFi password if you need to send it to remotes.';
    }
    return `Gateway is connected to ${ssid}. You can save a new network on the gateway or send credentials to remotes.`;
  }
  return 'Save WiFi credentials on the gateway or send them to remotes over LoRa.';
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
  return st?.relay_state !== undefined;
});
const monitorRelayOn = computed(() => {
  const st = monitorGatewayStatus.value;
  const relay = st?.relay_state;
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
let unlistenMqttState: UnlistenFn | null = null;
let unlistenMqttTelemetry: UnlistenFn | null = null;
let unlistenMqttGateway: UnlistenFn | null = null;
let unlistenMqttAdminResponse: UnlistenFn | null = null;

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
    activityPanelRef.value?.scrollBulkLogToBottom(port);
  }
  if (port === selectedPort.value) {
    pushSerialLog(line);
  }
}

function pushMonitorLogForPort(port: string, line: string) {
  if (!line || !port) return;
  pushGatewayEventFromLog(port, line);
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

function parseLogNumberField(line: string, field: string): number | undefined {
  const match = line.match(new RegExp(`\\b${field}=(-?\\d+)\\b`));
  if (!match) return undefined;
  const parsed = Number(match[1]);
  return Number.isSafeInteger(parsed) ? parsed : undefined;
}

function gatewayEventLevel(line: string, event: string): GatewayEventRecord['level'] {
  if (isCrashStart(line)) return 'crash';
  if (event === 'boot_banner' || event === 'boot' || event === 'gateway_log_timestamp_reset') return 'reset';
  if (line.includes('[ERROR]') || event.includes('_fail') || event.includes('failed')) return 'error';
  if (line.includes('[WARN]') || event.includes('timeout') || event.includes('_bad')) return 'warn';
  if (event === 'log_line') return 'log_line';
  if (!event || event === 'serial_raw') return 'raw';
  return 'info';
}

function parseGatewayEventLogLine(port: string, line: string): GatewayEventRecord {
  const eventMatch = line.match(/\bevent=([^\s]+)/);
  const hasLogPrefix = /\[(INFO|WARN|ERROR)\]/.test(line);
  const eventName = eventMatch?.[1] || (isCrashStart(line) ? 'crash_signature' : (hasLogPrefix ? 'log_line' : 'serial_raw'));
  return {
    port,
    ms: parseLogNumberField(line, 't') ?? 0,
    event: eventName,
    rssi: parseLogNumberField(line, 'rssi') ?? 0,
    counter: parseLogNumberField(line, 'counter') ?? 0,
    state: parseLogNumberField(line, 'state') ?? 0,
    raw: line,
    level: gatewayEventLevel(line, eventName)
  };
}

function appendGatewayEvent(event: GatewayEventRecord) {
  gatewayEvents.value.push(event);
  if (gatewayEvents.value.length > 2000) {
    gatewayEvents.value = gatewayEvents.value.slice(-2000);
  }
  const crashCount = gatewayEvents.value.filter(e => e.level === 'crash').length;
  const resetCount = gatewayEvents.value.filter(e => e.level === 'reset').length;
  const details = [
    crashCount > 0 ? `${crashCount} crash signature${crashCount === 1 ? '' : 's'}` : '',
    resetCount > 0 ? `${resetCount} reset marker${resetCount === 1 ? '' : 's'}` : ''
  ].filter(Boolean).join(' · ');
  gatewayEventsStatus.value = `${gatewayEvents.value.length} live serial line${gatewayEvents.value.length === 1 ? '' : 's'} captured${details ? ` · ${details}` : ''}.`;
}

function pushGatewayEventFromLog(port: string, line: string) {
  const event = parseGatewayEventLogLine(port, line);

  const lastMs = gatewayEventLastMsByPort.value[port];
  if (event.ms > 0 && lastMs !== undefined && event.ms + GATEWAY_UPTIME_ROLLBACK_GRACE_MS < lastMs) {
    appendGatewayEvent({
      port,
      ms: event.ms,
      event: 'gateway_log_timestamp_reset',
      rssi: 0,
      counter: 0,
      state: 0,
      raw: `Gateway log timestamp rolled back from ${lastMs}ms to ${event.ms}ms.`,
      level: 'reset'
    });
  }
  if (event.ms > 0) {
    gatewayEventLastMsByPort.value = {
      ...gatewayEventLastMsByPort.value,
      [port]: event.ms
    };
  }
  appendGatewayEvent(event);
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
      if (isMqttGatewayKey(known)) continue;

      // Clear physical cached data immediately so a reconnect forces a fresh probe
      const state = serialDevicesByPort.value[known];
      if (state) {
        state.deviceInfo = null;
        state.status = null;
        state.config = null;
        state.adminSupported = false;
        state.adminPassword = '';
      }
      // Increment generation/sequence to invalidate any running get_device_info read
      deviceInfoReadSeqByPort.value = {
        ...deviceInfoReadSeqByPort.value,
        [known]: (deviceInfoReadSeqByPort.value[known] || 0) + 1
      };
      // Delete any in-flight read promise to prevent reconnects from reusing it
      inFlightDeviceInfoReads.value.delete(known);

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
    serialDeviceState();

    const targetPort = selectedPort.value;
    const targetMode = activeMode.value || 'serial';
    if (targetPort) {
      ensureDeviceInfoForPort(targetPort, targetMode).then((ok) => {
        if (selectedPort.value !== targetPort || (activeMode.value || 'serial') !== targetMode) {
          return;
        }
        const state = serialDeviceState(targetPort);
        if (ok && targetMode === 'network') {
          if (state?.status && !state.status.role_tx) {
            networkStatusMessage.value = 'Selected device is not a Gateway.';
            return;
          }
          loadNetworkGateway();
        } else if (ok && targetMode === 'pair') {
          loadEasyPairGateway(true);
        }
      });
    }

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

  const ensureSelection = (port: string, _active: boolean): string => {
    if (currentNames.length === 0) return '';
    if (port && currentNames.includes(port)) return port;
    return activeReplacement;
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

function copySerialAdminConfigJson() {
  const cfg = serialAdminConfig.value;
  if (!cfg) {
    notify('Fetch settings first');
    return;
  }
  copyToClipboard(JSON.stringify(cfg, null, 2), 'device config JSON');
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

function formatGatewayEvent(event: GatewayEventRecord): string {
  const meta = `${event.ms}ms ${event.level} ${event.event || '-'} port=${event.port} rssi=${event.rssi} ctr=${event.counter} st=${event.state}`;
  if (event.level === 'raw' || event.level === 'log_line' || event.level === 'crash' || event.event === 'serial_raw') {
    return `${meta}\n${event.raw}`;
  }
  return `${meta} · ${event.raw}`;
}

function copyGatewayEvents(includeLogLines = true) {
  if (gatewayEvents.value.length === 0) {
    notify('No gateway events to copy');
    return;
  }
  const events = includeLogLines
    ? gatewayEvents.value
    : gatewayEvents.value.filter(event => event.level !== 'log_line');
  if (events.length === 0) {
    notify('No gateway events to copy with current filters');
    return;
  }
  const block = [
    `gateway_events: ${gatewayEventsStatus.value}`,
    ...events.map(formatGatewayEvent)
  ].join('\n');
  copyToClipboard(block, 'gateway events');
}

function inventoryFieldSummary(row: Partial<LoraInventoryDevice> | undefined): string {
  if (!row) return 'missing';
  const sensors = row.sensors?.map(s => `${s.kind}[${s.instance}]=${s.state}${s.value !== undefined ? `:${s.value}${s.unit || ''}` : ''}`).join('|') || '-';
  return [
    `chip=${row.chip_id || '-'}`,
    `fw=${row.fw_version || '-'}`,
    `wifiKnown=${row.wifi_connected_known === true ? '1' : '0'}`,
    `wifi=${row.wifi_connected === true ? '1' : '0'}`,
    `wifiRssi=${row.wifi_rssi_dbm ?? '-'}`,
    `ip=${row.ip || '-'}`,
    `relay=${row.relay_state ?? '-'}`,
    `inputKnown=${row.input_state_known === true ? '1' : '0'}`,
    `input=${row.input_state ?? '-'}`,
    `uptime=${row.uptime_ms ?? '-'}`,
    `age=${row.age_ms ?? '-'}`,
    `rssi=${row.rssi ?? '-'}`,
    `sensors=${sensors}`
  ].join(' ');
}

function copyLoraInventoryDebug() {
  const inventory = lastLoraInventoryStatus.value;
  if (!inventory) {
    notify('No lora_inventory_status response captured yet');
    return;
  }
  const rawRows = inventory.devices || [];
  const rawByAddress = new Map(rawRows.map(row => [row.address, row]));
  const renderedByAddress = new Map(fleetDisplayRows.value.map(row => [row.address, row]));
  const bestRaw = rawRows
    .slice()
    .sort((a, b) => {
      const score = (row: LoraInventoryDevice) =>
        Number(!!row.fw_version) +
        Number(!!row.ip) +
        Number(row.input_state_known === true) +
        Number(row.relay_state !== undefined) +
        Number(!!row.uptime_ms) +
        Number((row.sensors || []).length > 0);
      return score(b) - score(a);
    })[0];
  const addresses = Array.from(new Set([1, 2, bestRaw?.address].filter((addr): addr is number => Number.isFinite(addr))));
  const capturedAt = lastLoraInventoryStatusAtMs.value ? new Date(lastLoraInventoryStatusAtMs.value).toISOString() : 'unknown';
  const lines = [
    `lora_inventory_debug: captured_at=${capturedAt} port=${lastLoraInventoryStatusPort.value || '-'} devices=${rawRows.length} candidates=${inventory.candidates?.length || 0}`,
    `scan=${JSON.stringify(inventory.scan || null)}`,
    ...addresses.flatMap(address => {
      const rendered = renderedByAddress.get(address);
      return [
        `addr ${address} admin_json: ${inventoryFieldSummary(rawByAddress.get(address))}`,
        `addr ${address} fleet_row: chip=${rendered?.deviceName || '-'} fw=${rendered?.fw_version || '-'} wifi=${rendered?.wifi_rssi_dbm ?? (rendered?.wifi_connected ? 'OK' : '-')} ip=${rendered?.ip || '-'} relay=${rendered?.relayLabel || '-'} input=${rendered?.inputLabel || '-'} sensors=${rendered ? [rendered.inputLabel, rendered.tempLabel, rendered.tankLabel].filter(Boolean).join('|') : '-'} uptime=${rendered?.uptimeLabel || '-'} rssi=${rendered?.rssi ?? '-'} age=${rendered?.ageSeconds ?? '-'}`
      ];
    }),
    'raw_lora_inventory_status:',
    JSON.stringify(inventory, null, 2)
  ];
  copyToClipboard(lines.join('\n'), 'lora inventory debug');
}

function clearGatewayEvents() {
  gatewayEvents.value = [];
  gatewayEventLastMsByPort.value = {};
  gatewayEventsStatus.value = 'Gateway events cleared. Waiting for firmware log events.';
}

function copyActivePassword() {
  const password = deviceInfo.value?.password?.trim();
  if (!password) {
    notify('Load device info first to copy password');
    return;
  }
  copyToClipboard(password, 'factory password');
}

function handleCopyLocalGatewaySettings() {
  const settings = `mqtt_client_enabled=true\nmqtt_control_enabled=true\nmqtt_host=${localBrokerState.value.lans[0] || '127.0.0.1'}\nmqtt_port=${localBrokerPort.value}\nmqtt_topic_root=lora`;
  copyToClipboard(settings, 'Local configuration');
}

function handleCopyPayload(payload: { text: string; label: string }) {
  copyToClipboard(payload.text, payload.label);
}

function handleMonitorPollDeviceDiagnostics(address: number) {
  const d = monitorFleetRows.value.find(x => x.address === address);
  if (d) executeRemotePollDiagnostics(d);
}

function handleFleetRemoteFlash(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) flashLoraRemote(d);
}

function handleFleetRemoteSettings(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) openSettingsModal(d);
}

function handleFleetRemoteReboot(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) executeRemoteReboot(d);
}

function handleFleetRemoteViewLogs(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) triggerRemoteUdpLogging(d);
}

function handleFleetRemoteForget(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) executeForgetRemote(d);
}

function handleFleetRemoteFactoryReset(address: number) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) openFactoryResetModal(d);
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
  pairFleetKeySource.value = 'factory_generated';
}

function clearPairFleetKey() {
  pairFleetKey.value = '';
  pairFleetKeySource.value = 'none';
}

function markPairFleetKeyManual() {
  pairFleetKeySource.value = 'manual';
}

function requireProvisionFleetKeyAuthority() {
  const status = serialDeviceState(pairGatewayKey.value)?.status;
  const isCommissioned = status?.commissioned === true && status?.fleet_passphrase_default !== true;
  if (isCommissioned && pairFleetKeySource.value !== 'gateway' && pairFleetKeySource.value !== 'manual') {
    throw new Error('commissioned gateway fleet key was not fetched from the gateway; load gateway again');
  }
  if (!isCommissioned && !pairFleetKey.value.trim()) {
    throw new Error('fleet key is empty; load a factory gateway or enter a key explicitly');
  }
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



function getLocalHostIpForTarget(targetIp?: string): string | null {
  if (!flasherInterfaces.value || flasherInterfaces.value.length === 0) {
    return null;
  }
  if (targetIp) {
    const targetParts = targetIp.split('.').map(Number);
    for (const iface of flasherInterfaces.value) {
      if (!iface.ip || !iface.netmask) continue;
      const ipParts = iface.ip.split('.').map(Number);
      const maskParts = iface.netmask.split('.').map(Number);
      let match = true;
      for (let i = 0; i < 4; i++) {
        if ((targetParts[i] & maskParts[i]) !== (ipParts[i] & maskParts[i])) {
          match = false;
          break;
        }
      }
      if (match) {
        return iface.ip;
      }
    }
  }
  const nonLoopback = flasherInterfaces.value.find(i => i.ip && !i.ip.startsWith('127.'));
  return nonLoopback ? nonLoopback.ip : null;
}

async function startNetworkUdpMonitor(): Promise<boolean> {
  try {
    const started = await invoke<string>('start_network_udp_monitor');
    pushNetworkLog(started);
    isNetworkUdpMonitoring.value = true;
    networkUdpTarget.value = 'Local Listener';
    networkUdpLogsExpanded.value = false;
    nextTick(() => scrollNetworkUdpToBottom());
    return true;
  } catch (e) {
    pushNetworkLog('UDP monitor start error: ' + e);
    notify('UDP monitor start error: ' + e);
    return false;
  }
}

async function triggerGatewayUdpLogging() {
  const hostIp = getLocalHostIpForTarget();
  if (!hostIp) {
    pushNetworkLog('Failed: No reachable Flasher LAN address available for UDP forwarding.');
    notify('No reachable Flasher LAN address');
    return;
  }

  if (!await startNetworkUdpMonitor()) {
    return;
  }
  const port = fleetGatewayCommandTarget().port;
  if (port) {
    const isMqtt = isMqttGatewayKey(port);
    if (isMqtt) {
      const password = fleetGatewayCommandTarget().password;
      if (!password) {
        pushNetworkLog('Gateway password not loaded; skipping gateway-side UDP log enable command.');
        return;
      }
      pushNetworkLog(`Requesting gateway UDP log forwarding to host IP: ${hostIp}...`);
      try {
        const res = await sendEasyPairCommandOnPort(port, 'udp_log_control', {
          admin_password: password,
          enabled: true,
          host: hostIp,
          port: 5514,
          ttl_s: 300
        }, 8000);
        pushNetworkLog('Gateway UDP logging enabled successfully: ' + JSON.stringify(res));
      } catch (e) {
        pushNetworkLog('Failed to enable gateway-side UDP log control: ' + e);
      }
    } else {
      pushNetworkLog('Gateway UDP logging is MQTT-only. Skipping gateway-side control command (connected via USB serial).');
    }
  }
}

async function triggerRemoteUdpLogging(device: LoraInventoryDevice) {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }

  const hostIp = getLocalHostIpForTarget(device.ip);
  if (!hostIp) {
    pushNetworkLog('Failed: No reachable Flasher LAN address available for UDP forwarding.');
    notify('No reachable Flasher LAN address');
    return;
  }

  if (!isNetworkUdpMonitoring.value) {
    if (!await startNetworkUdpMonitor()) {
      return;
    }
  }

  notify(`Requesting remote ${device.address} to mirror UDP logs to ${hostIp}...`);
  try {
    const res = await sendEasyPairCommandOnPort(port, 'remote_udp_log_control', {
      admin_password: password,
      address: device.address,
      enabled: true,
      host: hostIp,
      port: 5514,
      ttl_s: 300
    }, 8000);
    notify(`Remote UDP logging triggered for address ${device.address}`);
    pushNetworkLog(`Remote UDP logging enabled for Address ${device.address} sending to ${hostIp}: ` + JSON.stringify(res));
    networkUdpTarget.value = `Addr ${device.address}`;
    activeRemoteUdpAddress.value = device.address;
  } catch (e) {
    const msg = serialFeatureError(`Remote UDP log control`, e);
    notify(msg);
    pushNetworkLog(msg);
  }
}

async function stopNetworkUdpMonitor() {
  try {
    const stopped = await invoke<string>('stop_network_udp_monitor');
    pushNetworkLog(stopped);
    const port = fleetGatewayCommandTarget().port;
    const password = fleetGatewayCommandTarget().password;
    if (port && password) {
      if (activeRemoteUdpAddress.value !== null) {
        pushNetworkLog(`Requesting remote ${activeRemoteUdpAddress.value} to stop UDP log forwarding...`);
        try {
          await sendEasyPairCommandOnPort(port, 'remote_udp_log_control', {
            admin_password: password,
            address: activeRemoteUdpAddress.value,
            enabled: false
          }, 8000);
          pushNetworkLog(`Remote ${activeRemoteUdpAddress.value} UDP logging disabled.`);
        } catch (e) {
          pushNetworkLog(`Failed to disable remote UDP logging on Address ${activeRemoteUdpAddress.value}: ` + e);
        }
      }

      if (isMqttGatewayKey(port)) {
        pushNetworkLog('Requesting gateway to stop UDP log forwarding...');
        try {
          await sendEasyPairCommandOnPort(port, 'udp_log_control', {
            admin_password: password,
            enabled: false
          }, 8000);
          pushNetworkLog('Gateway UDP logging disabled.');
        } catch (e) {
          pushNetworkLog('Failed to disable gateway-side UDP logging: ' + e);
        }
      }
    }
  } catch (e) {
    pushNetworkLog('UDP monitor stop error: ' + e);
  } finally {
    isNetworkUdpMonitoring.value = false;
    networkUdpTarget.value = '';
    activeRemoteUdpAddress.value = null;
    networkUdpLogsExpanded.value = false;
  }
}

function pairPassword(): string {
  return adminPasswordForPort(pairGatewayKey.value);
}

function fleetGatewayCommandTarget() {
  const isMqtt = fleetTransport.value === 'mqtt';
  const port = isMqtt ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
  const password = adminPasswordForPort(port);
  return { port, password, isMqtt };
}

function monitorGatewayCommandTarget() {
  const isMqtt = monitorTransport.value === 'mqtt';
  const port = isMqtt ? selectedMqttGatewayChipId.value : monitorSelectedPort.value;
  const password = adminPasswordForPort(port);
  return { port, password, isMqtt };
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

function normalizeRole(role: string | null | undefined): string {
  const r = String(role || '').trim().toLowerCase();
  if (r === 'tx' || r === 'transmitter' || r === 'gateway') {
    return 'gateway';
  }
  if (r === 'rx' || r === 'receiver' || r === 'remote') {
    return 'remote';
  }
  return r;
}

function canonicalChipId(raw: string | undefined | null): string {
  const norm = normalizeChipId(raw);
  if (!norm) return '';
  return norm.length < 8 ? norm.padStart(8, '0') : norm;
}

function isMqttGatewayKey(key: string): boolean {
  if (!key) return false;
  return key.startsWith('lrs-') || /^[0-9a-fA-F]{6,8}$/.test(key);
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
  if (major === 0 && minor === 0 && patch === 0) return '-';
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
    await forceDeviceInfoReadForOperation(port, 'serial');
  }
}

async function sendPairCommand<T = any>(cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000, options: SerialJobOptions = {}): Promise<T> {
  return sendEasyPairCommandOnPort<T>(pairGatewayKey.value, cmd, payload, timeoutMs, options);
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
    lora_spreading_factor: numberValue(cfg.lora_spreading_factor, 7),
    lora_bandwidth_hz: numberValue(cfg.lora_bandwidth_hz, 125000),
    lora_coding_rate: numberValue(cfg.lora_coding_rate, 5),
    heartbeat_ms: numberValue(cfg.heartbeat_ms, 60000),
    heartbeat_enabled: boolValue(cfg.heartbeat_enabled, true),
    ack_timeout_ms: numberValue(cfg.ack_timeout_ms, 3000),
    mqtt_remote_retry_timeout_ms: numberValue(cfg.mqtt_remote_retry_timeout_ms, 180000),
    tx_mqtt_remote_polling_enabled: boolValue(cfg.tx_mqtt_remote_polling_enabled, false),
    tx_mqtt_remote_default_poll_interval_ms: numberValue(cfg.tx_mqtt_remote_default_poll_interval_ms, 300000),
    input_control_paired_lora_enabled: boolValue(cfg.input_control_paired_lora_enabled, false),
    tx_command_retry_timeout_ms: numberValue(cfg.tx_command_retry_timeout_ms, 180000),
    rx_failsafe_mode: stringValue(cfg.rx_failsafe_mode, 'hold_last'),
    rx_failsafe_timeout_ms: numberValue(cfg.rx_failsafe_timeout_ms, 180000),
    wifi_sta_ssid: stringValue(cfg.wifi_sta_ssid, wifi?.sta_ssid || ''),
    wifi_sta_password: settingsTransport.value === 'mqtt' ? '' : getCachedWifiPassword(stringValue(cfg.wifi_sta_ssid, wifi?.sta_ssid || '')),
    lan_hostname: stringValue(cfg.lan_hostname, ''),
    ap_always_on: boolValue(cfg.ap_always_on, false),
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
    sensor_temp_enabled: boolValue(cfg.sensor_temp_enabled, !!status?.sensors?.some(s => s.kind === 'temperature' && sensorTelemetryStateEnabled(s.state))),
    sensor_temp_pin: numberValue(cfg.sensor_temp_pin, 0),
    sensor_temp_interval_s: numberValue(cfg.sensor_temp_interval_s, 10),
    sensor_tank_enabled: boolValue(cfg.sensor_tank_enabled, !!status?.sensors?.some(s => s.kind === 'tank_level' && sensorTelemetryStateEnabled(s.state))),
    sensor_tank_range_mm: numberValue(cfg.sensor_tank_range_mm, 5000),
    sensor_tank_vref_mv: numberValue(cfg.sensor_tank_vref_mv, 3553),
    sensor_tank_sense_ohms: numberValue(cfg.sensor_tank_sense_ohms, 120),
    sensor_tank_interval_s: numberValue(cfg.sensor_tank_interval_s, 5),
    fleet_passphrase: stringValue(cfg.fleet_passphrase, ''),
    admin_password: ''
  };
}

const {
  sendMqttAdminCommand,
  handleMqttAdminResponse
} = useMqttAdmin({
  monitorMqttConnected,
  monitorMqttTopicRoot,
  adminPasswordForPort,
  normalizeChipId
});

const {
  serialAdminBusyForPort,
  runSerialAdminCommand,
  serialBackgroundSkipped
} = useSerialAdmin({
  invokeSerialAdminCommand: async (port, cmd, payload, timeoutMs) => {
    return await invoke('serial_admin_command', {
      port,
      request: { cmd, ...payload },
      timeoutMs
    });
  },
  onBeforeSerialCommand: (port, _cmd) => {
    noteMonitorReleasedForPort(port, 'serial admin command needs this port');
  }
});

async function sendEasyPairCommandOnPort<T = any>(port: string, cmd: string, payload: Record<string, any> = {}, timeoutMs = 8000, options: SerialJobOptions = {}): Promise<T> {
  if (!port) throw new Error('Select the USB gateway first');

  const isMqtt = isMqttGatewayKey(port);
  if (isMqtt) {
    const targetChipId = port.replace(/^lrs-/, '');
    return await sendMqttAdminCommand<T>(targetChipId, cmd, payload, timeoutMs);
  }

  return await runSerialAdminCommand<T>(port, cmd, payload, timeoutMs, options);
}

async function loadNetworkGateway() {
  if (fleetTransport.value === 'mqtt') {
    const chipId = selectedMqttGatewayChipId.value;
    if (!chipId || isNetworkGatewayLoading.value) return;
    isNetworkGatewayLoading.value = true;
    networkStatusMessage.value = `Loading MQTT gateway lrs-${chipId}...`;
    try {
      const gw = mqttGateways.value[chipId];
      const state = serialDeviceState(chipId);
      if (state) {
        await ensureMqttGatewayDefaultState(chipId);
        state.deviceInfo = {
          ...state.deviceInfo!,
          mac: gw?.mac || state.deviceInfo?.mac || '',
          ssid: gw?.sta_ssid || state.deviceInfo?.ssid || ''
        };
      }
      await refreshGatewaySnapshot(chipId, false, 'fleet');
      networkStatusMessage.value = `Gateway loaded over MQTT; firmware ${gw?.fw_version || 'unknown'}.`;
      startLoraInventoryPolling();
    } catch (e) {
      networkStatusMessage.value = `MQTT Gateway load error: ${e}`;
      notify(networkStatusMessage.value);
    } finally {
      isNetworkGatewayLoading.value = false;
    }
    return;
  }

  const port = gatewaySelectedPort.value;
  if (!port || isNetworkGatewayLoading.value) return;
  isNetworkGatewayLoading.value = true;
  networkStatusMessage.value = `Loading gateway on ${port}...`;
  try {
    const ok = await ensureDeviceInfoForPort(port, 'network');
    if (!ok || !serialDeviceState(port)?.deviceInfo) {
      throw new Error('Unable to read device details');
    }
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

function clearFleetGatewayCache() {
  if (fleetScanSettleTimer.value) {
    window.clearTimeout(fleetScanSettleTimer.value);
    fleetScanSettleTimer.value = null;
  }
  clearFleetGatewayCacheComposable();
  loraAdoptionStatus.value = null;
  isLoraInventoryScanning.value = false;
  isLoraInventoryScanStarting.value = false;
  fleetScanActiveSinceMs.value = 0;
  stopLoraInventoryPolling(false);
}

function mergeLoraInventoryRows(rows: LoraInventoryDevice[]) {
  const processed = mergeInventoryRows(rows);
  if (gatewaySelectedPort.value && gatewaySelectedPort.value === monitorSelectedPort.value) {
    monitorFleetRows.value = mergeMonitorRows(processed);
  }
}

function mergeLoraInventorySeedRows(rows: LoraInventoryDevice[]) {
  const existingByAddress = new Map(loraInventory.value.map(row => [row.address, row]));
  mergeLoraInventoryRows(rows.map(row => ({
    ...(existingByAddress.get(row.address) || {}),
    ...row,
  })));
}

function mergeLoraInventoryRow(row: LoraInventoryDevice) {
  mergeLoraInventoryRows([
    ...loraInventory.value.filter(existing => existing.address !== row.address),
    row
  ]);
}

function mergeMonitorSeedRows(rows: LoraInventoryDevice[]) {
  const existingByAddress = new Map(monitorFleetRows.value.map(row => [row.address, row]));
  monitorFleetRows.value = mergeMonitorRows(rows.map(row => ({
    ...(existingByAddress.get(row.address) || {}),
    ...row,
  })));
}

function scheduleFleetScanSettleRefresh(port: string) {
  if (fleetScanSettleTimer.value) {
    window.clearTimeout(fleetScanSettleTimer.value);
  }
  fleetScanSettleTimer.value = window.setTimeout(() => {
    fleetScanSettleTimer.value = null;
    const targetPort = fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
    if (activeMode.value !== 'network' || port !== targetPort || isLoraInventoryScanning.value) return;
    void refreshGatewaySnapshot(port, true, 'fleet');
  }, FLEET_SCAN_SETTLE_REFRESH_MS);
}

async function refreshLoraInventoryPeers(port: string, addresses: number[], background: boolean, source: 'fleet' | 'monitor') {
  const uniqueAddresses = Array.from(new Set(addresses.filter(address => Number.isFinite(address) && address >= 1 && address <= LRS_REMOTE_SCAN_CAP)))
    .sort((a, b) => a - b);
  const password = adminPasswordForPort(port);
  for (const address of uniqueAddresses) {
    try {
      if (password) {
        await sendEasyPairCommandOnPort(
          port,
          'refresh_lora_peer',
          { admin_password: password, address },
          2500,
          { label: `Refresh LoRa peer ${address}`, priority: background ? 'background' : 'user', dropIfBusy: background }
        );
        await new Promise(resolve => setTimeout(resolve, FLEET_PEER_REFRESH_SETTLE_MS));
      }
      const peer = await sendEasyPairCommandOnPort<LoraInventoryPeerStatus>(
        port,
        'lora_inventory_peer',
        { address },
        3000,
        { label: `Gateway peer ${address}`, priority: background ? 'background' : 'user', dropIfBusy: background }
      );
      if (!peer.device) continue;
      if (source === 'fleet') {
        const targetPort = fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
        if (port === targetPort) {
          mergeLoraInventoryRow(peer.device);
        }
      }
      const targetMonitor = monitorTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : monitorSelectedPort.value;
      if (source === 'monitor' && port === targetMonitor) {
        monitorFleetRows.value = mergeMonitorRows([
          ...monitorFleetRows.value.filter(existing => existing.address !== peer.device!.address),
          peer.device
        ]);
      }
    } catch (e) {
      if (serialBackgroundSkipped(e)) return;
      if (!background) {
        pushNetworkLog(`Peer ${address} inventory read failed: ${String(e || '')}`);
      }
    }
  }
}

function nextFleetHydrateAddress(addresses: number[]): number | null {
  const uniqueAddresses = Array.from(new Set(addresses.filter(address => Number.isFinite(address) && address >= 1 && address <= LRS_REMOTE_SCAN_CAP)))
    .sort((a, b) => a - b);
  if (uniqueAddresses.length === 0) return null;
  const next = uniqueAddresses[fleetInventoryHydrateCursor.value % uniqueAddresses.length];
  fleetInventoryHydrateCursor.value = (fleetInventoryHydrateCursor.value + 1) % uniqueAddresses.length;
  return next;
}




function fleetFreshnessClass(row: LoraInventoryDevice, ageMs?: number): string {
  const state = rowFreshness(row, ageMs);
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
  const uptime = row.uptime_ms;
  if (!uptime) return 'waiting';
  return formatUptime(uptime + Number(row.age_ms || 0));
}

function remoteInputLabel(row: LoraInventoryDevice): string {
  const value = row.input_state_known ? row.input_state : undefined;
  if (value === undefined || value === null) return 'waiting';
  return Number(value) === 1 ? 'Closed' : 'Open';
}

function remoteRelayLabel(row: LoraInventoryDevice): string {
  const value = row.relay_state;
  if (value === undefined || value === null) return 'waiting';
  return Number(value) === 1 ? 'On' : 'Off';
}

function remoteTempLabel(row: LoraInventoryDevice): string {
  const s = row.sensors?.find(x => x.kind === 'temperature');
  if (!s || s.state === 'disabled') return '-';
  if (s.state === 'ok') return `${s.value} °C`;
  return s.state;
}

function tankLabel(row: LoraInventoryDevice): string {
  const s = row.sensors?.find(x => x.kind === 'tank_level');
  if (!s || s.state === 'disabled') return '-';
  if (s.state === 'ok') return `${s.value} mm`;
  if (s.state === 'overrange') return 'Overrange';
  if (s.state === 'fault') return 'Fault';
  if (s.state === 'missing') return 'Missing';
  return 'waiting';
}

function tankDetailLabel(_row: LoraInventoryDevice): string {
  return '';
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

  if (background && isMonitoring.value && activeMonitorPort.value === port) {
    if (source === 'fleet') {
      networkStatusMessage.value = 'Background gateway refresh paused while serial monitor owns this port.';
    } else {
      monitorStatusMessage.value = 'Background refresh paused while serial monitor owns this port.';
    }
    return;
  }

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
        clearFleetGatewayCache();
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
      FLEET_INVENTORY_STATUS_TIMEOUT_MS,
      { label: 'Gateway peer cache snapshot', priority: background ? 'background' : 'user', dropIfBusy: background }
    );
    lastLoraInventoryStatus.value = inventory;
    lastLoraInventoryStatusAtMs.value = Date.now();
    lastLoraInventoryStatusPort.value = port;

    const targetPort = fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
    if (source === 'fleet' && port === targetPort) {
      const wasScanning = isLoraInventoryScanning.value;
      const scanActive = !!inventory.scan?.active;
      const inventoryAddresses = (inventory.devices || []).map(device => device.address);
      loraInventoryScan.value = inventory.scan || null;
      mergeLoraInventorySeedRows(inventory.devices || []);
      const hydrateAddress = fleetTransport.value === 'serial' ? nextFleetHydrateAddress(inventoryAddresses) : null;
      if (hydrateAddress !== null) {
        await refreshLoraInventoryPeers(port, [hydrateAddress], background, 'fleet');
      }
      loraCandidates.value = (inventory.candidates || []).map(c => deriveCandidateLocalTimestamp(c, fleetClockMs.value));
      candidateTotal.value = inventory.candidate_total || (inventory.candidates || []).length;
      candidateTruncated.value = !!inventory.candidate_truncated;
      loraAdoptionStatus.value = inventory.adoption || null;
      isLoraInventoryScanning.value = scanActive;
      isLoraInventoryScanStarting.value = false;
      if (scanActive && fleetScanActiveSinceMs.value === 0) {
        fleetScanActiveSinceMs.value = Date.now();
      }
      if (!scanActive) {
        fleetScanActiveSinceMs.value = 0;
      }
      networkStatusMessage.value = `${loraInventoryProgressLabel.value}; gateway cache has ${loraInventory.value.length} peer${loraInventory.value.length === 1 ? '' : 's'}.`;
      if (wasScanning && !scanActive) {
        scheduleFleetScanSettleRefresh(port);
      }
      if (!scanActive && networkInventoryPollMode.value === 'scan') {
        stopLoraInventoryPolling(false);
        startFleetCachePolling();
      }
    }
    const targetMonitor = monitorTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : monitorSelectedPort.value;
    if (source === 'monitor' && port === targetMonitor) {
      mergeMonitorSeedRows(inventory.devices || []);
      const monitorAddresses = (inventory.devices || []).map(device => device.address);
      const hydrateAddress = monitorTransport.value === 'serial' ? nextFleetHydrateAddress(monitorAddresses) : null;
      if (hydrateAddress !== null) {
        await refreshLoraInventoryPeers(port, [hydrateAddress], background, 'monitor');
      }
      monitorStatusMessage.value = `Updated ${new Date().toLocaleTimeString()} · ${monitorFleetRows.value.length} peer${monitorFleetRows.value.length === 1 ? '' : 's'} visible.`;
    }
  } catch (e) {
    if (serialBackgroundSkipped(e)) return;
    if (source === 'fleet') {
      const errMsg = String(e || '');
      if (background && errMsg.includes('timed out waiting for')) {
        if (shouldClearScanStateOnTimeout(isLoraInventoryScanning.value, fleetScanActiveSinceMs.value, Date.now())) {
          isLoraInventoryScanning.value = false;
          fleetScanActiveSinceMs.value = 0;
          loraInventoryScan.value = null;
          if (networkInventoryPollMode.value === 'scan') {
            stopLoraInventoryPolling(false);
            startFleetCachePolling();
          }
        }
        return;
      }
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



async function ensureFleetGatewayStatus(force = false): Promise<SerialAdminStatus | null> {
  const port = fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
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
  if (isLoraInventoryScanning.value || isLoraInventoryScanStarting.value) return;
  const { port } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  const remaining = fleetForceScanCooldownRemainingMs.value;
  if (remaining > 0) {
    notify(`Scan available in ${Math.ceil(remaining / 1000)}s`);
    return;
  }
  await beginLoraInventoryScan(port, true);
}

async function beginLoraInventoryScan(port: string, showErrors = true) {
  isLoraInventoryScanStarting.value = true;
  networkStatusMessage.value = 'Starting LoRa inventory scan...';
  try {
    await withGatewayForeground(port, async () => {
      if (!portGatewayReady(port)) await loadNetworkGateway();
      const { password } = fleetGatewayCommandTarget();
      if (!password) {
        isLoraInventoryScanStarting.value = false;
        if (showErrors) notify('Unable to read the gateway admin password from device details');
        return;
      }
      const gatewayStatus = await ensureFleetGatewayStatus(true);
      const blockedMessage = fleetScanBlockedMessage(gatewayStatus);
      if (blockedMessage) {
        isLoraInventoryScanning.value = false;
        isLoraInventoryScanStarting.value = false;
        networkStatusMessage.value = blockedMessage;
        if (showErrors) notify(blockedMessage);
        return;
      }
      isLoraInventoryScanning.value = true;
      if (fleetScanActiveSinceMs.value === 0) {
        fleetScanActiveSinceMs.value = Date.now();
      }
      fleetForceScanCooldownUntilMs.value = Math.max(fleetForceScanCooldownUntilMs.value, Date.now() + FLEET_FORCE_SCAN_COOLDOWN_MS);
      await sendEasyPairCommandOnPort(port, 'start_lora_inventory', {
        admin_password: password,
        start_address: 1,
        end_address: LRS_REMOTE_SCAN_CAP,
        interval_ms: 1500
      }, 8000);
      isLoraInventoryScanStarting.value = false;
      networkStatusMessage.value = 'LoRa inventory scan started.';
      startLoraInventoryPolling();
      void refreshLoraInventoryStatus(true);
    });
  } catch (e) {
    isLoraInventoryScanning.value = false;
    isLoraInventoryScanStarting.value = false;
    fleetScanActiveSinceMs.value = 0;
    networkStatusMessage.value = fleetScanErrorMessage(e);
    if (showErrors) notify(networkStatusMessage.value);
  }
}

async function cancelLoraInventoryScan() {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    await withGatewayForeground(port, async () => {
      await sendEasyPairCommandOnPort(port, 'cancel_lora_inventory', { admin_password: password }, 5000);
      isLoraInventoryScanStarting.value = false;
      fleetScanActiveSinceMs.value = 0;
      stopLoraInventoryPolling();
      await refreshLoraInventoryStatus(false);
    });
    startFleetCachePolling();
  } catch (e) {
    notify(serialFeatureError('Cancel LoRa inventory', e));
  }
}

const flasherInterfaces = ref<NetworkInterface[]>([]);






function openSettingsModal(device: LoraInventoryDevice, tab: SettingsModalState['activeTab'] = 'sensors') {
  activeDropdownAddress.value = null;
  const ssid = pairWifiSsid.value || '';
  settingsDeviceModal.value = {
    device,
    activeTab: tab,
    wifi_ssid: ssid,
    wifi_password: getCachedWifiPassword(ssid) || pairAdminPassword.value || '',
    sensor_temp_enabled: !!device.sensors?.some(s => s.kind === 'temperature' && sensorTelemetryStateEnabled(s.state)),
    sensor_tank_enabled: !!device.sensors?.some(s => s.kind === 'tank_level' && sensorTelemetryStateEnabled(s.state)),
    power_save_listen_only: !!device.power_save_listen_only,
    fleet_key: '',
    fleet_key_confirmed: false,
    show_fleet_key: false
  };
}

async function executeRemoteWifi(device: LoraInventoryDevice, ssid: string, password_value: string) {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
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


async function executeRemoteSensors(device: LoraInventoryDevice, tempEnabled: boolean, tankEnabled: boolean, powerSaveEnabled: boolean) {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    const currentPowerSave = device.power_save_listen_only === true;
    const isPowerSaveChange = powerSaveEnabled !== currentPowerSave;
    const actionLabel = isPowerSaveChange ? 'power configuration' : 'sensor configuration';
    notify(`Transmitting ${actionLabel} to remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'remote_sensor_config', {
      admin_password: password,
      target_address: device.address,
      sensor_temp_enabled: tempEnabled,
      sensor_tank_enabled: tankEnabled,
      power_save_listen_only: powerSaveEnabled
    }, 8000);
    notify(`${actionLabel.charAt(0).toUpperCase() + actionLabel.slice(1)} transmitted successfully to remote ${device.address}`);
    // Save to history so it survives refreshes
    const now = Date.now();
    const nextHistory = {
      ...(fleetRowHistory.value[device.address] || {})
    };
    if (isPowerSaveChange) {
      nextHistory.pendingPowerSaveListenOnly = powerSaveEnabled;
      nextHistory.pendingPowerSaveTxMs = now;
    }
    fleetRowHistory.value[device.address] = nextHistory;
    loraInventory.value = loraInventory.value.map(row => {
      if (row.address === device.address) {
        const nextRow = {
          ...row,
          temp_enabled: tempEnabled,
          tank_enabled: tankEnabled
        };
        if (isPowerSaveChange) {
          return {
            ...nextRow,
            pending_power_save_listen_only: powerSaveEnabled,
            pending_power_save_tx_ms: now
          };
        }
        return nextRow;
      }
      return row;
    });
    settingsDeviceModal.value = null;
  } catch (e) {
    const msg = serialFeatureError(`Remote sensors update`, e);
    notify(msg);
  }
}

async function executeRemotePollDiagnostics(device: LoraInventoryDevice) {
  activeDropdownAddress.value = null;
  const { port, password } = monitorGatewayCommandTarget();
  if (!port) {
    notify(monitorTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  try {
    notify(`Requesting one-shot diagnostics polling from remote ${device.address}...`);
    await sendEasyPairCommandOnPort(port, 'poll_diagnostics', {
      admin_password: password,
      address: device.address
    }, 8000);
    notify(`Diagnostics polling request sent to remote ${device.address}`);
  } catch (e) {
    const msg = serialFeatureError(`Diagnostics polling request`, e);
    notify(msg);
  }
}

async function executeSelectedMonitorPollDiagnostics() {
  if (selectedMonitorDeviceAddress.value === null) return;
  const dev = monitorFleetRows.value.find(d => d.address === selectedMonitorDeviceAddress.value);
  if (dev) {
    await executeRemotePollDiagnostics(dev);
  }
}

async function executeRemoteReboot(device: LoraInventoryDevice) {
  activeDropdownAddress.value = null;
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
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

async function adoptCandidate(candidate: LoraAdoptionCandidate) {
  const isMqtt = fleetTransport.value === 'mqtt';
  const port = isMqtt ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
  const password = pairAdminPassword.value;

  if (!port) {
    notify(isMqtt ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }

  const label = candidate.chip_id ? `remote chip ${candidate.chip_id}` : `remote at address ${candidate.address}`;
  if (!await confirmOperatorAction(`Adopt candidate ${label}?`, { confirmText: 'Adopt remote', danger: false })) {
    return;
  }

  try {
    notify(`Adopting candidate ${label}...`);
    await sendEasyPairCommandOnPort(port, 'adopt_candidate', {
      admin_password: password,
      chip_id: candidate.chip_id
    }, 8000);
    notify(`Adoption request sent for candidate ${label}.`);

    // Ensure active polling starts/continues
    if (isMqtt) {
      startLoraInventoryPolling();
    } else {
      startFleetCachePolling();
    }

    await refreshLoraInventoryStatus(false);
  } catch (e) {
    const msg = serialFeatureError(`Candidate adoption`, e);
    notify(msg);
  }
}

function openFactoryResetModal(device: LoraInventoryDevice) {
  activeDropdownAddress.value = null;
  factoryResetTargetModal.value = {
    device,
    keep_shared_fleet_key: false,
    keep_wifi_credentials: false
  };
}

async function executeRemoteFactoryReset(device: LoraInventoryDevice, keepFleet: boolean, keepWifi: boolean) {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
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
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
  if (!password) {
    notify('Enter the gateway admin password');
    return;
  }
  const deviceName = device.chip_id ? lrsDeviceName(device.chip_id) : `Address ${device.address}`;
  const confirmed = await confirmOperatorAction(
    `Remove remote device ${deviceName} from Gateway?\n\nThis will permanently delete its address and name pairing from the gateway configuration.\n\nThe remote itself is not reset. It remains on this fleet key and may appear as a same-key adoption candidate.`,
    { confirmText: 'Remove from Gateway', danger: true }
  );
  if (!confirmed) return;

  notify(`Removing remote ${deviceName}...`);
  try {
    await sendEasyPairCommandOnPort(port, 'forget_gateway_target', {
      admin_password: password,
      address: device.address
    });
    notify(`Successfully removed remote ${deviceName}`);
    loraInventory.value = loraInventory.value.filter(d => d.address !== device.address);
    refreshLoraInventoryStatus(false);
  } catch (e) {
    const msg = serialFeatureError(`Remove remote`, e);
    notify(msg);
  }
}


async function executeRemoteFleetKeyChange(device: LoraInventoryDevice, newKey: string) {
  const { port, password } = fleetGatewayCommandTarget();
  if (!port) {
    notify(fleetTransport.value === 'mqtt' ? 'Select the MQTT gateway first' : 'Select the USB gateway first');
    return;
  }
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
  const { port, isMqtt } = fleetGatewayCommandTarget();
  if (!port) return isMqtt ? 'Select an MQTT gateway first' : 'Select a USB gateway first';
  if (isMqtt && !monitorMqttConnected.value) return 'MQTT broker is not connected. Connect in the Monitor tab first.';
  if (isMqtt && !fleetGatewayCommandTarget().password) return 'Enter the gateway admin password';
  if (fleetGatewayFlashPhase.value !== 'idle') return 'Gateway flash is already running';
  if (isNetworkGatewayLoading.value) return 'Gateway identity is loading';
  if (isLoraInventoryScanning.value) return 'Stop the fleet scan before flashing the gateway';
  if (hasActiveRemoteOtaPulls.value) return 'Wait for remote OTA pulls to finish';
  if (isFirmwareServerStarting.value) return 'Firmware server is starting';
  if (isPairBusy.value) return 'Provisioning is active';
  return isMqtt ? 'Upgrade the selected MQTT gateway via OTA' : 'Upgrade the selected USB gateway';
}

async function flashFleetGateway() {
  const { port, password, isMqtt } = fleetGatewayCommandTarget();
  if (!port) {
    notify(isMqtt ? 'Select an MQTT gateway first' : 'Select a USB gateway first');
    return;
  }
  if (fleetGatewayFlashDisabled.value) {
    notify(fleetGatewayFlashUnavailableReason());
    return;
  }
  const firmwareOptions = networkOtaFirmwareOptions();
  if (!firmwareOptions) return;
  const label = isMqtt ? `lrs-${port}` : (fleetGatewayIdentity.value?.ssid || port);
  const currentFw = fleetGatewayStatus.value?.fw_version || 'unknown';
  const targetFw = selectedVersion.value.startsWith(LOCAL_LABEL_PREFIX)
    ? `${flasherAppVersion.value} (local build)`
    : selectedVersion.value;

  const confirmMsg = isMqtt
    ? `Upgrade the MQTT gateway ${label} via OTA?\n\n` +
      `• Current version: ${currentFw}\n` +
      `• Upgrade version: ${targetFw}\n\n` +
      `This will trigger the gateway to download the update and reboot. Gateway operations will temporarily pause.`
    : `Upgrade the USB gateway ${label} on ${port}?\n\n` +
      `• Current version: ${currentFw}\n` +
      `• Upgrade version: ${targetFw}\n\n` +
      `This will reboot the gateway and pause Fleet operations while upgrading.`;

  const confirmed = await confirmOperatorAction(confirmMsg, { confirmText: 'Upgrade gateway', danger: true });
  if (!confirmed) {
    return;
  }

  isFlashing.value = true;
  fleetGatewayFlashPhase.value = 'flashing';

  if (isMqtt) {
    networkStatusMessage.value = `Triggering OTA upgrade for MQTT gateway ${label}...`;
    pushNetworkLog(`Triggering OTA upgrade for MQTT gateway ${label} with ${firmwareOptions.firmware_path}.`);
    let otaCommandAcceptedOrIndeterminate = false;
    try {
      const info = await ensureFirmwareServer();
      const otaUrl = info.urls.find(u => !u.includes('127.0.0.1') && !u.includes('localhost'));
      if (!otaUrl) throw new Error('No LAN firmware server URL available for the MQTT gateway');

      const cmdSentTime = Date.now();
      delete mqttOtaStatus.value[port];
      try {
        await sendEasyPairCommandOnPort<any>(port, 'ota_pull', {
          admin_password: password,
          url: otaUrl,
          sha256: info.sha256
        }, 35000);
      } catch (e) {
        const errText = String(e || '');
        if (errText.includes('timeout')) {
          pushNetworkLog('MQTT command response timed out, but gateway may have started downloading. Transitioning to verify phase...');
        } else {
          throw e;
        }
      }

      otaCommandAcceptedOrIndeterminate = true;
      fleetGatewayFlashPhase.value = 'rebooting';
      networkStatusMessage.value = 'Network reconnecting...';
      notify('Gateway OTA upgrade triggered');
      await new Promise(resolve => setTimeout(resolve, 4000));

      fleetGatewayFlashPhase.value = 'waiting';
      networkStatusMessage.value = 'Network reconnecting...';
      await waitForMqttGatewayUpdate(port, selectedFirmwareCandidateVersion(), cmdSentTime, 600000); // 10 minutes

      fleetGatewayFlashPhase.value = 'updated';
      networkStatusMessage.value = 'Gateway upgraded and reconnected over MQTT.';
      notify('Gateway OTA upgrade successful');
      await loadNetworkGateway();
      window.setTimeout(() => {
        if (fleetGatewayFlashPhase.value === 'updated') {
          fleetGatewayFlashPhase.value = 'idle';
        }
      }, 8000);
    } catch (e) {
      if (otaCommandAcceptedOrIndeterminate && String(e || '').includes('timeout waiting for reconnection')) {
        fleetGatewayFlashPhase.value = 'unknown';
        const msg = 'Gateway rebooted, but network reconnect timed out. Upgrade status unknown.';
        networkStatusMessage.value = msg;
        pushNetworkLog(msg);
        notify('Gateway network reconnect timeout');
        window.setTimeout(() => {
          if (fleetGatewayFlashPhase.value === 'unknown') {
            fleetGatewayFlashPhase.value = 'idle';
          }
        }, 15000);
      } else {
        fleetGatewayFlashPhase.value = 'failed';
        const msg = `MQTT Gateway OTA failed: ${e}`;
        networkStatusMessage.value = msg;
        pushNetworkLog(msg);
        notify(msg);
        window.setTimeout(() => {
          if (fleetGatewayFlashPhase.value === 'failed') {
            fleetGatewayFlashPhase.value = 'idle';
          }
        }, 15000);
      }
    } finally {
      isFlashing.value = false;
    }
  } else {
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

async function waitForMqttGatewayUpdate(chipId: string, targetVersion: string, startTime: number, timeoutMs = 60000): Promise<boolean> {
  const started = Date.now();
  const cleanTarget = targetVersion.replace(/^Local:\s*/i, '').trim();
  while (Date.now() - started < timeoutMs) {
    const otaStat = mqttOtaStatus.value[chipId];
    if (otaStat && otaStat.timestamp >= startTime) {
      if (otaStat.status.startsWith('failed:')) {
        throw new Error(`OTA pull failed: ${otaStat.status.substring(7)}`);
      }
    }
    const lastDiscovery = lastMqttDiscoveryMs.value[chipId] || 0;
    if (lastDiscovery >= startTime) {
      const currentFw = serialDeviceState(chipId)?.status?.fw_version;
      if (currentFw) {
        const p1 = parseVersion(currentFw);
        const p2 = parseVersion(cleanTarget);
        if (p1 && p2 && compareParsedVersions(p1, p2) >= 0) {
          return true;
        }
      }
    }
    await new Promise(resolve => setTimeout(resolve, 1500));
  }
  throw new Error('MQTT Gateway OTA timeout waiting for reconnection/version update');
}

function serialFeatureError(feature: string, err: unknown): string {
  const text = String(err || 'serial command failed');
  if (text.includes('unknown_cmd')) {
    return `${feature} failed: unknown_cmd - Flash this gateway with the latest firmware and try again.`;
  }
  if (text.includes('cooldown_active')) {
    return `${feature} failed: Sending WiFi credentials to remotes is limited to once per 60 seconds. Please wait a moment and try again.`;
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

async function refreshSerialAdminStatus(port: unknown = selectedPort.value) {
  const targetPort = typeof port === 'string' ? port : selectedPort.value;
  if (!targetPort) {
    if (settingsTransport.value === 'mqtt') {
      notify('Select an MQTT gateway first');
    } else {
      notify('Select a USB device first');
    }
    return;
  }
  isSerialAdminLoading.value = true;
  pushSerialLog(settingsTransport.value === 'mqtt' ? 'Refreshing MQTT admin status...' : 'Refreshing local admin status...');
  try {
    const state = serialDeviceState(targetPort);
    if (settingsTransport.value !== 'mqtt' && state && !state.adminSupported) {
      await probeSerialAdminSupport(targetPort);
    }
    const out = await sendEasyPairCommandOnPort<SerialAdminStatus>(targetPort, 'status', {}, 5000, { label: 'Refresh status' });
    applySerialAdminStatus(out, targetPort);
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

async function loadSerialAdminConfig(port: unknown = selectedPort.value) {
  const targetPort = typeof port === 'string' ? port : selectedPort.value;
  if (!targetPort) {
    if (settingsTransport.value === 'mqtt') {
      notify('Select an MQTT gateway first');
    } else {
      notify('Select a USB device first');
    }
    return;
  }
  const state = serialDeviceState(targetPort);
  let password = '';
  if (settingsTransport.value === 'mqtt') {
    password = state?.adminPassword?.trim() || '';
    if (!password) {
      notify('Enter the remote admin password first');
      return;
    }
  } else {
    password = state?.deviceInfo?.password?.trim() || '';
    if (!password) {
      notify('Get device info first to use the factory password');
      return;
    }
  }
  isSerialAdminLoading.value = true;
  pushSerialLog('Loading device configuration...');
  try {
    if (settingsTransport.value === 'mqtt') {
      if (!state?.config) {
        pushSerialLog('Waiting for retained MQTT config topics to load...');
        const bufState = await ensureMqttConfigLoaded(targetPort);
        if (state) {
          state.config = normalizeSerialAdminConfig(bufState.buffer as Partial<SerialAdminConfig>, state.status);
        }
      }
      pushSerialLog('Configuration loaded from retained MQTT topics. Password fields stay blank.');
      return;
    }

    if (state && !state.adminSupported) {
      await probeSerialAdminSupport(targetPort);
    }
    const out = await sendEasyPairCommandOnPort<{ ok: boolean; cmd: string; config: SerialAdminConfig }>(targetPort, 'get_config', {
      admin_password: password,
      include_secrets: true
    }, 15000, { label: 'Fetch settings' });
    if (state) {
      state.config = normalizeSerialAdminConfig(out.config, state.status);
    }
    pushSerialLog('Configuration loaded. Password fields stay blank unless you enter new values.');
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
    if (settingsTransport.value === 'mqtt') {
      notify('Select an MQTT gateway first');
    } else {
      notify('Select a USB device first');
    }
    return;
  }
  if (settingsTransport.value !== 'mqtt' && !hasActiveDeviceInfo.value) {
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
    lora_spreading_factor: Number(cfg.lora_spreading_factor || 7),
    lora_bandwidth_hz: Number(cfg.lora_bandwidth_hz || 125000),
    lora_coding_rate: Number(cfg.lora_coding_rate || 5),
    heartbeat_ms: Number(cfg.heartbeat_ms || 60000),
    heartbeat_enabled: cfg.heartbeat_enabled !== false,
    ack_timeout_ms: Number(cfg.ack_timeout_ms || 3000),
    mqtt_remote_retry_timeout_ms: Number(cfg.mqtt_remote_retry_timeout_ms || 180000),
    tx_mqtt_remote_polling_enabled: !!cfg.tx_mqtt_remote_polling_enabled,
    tx_mqtt_remote_default_poll_interval_ms: Number(cfg.tx_mqtt_remote_default_poll_interval_ms || 300000),
    input_control_paired_lora_enabled: !!cfg.input_control_paired_lora_enabled,
    tx_command_retry_timeout_ms: Number(cfg.tx_command_retry_timeout_ms || 180000),
    rx_failsafe_mode: cfg.rx_failsafe_mode || 'hold_last',
    rx_failsafe_timeout_ms: Number(cfg.rx_failsafe_timeout_ms || 180000),
    wifi_sta_ssid: cfg.wifi_sta_ssid || '',
    lan_hostname: cfg.lan_hostname || '',
    ap_always_on: !!cfg.ap_always_on,
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
  const port = selectedPort.value;
  if (!port) {
    if (settingsTransport.value === 'mqtt') {
      notify('No MQTT gateway selected');
    } else {
      notify('No USB port selected');
    }
    return;
  }
  if (!serialAdminConfig.value) {
    notify('Load config first');
    return;
  }
  let password = '';
  if (settingsTransport.value === 'mqtt') {
    password = settingsAdminPassword.value;
    if (!password) {
      notify('Enter the remote admin password first');
      return;
    }
  } else {
    password = serialAdminPassword.value;
    if (!password) {
      notify('Get device info first to use the factory password');
      return;
    }
  }
  isSerialAdminSaving.value = true;
  pushSerialLog(settingsTransport.value === 'mqtt' ? 'Saving remote configuration...' : 'Saving local device configuration...');
  try {
    const out = await sendEasyPairCommandOnPort<any>(port, 'set_config', {
      admin_password: password,
      config: serialConfigPatch()
    }, 12000, { label: 'Save settings' });
    const rebooting = !!(out.rebooting || out.ota_auth_changed);
    const effects = [
      out.network_restarted ? 'networking restarted' : '',
      rebooting ? 'admin password changed; device is rebooting' : ''
    ].filter(Boolean);
    pushSerialLog(`Configuration saved${effects.length ? `; ${effects.join('; ')}` : ''}.`);
    notify(`Configuration saved${effects.length ? `; ${effects.join('; ')}` : ''}.`);
    if (rebooting) {
      const state = serialDeviceState(port);
      if (state) {
        state.status = null;
        state.config = null;
      }
    } else {
      if (selectedPort.value === port) {
        if (settingsTransport.value === 'mqtt' || out.network_restarted) {
          pushSerialLog('Networking or transport may be restarting. Click "Refresh status" or "Fetch settings" manually once the device settles.');
          setTimeout(async () => {
            try {
              await refreshSerialAdminStatus(port);
              await loadSerialAdminConfig(port);
            } catch (err) {
              pushSerialLog('Auto-refresh deferred: ' + (err instanceof Error ? err.message : String(err)));
            }
          }, 3000);
        } else {
          await refreshSerialAdminStatus(port);
          await loadSerialAdminConfig(port);
        }
      }
    }
  } catch (e) {
    const msg = serialFeatureError('Config save', e);
    pushSerialLog(msg);
    notify(msg);
  } finally {
    isSerialAdminSaving.value = false;
  }
}

async function rebootSerialDevice() {
  const port = selectedPort.value;
  if (!port) {
    notify('No USB port selected');
    return;
  }
  const password = serialAdminPassword.value;
  if (!password) {
    notify('Get device info first to use the factory password');
    return;
  }
  if (!await confirmOperatorAction('Reboot the selected USB device now?', { confirmText: 'Reboot' })) return;
  if (selectedPort.value !== port) {
    notify('Selected port changed during confirmation');
    return;
  }
  isSerialSystemAction.value = true;
  pushSerialLog('Sending reboot command...');
  try {
    await sendEasyPairCommandOnPort(port, 'reboot', { admin_password: password }, 5000, { label: 'Reboot device' });
    const state = serialDeviceState(port);
    if (state) {
      state.status = null;
    }
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
  const port = selectedPort.value;
  if (!port) {
    notify('No USB port selected');
    return;
  }
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
  if (selectedPort.value !== port) {
    notify('Selected port changed during confirmation');
    return;
  }
  isSerialSystemAction.value = true;
  pushSerialLog(`Sending factory reset command (${summary})...`);
  try {
    await sendEasyPairCommandOnPort(port, 'factory_reset', {
      admin_password: password,
      keep_shared_fleet_key: serialFactoryKeepFleet.value,
      keep_wifi_credentials: serialFactoryKeepWifi.value
    }, 6000, { label: 'Factory reset' });
    const state = serialDeviceState(port);
    if (state) {
      state.status = null;
      state.config = null;
      state.wifiNetworks = [];
    }
    if (port && port === gatewaySelectedPort.value) {
      clearFleetGatewayCache();
    }
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
  const key = pairGatewayKey.value;
  if (!key) return;
  // Prevent concurrent calls from multiple watchers firing on startup
  if (loadGatewayInFlight) return;
  loadGatewayInFlight = true;
  isGatewayLoading.value = true;

  if (pairTransport.value === 'mqtt') {
    const chipId = key.replace(/^lrs-/, '');
    pushPairLog(`Loading MQTT gateway lrs-${chipId}...`);
    try {
      const gw = mqttGateways.value[chipId];
      const state = serialDeviceState(chipId);
      if (state) {
        await ensureMqttGatewayDefaultState(chipId);
        state.deviceInfo = {
          ...state.deviceInfo!,
          mac: gw?.mac || state.deviceInfo?.mac || '',
          ssid: gw?.sta_ssid || state.deviceInfo?.ssid || ''
        };
      }
      await refreshGatewayStatusForPair();
      pushPairLog('Reading retained MQTT gateway configuration...');
      try {
        const configState = await ensureMqttConfigLoaded(chipId);
        const latestState = serialDeviceState(chipId);
        if (latestState) {
          latestState.config = normalizeSerialAdminConfig(configState.buffer as Partial<SerialAdminConfig>, latestState.status);
        }
        const status = latestState?.status;
        const cfg = latestState?.config;
        const isCommissioned = status?.commissioned ?? cfg?.commissioned;
        const isDefaultKey = status?.fleet_passphrase_default ?? !!configState.secretsMetadata['fleet_passphrase_default'];

        if (isCommissioned && isDefaultKey === false) {
          pairFleetKeySource.value = 'manual';
          if (pairFleetKey.value) {
            pushPairLog('Gateway is commissioned; MQTT does not return secrets, using the existing fleet key in the Provision form.');
          } else {
            pushPairLog('Gateway is commissioned but fleet key is hidden. Please input the fleet key to proceed.');
          }
        } else if (!isCommissioned || isDefaultKey === true) {
          generatePairFleetKey(true);
          pairFleetKeySource.value = 'factory_generated';
          pushPairLog('Gateway is factory/uncommissioned; generated a new fleet key for first commissioning.');
        } else {
          pairFleetKeySource.value = pairFleetKey.value ? 'manual' : 'none';
          pushPairLog('Gateway fleet key state is unknown from retained MQTT config. Please verify the fleet key before provisioning.');
        }
        pairGatewayLoaded.value = true;
      } catch (configErr) {
        pushPairLog('Gateway config topics failed: ' + configErr);
      }
    } catch (e) {
      pushPairLog('Gateway check failed: ' + e);
      if (!isAuto) {
        notify('Gateway check failed: ' + e);
      }
    } finally {
      loadGatewayInFlight = false;
      isGatewayLoading.value = false;
    }
    return;
  }

  pushPairLog('Loading USB gateway...');
  try {
    const ok = await ensureDeviceInfoForPort(key, 'pair');
    const state = serialDeviceState(key);
    if (!ok || !state || !state.deviceInfo) {
      throw new Error('Unable to read device details');
    }
    state.adminPassword = state.deviceInfo.password || '';
    pushPairLog('Waiting for serial admin to become ready...');
    const hello = await waitForSerialAdminHello(key);
    pushPairLog(`Gateway ready on ${key}; firmware ${hello.fw_version || 'unknown'}, max remotes ${hello.max_remotes || 12}.`);
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
        const isCommissioned = serialDeviceState(key)?.status?.commissioned;
        const isDefaultKey = serialDeviceState(key)?.status?.fleet_passphrase_default;

        if (isCommissioned && retrievedKey && retrievedKey !== 'lora-default-passphrase' && isDefaultKey === false) {
          pairFleetKey.value = retrievedKey;
          pairFleetKeySource.value = 'gateway';
          pushPairLog(`Retrieved commissioned fleet key from gateway.`);
        } else if (!isCommissioned || isDefaultKey === true) {
          generatePairFleetKey(true);
          pairFleetKeySource.value = 'factory_generated';
          pushPairLog('Gateway is factory/uncommissioned; generated a new fleet key for first commissioning.');
        } else {
          clearPairFleetKey();
          pushPairLog('Gateway fleet key could not be verified; fleet key field cleared.');
        }
        pairGatewayLoaded.value = true;
      } catch (configErr) {
        clearPairFleetKey();
        pushPairLog('Gateway config fetch failed: ' + configErr);
      }
    } else {
      pairGatewayLoaded.value = true;
    }
  } catch (e) {
    const state = serialDeviceState(key);
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
  const maxRemotes = Math.max(1, Math.min(12, Number(pairExpectedCount.value) || 12));
  pairExpectedCount.value = maxRemotes;
  isPairBusy.value = true;
  provisionCacheRefreshedChips.clear();
  processedEasyPairLogLines.value.clear();
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
    requireProvisionFleetKeyAuthority();
    const password = pairPassword();
    await sendPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      max_remotes: maxRemotes
    }, 10000);
    pairFleetKeySource.value = 'gateway';
    pushPairLog(`Gateway prepared. Scanning for up to ${maxRemotes} remote device${maxRemotes === 1 ? '' : 's'}...`);
    await sendPairCommand('start_discovery', {
      admin_password: password,
      max_remotes: maxRemotes
    }, 10000);
    startEasyPairStatusPolling();
    await waitForEasyPairState(['ready', 'error'], 150000);
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
let lastMqttPairErrorTimeMs = 0;

async function refreshEasyPairStatus(log = false) {
  try {
    const rawStatus = await sendPairCommand<any>('provisioning_status', {}, 5000);
    if (!rawStatus) return;

    // 1. Normalize session keys
    if (rawStatus.session) {
      const s = rawStatus.session;
      if (s.max !== undefined) s.max_remotes = s.max;
      if (s.found !== undefined) s.discovered_count = s.found;
      if (s.verified !== undefined) s.verified_count = s.verified;
      if (s.failed !== undefined) s.failed_count = s.failed;
      if (s.deadline !== undefined) s.phase_deadline_ms = s.deadline;
      if (s.selected_count === undefined) s.selected_count = s.discovered_count || 0;
      if (s.conflict_count === undefined) s.conflict_count = 0;
    }

    // 2. Normalize device rows
    if (Array.isArray(rawStatus.devices)) {
      rawStatus.devices = rawStatus.devices.map((device: any) => {
        if (Array.isArray(device)) {
          return {
            chip_id_hex: '0x' + device[0],
            current_address: Number(device[1]) || 0,
            assigned_address: Number(device[2]) || 0,
            rssi: Number(device[3]) || 0,
            state: device[4],
            fw_major: Number(device[5]) || 0,
            fw_minor: Number(device[6]) || 0,
            fw_patch: Number(device[7]) || 0,
            fw_build: Number(device[8]) || 0,
            role_tx: false,
            selected: true,
            address_conflict: false
          };
        }
        return device;
      });
    }

    pairStatus.value = rawStatus;

    if (pairStatus.value?.session?.debug_events) {
      for (const line of pairStatus.value.session.debug_events) {
        if (!processedEasyPairLogLines.value.has(line)) {
          processedEasyPairLogLines.value.add(line);
          pushPairLog("[FW] " + line);
        }
      }
    }
    if (log && pairStatus.value && pairStatus.value.session) {
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
    if (log) {
      pushPairLog('Status refresh failed: ' + e);
    } else if (pairTransport.value === 'mqtt') {
      const now = Date.now();
      if (now - lastMqttPairErrorTimeMs >= 12000) {
        lastMqttPairErrorTimeMs = now;
        pushPairLog('Status refresh failed (MQTT): ' + e);
      }
    }
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
  processedEasyPairLogLines.value.clear();
  try {
    // Always load gateway status/config before discovery to ensure fleet key is fetched or generated correctly
    await loadEasyPairGateway();
    if (!gatewayReady.value) throw new Error('Unable to load gateway');

    const fleetKey = pairFleetKey.value.trim();
    if (!fleetKey) {
      throw new Error('Fleet key is empty');
    }
    requireProvisionFleetKeyAuthority();
    const password = pairPassword();
    if (!password) throw new Error('gateway password unavailable');
    await sendPairCommand('configure_gateway', {
      admin_password: password,
      fleet_passphrase: fleetKey,
      max_remotes: pairExpectedCount.value
    }, 10000);
    pairFleetKeySource.value = 'gateway';
    pushPairLog(`Gateway prepared. Scanning for up to ${pairExpectedCount.value} powered remote devices...`);
    await sendPairCommand('start_discovery', {
      admin_password: password,
      max_remotes: pairExpectedCount.value
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
  if (pairTransport.value === 'mqtt') {
    const key = pairGatewayKey.value;
    if (!key) return [];
    const state = serialDeviceState(key);
    let config = state?.config;
    if (!config) {
      const configState = await ensureMqttConfigLoaded(key);
      config = normalizeSerialAdminConfig(configState.buffer as Partial<SerialAdminConfig>, state?.status || null);
      if (state) state.config = config;
    }
    return uniqueSortedAddresses(normalizeAddressArray(config?.paired_target_addresses));
  }

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
      addresses: newAddresses,
      replace: false
    }, 10000);
    if (out.target_count && out.target_count < mergedAddresses.length) {
      throw new Error(`gateway target list shrank unexpectedly (${out.target_count} < ${mergedAddresses.length})`);
    }
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
  if (out && out.role) {
    out.role = normalizeRole(out.role);
  }
  noteGatewayStatusForRebootDetection(out, port);
  state.status = out;
  if (port === selectedPort.value) {
    serialUptimeMs.value = Number(out.uptime_ms || 0);
  }
  state.adminSupported = true;

  const targetPort = fleetTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
  if (port && port === targetPort) {
    const sessionKey = `${out.chip_id || ''}:${out.commissioned === true}:${out.fleet_passphrase_default === true}:${out.role_tx === true}:${out.local_address || 0}`;
    if (activeGatewaySessionKey.value && activeGatewaySessionKey.value !== sessionKey) {
      clearFleetGatewayCache();
    }
    activeGatewaySessionKey.value = sessionKey;
  }

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

  const state = serialDeviceState(port);
  if (!state) return false;
  state.gatewayWifiReadySsid = ssid;
  state.gatewayWifiReadyIp = ip;
  return true;
}

async function refreshGatewayStatusForPair(): Promise<boolean> {
  const port = pairGatewayKey.value;
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
  if (isFleetWifiSending.value) return;
  const ssid = selectedProvisionWifiSsid();
  const password = selectedProvisionAdminPassword();
  if (!ssid || !password) {
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
  if (isWifiApplying.value) return;
  const ssid = selectedProvisionWifiSsid();
  const password = selectedProvisionAdminPassword();
  if (!ssid || !password) {
    notify('Select a WiFi network and enter the admin password first');
    return;
  }
  isFleetWifiSending.value = true;
  pushPairLog(`Sending WiFi credentials to remotes over LoRa for ${ssid}...`);
  try {
    const out = await sendPairCommand<any>('provision_fleet_wifi', {
      admin_password: password,
      wifi_sta_ssid: ssid,
      ssid,
      wifi_sta_password: pairWifiPassword.value,
      password_value: pairWifiPassword.value,
      target_address: 255
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

async function ensureDeviceInfoForPort(port: string, mode: ActiveMode | 'network', force = false): Promise<boolean> {
  if (!port) return false;

  const state = serialDeviceState(port);
  if (state?.isFlashing || state?.isResetting || (isMonitoring.value && activeMonitorPort.value === port)) {
    return false;
  }

  if (state?.deviceInfo && !force) {
    return true;
  }

  const currentSeq = deviceInfoReadSeqByPort.value[port] || 0;
  const inFlight = inFlightDeviceInfoReads.value.get(port);
  if (inFlight && inFlight.seq === currentSeq) {
    return inFlight.promise;
  }

  const nextSeq = currentSeq + 1;
  deviceInfoReadSeqByPort.value = { ...deviceInfoReadSeqByPort.value, [port]: nextSeq };

  const promise = readDeviceInfoForPort(port, mode, nextSeq);
  inFlightDeviceInfoReads.value.set(port, { seq: nextSeq, promise });

  promise.finally(() => {
    const active = inFlightDeviceInfoReads.value.get(port);
    if (active && active.seq === nextSeq) {
      inFlightDeviceInfoReads.value.delete(port);
    }
  });

  return promise;
}

async function readSerialAdminDeviceInfoForPort(port: string): Promise<DeviceInfo> {
  const identity = await sendEasyPairCommandOnPort<any>(
    port,
    'identity',
    {},
    5000,
    { label: 'Read serial admin identity' }
  );
  const chipId = String(identity.chip_id || '').replace(/^0x/i, '').toLowerCase();
  if (!/^[0-9a-f]{6,8}$/.test(chipId)) {
    throw new Error('serial admin identity did not return a valid chip_id');
  }
  const derived = await invoke<DeviceInfo>('derive_device_info_from_chip_id', { chipId });
  return {
    ...derived,
    chip_id: chipId.padStart(8, '0'),
    local_addr: Number(identity.local_address ?? derived.local_addr),
    remote_addr: Number(identity.remote_address ?? derived.remote_addr),
    ssid: String(identity.ap_ssid || derived.ssid)
  };
}

async function forceDeviceInfoReadForOperation(port: string, mode: ActiveMode | 'network'): Promise<boolean> {
  if (!port) return false;
  if (isMonitoring.value && activeMonitorPort.value === port) return false;

  const currentSeq = deviceInfoReadSeqByPort.value[port] || 0;
  const nextSeq = currentSeq + 1;
  deviceInfoReadSeqByPort.value = { ...deviceInfoReadSeqByPort.value, [port]: nextSeq };

  inFlightDeviceInfoReads.value.delete(port);
  const promise = readDeviceInfoForPort(port, mode, nextSeq);
  inFlightDeviceInfoReads.value.set(port, { seq: nextSeq, promise });

  promise.finally(() => {
    const active = inFlightDeviceInfoReads.value.get(port);
    if (active && active.seq === nextSeq) {
      inFlightDeviceInfoReads.value.delete(port);
    }
  });

  return promise;
}

async function readDeviceInfo() {
  if (!selectedPort.value) return;
  const port = selectedPort.value;
  return await ensureDeviceInfoForPort(port, activeMode.value || 'serial', true);
}

async function readDeviceInfoForPort(port: string, _ownerMode: ActiveMode | 'network', forcedSeq?: number): Promise<boolean> {
  const seq = forcedSeq !== undefined ? forcedSeq : (deviceInfoReadSeqByPort.value[port] || 0) + 1;
  if (forcedSeq === undefined) {
    deviceInfoReadSeqByPort.value = { ...deviceInfoReadSeqByPort.value, [port]: seq };
  }
  if (isMonitoring.value && activeMonitorPort.value === port) {
    pushSerialLog(`Skipped device info read for ${port}: serial monitor owns this port.`);
    return false;
  }
  pushSerialLog(`Reading device information from ${port}...`);
  try {
    const info = _ownerMode === 'serial'
      ? await invoke<DeviceInfo>('get_device_info', { port })
      : await readSerialAdminDeviceInfoForPort(port);
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
  const loaded = await forceDeviceInfoReadForOperation(port, 'serial');
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
            await forceDeviceInfoReadForOperation(port, 'serial');
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
            await forceDeviceInfoReadForOperation(port, 'serial');
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

const activityPanelRef = ref<{ scrollToBottom: () => void; scrollBulkLogToBottom: (port: string) => void } | null>(null);

function scrollToBottom() {
  activityPanelRef.value?.scrollToBottom();
}

function scrollNetworkUdpToBottom() {
  if (networkUdpLogContainer.value) {
    networkUdpLogContainer.value.scrollTop = networkUdpLogContainer.value.scrollHeight;
  }
}

watch(networkLogs, () => {
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
  serialDeviceState();

  const port = selectedPort.value;
  const targetMode = mode || 'serial';
  if (port && !isSelectedPortMonitoring.value) {
    ensureDeviceInfoForPort(port, targetMode).then((ok) => {
      if (selectedPort.value !== port || (activeMode.value || 'serial') !== targetMode) {
        return;
      }
      const state = serialDeviceState(port);
      if (ok && targetMode === 'network') {
        if (state?.status && !state.status.role_tx) {
          networkStatusMessage.value = 'Selected device is not a Gateway.';
          return;
        }
        loadNetworkGateway();
      } else if (ok && targetMode === 'pair') {
        loadEasyPairGateway(true);
      }
    });
  }

  if (mode !== 'monitor') stopMonitorPolling();
  if (mode !== 'network') {
    stopLoraInventoryPolling(false);
  } else if (portGatewayReady(gatewaySelectedPort.value)) {
    // Gateway was already loaded (e.g. switching back to Fleet tab) — just refresh inventory
    refreshLoraInventoryStatus(false).finally(() => startFleetCachePolling());
  }
});

watch(selectedPort, (port) => {
  serialDeviceState();
  serialUptimeMs.value = activeSerialDevice.value?.status?.uptime_ms ?? null;

  const targetMode = activeMode.value || 'serial';
  if (port && !isSelectedPortMonitoring.value) {
    ensureDeviceInfoForPort(port, targetMode).then((ok) => {
      if (selectedPort.value !== port || (activeMode.value || 'serial') !== targetMode) {
        return;
      }
      const state = serialDeviceState(port);
      if (ok && targetMode === 'network') {
        if (state?.status && !state.status.role_tx) {
          networkStatusMessage.value = 'Selected device is not a Gateway.';
          return;
        }
        loadNetworkGateway();
      } else if (ok && targetMode === 'pair') {
        loadEasyPairGateway(true);
      }
    });
  }

  if (activeMode.value === 'monitor') {
    monitorFleetRows.value = [];
    stopMonitorPolling();
  }
});

watch(selectedMqttGatewayChipId, (newVal) => {
  clearFleetGatewayCache();
  activeGatewaySessionKey.value = '';
  if (newVal && activeMode.value === 'network') {
    loadNetworkGateway();
  }
});

watch(fleetTransport, () => {
  clearFleetGatewayCache();
  activeGatewaySessionKey.value = '';
});

watch(gatewaySelectedPort, (port) => {
  // Provisioning side-effects
  pairStatus.value = null;
  saveTabPort('pair', port);
  // Fleet side-effects
  saveTabPort('network', port);
  clearFleetGatewayCache();
  activeGatewaySessionKey.value = '';

  const targetMode = activeMode.value || 'serial';
  if (port && !isSelectedPortMonitoring.value) {
    ensureDeviceInfoForPort(port, targetMode).then((ok) => {
      if (gatewaySelectedPort.value !== port || (activeMode.value || 'serial') !== targetMode) {
        return;
      }
      const state = serialDeviceState(port);
      if (ok && targetMode === 'network') {
        if (state?.status && !state.status.role_tx) {
          networkStatusMessage.value = 'Selected device is not a Gateway.';
          return;
        }
        loadNetworkGateway();
      } else if (ok && targetMode === 'pair') {
        loadEasyPairGateway(true);
      }
    });
  }
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

watch(sessionConnectionType, async (newVal) => {
  if (newVal === 'local_broker') {
    await startAndConnectLocalBroker();
  }
});

const handleWindowClick = () => {
  activeDropdownAddress.value = null;
};

onMounted(async () => {
  window.addEventListener('click', handleWindowClick);
  fleetClockTimer.value = window.setInterval(() => {
    fleetClockMs.value = Date.now();
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
  networkInterfaceInterval.value = window.setInterval(async () => {
    try {
      const nextInterfaces = await invoke<NetworkInterface[]>('get_network_interfaces');
      const prevStr = JSON.stringify(flasherInterfaces.value.map(i => i.ip).sort());
      const nextStr = JSON.stringify(nextInterfaces.map(i => i.ip).sort());
      if (prevStr !== nextStr) {
        pushNetworkLog('Host network interfaces changed. Updating subnets...');
        flasherInterfaces.value = nextInterfaces;

        await handleNetworkInterfacesChanged(
          nextInterfaces,
          isFlashing.value || fleetGatewayFlashPhase.value !== 'idle'
        );
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
  initializeRegion();

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
          handleOtaLogLine(trimmed, dev);
        }
      }
    });
  });

  unlistenPortsChanged = await listen<PortsChangedEvent>('serial-ports-changed', () => {
    if (!isFlashing.value) {
      refreshPorts(true);
    }
  });

  unlistenMqttState = await listen<any>('mqtt-state-changed', (event) => {
    applyMqttStateChanged(event.payload);
  });

  unlistenMqttGateway = await listen<any>('mqtt-gateway-update', (event) => {
    const payload = event.payload;
    mqttGateways.value[payload.chip_id] = payload;
    lastMqttDiscoveryMs.value[payload.chip_id] = Date.now();
    ensureMqttGatewayDefaultState(payload.chip_id);
    if (!selectedMqttGatewayChipId.value) {
      selectedMqttGatewayChipId.value = payload.chip_id;
    }
    const state = serialDeviceState(payload.chip_id);
    if (state) {
      const staConnected = !!payload.sta_ip && payload.sta_ip !== '0.0.0.0';
      const statusObj: SerialAdminStatus = {
        ok: true,
        cmd: 'status',
        fw_version: payload.fw_version || '',
        chip_id: payload.chip_id,
        uptime_ms: Number(payload.uptime_ms || 0),
        heap_free: state.status?.heap_free || 0,
        heap_frag_pct: state.status?.heap_frag_pct || 0,
        heap_max_block: state.status?.heap_max_block || 0,
        mode: state.status?.mode || '',
        role: normalizeRole(payload.role),
        role_tx: normalizeRole(payload.role) === 'gateway',
        local_address: payload.addr || state.status?.local_address || 254,
        remote_address: payload.remote_addr || state.status?.remote_address || 0,
        commissioned: true,
        fleet_passphrase_default: state.status?.fleet_passphrase_default || false,
        wifi: {
          admin_enabled: true,
          sta_ssid: payload.sta_ssid || '',
          sta_connected: staConnected,
          status: staConnected ? 'connected' : 'disconnected',
          ip: payload.sta_ip || '',
          rssi: state.status?.wifi?.rssi || 0,
          ap_active: !!payload.ap_ip
        }
      };
      state.status = statusObj;
      state.adminSupported = true;
      adoptGatewayWifiFromStatus(statusObj, payload.chip_id);
    }
  });

  unlistenMqttTelemetry = await listen<any>('mqtt-telemetry-update', (event) => {
    const payload = event.payload;
    if (fleetTransport.value === 'mqtt' && payload.gateway_id === selectedMqttGatewayChipId.value) {
      applyTelemetryUpdate(payload);
      if (gatewaySelectedPort.value === monitorSelectedPort.value) {
        monitorFleetRows.value = mergeMonitorRows(loraInventory.value);
      }
    }
  });

  unlistenMqttOtaStatus = await listen<any>('mqtt-ota-status-update', (event) => {
    const payload = event.payload;
    mqttOtaStatus.value[payload.chip_id] = {
      status: payload.status,
      timestamp: Date.now()
    };
  });

  unlistenMqttConfig = await listen<any>('mqtt-config-update', (event) => {
    const { chip_id, field, value } = event.payload;
    handleConfigUpdate(chip_id, field, value, (completedChipId, configBuffer) => {
      const state = serialDeviceState(completedChipId);
      if (state) {
        state.config = normalizeSerialAdminConfig(configBuffer as SerialAdminConfig, state.status);
      }
    });
  });


  unlistenMqttAdminResponse = await listen<any>('mqtt-admin-response', (event) => {
    handleMqttAdminResponse(event.payload);
  });

  invoke<any>('get_mqtt_state').then((state) => {
    hydrateMqttState(state);
  }).catch((e) => {
    console.error('Failed to get MQTT state:', e);
  });

  invoke<number | null>('get_local_mqtt_broker_status').then((activePort) => {
    hydrateLocalBrokerStatus(activePort);
  }).catch((e) => {
    console.error('Failed to get local broker status:', e);
  });
});

watch(fleetGatewayFlashPhase, (newPhase) => {
  if (newPhase === 'idle') {
    revalidateFirmwareServerAfterNetworkChange();
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
  if (fleetScanSettleTimer.value) window.clearTimeout(fleetScanSettleTimer.value);
  if (networkInterfaceInterval.value) window.clearInterval(networkInterfaceInterval.value);
  cleanupFleetOtaTimers();
  if (identifyTimer.value) window.clearTimeout(identifyTimer.value);
  if (unlistenFlash) unlistenFlash();
  if (unlistenMonitor) unlistenMonitor();
  if (unlistenNetworkMonitor) unlistenNetworkMonitor();
  if (unlistenPortsChanged) unlistenPortsChanged();
  if (unlistenMqttState) unlistenMqttState();
  if (unlistenMqttTelemetry) unlistenMqttTelemetry();
  if (unlistenMqttGateway) unlistenMqttGateway();
  if (unlistenMqttAdminResponse) unlistenMqttAdminResponse();
  if (unlistenMqttOtaStatus) unlistenMqttOtaStatus();
  if (unlistenMqttConfig) unlistenMqttConfig();
  if (isNetworkUdpMonitoring.value) {
    invoke('stop_network_udp_monitor').catch(() => {});
  }
  cleanupFirmwareServer().catch(() => {});
});

function formatLabel(key: string) {
  const mapping: Record<string, string> = {
    'chip_id': 'Chip ID',
    'local_addr': 'Local addr',
    'remote_addr': 'Remote addr',
    'ssid': 'Soft AP SSID',
    'mac': 'MAC',
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

const fleetDisplayRows = computed<FleetDisplayRow[]>(() => {
  const clock = fleetClockMs.value;
  return loraInventory.value.map(device => {
    const history = fleetRowHistory.value[device.address];
    const ageMs = calculateDynamicAgeMs(device, history, clock);
    const displayDevice = fleetDeviceWithDisplayState(device, clock);

    return {
      address: displayDevice.address,
      selected: !!displayDevice.selected,
      deviceName: displayDevice.chip_id ? lrsDeviceName(displayDevice.chip_id) : '-',
      conflict_chip_id: displayDevice.conflict_chip_id,
      fw_version: displayDevice.fw_version,
      roleModeLabel: `${displayDevice.role || '-'} / ${displayDevice.mode || '-'}`,
      wifi_pending_offline: !!displayDevice.wifi_pending_offline,
      wifi_connected_known: !!displayDevice.wifi_connected_known,
      wifi_connected: !!displayDevice.wifi_connected,
      wifi_rssi_dbm: displayDevice.wifi_rssi_dbm,
      pending_power_save_listen_only: displayDevice.pending_power_save_listen_only,
      power_save_listen_only: !!displayDevice.power_save_listen_only,
      ip: displayDevice.ip,
      relayLabel: remoteRelayLabel(displayDevice),
      inputLabel: remoteInputLabel(displayDevice),
      tempLabel: remoteTempLabel(displayDevice),
      tankLabel: tankLabel(displayDevice),
      tankDetailLabel: tankDetailLabel(displayDevice),
      uptimeLabel: displayDevice.uptime_ms ? formatUptime(displayDevice.uptime_ms) : '-',
      rowStatusLabel: fleetRowStatusLabel(displayDevice),
      rowState: displayDevice.row_state,
      rssi: displayDevice.rssi,
      ageSeconds: ageMs != null ? Math.round(ageMs / 1000) : null,
      freshnessClass: fleetFreshnessClass(displayDevice, ageMs),
      rowClass: fleetRowClass(displayDevice),
      flashAvailable: fleetFlashAvailable(displayDevice),
      flashUnavailableReason: fleetFlashUnavailableReason(displayDevice)
    };
  });
});

const fleetDisplayCandidates = computed<FleetCandidateDisplayRow[]>(() => {
  const clock = fleetClockMs.value;
  return loraCandidates.value.map(c => {
    const ageMs = calculateCandidateAgeMs(c, clock);

    const isRecentFailed = c.state === 'failed' && (ageMs == null || ageMs < CANDIDATE_RECENT_IDENTITY_MS);
    const textClass =
      (c.state === 'seen_address_only' || isRecentFailed) ? 'text-slate-500 animate-pulse' :
      c.state === 'readdressing' ? 'text-sky-400 animate-pulse' :
      c.state === 'reset_requested' ? 'text-amber-400 animate-pulse' :
      c.state === 'failed' ? 'text-rose-500' :
      'text-slate-500';
    return {
      address: c.address,
      chip_id: c.chip_id,
      deviceName: c.chip_id ? lrsDeviceName(c.chip_id) : 'querying...',
      rssi: c.rssi || 0,
      ageSeconds: ageMs != null ? Math.round(ageMs / 1000) : null,
      reason: c.reason || 'unknown',
      state: c.state || '',
      stateText: candidateStateText(c),
      stateClass: candidateStateClass(c),
      showAdoptButton: ['identified', 'failed'].includes(c.state || '') && !!c.chip_id,
      adoptTextClass: textClass
    };
  });
});

const fleetGatewayStatusComputed = computed<FleetGatewayStatus>(() => {
  const info = gatewaySelectedPort.value ? serialDeviceState(gatewaySelectedPort.value) : null;
  const rebootAlert = fleetGatewayUnexpectedReboot.value;
  const rebootAlertLine = rebootAlert
    ? `Uptime rolled back from ${formatUptime(rebootAlert.previousUptimeMs)} to ${formatUptime(rebootAlert.currentUptimeMs)}.`
    : '';
  return {
    hasGatewayDeviceWarning: !!(gatewaySelectedPort.value && info?.status && !info.status.role_tx),
    hasUnexpectedReboot: !!rebootAlert,
    rebootAlertLine,
    badgeClass: fleetGatewayBadgeClass.value,
    badgeLabel: fleetGatewayBadgeLabel.value,
    statusLabel: fleetGatewayStatusLabel.value,
    summary: fleetGatewaySummary.value,
    isLoading: isNetworkGatewayLoading.value,
    isIdentifyDisabled: identifyDisabled.value,
    isIdentifying: isIdentifying.value,
    isUpgradeAvailable: isGatewayUpgradeAvailable.value,
    isFlashing: isFlashing.value,
    flashDisabled: fleetGatewayFlashDisabled.value,
    flashUnavailableReason: fleetGatewayFlashUnavailableReason(),
    port: selectedPort.value,
    name: lrsDeviceName(fleetGatewayStatus.value?.chip_id || fleetGatewayIdentity.value?.chip_id),
    firmware: displayFirmwareVersion(fleetGatewayStatus.value?.fw_version),
    role: fleetGatewayStatus.value?.role || '-',
    addressLine: fleetGatewayStatus.value
      ? `${fleetGatewayStatus.value.local_address}->${fleetGatewayStatus.value.remote_address}`
      : fleetGatewayIdentity.value
        ? `${fleetGatewayIdentity.value.local_addr}->${fleetGatewayIdentity.value.remote_addr}`
        : '-',
    wifiLine: (() => {
      const wifi = fleetGatewayStatus.value?.wifi;
      if (!wifi?.sta_connected) {
        return wifi?.status || '-';
      }
      const ipStr = wifi.ip || 'connected';
      if (wifi.rssi !== undefined && wifi.rssi !== null && wifi.rssi !== 0) {
        const rssi = wifi.rssi;
        const bars = rssi >= -60 ? '▂▄▆' : rssi >= -70 ? '▂▄▅' : rssi >= -80 ? '▂▄' : '▂';
        return `${ipStr} (${bars} ${rssi})`;
      }
      return ipStr;
    })(),
    wifiIp: fleetGatewayStatus.value?.wifi?.ip || '',
    wifiRssi: (fleetGatewayStatus.value?.wifi?.sta_connected && fleetGatewayStatus.value?.wifi?.rssi) ? fleetGatewayStatus.value.wifi.rssi : undefined,
    wifiConnected: !!fleetGatewayStatus.value?.wifi?.sta_connected,
    uptimeLine: rebootAlert
      ? `${formatUptime(rebootAlert.currentUptimeMs)} (reboot)`
      : fleetGatewayStatus.value?.uptime_ms ? formatUptime(fleetGatewayStatus.value.uptime_ms) : '-',
    uptimeClass: rebootAlert ? 'text-rose-300' : 'text-slate-300',
    uptimeTitle: rebootAlert ? rebootAlertLine : ''
  };
});

const fleetServerStatusComputed = computed<FleetServerStatus>(() => ({
  isServerOn: !!firmwareServerInfo.value,
  serverFilename: firmwareServerInfo.value?.filename || null,
  serverUrl: firmwareServerInfo.value?.urls[0] || null,
  isLoraInventoryScanning: isLoraInventoryScanning.value,
  scanDisabled: fleetScanDisabled.value,
  scanLabel: isLoraInventoryScanStarting.value ? 'Starting...' : fleetForceScanLabel.value,
  statusLine: remotesAndCandidatesStatusLine.value,
  progressLabel: loraInventoryProgressLabel.value,
  isServerStarting: isFirmwareServerStarting.value,
  versions: firmwareVersions.value,
  localOption: LOCAL_OPTION,
  isFetchingFirmware: isFetchingFirmware.value,
  networkStatusMessage: networkStatusMessage.value
}));

const fleetUdpLogsComputed = computed<FleetUdpLogs>(() => ({
  isMonitoring: isNetworkUdpMonitoring.value,
  target: networkUdpTarget.value,
  logs: filteredNetworkLogs.value
}));

const gatewayEventsComputed = computed(() => ({
  events: gatewayEvents.value,
  status: gatewayEventsStatus.value,
  isLoading: false
}));

const mqttGatewayOptionsComputed = computed<MqttGatewayOption[]>(() =>
  Object.values(mqttGateways.value).map(gw => ({
    chip_id: gw.chip_id,
    label: `${lrsDeviceName(gw.chip_id)} (lrs-${gw.chip_id})`
  }))
);

const fleetTransportStateComputed = computed<FleetTransportState>(() => ({
  ports: ports.value.map(p => ({ port_name: p.port_name, description: p.description || undefined })),
  mqttGatewayOptions: mqttGatewayOptionsComputed.value,
  isSelectedMqttGatewayDiscovered: isSelectedMqttGatewayDiscovered.value,
  manualMqttGatewayError: manualMqttGatewayError.value,
  fleetTransport: fleetTransport.value,
  serialPortSelectorDisabled: serialPortSelectorDisabled.value,
  showPairAdminPassword: showPairAdminPassword.value
}));

const fleetInventorySummaryComputed = computed<FleetInventorySummary>(() => ({
  totalCount: loraInventory.value.length,
  selectedCount: selectedLoraInventoryCount.value,
  hasAnyRemoteIp: hasAnyRemoteIp.value
}));

const fleetCandidateSummaryComputed = computed<FleetCandidateSummary>(() => ({
  total: candidateTotal.value,
  truncated: candidateTruncated.value
}));

const fleetConfigComputed = computed<FleetConfig>({
  get: () => ({
    sessionConnectionType: sessionConnectionType.value,
    selectedPort: selectedPort.value,
    selectedMqttManualChipId: selectedMqttManualChipId.value,
    selectedMqttGatewayChipId: selectedMqttGatewayChipId.value,
    pairAdminPassword: pairAdminPassword.value,
    region: region.value,
    selectedVersion: selectedVersion.value
  }),
  set: (val) => {
    sessionConnectionType.value = val.sessionConnectionType;
    selectedPort.value = val.selectedPort;
    selectedMqttManualChipId.value = val.selectedMqttManualChipId;
    selectedMqttGatewayChipId.value = val.selectedMqttGatewayChipId;
    pairAdminPassword.value = val.pairAdminPassword;
    region.value = val.region as any;
    selectedVersion.value = val.selectedVersion;
  }
});

function handleToggleRowSelection(address: number | string, selected: boolean) {
  const d = loraInventory.value.find(x => x.address === address);
  if (d) {
    d.selected = selected;
  }
}

function handleAdoptCandidatePayload(payload: FleetCandidateActionPayload) {
  const candidate = loraCandidates.value.find(c =>
    (payload.chip_id && c.chip_id === payload.chip_id) ||
    (c.address === payload.address)
  );
  if (candidate) {
    adoptCandidate(candidate);
  } else {
    notify('Selected adoption candidate not found.');
  }
}


const provisionConfigComputed = computed<ProvisionConfig>({
  get: () => ({
    pairTransport: pairTransport.value,
    selectedPort: selectedPort.value,
    selectedMqttManualChipId: selectedMqttManualChipId.value,
    selectedMqttGatewayChipId: selectedMqttGatewayChipId.value,
    pairExpectedCount: pairExpectedCount.value,
    pairFleetKey: pairFleetKey.value,
    pairAdminPassword: pairAdminPassword.value,
    pairWifiSsid: pairWifiSsid.value,
    pairWifiPassword: pairWifiPassword.value
  }),
  set: (val) => {
    pairTransport.value = val.pairTransport;
    selectedPort.value = val.selectedPort;
    selectedMqttManualChipId.value = val.selectedMqttManualChipId;
    selectedMqttGatewayChipId.value = val.selectedMqttGatewayChipId;
    pairExpectedCount.value = val.pairExpectedCount;
    pairFleetKey.value = val.pairFleetKey;
    pairAdminPassword.value = val.pairAdminPassword;
    pairWifiSsid.value = val.pairWifiSsid;
    pairWifiPassword.value = val.pairWifiPassword;
  }
});

const provisionUiStateComputed = computed<ProvisionUiState>({
  get: () => ({
    pairPanelTab: pairPanelTab.value,
    showPairFleetKey: showPairFleetKey.value,
    showPairAdminPassword: showPairAdminPassword.value,
    showPairWifiPassword: showPairWifiPassword.value
  }),
  set: (val) => {
    pairPanelTab.value = val.pairPanelTab;
    showPairFleetKey.value = val.showPairFleetKey;
    showPairAdminPassword.value = val.showPairAdminPassword;
    showPairWifiPassword.value = val.showPairWifiPassword;
  }
});

const provisionGatewayStateComputed = computed<ProvisionGatewayState>(() => {
  const gatewayKey = pairTransport.value === 'mqtt' ? selectedMqttGatewayChipId.value : gatewaySelectedPort.value;
  const state = gatewayKey ? serialDeviceState(gatewayKey) : null;
  const hasWarning = !!(gatewayKey && state?.status && !state.status.role_tx);
  const hasUncommissioned = !!(gatewayKey && state?.status && state.status.role_tx && (!state.status.commissioned || state.status.fleet_passphrase_default));
  const gatewayLabel = pairTransport.value === 'mqtt' && gatewayKey
    ? `lrs-${gatewayKey}`
    : (gatewayKey || '-');

  return {
    hasGatewayDeviceWarning: hasWarning,
    hasUncommissionedWarning: hasUncommissioned,
    gatewayLabel,
    isGatewayLoadDisabled: isGatewayLoading.value || isPairBusy.value || !gatewayKey,
    isGatewayLoading: isGatewayLoading.value,
    gatewayRoleLabel: state?.status?.role || '-'
  };
});

const provisionTransportStateComputed = computed<ProvisionTransportState>(() => ({
  ports: ports.value.map(p => ({ port_name: p.port_name, description: p.description || undefined })),
  mqttGatewayOptions: Object.values(mqttGateways.value).map(gw => ({
    chip_id: gw.chip_id,
    label: `${lrsDeviceName(gw.chip_id)} (lrs-${gw.chip_id})`
  })),
  isSelectedMqttGatewayDiscovered: isSelectedMqttGatewayDiscovered.value,
  manualMqttGatewayError: manualMqttGatewayError.value,
  serialPortSelectorDisabled: serialPortSelectorDisabled.value,
  isRefreshingPorts: isRefreshingPorts.value
}));

const provisionSessionSummaryComputed = computed<ProvisionSessionSummary | null>(() => {
  const session = pairStatus.value?.session;
  if (!session) return null;
  return {
    state: session.state,
    foundCount: pairDiscoveredDeviceCount.value,
    maxRemotes: session.max_remotes,
    verifiedCount: session.verified_count,
    failedCount: session.failed_count
  };
});

const provisionDiscoveredDeviceRowComputed = computed<ProvisionDiscoveredDeviceRow[]>(() => {
  const devices = pairStatus.value?.devices || [];
  return devices.map(device => ({
    chip_id_hex: device.chip_id_hex,
    rssi: device.rssi,
    current_address: device.current_address > 0 && device.current_address < 255 ? device.current_address : '-',
    assigned_address: device.assigned_address || '-',
    firmware: compactFirmwareVersion(device.fw_major, device.fw_minor, device.fw_patch, device.fw_build),
    isConflict: !!device.address_conflict,
    state: device.state,
    stateBorderClass: device.address_conflict ? 'border-amber-500/40 bg-amber-500/10 text-amber-300' : 'border-slate-700 bg-slate-800/40 text-slate-300'
  }));
});

const provisionPairStateComputed = computed<ProvisionPairState>(() => ({
  isPairBusy: isPairBusy.value,
  pairPrimaryDisabled: pairPrimaryDisabled.value,
  gatewayReady: gatewayReady.value,
  sessionSummary: provisionSessionSummaryComputed.value,
  discoveredDevices: provisionDiscoveredDeviceRowComputed.value
}));

const provisionWifiStateComputed = computed<ProvisionWifiState>(() => ({
  wifiNetworks: wifiNetworks.value.map(n => ({
    ssid: n.ssid,
    bssid: n.bssid,
    rssi: n.rssi,
    channel: n.channel,
    secure: n.secure
  })),
  isWifiScanning: isWifiScanning.value,
  isWifiApplying: isWifiApplying.value,
  isFleetWifiSending: isFleetWifiSending.value,
  gatewayWifiReady: gatewayWifiReady.value,
  gatewayWifiStatusText: gatewayWifiStatusText.value,
  gatewayWifiHelpText: gatewayWifiHelpText.value,
  pairWifiSsidInScan: pairWifiSsidInScan.value
}));

const provisionIdentifyStateComputed = computed<ProvisionIdentifyState>(() => ({
  available: identifyAvailable.value,
  disabled: identifyDisabled.value,
  isIdentifying: isIdentifying.value
}));

</script>

<template>
  <div class="relative h-full flex flex-col gap-3">
    <SessionMqttBanner
      v-model="sessionConnectionType"
      v-model:show-session-config-panel="showSessionConfigPanel"
      v-model:local-broker-port="localBrokerPort"
      v-model:mqtt-draft="monitorMqttDraftState"
      :is-session-connected="isSessionConnected"
      :local-broker-state="localBrokerState"
      :mqtt-settings-state="mqttSettingsState"
      @start-local-broker="startAndConnectLocalBroker"
      @copy-gateway-settings="handleCopyLocalGatewaySettings"
      @toggle-mqtt-connection="toggleMonitorMqttConnection"
    />

    <div :class="['grid gap-3 flex-1 min-h-0 transition-all duration-500', activityFullscreen || activeMode === 'network' || activeMode === 'monitor' ? 'grid-cols-1' : 'grid-cols-1 lg:grid-cols-2']">
      <!-- Log Panel -->
      <ActivityPanel
        ref="activityPanelRef"
        :active-mode="activeMode || 'pair'"
        :activity-fullscreen="activityFullscreen"
        :active-logs="activeLogs"
        :activity-busy="activityBusy"
        :crash-count="crashCount"
        :has-active-device-info="hasActiveDeviceInfo"
        :serial-devices-by-port="serialDevicesByPort"
        :bulk-state="{
          bulkMode,
          bulkSelectedPorts,
          isBulkFlashing,
          isBulkResetting
        }"
        :monitor-state="{
          isMonitoring,
          monitorDeviceLabel,
          activeMonitorPort,
          selectedPort,
          serialUptimeLabel
        }"
        :identify-state="{
          available: identifyAvailable,
          disabled: identifyDisabled,
          isIdentifying
        }"
        @copy-activity-log="copyActivityLog"
        @clear-activity-log="clearActivityLog"
        @trigger-identify="triggerIdentify"
        @copy-active-password="copyActivePassword"
        @toggle-monitor="toggleMonitor"
      />

      <ProvisionMode
        v-if="activeMode === 'pair'"
        v-model="provisionConfigComputed"
        v-model:ui="provisionUiStateComputed"
        :gateway-state="provisionGatewayStateComputed"
        :transport-state="provisionTransportStateComputed"
        :pair-state="provisionPairStateComputed"
        :wifi-state="provisionWifiStateComputed"
        :identify-state="provisionIdentifyStateComputed"
        @trigger-identify="triggerIdentify"
        @load-gateway="loadEasyPairGateway(false)"
        @open-wifi-tab="openPairWifiTab"
        @refresh-ports="refreshPorts"
        @manual-chip-input="handleManualMqttGatewayInput"
        @generate-fleet-key="generatePairFleetKey(true)"
        @mark-fleet-key-manual="markPairFleetKeyManual"
        @copy-fleet-key="copyPairFleetKey"
        @copy-admin-password="copyPairAdminPassword"
        @run-or-cancel-provision="isPairBusy ? cancelEasyPair() : runEasyPair()"
        @start-discovery="startEasyPairDiscovery"
        @scan-wifi="scanGatewayWifi"
        @connect-gateway-wifi="connectGatewayWifi"
        @send-wifi-to-remotes="sendWifiToRemotes"
        @refresh-easy-pair-status="refreshEasyPairStatus(true)"
      />

      <!-- Right Panel (Controls + Details) - Hidden in Monitor Mode -->
      <FlashMode
        v-if="activeMode === 'serial' && !isMonitoring"
        v-model:form="flashFormDraftState"
        :device-state="flashDeviceState"
        :bulk-state="flashBulkState"
        :system-state="systemConfigState"
        :class="{ 'opacity-0 pointer-events-none': isMonitoring }"
        class="transition-opacity duration-300"
        @trigger-identify="triggerIdentify"
        @refresh-ports="refreshPorts"
        @fetch-firmware="fetchFirmware"
        @start-flash="startFlash"
        @read-device-info="readDeviceInfo"
        @copy-all-device-info="copyAllDeviceInfo"
        @copy-to-clipboard="copyToClipboard"
        @toggle-select-all-bulk="toggleSelectAllBulkPorts"
        @start-bulk-flash="startBulkFlash"
        @start-bulk-reset="startBulkFactoryReset"
      />

      <SettingsMode
        v-if="activeMode === 'settings'"
        v-model:form="settingsFormComputed"
        v-model:config="serialAdminConfig"
        :header-state="settingsHeaderStateComputed"
        :transport-state="settingsTransportStateComputed"
        :admin-status="settingsAdminStatusStateComputed"
        :secret-state="settingsSecretStateComputed"
        :wifi-state="settingsWifiStateComputed"
        :serial-admin-is-factory-default="serialAdminIsFactoryDefault"
        :has-active-device-info="hasActiveDeviceInfo"
        :settings-empty-message="settingsEmptyMessage"
        @trigger-identify="triggerIdentify"
        @read-device-info="readDeviceInfo"
        @refresh-status="refreshSerialAdminStatus"
        @fetch-settings="fetchSerialDeviceSettings"
        @copy-config-json="copySerialAdminConfigJson"
        @refresh-ports="refreshPorts"
        @manual-chip-input="handleManualMqttGatewayInput"
        @open-mqtt-settings="openMonitorMqttSettings"
        @scan-wifi="scanSettingsWifi"
        @save-config="saveSerialAdminConfig"
        @reboot-device="rebootSerialDevice"
        @factory-reset="factoryResetSerialDevice"
        @copy-payload="handleCopyPayload"
      />

      <MonitorMode
        v-if="activeMode === 'monitor'"
        v-model:form="monitorFormComputed"
        :header-state="monitorHeaderStateComputed"
        :transport-state="monitorTransportStateComputed"
        :gateway-warning-state="monitorGatewayWarningStateComputed"
        :gateway-summary-state="monitorGatewaySummaryStateComputed"
        :fleet-summary-state="monitorFleetSummaryStateComputed"
        :rows="monitorDisplayRowsComputed"
        :gateway-events="gatewayEventsComputed"
        @trigger-identify="triggerIdentify"
        @toggle-monitor-loop="toggleMonitorLoop"
        @open-mqtt-settings="openMonitorMqttSettings"
        @poll-selected-diagnostics="executeSelectedMonitorPollDiagnostics"
        @poll-device-diagnostics="handleMonitorPollDeviceDiagnostics"
        @gateway-events-clear="clearGatewayEvents"
        @gateway-events-copy="copyGatewayEvents"
      />

      <MonitorMqttSettingsModal
        v-model="showMonitorMqttSettings"
        v-model:draft="monitorMqttDraftState"
        :monitor-mqtt-connected="monitorMqttConnected"
        @close="closeMonitorMqttSettings"
        @toggle-connection="toggleMonitorMqttConnection"
      />

      <FleetMode
        v-if="activeMode === 'network'"
        v-model="fleetConfigComputed"
        v-model:active-dropdown-address="activeDropdownAddress"
        v-model:network-udp-logs-expanded="networkUdpLogsExpanded"
        v-model:gateway-events-expanded="fleetGatewayEventsExpanded"
        v-model:gateway-events-include-log-lines="fleetGatewayEventsIncludeLogLines"
        :rows="fleetDisplayRows"
        :candidates="fleetDisplayCandidates"
        :gateway="fleetGatewayStatusComputed"
        :server="fleetServerStatusComputed"
        :udp-logs="fleetUdpLogsComputed"
        :gateway-events="gatewayEventsComputed"
        :transport-state="fleetTransportStateComputed"
        :inventory-summary="fleetInventorySummaryComputed"
        :candidate-summary="fleetCandidateSummaryComputed"
        @toggle-row-selection="handleToggleRowSelection"
        @scan-start="startLoraInventoryScan"
        @scan-cancel="cancelLoraInventoryScan"
        @server-start="startFirmwareServer"
        @server-stop="stopFirmwareServer"
        @udp-logging-start="triggerGatewayUdpLogging"
        @udp-logging-stop="stopNetworkUdpMonitor"
        @udp-logs-copy="copyNetworkUdpLog"
        @gateway-events-clear="clearGatewayEvents"
        @gateway-events-copy="copyGatewayEvents"
        @lora-inventory-debug-copy="copyLoraInventoryDebug"
        @manual-chip-input="handleManualMqttGatewayInput"
        @firmware-fetch="fetchFirmware"
        @gateway-load="loadNetworkGateway"
        @gateway-identify="triggerIdentify"
        @gateway-flash="flashFleetGateway"
        @remote-flash="handleFleetRemoteFlash"
        @remote-settings="handleFleetRemoteSettings"
        @remote-reboot="handleFleetRemoteReboot"
        @remote-view-logs="handleFleetRemoteViewLogs"
        @remote-forget="handleFleetRemoteForget"
        @remote-factory-reset="handleFleetRemoteFactoryReset"
        @candidate-adopt="handleAdoptCandidatePayload"
      />
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
    <RemoteFactoryResetModal
      v-model="isFactoryResetTargetModalOpen"
      v-model:draft="factoryResetTargetDraftComputed"
      :address="factoryResetTargetAddressComputed"
      @cancel="factoryResetTargetModal = null"
      @confirm="confirmRemoteFactoryReset"
    />

    <!-- Device Settings Modal -->
    <RemoteSettingsModal
      v-model="isSettingsDeviceModalOpen"
      v-model:draft="settingsDeviceModalState"
      :device="settingsDeviceModal ? settingsDeviceModal.device : { address: '' }"
      @close="settingsDeviceModal = null"
      @execute-sensors="handleRemoteSettingsSensors"
      @execute-wifi="handleRemoteSettingsWifi"
      @execute-fleet-key="handleRemoteSettingsFleetKey"
    />
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
