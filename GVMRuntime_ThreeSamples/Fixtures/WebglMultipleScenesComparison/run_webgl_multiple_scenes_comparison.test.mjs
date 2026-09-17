import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildSummary,
  createGeneratedArtifactReport,
  parseArguments,
  parseRepeatCount
} from './run_webgl_multiple_scenes_comparison.mjs';

test('multiple-scenes fixture rejects implicit and duplicate option values', () => {
  assert.deepEqual(parseArguments(['node', 'runner', '--repeat', '3']), { repeat: '3' });
  assert.throws(() => parseArguments(['node', 'runner', '--repeat']), /Expected/u);
  assert.throws(() => parseArguments([
    'node', 'runner', '--repeat', '3', '--repeat', '4'
  ]), /Duplicate/u);
});

test('multiple-scenes fixture requires at least three deterministic repetitions', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
});

test('multiple-scenes summary requires complete quadrant, parity, stability, and lint gates', () => {
  const pass = { status: 'pass' };
  const summary = buildSummary(
    3,
    1,
    [{ caseId: 'webgl_multiple_scenes_comparison', status: 'pass' }],
    [pass],
    [pass],
    pass,
    pass
  );
  assert.equal(summary.status, 'pass');
  assert.equal(summary.runCount, 1);
  assert.equal(summary.expectedRunCount, 1);
  assert.deepEqual(summary.selectedCases, ['webgl_multiple_scenes_comparison']);
});

test('multiple-scenes generated lint reports missing isolated pipeline artifacts', () => {
  const result = createGeneratedArtifactReport('/definitely/missing/generated-root');
  assert.equal(result.status, 'fail');
  assert.equal(result.checkedPasses.length, 3);
  assert.match(result.failures[0], /缺少生成产物/u);
});
