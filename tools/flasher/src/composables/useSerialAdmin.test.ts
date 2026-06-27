import { describe, it, expect, beforeEach } from 'vitest';
import { useSerialAdmin } from './useSerialAdmin';

describe('useSerialAdmin', () => {
  let commandCalls: { port: string; cmd: string; payload: any; timeoutMs: number }[] = [];
  let beforeCalls: { port: string; cmd: string }[] = [];

  beforeEach(() => {
    commandCalls = [];
    beforeCalls = [];
  });

  const createAdmin = () => {
    return useSerialAdmin({
      invokeSerialAdminCommand: async (port, cmd, payload, timeoutMs) => {
        commandCalls.push({ port, cmd, payload, timeoutMs });
        return { ok: true };
      },
      onBeforeSerialCommand: (port, cmd) => {
        beforeCalls.push({ port, cmd });
      }
    });
  };

  it('serial jobs for the same port run sequentially', async () => {
    const admin = createAdmin();
    let resolve1!: (val: any) => void;
    const job1Promise = new Promise(resolve => {
      resolve1 = resolve;
    });

    const executionOrder: string[] = [];

    const p1 = admin.runSerialAdminJob('COM1', 'job1', {}, async () => {
      executionOrder.push('start1');
      await job1Promise;
      executionOrder.push('end1');
      return 1;
    });

    const p2 = admin.runSerialAdminJob('COM1', 'job2', {}, async () => {
      executionOrder.push('start2');
      return 2;
    });

    // Let microtasks flush
    await Promise.resolve();
    expect(executionOrder).toEqual(['start1']);

    resolve1(null);
    await p1;
    await p2;

    expect(executionOrder).toEqual(['start1', 'end1', 'start2']);
  });

  it('jobs for different ports can proceed independently', async () => {
    const admin = createAdmin();
    let resolve1!: (val: any) => void;
    const job1Promise = new Promise(resolve => {
      resolve1 = resolve;
    });

    const executionOrder: string[] = [];

    const p1 = admin.runSerialAdminJob('COM1', 'job1', {}, async () => {
      executionOrder.push('start1');
      await job1Promise;
      executionOrder.push('end1');
      return 1;
    });

    const p2 = admin.runSerialAdminJob('COM2', 'job2', {}, async () => {
      executionOrder.push('start2');
      return 2;
    });

    await Promise.resolve();
    // Since COM2 is a different port, job2 should start immediately without waiting for job1
    expect(executionOrder).toEqual(['start1', 'start2']);

    resolve1(null);
    await Promise.all([p1, p2]);
  });

  it('dropIfBusy rejects with serial_admin_background_skipped', async () => {
    const admin = createAdmin();
    let resolve1!: (val: any) => void;
    const job1Promise = new Promise(resolve => {
      resolve1 = resolve;
    });

    const p1 = admin.runSerialAdminJob('COM1', 'job1', {}, async () => {
      await job1Promise;
      return 1;
    });

    // Since COM1 is busy running job1, running a dropIfBusy job should reject immediately
    const p2 = admin.runSerialAdminJob('COM1', 'job2', { dropIfBusy: true }, async () => {
      return 2;
    });

    await expect(p2).rejects.toThrow('serial_admin_background_skipped');
    expect(admin.serialBackgroundSkipped('serial_admin_background_skipped')).toBe(true);

    resolve1(null);
    await p1;
  });

  it('busy label is set during the job and cleared afterward', async () => {
    const admin = createAdmin();
    let resolve1!: (val: any) => void;
    const job1Promise = new Promise(resolve => {
      resolve1 = resolve;
    });

    expect(admin.serialAdminBusyForPort('COM1')).toBe(false);

    const p1 = admin.runSerialAdminJob('COM1', 'flashing', {}, async () => {
      await job1Promise;
      return 1;
    });

    await Promise.resolve();
    expect(admin.serialAdminBusyForPort('COM1')).toBe(true);
    expect(admin.serialAdminPortBusy.value['COM1']).toBe('flashing');

    resolve1(null);
    await p1;

    expect(admin.serialAdminBusyForPort('COM1')).toBe(false);
    expect(admin.serialAdminPortBusy.value['COM1']).toBeUndefined();
  });

  it('failed jobs still clear busy state and release the queue', async () => {
    const admin = createAdmin();
    const p1 = admin.runSerialAdminJob('COM1', 'failing-job', {}, async () => {
      throw new Error('job failed');
    });

    await expect(p1).rejects.toThrow('job failed');
    expect(admin.serialAdminBusyForPort('COM1')).toBe(false);
    expect(admin.serialAdminPortBusy.value['COM1']).toBeUndefined();

    // Verify next job can run
    const p2 = admin.runSerialAdminJob('COM1', 'next-job', {}, async () => {
      return 'ok';
    });
    expect(await p2).toBe('ok');
  });

  it('runSerialAdminCommand calls onBeforeSerialCommand before invokeSerialAdminCommand', async () => {
    let executionTrace: string[] = [];
    const testAdmin = useSerialAdmin({
      invokeSerialAdminCommand: async () => {
        executionTrace.push('invoke');
        return { ok: true };
      },
      onBeforeSerialCommand: () => {
        executionTrace.push('before');
      }
    });

    await testAdmin.runSerialAdminCommand('COM1', 'test_cmd');
    expect(executionTrace).toEqual(['before', 'invoke']);
  });
});
