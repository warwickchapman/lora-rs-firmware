import { ref, getCurrentInstance, onUnmounted } from 'vue';
import { invoke } from '@tauri-apps/api/core';

export interface FirmwareServerInfo {
  filename: string;
  sha256: string;
  size_bytes: number;
  port: number;
  urls: string[];
}

export interface FirmwareServerOptions {
  firmware_path: string;
  region: string | null;
}

export interface NetworkInterface {
  ip: string;
  netmask: string;
}

export interface UseFirmwareServerOptions {
  resolveFirmwareOptions: () => FirmwareServerOptions | null;
  idleShutdownMs?: number;
  pushNetworkLog?: (msg: string) => void;
  notify?: (msg: string) => void;
}

export function useFirmwareServer(options: UseFirmwareServerOptions) {
  const isFirmwareServerStarting = ref(false);
  const firmwareServerInfo = ref<FirmwareServerInfo | null>(null);
  const firmwareServerRevalidatePending = ref(false);
  const idleShutdownMs = options.idleShutdownMs ?? 60000;
  let firmwareServerBusy = false;
  let idleShutdownTimer: ReturnType<typeof setTimeout> | null = null;

  function log(msg: string) {
    options.pushNetworkLog?.(msg);
  }

  function showNotification(msg: string) {
    options.notify?.(msg);
  }

  function cancelIdleShutdown() {
    if (!idleShutdownTimer) return;
    clearTimeout(idleShutdownTimer);
    idleShutdownTimer = null;
  }

  function scheduleIdleShutdown() {
    cancelIdleShutdown();
    if (firmwareServerBusy || !firmwareServerInfo.value) return;
    idleShutdownTimer = setTimeout(() => {
      idleShutdownTimer = null;
      if (firmwareServerBusy || !firmwareServerInfo.value) return;
      log('Firmware server idle for 60 seconds; stopping.');
      stopFirmwareServer().catch(e => log(`Firmware server idle stop error: ${e}`));
    }, idleShutdownMs);
  }

  function setFirmwareServerBusy(busy: boolean) {
    firmwareServerBusy = busy;
    if (busy) cancelIdleShutdown();
    else scheduleIdleShutdown();
  }

  async function startFirmwareServerWithOptions(fwOptions: FirmwareServerOptions | null) {
    if (!fwOptions) return;
    isFirmwareServerStarting.value = true;
    log('--- Firmware file server ---');
    try {
      const info = await invoke<FirmwareServerInfo>('start_firmware_file_server', {
        options: fwOptions
      });
      firmwareServerInfo.value = info;
      const statusMsg = `Serving ${info.filename} on ${info.urls[0] || `port ${info.port}`}.`;
      log(`${statusMsg} SHA256 ${info.sha256}`);
      scheduleIdleShutdown();
    } catch (e) {
      log(`Firmware server failed: ${e}`);
      showNotification(`Firmware server failed: ${e}`);
    } finally {
      isFirmwareServerStarting.value = false;
    }
  }

  async function ensureFirmwareServer(): Promise<FirmwareServerInfo> {
    cancelIdleShutdown();
    if (firmwareServerInfo.value) return firmwareServerInfo.value;
    const fwOptions = options.resolveFirmwareOptions();
    if (!fwOptions) throw new Error('Choose a firmware file or release first');
    await startFirmwareServerWithOptions(fwOptions);
    if (!firmwareServerInfo.value) throw new Error('Firmware server did not start');
    return firmwareServerInfo.value;
  }

  async function stopFirmwareServer() {
    cancelIdleShutdown();
    try {
      const stopped = await invoke<string>('stop_firmware_file_server');
      log(stopped);
    } catch (e) {
      log(`Firmware server stop error: ${e}`);
    } finally {
      firmwareServerInfo.value = null;
    }
  }

  function firmwareServerTarget(info: FirmwareServerInfo): { host: string; port: number } {
    const raw = info.urls.find(u => !u.includes('127.0.0.1')) || info.urls[0] || '';
    if (!raw) throw new Error('Firmware server has no reachable URL');
    const parsed = new URL(raw);
    return {
      host: parsed.hostname,
      port: Number(parsed.port || info.port)
    };
  }

  async function handleNetworkInterfacesChanged(nextInterfaces: NetworkInterface[], busy: boolean) {
    if (!firmwareServerInfo.value) return;

    if (busy) {
      log('Host network interfaces changed, but deferring firmware server validation/restart until active flash/upgrade completes.');
      firmwareServerRevalidatePending.value = true;
      return;
    }

    await revalidateUrls(nextInterfaces);
  }

  async function revalidateFirmwareServerAfterNetworkChange() {
    if (!firmwareServerRevalidatePending.value) return;
    try {
      const nextInterfaces = await invoke<NetworkInterface[]>('get_network_interfaces');
      await revalidateUrls(nextInterfaces);
      firmwareServerRevalidatePending.value = false;
    } catch (e) {
      log(`Failed to revalidate firmware server: ${e}`);
    }
  }

  async function revalidateUrls(nextInterfaces: NetworkInterface[]) {
    if (!firmwareServerInfo.value) return;
    const currentUrls = firmwareServerInfo.value.urls;
    const stillValid = currentUrls.some(url => {
      try {
        const parsed = new URL(url);
        return nextInterfaces.some(i => i.ip === parsed.hostname);
      } catch (_) {
        return false;
      }
    });

    if (!stillValid) {
      log('Firmware server is no longer reachable on this network. Restarting...');
      await stopFirmwareServer();
      const fwOptions = options.resolveFirmwareOptions();
      if (fwOptions) {
        await startFirmwareServerWithOptions(fwOptions);
      }
    }
  }

  async function cleanupFirmwareServer() {
    cancelIdleShutdown();
    if (firmwareServerInfo.value) {
      await stopFirmwareServer();
    }
  }

  if (getCurrentInstance()) {
    onUnmounted(() => {
      cleanupFirmwareServer().catch(() => {});
    });
  }

  return {
    isFirmwareServerStarting,
    firmwareServerInfo,
    firmwareServerRevalidatePending,
    ensureFirmwareServer,
    stopFirmwareServer,
    setFirmwareServerBusy,
    firmwareServerTarget,
    handleNetworkInterfacesChanged,
    revalidateFirmwareServerAfterNetworkChange,
    cleanupFirmwareServer
  };
}
