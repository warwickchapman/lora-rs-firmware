import { ref, computed, Ref, ComputedRef } from 'vue';

export interface MqttDraftState {
  host: string;
  port: number;
  topicRoot: string;
  user: string;
  pass: string;
  showPass: boolean;
}

export interface MqttBrokerIdentity {
  host: string;
  port: number;
  topicRoot: string;
  user: string;
}

export interface MqttGatewaySelectionDecision {
  chipId: string;
  automatic: boolean;
}

export function mqttBrokerSelectionKey(identity: MqttBrokerIdentity): string {
  const host = identity.host.trim().toLowerCase();
  const port = Number(identity.port || 1883);
  const topicRoot = identity.topicRoot.trim().replace(/^\/+|\/+$/g, '') || 'lora';
  const user = identity.user.trim();
  return JSON.stringify([host, port, topicRoot, user]);
}

export function chooseMqttGatewaySelection(
  preferredChipId: string,
  currentChipId: string,
  discoveredChipIds: string[],
): MqttGatewaySelectionDecision | null {
  const normalize = (value: string) => value.trim().toLowerCase().replace(/^lrs-/, '');
  const discovered = Array.from(new Set(discoveredChipIds.map(normalize).filter(Boolean)));
  const preferred = normalize(preferredChipId);
  const current = normalize(currentChipId);
  if (preferred && discovered.includes(preferred)) return { chipId: preferred, automatic: false };
  if (current && discovered.includes(current)) return { chipId: current, automatic: false };
  if (discovered.length === 1) return { chipId: discovered[0], automatic: true };
  return null;
}

interface MqttConnectionOverride {
  host: string;
  port: number;
  topicRoot: string;
  user: string;
  password: string;
}

export interface UseMqttConnectionOptions {
  notify: (msg: string) => void;
  setSessionStatusMessage: (msg: string) => void;
  isSerialSessionConnected: Ref<boolean> | ComputedRef<boolean>;
  serialSessionConnectionState: Ref<'active' | 'partial' | 'offline'> | ComputedRef<'active' | 'partial' | 'offline'>;
  // Optional Tauri invoke injection for test mocking
  invoke?: <T = any>(cmd: string, args?: any) => Promise<T>;
}

