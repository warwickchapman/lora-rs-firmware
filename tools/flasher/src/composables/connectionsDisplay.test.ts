import { describe, expect, it } from 'vitest';
import {
  connectionSummaryTitle,
  shouldAutoCollapseConnections,
  type ConnectionHeaderSummary,
} from './connectionsDisplay';

const activeMqttSummary: ConnectionHeaderSummary = {
  contextLabel: 'Fleet gateway',
  transportLabel: 'Remote MQTT',
  targetLabel: 'lrs-0030eb55',
  state: 'active',
  stateLabel: 'Active',
  brokerState: 'connected',
  brokerRelevant: true,
};

describe('connectionSummaryTitle', () => {
  it('keeps the operational target and shared broker state distinct', () => {
    expect(connectionSummaryTitle(activeMqttSummary)).toBe(
      'Fleet gateway: Remote MQTT · lrs-0030eb55 · Active · MQTT connected',
    );
  });

  it('omits the broker when it is irrelevant to the current state', () => {
    expect(connectionSummaryTitle({
      ...activeMqttSummary,
      transportLabel: 'USB Serial',
      targetLabel: '/dev/cu.usbserial-10',
      brokerRelevant: false,
    })).toBe('Fleet gateway: USB Serial · /dev/cu.usbserial-10 · Active');
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
