import assert from 'node:assert/strict';
import test from 'node:test';

import { parseArguments } from './generate_webgl_modifier_tessellation_asset.mjs';

test('tessellation asset generator requires explicit unique option values', () => {
  assert.deepEqual(parseArguments(['node', 'generator', '--output', 'asset.bin']), {
    output: 'asset.bin'
  });
  assert.throws(() => parseArguments(['node', 'generator', '--output']), /Expected/u);
  assert.throws(() => parseArguments([
    'node', 'generator', '--output', 'a', '--output', 'b'
  ]), /Duplicate/u);
});
