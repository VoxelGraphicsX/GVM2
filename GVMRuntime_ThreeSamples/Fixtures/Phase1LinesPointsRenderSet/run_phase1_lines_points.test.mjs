import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildCaseSummary,
  parseArguments,
  parseRepeatCount
} from './run_phase1_lines_points.mjs';

test('line fixture argument parser rejects implicit and duplicate values', () => {
  assert.deepEqual(parseArguments(['node', 'runner', '--repeat', '3']), { repeat: '3' });
  assert.throws(() => parseArguments(['node', 'runner', '--repeat']), /Expected/u);
  assert.throws(() => parseArguments([
    'node', 'runner', '--repeat', '3', '--repeat', '4'
  ]), /Duplicate/u);
});

test('line fixture requires at least three deterministic repetitions', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
});

test('line fixture builds independent per-case gate summaries', () => {
  const pass = { status: 'pass' };
  const summary = buildCaseSummary(
    'webgl_lines_colors',
    3,
    [
      { caseId: 'webgl_lines_colors', status: 'pass' },
      { caseId: 'webgl_lines_dashed', status: 'fail' }
    ],
    [
      { caseId: 'webgl_lines_colors', status: 'pass' },
      { caseId: 'webgl_lines_dashed', status: 'fail' }
    ],
    [
      { caseId: 'webgl_lines_colors', status: 'pass' },
      { caseId: 'webgl_lines_dashed', status: 'fail' }
    ],
    pass,
    pass
  );
  assert.equal(summary.status, 'pass');
  assert.deepEqual(summary.selectedCases, ['webgl_lines_colors']);
  assert.equal(summary.runCount, 1);
  assert.equal(summary.quadrants.length, 1);
});
