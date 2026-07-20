import { Ref } from 'vue';
import { invoke } from '@tauri-apps/api/core';

export interface PendingMqttRequest {
  resolve: (value: any) => void;
  reject: (err: any) => void;
  timer: ReturnType<typeof setTimeout>;
}

export interface MqttGatewaySession {
  sessionId: number | null;
  nextSeq: number;
  promiseChain: Promise<any>;
}

export interface UseMqttAdminOptions {
  monitorMqttConnected: Ref<boolean>;
  monitorMqttTopicRoot: Ref<string>;
  adminPasswordForPort: (port: string) => string;
  normalizeChipId: (raw: string | undefined | null) => string;
  invokeMqttPublish?: (topic: string, payload: string) => Promise<void>;
}

// PubSubClient's 1024-byte receive buffer also holds the MQTT topic/header.
const MAX_MQTT_ADMIN_COMMAND_BYTES = 900;

export function useMqttAdmin(options: UseMqttAdminOptions) {
  const pendingMqttRequests = new Map<string, PendingMqttRequest>();
  const mqttGatewaySessions = new Map<string, MqttGatewaySession>();

  function getOrCreateMqttSession(chipId: string): MqttGatewaySession {
    const norm = options.normalizeChipId(chipId);
    let sess = mqttGatewaySessions.get(norm);
    if (!sess) {
      sess = {
        sessionId: null,
        nextSeq: 1,
        promiseChain: Promise.resolve()
      };
      mqttGatewaySessions.set(norm, sess);
    }
    return sess;
  }

  async function executeMqttCommandRaw<T = any>(
    chipId: string,
    cmd: string,
    payload: Record<string, any> = {},
    timeoutMs = 8000
  ): Promise<T> {
    const normalizedChipId = options.normalizeChipId(chipId);
    const reqId = `req-${Math.random().toString(36).substring(2, 11)}`;
    const topic = `${options.monitorMqttTopicRoot.value}/lrs-${normalizedChipId}/admin_command`;
    const requestPayload = {
      id: reqId,
      cmd,
      ...payload
    };
    const serializedPayload = JSON.stringify(requestPayload);
    if (new TextEncoder().encode(serializedPayload).length > MAX_MQTT_ADMIN_COMMAND_BYTES) {
      throw new Error('MQTT admin command is too large.');
    }

    return new Promise<T>(async (resolve, reject) => {
      const timer = setTimeout(() => {
        pendingMqttRequests.delete(reqId);
        reject(new Error(`MQTT command timeout (${timeoutMs}ms)`));
      }, timeoutMs);

      pendingMqttRequests.set(reqId, { resolve, reject, timer });

      try {
        if (options.invokeMqttPublish) {
          await options.invokeMqttPublish(topic, serializedPayload);
        } else {
          await invoke('publish_mqtt_command', {
            topic,
            payload: serializedPayload
          });
        }
      } catch (e) {
        clearTimeout(timer);
        pendingMqttRequests.delete(reqId);
        reject(e);
      }
    });
  }

  async function refreshMqttSession(chipId: string): Promise<void> {
    const norm = options.normalizeChipId(chipId);
    const sess = getOrCreateMqttSession(norm);
    const res = await executeMqttCommandRaw<{ session_id: number; expires_in_ms: number }>(
      norm,
      'admin_challenge',
      {},
      5000
    );
    if (res && res.session_id !== undefined) {
      sess.sessionId = res.session_id;
      sess.nextSeq = 1;
    } else {
      throw new Error('Failed to establish MQTT admin session');
    }
  }

  async function sendMqttAdminCommand<T = any>(
    chipId: string,
    cmd: string,
    payload: Record<string, any> = {},
    timeoutMs = 8000
  ): Promise<T> {
    if (!options.monitorMqttConnected.value) {
      throw new Error('MQTT broker is not connected. Connect in the Monitor tab first.');
    }

    const normalizedChipId = options.normalizeChipId(chipId);

    if (cmd === 'admin_challenge') {
      const sess = getOrCreateMqttSession(normalizedChipId);
      const p = sess.promiseChain.then(async () => {
        return await executeMqttCommandRaw<T>(normalizedChipId, cmd, payload, timeoutMs);
      });
      sess.promiseChain = p.catch(() => {});
      return p;
    }

    const envelopePayload = { ...payload };
    if (envelopePayload.admin_password == null && envelopePayload.password == null) {
      let password = options.adminPasswordForPort(normalizedChipId);
      if (!password) {
        password = options.adminPasswordForPort(`lrs-${normalizedChipId}`);
      }
      if (!password) {
        throw new Error('Enter the gateway admin password');
      }
      envelopePayload.admin_password = password;
    }

    const sess = getOrCreateMqttSession(normalizedChipId);
    const p = sess.promiseChain.then(async () => {
      if (sess.sessionId === null) {
        await refreshMqttSession(normalizedChipId);
      }

      const cmdPayload = {
        ...envelopePayload,
        session_id: sess.sessionId,
        seq: sess.nextSeq++
      };

      try {
        return await executeMqttCommandRaw<T>(normalizedChipId, cmd, cmdPayload, timeoutMs);
      } catch (err: any) {
        const errMsg = err?.message || '';
        if (errMsg.includes('admin_session_expired') || errMsg.includes('admin_session_invalid')) {
          console.warn(`MQTT session for ${normalizedChipId} expired/invalid. Refreshing and retrying...`);
          await refreshMqttSession(normalizedChipId);
          const retryPayload = {
            ...envelopePayload,
            session_id: sess.sessionId,
            seq: sess.nextSeq++
          };
          return await executeMqttCommandRaw<T>(normalizedChipId, cmd, retryPayload, timeoutMs);
        }
        throw err;
      }
    });

    sess.promiseChain = p.catch(() => {});
    return p;
  }

  function handleMqttAdminResponse(payload: any) {
    const res = payload?.response;
    const reqId = res?.id;
    if (reqId) {
      const pending = pendingMqttRequests.get(reqId);
      if (pending) {
        clearTimeout(pending.timer);
        pendingMqttRequests.delete(reqId);
        if (res.ok === false) {
          pending.reject(new Error(res.error || 'command_failed'));
        } else {
          pending.resolve(res);
        }
      }
    }
  }

  return {
    pendingMqttRequests,
    mqttGatewaySessions,
    getOrCreateMqttSession,
    executeMqttCommandRaw,
    refreshMqttSession,
    sendMqttAdminCommand,
    handleMqttAdminResponse
  };
}
