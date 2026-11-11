import assert from 'node:assert/strict';
import { canonicalEditorVersion, compareVersions } from '../hub-tauri/src/version.ts';

const versions = ['v2026.10.18', 'v1.0.0', 'v2026.9.1', 'v1.10.0', 'v1.9.0', 'v0.1.16'];
assert.deepEqual(versions.sort((a, b) => compareVersions(b, a)),
  ['v1.10.0', 'v1.9.0', 'v1.0.0', 'v2026.10.18', 'v2026.9.1', 'v0.1.16']);
assert.equal(compareVersions('V1.0.0', '1.0.0'), 0);
assert.ok(compareVersions('invalid', 'v1.0.0') < 0);
assert.deepEqual(['1.0.0f2', '1.0.0b10', '1.0.0f10', '1.0.0a10', '1.0.0f1']
  .sort(compareVersions), ['1.0.0a10', '1.0.0b10', '1.0.0f1', '1.0.0f2', '1.0.0f10']);
assert.equal(canonicalEditorVersion('V1.0.0f1'), 'v1.0.0f1');
for (const invalid of ['1.0.0f0', '1.0.0f', '1.0.0f1/../', '2026.10.18f1', '1.0.0-preview']) {
  assert.equal(canonicalEditorVersion(invalid), null);
}
console.log('Hub release ordering checks passed.');
