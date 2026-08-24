import { describe, expect, it } from 'vitest';
import {
  connectionSummaryTitle,
  connectionTransportTitle,
  shouldAutoCollapseConnections,
  type ConnectionHeaderSummary,
} from './connectionsDisplay';

const activeMqttSummary: ConnectionHeaderSummary = {
  contextLabel: 'Fleet gateway',
  transportLabel: 'Remote MQTT',
  targetLabel: 'lrs-0030eb55',
  state: 'active',
  stateLabel: 'Active',
  activeTransport: 'mqtt',
  serial: {
    state: 'active',
    stateLabel: 'Ready',
    targetLabel: '/dev/cu.usbserial-10',
  },
  mqtt: {
    state: 'active',
    stateLabel: 'Active',
    targetLabel: 'lrs-0030eb55',
  },
};

describe('connectionSummaryTitle', () => {
  it('keeps the operational target and shared broker state distinct', () => {
    expect(connectionSummaryTitle(activeMqttSummary)).toBe(
      'Fleet gateway: Remote MQTT · lrs-0030eb55 · Active · S Ready · M Active',
    );
  });

  it('reports both transport states when Serial is selected', () => {
    expect(connectionSummaryTitle({
      ...activeMqttSummary,
      transportLabel: 'USB Serial',
      targetLabel: '/dev/cu.usbserial-10',
      activeTransport: 'serial',
      mqtt: {
        state: 'offline',
        stateLabel: 'Disconnected',
        targetLabel: 'remote.example:1883',
      },
    })).toBe('Fleet gateway: USB Serial · /dev/cu.usbserial-10 · Active · S Ready · M Disconnected');
  });
});

describe('connectionTransportTitle', () => {
  it('identifies the selected transport and target', () => {
    expect(connectionTransportTitle('Serial', activeMqttSummary.serial, true)).toBe(
      'Serial: Ready — /dev/cu.usbserial-10 (selected)',
    );
  });

  it('identifies a background transport without implying it is selected', () => {
    expect(connectionTransportTitle('MQTT', {
      state: 'active',
      stateLabel: 'Connected',
      targetLabel: 'remote.example:1883',
    }, false)).toBe('MQTT: Connected — remote.example:1883 (background)');
  });
});

describe('shouldAutoCollapseConnections', () => {
  it('collapses only after a newly active connection', () => {
    expect(shouldAutoCollapseConnections('partial', 'active', false)).toBe(true);
    expect(shouldAutoCollapseConnections('active', 'active', false)).toBe(false);
    expect(shouldAutoCollapseConnections(undefined, 'active', false)).toBe(false);
  });

  it('stays open while connection settings are being edited', () => {
    expect(shouldAutoCollapseConnections('partial', 'active', true)).toBe(false);
  });
});
