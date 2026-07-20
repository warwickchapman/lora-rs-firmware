import { describe, it, expect, vi, beforeEach } from 'vitest';
import { ref } from 'vue';
import { useMqttAdmin } from './useMqttAdmin';

describe('useMqttAdmin', () => {
  let monitorMqttConnected = ref(true);
  let monitorMqttTopicRoot = ref('lora');
  let passwords: Record<string, string> = {};
  let publishCalls: { topic: string; payload: any }[] = [];

  const adminPasswordForPort = (port: string) => passwords[port] || 'default-pass';
  const normalizeChipId = (raw: string | undefined | null) => String(raw || '').trim().replace(/^0x/i, '').replace(/[^0-9a-f]/gi, '').toLowerCase();

  beforeEach(() => {
    monitorMqttConnected.value = true;
    monitorMqttTopicRoot.value = 'lora';
    passwords = { '123456': 'secret-pass' };
    publishCalls = [];
    vi.useFakeTimers();
  });

  const createAdmin = (invokeMqttPublish?: (topic: string, payload: string) => Promise<void>) => {
    return useMqttAdmin({
      monitorMqttConnected,
      monitorMqttTopicRoot,
      adminPasswordForPort,
      normalizeChipId,
      invokeMqttPublish: invokeMqttPublish || (async (topic, payload) => {
        publishCalls.push({ topic, payload: JSON.parse(payload) });
      })
    });
  };

  it('successful command response resolves from response.id', async () => {
    const admin = createAdmin();
    // Establish session first
    const sess = admin.getOrCreateMqttSession('123456');
    sess.sessionId = 42;

    const promise = admin.sendMqttAdminCommand('123456', 'get_status');

    // Wait for the command to be sent (which publishes the request)
    await vi.advanceTimersByTimeAsync(10);

    expect(publishCalls.length).toBe(1);
    const sentReqId = publishCalls[0].payload.id;
    expect(admin.pendingMqttRequests.has(sentReqId)).toBe(true);

    // Simulate response arrival
    admin.handleMqttAdminResponse({
      response: {
        id: sentReqId,
        ok: true,
        status: 'online'
      }
    });

    const res = await promise;
    expect(res.status).toBe('online');
    expect(admin.pendingMqttRequests.has(sentReqId)).toBe(false);
  });

  it('timeout rejects and removes the pending request', async () => {
    const admin = createAdmin();
    const sess = admin.getOrCreateMqttSession('123456');
    sess.sessionId = 42;

    const promise = admin.sendMqttAdminCommand('123456', 'get_status', {}, 5000);

    await vi.advanceTimersByTimeAsync(10);
    expect(publishCalls.length).toBe(1);
    const sentReqId = publishCalls[0].payload.id;

    // Set up expectation first, then advance timers, then await expectation
    const testPromise = expect(promise).rejects.toThrow('MQTT command timeout (5000ms)');
    await vi.advanceTimersByTimeAsync(5000);
    await testPromise;
    expect(admin.pendingMqttRequests.has(sentReqId)).toBe(false);
  });

  it('rejects an oversized MQTT command before publishing', async () => {
    const admin = createAdmin();
    const sess = admin.getOrCreateMqttSession('123456');
    sess.sessionId = 42;

    await expect(admin.sendMqttAdminCommand('123456', 'set_config', {
      config: { value: 'x'.repeat(1000) }
    })).rejects.toThrow('MQTT admin command is too large');
    expect(publishCalls).toHaveLength(0);
  });

  it('expired/invalid session refreshes and retries once', async () => {
    let mockPublishCalls: any[] = [];
    const admin = useMqttAdmin({
      monitorMqttConnected,
      monitorMqttTopicRoot,
      adminPasswordForPort,
      normalizeChipId,
      invokeMqttPublish: async (_topic, payload) => {
        const parsed = JSON.parse(payload);
        mockPublishCalls.push(parsed);

        // Auto-respond to the request
        setTimeout(() => {
          if (parsed.cmd === 'admin_challenge') {
            admin.handleMqttAdminResponse({
              response: { id: parsed.id, ok: true, session_id: 999 }
            });
          } else if (parsed.cmd === 'get_status') {
            if (parsed.session_id === 42) {
              // Expired session error
              admin.handleMqttAdminResponse({
                response: { id: parsed.id, ok: false, error: 'admin_session_expired' }
              });
            } else if (parsed.session_id === 999) {
              // Retry success
              admin.handleMqttAdminResponse({
                response: { id: parsed.id, ok: true, status: 'recovered' }
              });
            }
          }
        }, 10);
      }
    });

    const sess = admin.getOrCreateMqttSession('123456');
    sess.sessionId = 42; // set stale session id

    const promise = admin.sendMqttAdminCommand('123456', 'get_status');

    await vi.advanceTimersByTimeAsync(100);

    const res = await promise;
    expect(res.status).toBe('recovered');
    expect(sess.sessionId).toBe(999);
    // Should have published: 1. original cmd (session_id=42), 2. challenge, 3. retried cmd (session_id=999)
    expect(mockPublishCalls.map(c => c.cmd)).toEqual(['get_status', 'admin_challenge', 'get_status']);
    expect(mockPublishCalls[2].session_id).toBe(999);
  });

  it('two queued commands for the same gateway where the first expires the session, then refreshes/retries, and both complete without leaving pending requests stuck', async () => {
    let mockPublishCalls: any[] = [];
    const admin = useMqttAdmin({
      monitorMqttConnected,
      monitorMqttTopicRoot,
      adminPasswordForPort,
      normalizeChipId,
      invokeMqttPublish: async (_topic, payload) => {
        const parsed = JSON.parse(payload);
        mockPublishCalls.push(parsed);

        setTimeout(() => {
          if (parsed.cmd === 'admin_challenge') {
            admin.handleMqttAdminResponse({
              response: { id: parsed.id, ok: true, session_id: 100 }
            });
          } else if (parsed.cmd === 'first_cmd') {
            if (parsed.session_id === 42) {
              admin.handleMqttAdminResponse({
                response: { id: parsed.id, ok: false, error: 'admin_session_expired' }
              });
            } else if (parsed.session_id === 100) {
              admin.handleMqttAdminResponse({
                response: { id: parsed.id, ok: true, val: 1 }
              });
            }
          } else if (parsed.cmd === 'second_cmd') {
            admin.handleMqttAdminResponse({
              response: { id: parsed.id, ok: true, val: 2 }
            });
          }
        }, 10);
      }
    });

    const sess = admin.getOrCreateMqttSession('123456');
    sess.sessionId = 42;

    const promise1 = admin.sendMqttAdminCommand('123456', 'first_cmd');
    const promise2 = admin.sendMqttAdminCommand('123456', 'second_cmd');

    await vi.advanceTimersByTimeAsync(200);

    const [res1, res2] = await Promise.all([promise1, promise2]);
    expect(res1.val).toBe(1);
    expect(res2.val).toBe(2);
    expect(admin.pendingMqttRequests.size).toBe(0);
    expect(mockPublishCalls.map(c => c.cmd)).toEqual(['first_cmd', 'admin_challenge', 'first_cmd', 'second_cmd']);
  });
});
