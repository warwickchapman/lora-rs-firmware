import { describe, expect, it } from 'vitest';
import {
  continueFactoryResetSequence,
  remoteFactoryResetDecision
} from './remoteFactoryResetStatus';

describe('remote factory-reset status', () => {
  it('ignores a different address or transaction', () => {
    expect(remoteFactoryResetDecision({ addr: 8, transaction_id: 10, stage: 'committed' }, 7, 10)).toBe('ignore');
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 11, stage: 'committed' }, 7, 10)).toBe('ignore');
  });

  it('maps only terminal gateway stages to terminal decisions', () => {
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 10, stage: 'awaiting_ack' }, 7, 10)).toBe('pending');
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 10, stage: 'confirming' }, 7, 10)).toBe('pending');
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 10, stage: 'committed' }, 7, 10)).toBe('committed');
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 10, stage: 'unconfirmed' }, 7, 10)).toBe('unconfirmed');
    expect(remoteFactoryResetDecision({ addr: 7, transaction_id: 10, stage: 'failed' }, 7, 10)).toBe('failed');
  });

  it('continues a selected-device sequence only after committed', () => {
    expect(continueFactoryResetSequence('committed')).toBe(true);
    expect(continueFactoryResetSequence('unconfirmed')).toBe(false);
    expect(continueFactoryResetSequence('failed')).toBe(false);
    expect(continueFactoryResetSequence('pending')).toBe(false);
  });
});