export function useMqttConnection(options: UseMqttConnectionOptions) {
  const {
    notify,
    setSessionStatusMessage,
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

  // Shared broker client configuration
  const sessionMqttHost = ref('venus.local');
  const sessionMqttPort = ref(1883);
  const sessionMqttUser = ref('');
  const sessionMqttPassword = ref('');
  const sessionMqttTopicRoot = ref('lora');
  const sessionMqttConnected = ref(false);
  const sessionMqttConnectionState = ref<'disconnected' | 'connecting' | 'connected' | 'error'>('disconnected');
  const sessionMqttError = ref('');
  const showSessionMqttPassword = ref(false);
  const showMqttConnectionSettings = ref(false);
  const sessionMqttDraftHost = ref('venus.local');
  const sessionMqttDraftPort = ref(1883);
  const sessionMqttDraftUser = ref('');
  const sessionMqttDraftPassword = ref('');
  const sessionMqttDraftTopicRoot = ref('lora');

  // Computeds
  const isSessionTransportConnected = computed(() => {
    if (sessionConnectionType.value === 'serial') {
      return isSerialSessionConnected.value;
    }
    if (sessionConnectionType.value === 'mqtt') {
      return sessionMqttConnected.value;
    }
    if (sessionConnectionType.value === 'local_broker') {
      return localBrokerRunning.value &&
             sessionMqttConnected.value &&
             sessionMqttHost.value === '127.0.0.1' &&
             sessionMqttPort.value === localBrokerPort.value;
    }
    return false;
  });

  const computedTransportConnectionState = computed<'active' | 'partial' | 'offline'>(() => {
    if (sessionConnectionType.value === 'serial') {
      return serialSessionConnectionState.value;
    }
    if (sessionConnectionType.value === 'mqtt') {
      if (sessionMqttConnected.value) {
        return 'active';
      }
      return 'partial';
    }
    if (sessionConnectionType.value === 'local_broker') {
      const isClientConnected = sessionMqttConnected.value && sessionMqttHost.value === '127.0.0.1' && sessionMqttPort.value === localBrokerPort.value;
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

  const sessionMqttDraftState = computed({
    get: () => ({
      host: sessionMqttDraftHost.value,
      port: sessionMqttDraftPort.value,
      topicRoot: sessionMqttDraftTopicRoot.value,
      user: sessionMqttDraftUser.value,
      pass: sessionMqttDraftPassword.value,
      showPass: showSessionMqttPassword.value
    }),
    set: (val) => {
      sessionMqttDraftHost.value = val.host;
      sessionMqttDraftPort.value = val.port;
      sessionMqttDraftTopicRoot.value = val.topicRoot;
      if (val.user !== undefined) sessionMqttDraftUser.value = val.user;
      if (val.pass !== undefined) sessionMqttDraftPassword.value = val.pass;
      if (val.showPass !== undefined) showSessionMqttPassword.value = val.showPass;
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
    connected: sessionMqttConnected.value,
    host: sessionMqttHost.value,
    port: sessionMqttPort.value
  }));

  // Connection Handler Methods
  async function disconnectSessionMqtt() {
    if (sessionMqttConnected.value) {
      try {
        await invoke('disconnect_mqtt_broker');
        sessionMqttConnected.value = false;
        sessionMqttConnectionState.value = 'disconnected';
        sessionMqttError.value = '';
        setSessionStatusMessage('MQTT broker disconnected.');
      } catch (e) {
        sessionMqttConnectionState.value = 'error';
        sessionMqttError.value = String(e);
        notify(`MQTT disconnect error: ${e}`);
      }
    } else {
      sessionMqttConnectionState.value = 'disconnected';
      sessionMqttError.value = '';
    }
  }

  async function connectSessionMqtt(
    preserveSessionConnectionType = false,
    connectionOverride?: MqttConnectionOverride
  ) {
    if (sessionMqttConnected.value) {
      await disconnectSessionMqtt();
      if (sessionMqttConnected.value) return;
    }

    sessionMqttHost.value = connectionOverride?.host || sessionMqttDraftHost.value.trim() || 'venus.local';
    sessionMqttPort.value = Number(connectionOverride?.port || sessionMqttDraftPort.value || 1883);
    sessionMqttUser.value = connectionOverride?.user ?? sessionMqttDraftUser.value;
    if (!connectionOverride && sessionMqttDraftPassword.value) {
      sessionMqttPassword.value = sessionMqttDraftPassword.value;
    }
    sessionMqttTopicRoot.value = connectionOverride?.topicRoot || sessionMqttDraftTopicRoot.value.trim() || 'lora';
    const connectionPassword = connectionOverride?.password ?? sessionMqttPassword.value;
    if (!preserveSessionConnectionType) {
      sessionConnectionType.value = 'mqtt';
    }

    sessionMqttConnectionState.value = 'connecting';
    sessionMqttError.value = '';
    setSessionStatusMessage('MQTT broker connecting...');
    try {
      await invoke('connect_mqtt_broker', {
        config: {
          host: sessionMqttHost.value,
          port: sessionMqttPort.value,
          user: sessionMqttUser.value || null,
          password: connectionPassword || null,
          topic_root: sessionMqttTopicRoot.value
        }
      });
      sessionMqttConnected.value = true;
      sessionMqttConnectionState.value = 'connected';
      if (!connectionOverride) sessionMqttDraftPassword.value = '';
      setSessionStatusMessage('MQTT broker connected.');
      showMqttConnectionSettings.value = false;
    } catch (e) {
      sessionMqttConnected.value = false;
      sessionMqttConnectionState.value = 'error';
      sessionMqttError.value = String(e);
      setSessionStatusMessage(`MQTT broker error: ${e}`);
      notify(`MQTT connection error: ${e}`);
    }
  }

  async function connectConfiguredSessionMqtt() {
    const configuredHost = sessionMqttDraftHost.value.trim() || 'venus.local';
    const configuredPort = Number(sessionMqttDraftPort.value || 1883);
    const configuredTopicRoot = sessionMqttDraftTopicRoot.value.trim() || 'lora';
    const alreadyConnected =
      sessionMqttConnected.value &&
      sessionMqttHost.value === configuredHost &&
      sessionMqttPort.value === configuredPort &&
      sessionMqttUser.value === sessionMqttDraftUser.value &&
      sessionMqttTopicRoot.value === configuredTopicRoot;
    if (alreadyConnected) return;
    await connectSessionMqtt(true);
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
    try {
      await connectSessionMqtt(true, {
        host: '127.0.0.1',
        port: localBrokerPort.value,
        user: '',
        password: '',
        topicRoot: 'lora'
      });
      if (sessionMqttConnected.value) {
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
      return;
    }

    const alreadyConnected =
      sessionMqttConnected.value &&
      sessionMqttHost.value === '127.0.0.1' &&
      sessionMqttPort.value === localBrokerPort.value;

    if (!alreadyConnected) {
      await applyLocalBrokerToMqttConfig();
    }
  }

  async function activateConfiguredSessionTransport() {
    if (sessionConnectionType.value === 'mqtt') {
      await connectConfiguredSessionMqtt();
    } else if (sessionConnectionType.value === 'local_broker') {
      await startAndConnectLocalBroker();
    }
  }

  function adoptSessionMqttFromStatus(st: { mqtt?: { host?: string; port?: number; topic_root?: string } } | null) {
    if (!st?.mqtt) return;
    sessionMqttHost.value = st.mqtt.host || sessionMqttHost.value;
    sessionMqttPort.value = Number(st.mqtt.port || sessionMqttPort.value || 1883);
    sessionMqttTopicRoot.value = st.mqtt.topic_root || sessionMqttTopicRoot.value || 'lora';
  }

  function openMqttConnectionSettings() {
    sessionMqttDraftHost.value = sessionMqttHost.value || 'venus.local';
    sessionMqttDraftPort.value = Number(sessionMqttPort.value || 1883);
    sessionMqttDraftUser.value = sessionMqttUser.value;
    sessionMqttDraftPassword.value = '';
    sessionMqttDraftTopicRoot.value = sessionMqttTopicRoot.value || 'lora';
    showMqttConnectionSettings.value = true;
  }

  function closeMqttConnectionSettings() {
    showMqttConnectionSettings.value = false;
  }

  // Hydration & state sync hooks for external events
  function applyMqttStateChanged(state: string | { Error: string } | null | undefined) {
    if (state === 'Connected') {
      sessionMqttConnected.value = true;
      sessionMqttConnectionState.value = 'connected';
      sessionMqttError.value = '';
      setSessionStatusMessage('MQTT broker connected.');
    } else if (state === 'Connecting') {
      sessionMqttConnectionState.value = 'connecting';
      sessionMqttError.value = '';
      setSessionStatusMessage('MQTT broker connecting...');
    } else if (state === 'Disconnected') {
      sessionMqttConnected.value = false;
      sessionMqttConnectionState.value = 'disconnected';
      sessionMqttError.value = '';
      setSessionStatusMessage('MQTT broker disconnected.');
    } else if (state && typeof state === 'object' && 'Error' in state) {
      sessionMqttConnected.value = false;
      sessionMqttConnectionState.value = 'error';
      sessionMqttError.value = state.Error;
      setSessionStatusMessage(`MQTT broker error: ${state.Error}`);
    }
  }

  function hydrateMqttState(state: string | null | undefined) {
    if (state === 'Connected') {
      sessionMqttConnected.value = true;
      sessionMqttConnectionState.value = 'connected';
      sessionMqttError.value = '';
      setSessionStatusMessage('MQTT broker connected.');
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
    sessionMqttHost,
    sessionMqttPort,
    sessionMqttUser,
    sessionMqttPassword,
    sessionMqttTopicRoot,
    sessionMqttConnected,
    sessionMqttConnectionState,
    sessionMqttError,
    showSessionMqttPassword,
    showMqttConnectionSettings,
    sessionMqttDraftHost,
    sessionMqttDraftPort,
    sessionMqttDraftUser,
    sessionMqttDraftPassword,
    sessionMqttDraftTopicRoot,
    isSessionTransportConnected,
    computedTransportConnectionState,
    sessionMqttDraftState,
    localBrokerState,
    mqttSettingsState,
    connectSessionMqtt,
    connectConfiguredSessionMqtt,
    activateConfiguredSessionTransport,
    disconnectSessionMqtt,
    startLocalMqttBroker,
    applyLocalBrokerToMqttConfig,
    startAndConnectLocalBroker,
    adoptSessionMqttFromStatus,
    openMqttConnectionSettings,
    closeMqttConnectionSettings,
    applyMqttStateChanged,
    hydrateMqttState,
    hydrateLocalBrokerStatus
  };
}
