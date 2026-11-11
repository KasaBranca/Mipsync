function parseVersion(version: string): { numbers: number[]; channel: string; build: number } | null {
  const match = version.trim().match(/^v?(\d+)\.(\d+)\.(\d+)(?:([abf])([1-9]\d*))?$/);
  if (!match) return null;
  const numbers = match.slice(1, 4).map(Number);
  const channel = match[4] ?? "";
  const build = Number(match[5] ?? 0);
  if ([...numbers, build].some((n) => !Number.isSafeInteger(n) || n > 0xffffffff)) return null;
  if (channel && (numbers[0] === 0 || numbers[0] >= 2000)) return null;
  return { numbers, channel, build };
}
export function canonicalEditorVersion(version: string | null | undefined): string | null {
  if (!version) return null;
  const parsed = parseVersion(version.trim().replace(/^V/, "v"));
  return parsed ? `v${parsed.numbers.join(".")}${parsed.channel}${parsed.channel ? parsed.build : ""}` : null;
}
function releaseKey(version: string): number[] {
  const parsed = parseVersion(version.trim().replace(/^V/, "v"));
  if (!parsed) return [-1, 0, 0, 0, 0, 0];
  const [major, minor, patch] = parsed.numbers;
  const era = major === 0 ? 0 : major >= 2000 ? 1 : 2;
  const stage = parsed.channel === "a" ? 0 : parsed.channel === "b" ? 1 : 2;
  return [era, major, minor, patch, stage, parsed.build];
}
export function compareVersions(a: string, b: string): number {
  const av = releaseKey(a), bv = releaseKey(b);
  for (let i = 0; i < av.length; ++i) { if (av[i] !== bv[i]) return av[i] - bv[i]; }
  return 0;
}
