import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildSummary,
  createGeneratedArtifactReport,
  parseArguments,
  parseRepeatCount
} from './run_webgl_modifier_tessellation.mjs';

test('tessellation fixture rejects implicit and duplicate option values', () => {
  assert.deepEqual(parseArguments(['node', 'runner', '--repeat', '3']), { repeat: '3' });
  assert.throws(() => parseArguments(['node', 'runner', '--repeat']), /Expected/u);
  assert.throws(() => parseArguments(['node', 'runner', '--repeat', '3', '--repeat', '4']), /Duplicate/u);
});

test('tessellation fixture requires at least three deterministic repetitions', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
});

test('tessellation summary requires every matrix and lint gate', () => {
  const pass = { status: 'pass' };
  const summary = buildSummary(3, 1, [{ status: 'pass' }], [pass], [pass], pass, pass);
  assert.equal(summary.status, 'pass');
  assert.deepEqual(summary.selectedCases, ['webgl_modifier_tessellation']);
});

test('tessellation generated lint reports absent dual-pipeline products', () => {
  const result = createGeneratedArtifactReport('/definitely/missing/generated-root');
  assert.equal(result.status, 'fail');
  assert.match(result.failures[0], /缺少生成产物/u);
});
