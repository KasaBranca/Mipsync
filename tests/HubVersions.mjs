import assert from 'node:assert/strict';
import { compareVersions } from '../hub-tauri/src/version.ts';

const versions = ['v2026.10.18', 'v1.0.0', 'v2026.9.1', 'v1.10.0', 'v1.9.0', 'v0.1.16'];
assert.deepEqual(versions.sort((a, b) => compareVersions(b, a)),
  ['v1.10.0', 'v1.9.0', 'v1.0.0', 'v2026.10.18', 'v2026.9.1', 'v0.1.16']);
assert.equal(compareVersions('V1.0.0', '1.0.0'), 0);
assert.ok(compareVersions('invalid', 'v1.0.0') < 0);
console.log('Hub release ordering checks passed.');
