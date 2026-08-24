export type RemoteIdentifyDecision =
  | 'ignore'
  | 'pending'
  | 'confirmed'
  | 'unconfirmed'
  | 'unavailable_power_save';

export function remoteIdentifyDecision(
  status: any,
  address: number,
  transactionId: number
): RemoteIdentifyDecision {
  if (Number(status?.addr) !== address || Number(status?.transaction_id) !== transactionId) {
    return 'ignore';
  }
  if (status.stage === 'confirmed') return 'confirmed';
  if (status.stage === 'unconfirmed') return 'unconfirmed';
  if (status.stage === 'unavailable_power_save') return 'unavailable_power_save';
  return 'pending';
}
