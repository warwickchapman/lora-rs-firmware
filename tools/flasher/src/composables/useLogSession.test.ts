import { describe, expect, it } from 'vitest';
import { useLogSession } from './useLogSession';

describe('useLogSession', () => {
  it('normalizes structured logs and filters a focused source', () => {
    const logs = useLogSession();
    logs.append({ sourceId: 'serial:a', sourceLabel: 'USB gateway · a', transport: 'serial', raw: '[WARN] event=radio_timeout t=42' });
    logs.append({ sourceId: 'serial:b', sourceLabel: 'USB gateway · b', transport: 'serial', raw: 'event=boot t=5' });
    logs.selectOnly('serial:a');

    expect(logs.visibleRecords.value).toHaveLength(1);
    expect(logs.visibleRecords.value[0]).toMatchObject({ event: 'radio_timeout', severity: 'warn', deviceTimeMs: 42 });
    expect(logs.exportJsonl()).toContain('USB gateway');
  });

  it('bounds one noisy source without discarding another source', () => {
    const logs = useLogSession();
    for (let index = 0; index < 510; index += 1) {
      logs.append({ sourceId: 'udp:a', sourceLabel: 'Remote 1', transport: 'udp', raw: `event=line n=${index}` });
    }
    logs.append({ sourceId: 'udp:b', sourceLabel: 'Remote 2', transport: 'udp', raw: 'event=kept' });

    expect(logs.records.value.filter(record => record.sourceId === 'udp:a')).toHaveLength(500);
    expect(logs.records.value.some(record => record.sourceId === 'udp:b')).toBe(true);
  });
});
