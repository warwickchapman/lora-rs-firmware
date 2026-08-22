import type { LoraAdoptionStatus } from '../types/fleet';

export function adoptionStatusText(status: LoraAdoptionStatus | null): string | null {
  if (!status) return null;
  if (status.stage === 'sending') return 'sending adoption';
  if (status.stage === 'awaiting_ack') return 'waiting for remote';
  if (status.stage === 'saving_gateway') return 'saving gateway';
  if (status.stage === 'unconfirmed') return 'adoption unconfirmed';
  if (status.stage === 'failed' && status.error_code === 1) return 'remote save failed';
  if (status.stage === 'failed' && status.error_code === 2) return 'gateway save failed';
  return null;
}

export function adoptionTransactionPending(status: LoraAdoptionStatus | null): boolean {
  return !!status && ['sending', 'awaiting_ack', 'saving_gateway'].includes(status.stage);
}

export function adoptionButtonLabel(status: LoraAdoptionStatus | null,
                                    candidateState: string): string {
  if (status?.stage === 'failed' && status.error_code === 2) return 'Retry save';
  return candidateState === 'failed' ? 'Retry adopt' : 'Adopt';
}
