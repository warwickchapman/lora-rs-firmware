export type RemoteFactoryResetDecision =
  | 'ignore'
  | 'pending'
  | 'committed'
  | 'unconfirmed'
  | 'failed';

export function remoteFactoryResetDecision(
  status: any,
  address: number,
  transactionId: number
): RemoteFactoryResetDecision {
  if (Number(status?.addr) !== address || Number(status?.transaction_id) !== transactionId) {
    return 'ignore';
  }
  if (status.stage === 'committed') return 'committed';
  if (status.stage === 'unconfirmed') return 'unconfirmed';
  if (status.stage === 'failed') return 'failed';
  return 'pending';
}

export function continueFactoryResetSequence(decision: RemoteFactoryResetDecision): boolean {
  return decision === 'committed';
}
