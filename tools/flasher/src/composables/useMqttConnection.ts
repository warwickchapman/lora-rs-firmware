import { ref, computed, Ref, ComputedRef } from 'vue';

export interface MqttDraftState {
  host: string;
  port: number;
  topicRoot: string;
  user: string;
  pass: string;
  showPass: boolean;
}

export interface UseMqttConnectionOptions {
  notify: (msg: string) => void;
  setMonitorStatusMessage: (msg: string) => void;
  isSerialSessionConnected: Ref<boolean> | ComputedRef<boolean>;
  serialSessionConnectionState: Ref<'active' | 'partial' | 'offline'> | ComputedRef<'active' | 'partial' | 'offline'>;
  // Optional Tauri invoke injection for test mocking
  invoke?: <T = any>(cmd: string, args?: any) => Promise<T>;
}

export function useMqttConnection(options: UseMqttConnectionOptions) {
  const { 
    notify, 
    setMonitorStatusMessage, 
    isSerialSessionConnected, 
    serialSessionConnectionState,
    invoke = (cmd: string, args?: any) => import('@tauri-apps/api/core').then(m => m.invoke(cmd, args))
  } = options;

  // Connection State Refs
  const sessionConnectionType = ref<'serial' | 'mqtt' | 'local_broker'>('serial');
  const localBrokerPort = ref(1883);
  const localBrokerRunning = ref(false);
  const localBrokerLans = ref<string[]>([]);
  const localBrokerError = ref('');
  const localBrokerClientMessage = ref('');
  const isLocalBrokerClientConnecting = ref(false);
  const isLocalBrokerStarting = ref(false);
  const showSessionConfigPanel = ref(false);

  // Monitor Client Configuration Refs
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

  // Computeds
  const isSessionConnected = computed(() => {
    if (sessionConnectionType.value === 'serial') {
      return isSerialSessionConnected.value;
    }
    if (sessionConnectionType.value === 'mqtt') {
      return monitorMqttConnected.value;
    }
    if (sessionConnectionType.value === 'local_broker') {
      return localBrokerRunning.value &&
             monitorMqttConnected.value &&
             monitorMqttHost.value === '127.0.0.1' &&
             monitorMqttPort.value === localBrokerPort.value;
    }
    return false;
  });

  const computedSessionConnectionState = computed<'active' | 'partial' | 'offline'>(() => {
    if (sessionConnectionType.value === 'serial') {
      return serialSessionConnectionState.value;
    }
    if (sessionConnectionType.value === 'mqtt') {
      if (monitorMqttConnected.value) {
        return 'active';
      }
      return 'partial';
    }
    if (sessionConnectionType.value === 'local_broker') {
      const isClientConnected = monitorMqttConnected.value && monitorMqttHost.value === '127.0.0.1' && monitorMqttPort.value === localBrokerPort.value;
      if (localBrokerRunning.value && isClientConnected) {
        return 'active';
      }
      if (localBrokerRunning.value || isLocalBrokerStarting.value || isLocalBrokerClientConnecting.value) {
        return 'partial';
      }
      return 'offline';
    }
    return 'offline';
  });

  const monitorMqttDraftState = computed({
    get: () => ({
      host: monitorMqttDraftHost.value,
      port: monitorMqttDraftPort.value,
      topicRoot: monitorMqttDraftTopicRoot.value,
      user: monitorMqttDraftUser.value,
      pass: monitorMqttDraftPassword.value,
      showPass: showMonitorMqttPassword.value
    }),
    set: (val) => {
      monitorMqttDraftHost.value = val.host;
      monitorMqttDraftPort.value = val.port;
      monitorMqttDraftTopicRoot.value = val.topicRoot;
      if (val.user !== undefined) monitorMqttDraftUser.value = val.user;
      if (val.pass !== undefined) monitorMqttDraftPassword.value = val.pass;
      if (val.showPass !== undefined) showMonitorMqttPassword.value = val.showPass;
    }
  });

  const localBrokerState = computed(() => ({
    running: localBrokerRunning.value,
    error: localBrokerError.value,
    lans: localBrokerLans.value,
    isStarting: isLocalBrokerStarting.value,
    isClientConnecting: isLocalBrokerClientConnecting.value,
    clientMessage: localBrokerClientMessage.value
  }));

  const mqttSettingsState = computed(() => ({
    connected: monitorMqttConnected.value,
    host: monitorMqttHost.value,
    port: monitorMqttPort.value
  }));

  // Connection Handler Methods
  async function toggleMonitorMqttConnection(preserveSessionConnectionType = false) {
    if (monitorMqttConnected.value) {
      try {
        await invoke('disconnect_mqtt_broker');
        monitorMqttConnected.value = false;
        setMonitorStatusMessage('MQTT monitor disconnected.');
      } catch (e) {
        notify(`MQTT disconnect error: ${e}`);
      }
    } else {
      monitorMqttHost.value = monitorMqttDraftHost.value.trim() || 'venus.local';
      monitorMqttPort.value = Number(monitorMqttDraftPort.value || 1883);
      monitorMqttUser.value = monitorMqttDraftUser.value;
      monitorMqttPassword.value = monitorMqttDraftPassword.value;
      monitorMqttTopicRoot.value = monitorMqttDraftTopicRoot.value.trim() || 'lora';
      if (!preserveSessionConnectionType) {
        sessionConnectionType.value = 'mqtt';
      }

      try {
        await invoke('connect_mqtt_broker', {
          config: {
            host: monitorMqttHost.value,
            port: monitorMqttPort.value,
            user: monitorMqttUser.value || null,
            password: monitorMqttPassword.value || null,
            topic_root: monitorMqttTopicRoot.value
          }
        });
        monitorMqttConnected.value = true;
        setMonitorStatusMessage('MQTT monitor connected.');
      } catch (e) {
        notify(`MQTT connection error: ${e}`);
      }
    }
    showMonitorMqttSettings.value = false;
  }

  async function startLocalMqttBroker() {
    if (localBrokerRunning.value) return;
    isLocalBrokerStarting.value = true;
    localBrokerError.value = '';
    localBrokerClientMessage.value = '';
    try {
      const lans = await invoke<string[]>('start_local_mqtt_broker', {
        port: localBrokerPort.value
      });
      localBrokerLans.value = lans;
      localBrokerRunning.value = true;
      notify(`Local broker started on port ${localBrokerPort.value}`);
    } catch (e) {
      localBrokerError.value = String(e);
      notify(`Failed to start local broker: ${e}`);
    } finally {
      isLocalBrokerStarting.value = false;
    }
  }

  async function applyLocalBrokerToMqttConfig() {
    if (!localBrokerRunning.value) return;
    isLocalBrokerClientConnecting.value = true;
    localBrokerClientMessage.value = `Connecting Flasher client to 127.0.0.1:${localBrokerPort.value}...`;
    monitorMqttDraftHost.value = '127.0.0.1';
    monitorMqttDraftPort.value = localBrokerPort.value;
    monitorMqttDraftUser.value = '';
    monitorMqttDraftPassword.value = '';
    monitorMqttDraftTopicRoot.value = 'lora';

    try {
      if (monitorMqttConnected.value) {
        await invoke('disconnect_mqtt_broker');
        monitorMqttConnected.value = false;
      }

      await toggleMonitorMqttConnection(true);
      if (monitorMqttConnected.value) {
        localBrokerClientMessage.value = `Flasher client connected to local broker on 127.0.0.1:${localBrokerPort.value}.`;
        notify('Flasher client connected to local broker.');
      } else {
        localBrokerClientMessage.value = 'Flasher client did not connect to the local broker.';
      }
    } catch (e) {
      localBrokerClientMessage.value = `Flasher client connection failed: ${e}`;
      notify(`Local broker client connection failed: ${e}`);
    } finally {
      isLocalBrokerClientConnecting.value = false;
    }
  }

  async function startAndConnectLocalBroker() {
    if (isLocalBrokerStarting.value || isLocalBrokerClientConnecting.value) return;

    if (!localBrokerRunning.value) {
      await startLocalMqttBroker();
    }

    if (!localBrokerRunning.value) {
      showSessionConfigPanel.value = true;
      return;
    }

    const alreadyConnected =
      monitorMqttConnected.value &&
      monitorMqttHost.value === '127.0.0.1' &&
      monitorMqttPort.value === localBrokerPort.value;

    if (!alreadyConnected) {
      await applyLocalBrokerToMqttConfig();
    }
  }

  function adoptMonitorMqttFromStatus(st: { mqtt?: { host?: string; port?: number; topic_root?: string } } | null) {
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

  // Hydration & state sync hooks for external events
  function applyMqttStateChanged(state: string | { Error: string } | null | undefined) {
    if (state === 'Connected') {
      monitorMqttConnected.value = true;
      setMonitorStatusMessage('MQTT monitor connected.');
    } else if (state === 'Connecting') {
      setMonitorStatusMessage('MQTT monitor connecting...');
    } else if (state === 'Disconnected') {
      monitorMqttConnected.value = false;
      setMonitorStatusMessage('MQTT monitor disconnected.');
    } else if (state && typeof state === 'object' && 'Error' in state) {
      monitorMqttConnected.value = false;
      setMonitorStatusMessage(`MQTT monitor error: ${state.Error}`);
    }
  }

  function hydrateMqttState(state: string | null | undefined) {
    if (state === 'Connected') {
      monitorMqttConnected.value = true;
      setMonitorStatusMessage('MQTT monitor connected.');
    }
  }

  function hydrateLocalBrokerStatus(activePort: number | null) {
    if (activePort) {
      localBrokerPort.value = activePort;
      localBrokerRunning.value = true;
    }
  }

  return {
    sessionConnectionType,
    localBrokerPort,
    localBrokerRunning,
    localBrokerLans,
    localBrokerError,
    localBrokerClientMessage,
    isLocalBrokerClientConnecting,
    isLocalBrokerStarting,
    showSessionConfigPanel,
    monitorMqttHost,
    monitorMqttPort,
    monitorMqttUser,
    monitorMqttPassword,
    monitorMqttTopicRoot,
    monitorMqttConnected,
    showMonitorMqttPassword,
    showMonitorMqttSettings,
    monitorMqttDraftHost,
    monitorMqttDraftPort,
    monitorMqttDraftUser,
    monitorMqttDraftPassword,
    monitorMqttDraftTopicRoot,
    isSessionConnected,
    computedSessionConnectionState,
    monitorMqttDraftState,
    localBrokerState,
    mqttSettingsState,
    toggleMonitorMqttConnection,
    startLocalMqttBroker,
    applyLocalBrokerToMqttConfig,
    startAndConnectLocalBroker,
    adoptMonitorMqttFromStatus,
    openMonitorMqttSettings,
    closeMonitorMqttSettings,
    applyMqttStateChanged,
    hydrateMqttState,
    hydrateLocalBrokerStatus
  };
}
