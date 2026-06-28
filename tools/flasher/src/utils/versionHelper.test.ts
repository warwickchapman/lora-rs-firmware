import { describe, it, expect } from 'vitest';
import { parseVersion, compareParsedVersions } from './versionHelper';

describe('versionHelper', () => {
  describe('parseVersion', () => {
    it('parses correct version string', () => {
      expect(parseVersion('1.2.3')).toEqual({ major: 1, minor: 2, patch: 3, dev: 0 });
    });

    it('parses dev build version string', () => {
      expect(parseVersion('1.2.3~4')).toEqual({ major: 1, minor: 2, patch: 3, dev: 4 });
    });

    it('parses local label versions', () => {
      expect(parseVersion('Local: 0.9.2~1')).toEqual({ major: 0, minor: 9, patch: 2, dev: 1 });
    });

    it('returns null for invalid formats', () => {
      expect(parseVersion('invalid-version')).toBeNull();
      expect(parseVersion('')).toBeNull();
    });
  });

  describe('compareParsedVersions', () => {
    it('compares versions correctly', () => {
      const v1 = parseVersion('1.2.3');
      const v2 = parseVersion('1.2.4');
      const v3 = parseVersion('1.3.0');
      const v4 = parseVersion('2.0.0');
      const v5 = parseVersion('1.2.3~1');

      expect(compareParsedVersions(v1, v2)).toBeLessThan(0);
      expect(compareParsedVersions(v2, v1)).toBeGreaterThan(0);
      expect(compareParsedVersions(v1, v1)).toBe(0);

      expect(compareParsedVersions(v1, v5)).toBeLessThan(0);
      expect(compareParsedVersions(v5, v1)).toBeGreaterThan(0);

      expect(compareParsedVersions(v2, v3)).toBeLessThan(0);
      expect(compareParsedVersions(v3, v4)).toBeLessThan(0);
    });

    it('handles nulls safely by returning 0', () => {
      expect(compareParsedVersions(null, null)).toBe(0);
      expect(compareParsedVersions(parseVersion('1.0.0'), null)).toBe(0);
    });
  });
});
