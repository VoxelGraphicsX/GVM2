import assert from 'node:assert/strict';
import test from 'node:test';

import { parseArguments } from './run_texture_2d_array_cases.mjs';

test('texture-array fixture parses explicit value-bearing options', () => {
  const options = parseArguments([
    'node',
    'fixture',
    '--binary-root',
    '/tmp/bin',
    '--repeat',
    '3'
  ]);
  assert.deepEqual(options, {
    'binary-root': '/tmp/bin',
    repeat: '3'
  });
});

test('texture-array fixture rejects duplicate options', () => {
  assert.throws(
    () => parseArguments([
      'node',
      'fixture',
      '--repeat',
      '3',
      '--repeat',
      '4'
    ]),
    /Duplicate --repeat/
  );
});
