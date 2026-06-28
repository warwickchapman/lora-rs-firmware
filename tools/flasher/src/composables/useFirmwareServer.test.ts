import { describe, it, expect, vi, beforeEach } from 'vitest';
import { useFirmwareServer, FirmwareServerInfo, FirmwareServerOptions } from './useFirmwareServer';
import { invoke } from '@tauri-apps/api/core';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}));

describe('useFirmwareServer', () => {
  const resolveFirmwareOptionsMock = vi.fn<() => FirmwareServerOptions | null>();
  const onStartingMock = vi.fn();
  const onStartedMock = vi.fn();
  const onStoppedMock = vi.fn();
  const pushNetworkLogMock = vi.fn();
  const notifyMock = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
  });

  const createComposable = () => {
    return useFirmwareServer({
      resolveFirmwareOptions: resolveFirmwareOptionsMock,
      onStarting: onStartingMock,
      onStarted: onStartedMock,
      onStopped: onStoppedMock,
      pushNetworkLog: pushNetworkLogMock,
      notify: notifyMock,
    });
  };

  it('startFirmwareServerWithOptions(null) is a no-op and does not call Tauri', async () => {
    const server = createComposable();
    await server.startFirmwareServerWithOptions(null);
    expect(invoke).not.toHaveBeenCalled();
    expect(server.isFirmwareServerStarting.value).toBe(false);
  });

  it('successful start stores firmwareServerInfo, clears starting flag, calls onStarting and onStarted, and logs', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'abc123sha',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/firmware.bin'],
    };
    vi.mocked(invoke).mockResolvedValue(mockInfo);

    const options: FirmwareServerOptions = { firmware_path: '/path/to/fw.bin', region: 'ZA' };
    await server.startFirmwareServerWithOptions(options);

    expect(server.isFirmwareServerStarting.value).toBe(false);
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);
    expect(onStartingMock).toHaveBeenCalledTimes(1);
    expect(onStartedMock).toHaveBeenCalledWith(mockInfo, 'Serving firmware.bin on http://192.168.1.10:8080/firmware.bin.');
    expect(pushNetworkLogMock).toHaveBeenCalledWith('--- Firmware file server ---');
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Serving firmware.bin on http://192.168.1.10:8080/firmware.bin. SHA256 abc123sha');
  });

  it('failed start clears starting flag and logs/notifies exact failure message', async () => {
    const server = createComposable();
    vi.mocked(invoke).mockRejectedValue(new Error('Port already in use'));

    const options: FirmwareServerOptions = { firmware_path: '/path/to/fw.bin', region: 'ZA' };
    await server.startFirmwareServerWithOptions(options);

    expect(server.isFirmwareServerStarting.value).toBe(false);
    expect(server.firmwareServerInfo.value).toBeNull();
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server failed: Error: Port already in use');
    expect(notifyMock).toHaveBeenCalledWith('Firmware server failed: Error: Port already in use');
  });

  it('ensureFirmwareServer returns existing info without restarting', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'abc123sha',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/firmware.bin'],
    };
    vi.mocked(invoke).mockResolvedValue(mockInfo);

    // Initial start
    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });
    expect(invoke).toHaveBeenCalledTimes(1);

    vi.clearAllMocks();
    const info = await server.ensureFirmwareServer();
    expect(info).toEqual(mockInfo);
    expect(invoke).not.toHaveBeenCalled();
  });

  it('ensureFirmwareServer starts when missing and errors if no options are available', async () => {
    const server = createComposable();
    resolveFirmwareOptionsMock.mockReturnValue(null);

    await expect(server.ensureFirmwareServer()).rejects.toThrow('Choose a firmware file or release first');
  });

  it('firmwareServerTarget chooses the first non-127.0.0.1 URL', () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://127.0.0.1:8080/fw.bin', 'http://192.168.1.10:8080/fw.bin'],
    };

    const target = server.firmwareServerTarget(mockInfo);
    expect(target).toEqual({ host: '192.168.1.10', port: 8080 });
  });

  it('firmwareServerTarget falls back to the first URL when needed', () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://127.0.0.1:8080/fw.bin'],
    };

    const target = server.firmwareServerTarget(mockInfo);
    expect(target).toEqual({ host: '127.0.0.1', port: 8080 });
  });

  it('firmwareServerTarget throws when no urls exist', () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: [],
    };

    expect(() => server.firmwareServerTarget(mockInfo)).toThrow('Firmware server has no reachable URL');
  });

  it('interface-change handling defers restart while gateway OTA/flash is busy', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/fw.bin'],
    };
    vi.mocked(invoke).mockResolvedValue(mockInfo);

    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });

    await server.handleNetworkInterfacesChanged([{ ip: '192.168.1.20', netmask: '255.255.255.0' }], true);

    expect(server.firmwareServerRevalidatePending.value).toBe(true);
    expect(pushNetworkLogMock).toHaveBeenCalledWith(
      expect.stringContaining('Host network interfaces changed, but deferring firmware server validation/restart')
    );
  });

  it('deferred revalidation runs when the phase returns to idle', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/fw.bin'],
    };
    vi.mocked(invoke).mockImplementation(async (cmd) => {
      if (cmd === 'start_firmware_file_server') return mockInfo;
      if (cmd === 'stop_firmware_file_server') return 'Server stopped';
      if (cmd === 'get_network_interfaces') {
        return [{ ip: '192.168.1.20', netmask: '255.255.255.0' }]; // new interface
      }
      return null;
    });

    resolveFirmwareOptionsMock.mockReturnValue({ firmware_path: '/path', region: null });

    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });
    
    // Set pending manually or trigger it through busy handler
    await server.handleNetworkInterfacesChanged([{ ip: '192.168.1.20', netmask: '255.255.255.0' }], true);
    expect(server.firmwareServerRevalidatePending.value).toBe(true);

    vi.clearAllMocks();
    await server.revalidateFirmwareServerAfterNetworkChange();

    expect(server.firmwareServerRevalidatePending.value).toBe(false);
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server is no longer reachable on this network. Restarting...');
    expect(onStoppedMock).toHaveBeenCalledWith('Firmware server stopped.');
  });

  it('revalidation restarts the server when current URLs no longer match host interfaces', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/fw.bin'],
    };
    vi.mocked(invoke).mockImplementation(async (cmd) => {
      if (cmd === 'start_firmware_file_server') return mockInfo;
      if (cmd === 'stop_firmware_file_server') return 'Server stopped';
      return null;
    });

    resolveFirmwareOptionsMock.mockReturnValue({ firmware_path: '/path', region: null });
    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });

    // Handle change with busy = false (immediate revalidation)
    vi.clearAllMocks();
    await server.handleNetworkInterfacesChanged([{ ip: '192.168.1.20', netmask: '255.255.255.0' }], false);

    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server is no longer reachable on this network. Restarting...');
    expect(onStoppedMock).toHaveBeenCalledWith('Firmware server stopped.');
  });

  it('stopFirmwareServer clears server info and logs correct stop messages', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/fw.bin'],
    };
    vi.mocked(invoke).mockImplementation(async (cmd) => {
      if (cmd === 'start_firmware_file_server') return mockInfo;
      if (cmd === 'stop_firmware_file_server') return 'Server stopped successfully';
      return null;
    });

    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);

    vi.clearAllMocks();
    await server.stopFirmwareServer();

    expect(server.firmwareServerInfo.value).toBeNull();
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Server stopped successfully');
    expect(onStoppedMock).toHaveBeenCalledWith('Firmware server stopped.');
  });

  it('cleanupFirmwareServer stops the server if running', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'hash',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/fw.bin'],
    };
    vi.mocked(invoke).mockImplementation(async (cmd) => {
      if (cmd === 'start_firmware_file_server') return mockInfo;
      if (cmd === 'stop_firmware_file_server') return 'Server stopped';
      return null;
    });

    await server.startFirmwareServerWithOptions({ firmware_path: '/path', region: null });
    expect(server.firmwareServerInfo.value).not.toBeNull();

    vi.clearAllMocks();
    await server.cleanupFirmwareServer();

    expect(server.firmwareServerInfo.value).toBeNull();
    expect(invoke).toHaveBeenCalledWith('stop_firmware_file_server');
  });
});
