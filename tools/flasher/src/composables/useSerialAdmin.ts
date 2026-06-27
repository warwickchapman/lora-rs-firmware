import { ref } from 'vue';

export type SerialJobPriority = 'user' | 'background';

export interface SerialJobOptions {
  label?: string;
  priority?: SerialJobPriority;
  dropIfBusy?: boolean;
}

export interface UseSerialAdminOptions {
  invokeSerialAdminCommand: (port: string, cmd: string, payload: Record<string, any>, timeoutMs: number) => Promise<any>;
  onBeforeSerialCommand: (port: string, cmd: string) => void;
}

export function useSerialAdmin(options: UseSerialAdminOptions) {
  const serialAdminPortBusy = ref<Record<string, string>>({});
  const serialAdminPortQueues = new Map<string, Promise<void>>();

  function serialAdminBusyForPort(port: string): boolean {
    return !!serialAdminPortBusy.value[port] || serialAdminPortQueues.has(port);
  }

  function serialBackgroundSkipped(err: unknown): boolean {
    return String(err || '').includes('serial_admin_background_skipped');
  }

  async function runSerialAdminJob<T>(port: string, label: string, jobOptions: SerialJobOptions, job: () => Promise<T>): Promise<T> {
    if (jobOptions.dropIfBusy && serialAdminBusyForPort(port)) {
      throw new Error('serial_admin_background_skipped');
    }

    const previous = serialAdminPortQueues.get(port) || Promise.resolve();
    let releaseQueue!: () => void;
    const current = new Promise<void>(resolve => {
      releaseQueue = resolve;
    });
    const queued = previous.then(() => current);
    serialAdminPortQueues.set(port, queued);

    try {
      await previous;
      serialAdminPortBusy.value = { ...serialAdminPortBusy.value, [port]: label };
      return await job();
    } finally {
      const nextBusy = { ...serialAdminPortBusy.value };
      if (nextBusy[port] === label) delete nextBusy[port];
      serialAdminPortBusy.value = nextBusy;
      releaseQueue();
      if (serialAdminPortQueues.get(port) === queued) {
        serialAdminPortQueues.delete(port);
      }
    }
  }

  async function runSerialAdminCommand<T = any>(
    port: string,
    cmd: string,
    payload: Record<string, any> = {},
    timeoutMs = 8000,
    jobOptions: SerialJobOptions = {}
  ): Promise<T> {
    const label = jobOptions.label || cmd.replace(/_/g, ' ');
    return await runSerialAdminJob<T>(port, label, jobOptions, async () => {
      options.onBeforeSerialCommand(port, cmd);
      return await options.invokeSerialAdminCommand(port, cmd, payload, timeoutMs);
    });
  }

  return {
    serialAdminPortBusy,
    serialAdminPortQueues,
    serialAdminBusyForPort,
    serialBackgroundSkipped,
    runSerialAdminJob,
    runSerialAdminCommand
  };
}
