export type ConnectionIndicatorState = 'active' | 'partial' | 'offline' | 'error';
export type BrokerIndicatorState = 'disconnected' | 'connecting' | 'connected' | 'error';

export interface ConnectionHeaderSummary {
  contextLabel: string;
  transportLabel: string;
  targetLabel: string;
  state: ConnectionIndicatorState;
  stateLabel: string;
  brokerState: BrokerIndicatorState;
  brokerRelevant: boolean;
}

export const EMPTY_CONNECTION_SUMMARY: ConnectionHeaderSummary = {
  contextLabel: 'Connection',
  transportLabel: '',
  targetLabel: 'No target selected',
  state: 'offline',
  stateLabel: 'No target',
  brokerState: 'disconnected',
  brokerRelevant: false,
};

export function connectionSummaryTitle(summary: ConnectionHeaderSummary): string {
  const target = summary.targetLabel || 'No target selected';
  const transport = summary.transportLabel ? `${summary.transportLabel} · ` : '';
  const broker = summary.brokerRelevant ? ` · MQTT ${summary.brokerState}` : '';
  return `${summary.contextLabel}: ${transport}${target} · ${summary.stateLabel}${broker}`;
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
