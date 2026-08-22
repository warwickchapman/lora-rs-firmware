import { describe, expect, it } from 'vitest';
import {
  adoptionButtonLabel,
  adoptionStatusText,
  adoptionTransactionPending
} from './adoptionStatus';
import type { LoraAdoptionStatus } from '../types/fleet';

function status(stage: LoraAdoptionStatus['stage'], error_code = 0): LoraAdoptionStatus {
  return {
    active: ['sending', 'awaiting_ack', 'saving_gateway'].includes(stage),
    chip_id: '123456',
    assigned_address: 3,
    transaction_id: 42,
    stage,
    error_code
  };
}

describe('adoption status presentation', () => {
  it('shows the bounded transaction stages', () => {
    expect(adoptionStatusText(status('sending'))).toBe('sending adoption');
    expect(adoptionStatusText(status('awaiting_ack'))).toBe('waiting for remote');
    expect(adoptionStatusText(status('saving_gateway'))).toBe('saving gateway');
    expect(adoptionStatusText(status('unconfirmed'))).toBe('adoption unconfirmed');
  });

  it('distinguishes remote and gateway persistence failures', () => {
    expect(adoptionStatusText(status('failed', 1))).toBe('remote save failed');
    expect(adoptionStatusText(status('failed', 2))).toBe('gateway save failed');
    expect(adoptionButtonLabel(status('failed', 2), 'failed')).toBe('Retry save');
    expect(adoptionButtonLabel(status('failed', 1), 'failed')).toBe('Retry adopt');
  });

  it('only treats live handoff stages as pending', () => {
    expect(adoptionTransactionPending(status('awaiting_ack'))).toBe(true);
    expect(adoptionTransactionPending(status('unconfirmed'))).toBe(false);
  });
});
