function releaseKey(version: string): number[] {
  const match = version.trim().match(/^v?(\d+)\.(\d+)\.(\d+)$/i);
  if (!match) return [-1, 0, 0, 0];
  const [major, minor, patch] = match.slice(1).map(Number);
  const era = major === 0 ? 0 : major >= 2000 ? 1 : 2;
  return [era, major, minor, patch];
}

export function compareVersions(a: string, b: string): number {
  const av = releaseKey(a);
  const bv = releaseKey(b);
  for (let i = 0; i < av.length; ++i) {
    if (av[i] !== bv[i]) return av[i] - bv[i];
  }
  return 0;
}
