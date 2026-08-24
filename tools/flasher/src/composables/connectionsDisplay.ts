export type ConnectionIndicatorState = 'active' | 'partial' | 'offline' | 'error';

export interface ConnectionTransportSummary {
  state: ConnectionIndicatorState;
  stateLabel: string;
  targetLabel: string;
}

export interface ConnectionHeaderSummary {
  contextLabel: string;
  transportLabel: string;
  targetLabel: string;
  state: ConnectionIndicatorState;
  stateLabel: string;
  activeTransport: 'serial' | 'mqtt';
  serial: ConnectionTransportSummary;
  mqtt: ConnectionTransportSummary;
}

export const EMPTY_CONNECTION_SUMMARY: ConnectionHeaderSummary = {
  contextLabel: 'Connection',
  transportLabel: '',
  targetLabel: 'No target selected',
  state: 'offline',
  stateLabel: 'No target',
  activeTransport: 'serial',
  serial: {
    state: 'offline',
    stateLabel: 'No serial target',
    targetLabel: 'No target selected',
  },
  mqtt: {
    state: 'offline',
    stateLabel: 'Disconnected',
    targetLabel: 'No broker configured',
  },
};

export function connectionSummaryTitle(summary: ConnectionHeaderSummary): string {
  const target = summary.targetLabel || 'No target selected';
  const transport = summary.transportLabel ? `${summary.transportLabel} · ` : '';
  return `${summary.contextLabel}: ${transport}${target} · ${summary.stateLabel} · ` +
    `S ${summary.serial.stateLabel} · M ${summary.mqtt.stateLabel}`;
}

export function connectionTransportTitle(
  label: 'Serial' | 'MQTT',
  transport: ConnectionTransportSummary,
  active: boolean,
): string {
  const target = transport.targetLabel ? ` — ${transport.targetLabel}` : '';
  return `${label}: ${transport.stateLabel}${target} (${active ? 'selected' : 'background'})`;
}

export function shouldAutoCollapseConnections(
  previousState: ConnectionIndicatorState | undefined,
  nextState: ConnectionIndicatorState,
  connectionSettingsOpen: boolean,
): boolean {
  return previousState !== undefined &&
    previousState !== 'active' &&
    nextState === 'active' &&
    !connectionSettingsOpen;
}
