import { describe, it, expect, vi, beforeEach } from 'vitest';
import { useFirmwareServer, FirmwareServerInfo, FirmwareServerOptions } from './useFirmwareServer';
import { invoke } from '@tauri-apps/api/core';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}));

describe('useFirmwareServer', () => {
  const resolveFirmwareOptionsMock = vi.fn<() => FirmwareServerOptions | null>();
  const pushNetworkLogMock = vi.fn();
  const notifyMock = vi.fn();

  beforeEach(() => {
    vi.useRealTimers();
    vi.clearAllMocks();
    resolveFirmwareOptionsMock.mockReturnValue({ firmware_path: '/path/to/fw.bin', profile: '433_za' });
  });

  const createComposable = () => {
    return useFirmwareServer({
      resolveFirmwareOptions: resolveFirmwareOptionsMock,
      pushNetworkLog: pushNetworkLogMock,
      notify: notifyMock,
    });
  };

  it('ensure starts the server, stores its details, and logs the served artifact', async () => {
    const server = createComposable();
    const mockInfo: FirmwareServerInfo = {
      filename: 'firmware.bin',
      sha256: 'abc123sha',
      size_bytes: 1024,
      port: 8080,
      urls: ['http://192.168.1.10:8080/firmware.bin'],
    };
    vi.mocked(invoke).mockResolvedValue(mockInfo);

    await server.ensureFirmwareServer();

    expect(server.isFirmwareServerStarting.value).toBe(false);
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);
    expect(pushNetworkLogMock).toHaveBeenCalledWith('--- Firmware file server ---');
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Serving firmware.bin on http://192.168.1.10:8080/firmware.bin. SHA256 abc123sha');
  });

  it('failed start clears starting flag and logs/notifies exact failure message', async () => {
    const server = createComposable();
    vi.mocked(invoke).mockRejectedValue(new Error('Port already in use'));

    await expect(server.ensureFirmwareServer()).rejects.toThrow('Firmware server did not start');

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
    await server.ensureFirmwareServer();
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

    await server.ensureFirmwareServer();

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

    resolveFirmwareOptionsMock.mockReturnValue({ firmware_path: '/path', profile: null });

    await server.ensureFirmwareServer();
    
    // Set pending manually or trigger it through busy handler
    await server.handleNetworkInterfacesChanged([{ ip: '192.168.1.20', netmask: '255.255.255.0' }], true);
    expect(server.firmwareServerRevalidatePending.value).toBe(true);

    vi.clearAllMocks();
    await server.revalidateFirmwareServerAfterNetworkChange();

    expect(server.firmwareServerRevalidatePending.value).toBe(false);
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server is no longer reachable on this network. Restarting...');
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

    resolveFirmwareOptionsMock.mockReturnValue({ firmware_path: '/path', profile: null });
    await server.ensureFirmwareServer();

    // Handle change with busy = false (immediate revalidation)
    vi.clearAllMocks();
    await server.handleNetworkInterfacesChanged([{ ip: '192.168.1.20', netmask: '255.255.255.0' }], false);

    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server is no longer reachable on this network. Restarting...');
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

    await server.ensureFirmwareServer();
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);

    vi.clearAllMocks();
    await server.stopFirmwareServer();

    expect(server.firmwareServerInfo.value).toBeNull();
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Server stopped successfully');
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

    await server.ensureFirmwareServer();
    expect(server.firmwareServerInfo.value).not.toBeNull();

    vi.clearAllMocks();
    await server.cleanupFirmwareServer();

    expect(server.firmwareServerInfo.value).toBeNull();
    expect(invoke).toHaveBeenCalledWith('stop_firmware_file_server');
  });

  it('stops 60 seconds after OTA activity becomes idle', async () => {
    vi.useFakeTimers();
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

    server.setFirmwareServerBusy(true);
    await server.ensureFirmwareServer();
    await vi.advanceTimersByTimeAsync(60000);
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);

    server.setFirmwareServerBusy(false);
    await vi.advanceTimersByTimeAsync(59999);
    expect(server.firmwareServerInfo.value).toEqual(mockInfo);
    await vi.advanceTimersByTimeAsync(1);

    expect(server.firmwareServerInfo.value).toBeNull();
    expect(invoke).toHaveBeenCalledWith('stop_firmware_file_server');
    expect(pushNetworkLogMock).toHaveBeenCalledWith('Firmware server idle for 60 seconds; stopping.');
  });

  it('cancels a pending idle shutdown when another OTA starts', async () => {
    vi.useFakeTimers();
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

    server.setFirmwareServerBusy(true);
    await server.ensureFirmwareServer();
    server.setFirmwareServerBusy(false);
    await vi.advanceTimersByTimeAsync(30000);
    server.setFirmwareServerBusy(true);
    await vi.advanceTimersByTimeAsync(60000);

    expect(server.firmwareServerInfo.value).toEqual(mockInfo);
    expect(invoke).not.toHaveBeenCalledWith('stop_firmware_file_server');
  });
});
