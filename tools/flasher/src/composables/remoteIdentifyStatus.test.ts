import { describe, expect, it } from 'vitest';
import { remoteIdentifyDecision } from './remoteIdentifyStatus';

describe('remoteIdentifyDecision', () => {
  it('requires exact address and transaction correlation', () => {
    expect(remoteIdentifyDecision({ addr: 8, transaction_id: 10, stage: 'confirmed' }, 7, 10)).toBe('ignore');
    expect(remoteIdentifyDecision({ addr: 7, transaction_id: 11, stage: 'confirmed' }, 7, 10)).toBe('ignore');
  });

  it('maps terminal and pending stages', () => {
    expect(remoteIdentifyDecision({ addr: 7, transaction_id: 10, stage: 'awaiting_ack' }, 7, 10)).toBe('pending');
    expect(remoteIdentifyDecision({ addr: 7, transaction_id: 10, stage: 'confirmed' }, 7, 10)).toBe('confirmed');
    expect(remoteIdentifyDecision({ addr: 7, transaction_id: 10, stage: 'unconfirmed' }, 7, 10)).toBe('unconfirmed');
    expect(remoteIdentifyDecision({ addr: 7, transaction_id: 10, stage: 'unavailable_power_save' }, 7, 10)).toBe('unavailable_power_save');
  });
});
