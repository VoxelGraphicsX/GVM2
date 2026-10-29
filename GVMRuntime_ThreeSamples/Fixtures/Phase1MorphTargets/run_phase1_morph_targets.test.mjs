import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildCaseSummary,
  parseArguments,
  parseRepeatCount
} from './run_phase1_morph_targets.mjs';

test('morph runner parses only explicit value-bearing options', () => {
  assert.deepEqual(parseArguments([
    'node',
    'runner',
    '--binary-root',
    'bin',
    '--repeat',
    '3'
  ]), {
    'binary-root': 'bin',
    repeat: '3'
  });
  assert.throws(() => parseArguments(['node', 'runner', '--binary-root']), /Expected/u);
});

test('morph runner enforces three stability repetitions', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('1'), /at least 3/u);
});

test('case summary requires every independent gate', () => {
  const quadrant = { caseId: 'webgl_morphtargets', status: 'pass' };
  const comparison = { caseId: 'webgl_morphtargets', status: 'pass' };
  const passing = buildCaseSummary(
    'webgl_morphtargets',
    3,
    [quadrant],
    [comparison],
    [comparison],
    { status: 'pass' },
    { status: 'pass' }
  );
  assert.equal(passing.status, 'pass');
  const failing = buildCaseSummary(
    'webgl_morphtargets',
    3,
    [quadrant],
    [{ ...comparison, status: 'fail' }],
    [comparison],
    { status: 'pass' },
    { status: 'pass' }
  );
  assert.equal(failing.status, 'fail');
});
