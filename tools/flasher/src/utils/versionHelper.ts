export interface ParsedVersion {
  major: number;
  minor: number;
  patch: number;
  dev: number;
}

export function parseVersion(v: string): ParsedVersion | null {
  const clean = v.replace(/^Local:\s*/i, '').trim();
  const match = clean.match(/^(\d+)\.(\d+)\.(\d+)(?:~(\d+))?/);
  if (match) {
    return {
      major: parseInt(match[1], 10),
      minor: parseInt(match[2], 10),
      patch: parseInt(match[3], 10),
      dev: match[4] ? parseInt(match[4], 10) : 0
    };
  }
  return null;
}

export function compareParsedVersions(
  a: ParsedVersion | null,
  b: ParsedVersion | null
): number {
  if (!a || !b) return 0;
  if (a.major !== b.major) return a.major - b.major;
  if (a.minor !== b.minor) return a.minor - b.minor;
  if (a.patch !== b.patch) return a.patch - b.patch;
  return a.dev - b.dev;
}
