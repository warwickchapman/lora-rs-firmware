import { describe, it, expect, vi } from 'vitest';
import { ref } from 'vue';
import { useMqttConnection } from './useMqttConnection';

describe('useMqttConnection', () => {
  const mockNotify = vi.fn();
  const mockSetMonitorStatusMessage = vi.fn();
  const isSerialSessionConnected = ref(false);
  const serialSessionConnectionState = ref<'active' | 'partial' | 'offline'>('offline');

  const defaultOptions = (mockInvoke = vi.fn()) => ({
    notify: mockNotify,
    setMonitorStatusMessage: mockSetMonitorStatusMessage,
    isSerialSessionConnected,
    serialSessionConnectionState,
    invoke: mockInvoke,
  });

  it('computes isSessionConnected correctly based on sessionConnectionType', async () => {
    const { sessionConnectionType, isSessionConnected, monitorMqttConnected, localBrokerRunning, monitorMqttHost, monitorMqttPort, localBrokerPort } = useMqttConnection(defaultOptions());

    // 1. Serial Type
    sessionConnectionType.value = 'serial';
    isSerialSessionConnected.value = false;
    expect(isSessionConnected.value).toBe(false);
    isSerialSessionConnected.value = true;
    expect(isSessionConnected.value).toBe(true);

    // 2. MQTT Type
    sessionConnectionType.value = 'mqtt';
    monitorMqttConnected.value = false;
    expect(isSessionConnected.value).toBe(false);
    monitorMqttConnected.value = true;
    expect(isSessionConnected.value).toBe(true);

    // 3. Local Broker Type
    sessionConnectionType.value = 'local_broker';
    localBrokerRunning.value = false;
    expect(isSessionConnected.value).toBe(false);

    localBrokerRunning.value = true;
    monitorMqttConnected.value = true;
    monitorMqttHost.value = '127.0.0.1';
    monitorMqttPort.value = 1883;
    localBrokerPort.value = 1883;
    expect(isSessionConnected.value).toBe(true);

    monitorMqttPort.value = 1884;
    expect(isSessionConnected.value).toBe(false);
  });

  it('computes computedSessionConnectionState correctly', () => {
    const { sessionConnectionType, computedSessionConnectionState, monitorMqttConnected, localBrokerRunning, isLocalBrokerStarting } = useMqttConnection(defaultOptions());

    sessionConnectionType.value = 'serial';
    serialSessionConnectionState.value = 'active';
    expect(computedSessionConnectionState.value).toBe('active');

    sessionConnectionType.value = 'mqtt';
    monitorMqttConnected.value = true;
    expect(computedSessionConnectionState.value).toBe('active');
    monitorMqttConnected.value = false;
    expect(computedSessionConnectionState.value).toBe('partial');

    sessionConnectionType.value = 'local_broker';
    localBrokerRunning.value = false;
    isLocalBrokerStarting.value = true;
    expect(computedSessionConnectionState.value).toBe('partial');
  });

  it('manages settings modal open/close states and draft updates', () => {
    const { 
      showMonitorMqttSettings, 
      monitorMqttHost, 
      monitorMqttPort, 
      monitorMqttDraftHost, 
      monitorMqttDraftPort, 
      monitorMqttDraftState,
      openMonitorMqttSettings, 
      closeMonitorMqttSettings 
    } = useMqttConnection(defaultOptions());

    monitorMqttHost.value = 'custom.host';
    monitorMqttPort.value = 8883;
    
    openMonitorMqttSettings();
    expect(showMonitorMqttSettings.value).toBe(true);
    expect(monitorMqttDraftHost.value).toBe('custom.host');
    expect(monitorMqttDraftPort.value).toBe(8883);

    // Update draft computed
    monitorMqttDraftState.value = {
      host: 'new.host',
      port: 1883,
      topicRoot: 'lora-new',
      user: 'user',
      pass: 'pass',
      showPass: true
    };

    expect(monitorMqttDraftHost.value).toBe('new.host');

    closeMonitorMqttSettings();
    expect(showMonitorMqttSettings.value).toBe(false);
  });

  describe('hydration and state change handlers', () => {
    it('applies MQTT state changed payload cases', () => {
      const { monitorMqttConnected, applyMqttStateChanged } = useMqttConnection(defaultOptions());

      mockSetMonitorStatusMessage.mockClear();
      applyMqttStateChanged('Connected');
      expect(monitorMqttConnected.value).toBe(true);
      expect(mockSetMonitorStatusMessage).toHaveBeenCalledWith('MQTT monitor connected.');

      mockSetMonitorStatusMessage.mockClear();
      applyMqttStateChanged('Connecting');
      expect(mockSetMonitorStatusMessage).toHaveBeenCalledWith('MQTT monitor connecting...');

      mockSetMonitorStatusMessage.mockClear();
      applyMqttStateChanged('Disconnected');
      expect(monitorMqttConnected.value).toBe(false);
      expect(mockSetMonitorStatusMessage).toHaveBeenCalledWith('MQTT monitor disconnected.');

      mockSetMonitorStatusMessage.mockClear();
      applyMqttStateChanged({ Error: 'Authentication Failed' });
      expect(monitorMqttConnected.value).toBe(false);
      expect(mockSetMonitorStatusMessage).toHaveBeenCalledWith('MQTT monitor error: Authentication Failed');
    });

    it('hydrates MQTT state', () => {
      const { monitorMqttConnected, hydrateMqttState } = useMqttConnection(defaultOptions());
      hydrateMqttState('Connected');
      expect(monitorMqttConnected.value).toBe(true);
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
    it('toggles MQTT connection', async () => {
      const mockInvoke = vi.fn().mockResolvedValue(true);
      const { monitorMqttConnected, toggleMonitorMqttConnection } = useMqttConnection(defaultOptions(mockInvoke));

      // 1. Connect path
      monitorMqttConnected.value = false;
      await toggleMonitorMqttConnection();
      expect(mockInvoke).toHaveBeenCalledWith('connect_mqtt_broker', expect.any(Object));
      expect(monitorMqttConnected.value).toBe(true);

      // 2. Disconnect path
      await toggleMonitorMqttConnection();
      expect(mockInvoke).toHaveBeenCalledWith('disconnect_mqtt_broker');
      expect(monitorMqttConnected.value).toBe(false);
    });

    it('starts local MQTT broker', async () => {
      const mockInvoke = vi.fn().mockResolvedValue(['192.168.1.10']);
      const { localBrokerRunning, localBrokerLans, startLocalMqttBroker } = useMqttConnection(defaultOptions(mockInvoke));

      await startLocalMqttBroker();
      expect(mockInvoke).toHaveBeenCalledWith('start_local_mqtt_broker', { port: 1883 });
      expect(localBrokerRunning.value).toBe(true);
      expect(localBrokerLans.value).toEqual(['192.168.1.10']);
    });
  });
});
