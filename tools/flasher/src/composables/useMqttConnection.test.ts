import { describe, it, expect, vi } from 'vitest';
import { ref } from 'vue';
import { useMqttConnection } from './useMqttConnection';

describe('useMqttConnection', () => {
  const mockNotify = vi.fn();
  const mockSetSessionStatusMessage = vi.fn();
  const isSerialSessionConnected = ref(false);
  const serialSessionConnectionState = ref<'active' | 'partial' | 'offline'>('offline');

  const defaultOptions = (mockInvoke = vi.fn()) => ({
    notify: mockNotify,
    setSessionStatusMessage: mockSetSessionStatusMessage,
    isSerialSessionConnected,
    serialSessionConnectionState,
    invoke: mockInvoke,
  });

  it('computes transport connectivity based on the selected session transport', () => {
    const { sessionConnectionType, isSessionTransportConnected, sessionMqttConnected, localBrokerRunning, sessionMqttHost, sessionMqttPort, localBrokerPort } = useMqttConnection(defaultOptions());

    // 1. Serial Type
    sessionConnectionType.value = 'serial';
    isSerialSessionConnected.value = false;
    expect(isSessionTransportConnected.value).toBe(false);
    isSerialSessionConnected.value = true;
    expect(isSessionTransportConnected.value).toBe(true);

    // 2. MQTT Type
    sessionConnectionType.value = 'mqtt';
    sessionMqttConnected.value = false;
    expect(isSessionTransportConnected.value).toBe(false);
    sessionMqttConnected.value = true;
    expect(isSessionTransportConnected.value).toBe(true);

    // 3. Local Broker Type
    sessionConnectionType.value = 'local_broker';
    localBrokerRunning.value = false;
    expect(isSessionTransportConnected.value).toBe(false);

    localBrokerRunning.value = true;
    sessionMqttConnected.value = true;
    sessionMqttHost.value = '127.0.0.1';
    sessionMqttPort.value = 1883;
    localBrokerPort.value = 1883;
    expect(isSessionTransportConnected.value).toBe(true);

    sessionMqttPort.value = 1884;
    expect(isSessionTransportConnected.value).toBe(false);
  });

  it('computes transport connection state correctly', () => {
    const { sessionConnectionType, computedTransportConnectionState, sessionMqttConnected, localBrokerRunning, isLocalBrokerStarting } = useMqttConnection(defaultOptions());

    sessionConnectionType.value = 'serial';
    serialSessionConnectionState.value = 'active';
    expect(computedTransportConnectionState.value).toBe('active');

    sessionConnectionType.value = 'mqtt';
    sessionMqttConnected.value = true;
    expect(computedTransportConnectionState.value).toBe('active');
    sessionMqttConnected.value = false;
    expect(computedTransportConnectionState.value).toBe('partial');

    sessionConnectionType.value = 'local_broker';
    localBrokerRunning.value = false;
    isLocalBrokerStarting.value = true;
    expect(computedTransportConnectionState.value).toBe('partial');
  });

  it('manages settings modal open/close states and draft updates', () => {
    const {
      showMqttConnectionSettings,
      sessionMqttHost,
      sessionMqttPort,
      sessionMqttDraftHost,
      sessionMqttDraftPort,
      sessionMqttDraftState,
      openMqttConnectionSettings,
      closeMqttConnectionSettings
    } = useMqttConnection(defaultOptions());

    sessionMqttHost.value = 'custom.host';
    sessionMqttPort.value = 8883;
    
    openMqttConnectionSettings();
    expect(showMqttConnectionSettings.value).toBe(true);
    expect(sessionMqttDraftHost.value).toBe('custom.host');
    expect(sessionMqttDraftPort.value).toBe(8883);

    // Update draft computed
    sessionMqttDraftState.value = {
      host: 'new.host',
      port: 1883,
      topicRoot: 'lora-new',
      user: 'user',
      pass: 'pass',
      showPass: true
    };

    expect(sessionMqttDraftHost.value).toBe('new.host');

    closeMqttConnectionSettings();
    expect(showMqttConnectionSettings.value).toBe(false);
  });

  describe('hydration and state change handlers', () => {
    it('applies MQTT state changed payload cases', () => {
      const { sessionMqttConnected, applyMqttStateChanged } = useMqttConnection(defaultOptions());

      mockSetSessionStatusMessage.mockClear();
      applyMqttStateChanged('Connected');
      expect(sessionMqttConnected.value).toBe(true);
      expect(mockSetSessionStatusMessage).toHaveBeenCalledWith('MQTT broker connected.');

      mockSetSessionStatusMessage.mockClear();
      applyMqttStateChanged('Connecting');
      expect(mockSetSessionStatusMessage).toHaveBeenCalledWith('MQTT broker connecting...');

      mockSetSessionStatusMessage.mockClear();
      applyMqttStateChanged('Disconnected');
      expect(sessionMqttConnected.value).toBe(false);
      expect(mockSetSessionStatusMessage).toHaveBeenCalledWith('MQTT broker disconnected.');

      mockSetSessionStatusMessage.mockClear();
      applyMqttStateChanged({ Error: 'Authentication Failed' });
      expect(sessionMqttConnected.value).toBe(false);
      expect(mockSetSessionStatusMessage).toHaveBeenCalledWith('MQTT broker error: Authentication Failed');
    });

    it('hydrates MQTT state', () => {
      const { sessionMqttConnected, hydrateMqttState } = useMqttConnection(defaultOptions());
      hydrateMqttState('Connected');
      expect(sessionMqttConnected.value).toBe(true);
    });

    it('hydrates local broker status', () => {
      const { localBrokerPort, localBrokerRunning, hydrateLocalBrokerStatus } = useMqttConnection(defaultOptions());
      hydrateLocalBrokerStatus(1883);
      expect(localBrokerPort.value).toBe(1883);
      expect(localBrokerRunning.value).toBe(true);

      const next = useMqttConnection(defaultOptions());
      next.hydrateLocalBrokerStatus(null);
      expect(next.localBrokerRunning.value).toBe(false);
    });
  });

  describe('actions', () => {
    it('connects and disconnects MQTT explicitly', async () => {
      const mockInvoke = vi.fn().mockResolvedValue(true);
      const { sessionMqttConnected, connectSessionMqtt, disconnectSessionMqtt } = useMqttConnection(defaultOptions(mockInvoke));

      // 1. Connect path
      sessionMqttConnected.value = false;
      await connectSessionMqtt();
      expect(mockInvoke).toHaveBeenCalledWith('connect_mqtt_broker', expect.any(Object));
      expect(sessionMqttConnected.value).toBe(true);

      // 2. Disconnect path
      await disconnectSessionMqtt();
      expect(mockInvoke).toHaveBeenCalledWith('disconnect_mqtt_broker');
      expect(sessionMqttConnected.value).toBe(false);
    });

    it('can connect the shared broker without changing the operational transport and preserves a blank password draft', async () => {
      const mockInvoke = vi.fn().mockResolvedValue(true);
      const connection = useMqttConnection(defaultOptions(mockInvoke));
      connection.sessionConnectionType.value = 'serial';
      connection.sessionMqttPassword.value = 'existing-secret';
      connection.sessionMqttDraftPassword.value = '';

      await connection.connectSessionMqtt(true);

      expect(connection.sessionConnectionType.value).toBe('serial');
      expect(connection.sessionMqttDraftPassword.value).toBe('');
      expect(mockInvoke).toHaveBeenCalledWith('connect_mqtt_broker', {
        config: expect.objectContaining({ password: 'existing-secret' })
      });
    });

    it('starts local MQTT broker', async () => {
      const mockInvoke = vi.fn().mockResolvedValue(['192.168.1.10']);
      const { localBrokerRunning, localBrokerLans, startLocalMqttBroker } = useMqttConnection(defaultOptions(mockInvoke));

      await startLocalMqttBroker();
      expect(mockInvoke).toHaveBeenCalledWith('start_local_mqtt_broker', { port: 1883 });
      expect(localBrokerRunning.value).toBe(true);
      expect(localBrokerLans.value).toEqual(['192.168.1.10']);
    });

    it('keeps remote broker parameters while connecting the local broker', async () => {
      const mockInvoke = vi.fn().mockImplementation((command: string) => {
        if (command === 'start_local_mqtt_broker') return Promise.resolve(['192.168.1.10']);
        return Promise.resolve(true);
      });
      const connection = useMqttConnection(defaultOptions(mockInvoke));
      connection.sessionMqttDraftState.value = {
        host: 'remote.example',
        port: 2883,
        topicRoot: 'field',
        user: 'operator',
        pass: '',
        showPass: false
      };
      connection.sessionMqttPassword.value = 'remote-secret';

      await connection.startAndConnectLocalBroker();

      expect(connection.sessionMqttDraftState.value).toEqual(expect.objectContaining({
        host: 'remote.example',
        port: 2883,
        topicRoot: 'field',
        user: 'operator'
      }));
      expect(mockInvoke).toHaveBeenCalledWith('connect_mqtt_broker', {
        config: expect.objectContaining({
          host: '127.0.0.1',
          port: 1883,
          user: null,
          password: null,
          topic_root: 'lora'
        })
      });

      await connection.connectConfiguredSessionMqtt();
      expect(mockInvoke).toHaveBeenLastCalledWith('connect_mqtt_broker', {
        config: expect.objectContaining({
          host: 'remote.example',
          port: 2883,
          user: 'operator',
          password: 'remote-secret',
          topic_root: 'field'
        })
      });
    });
  });
});
