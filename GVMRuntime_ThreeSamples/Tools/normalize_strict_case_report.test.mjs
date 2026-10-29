import assert from 'node:assert/strict';
import test from 'node:test';

import {normalizeStrictCaseReport, parseNormalizationArguments} from './normalize_strict_case_report.mjs';

test('normalization preserves dedicated run/comparison evidence and records provenance', () => {
  const report = {
    status: 'pass',
    caseId: 'webgpu_example',
    runs: [{scenario: 'initial', pipeline: 'legacy', backend: 'metal', repetition: 1}],
    comparisons: []
  };
  const normalized = normalizeStrictCaseReport(report, 'webgpu_example', 'old/summary.json');
  assert.equal(normalized.normalization.status, 'pass');
  assert.equal(normalized.normalization.provenance.sourcePath, 'old/summary.json');
  assert.equal(normalized.runs[0].scenario, 'initial');
});

test('rows without retained artifact paths are blocked instead of promoted', () => {
  const normalized = normalizeStrictCaseReport({
    status: 'pass',
    caseId: 'webgpu_example',
    rows: [{scenario: 'initial', pipeline: 'legacy', backend: 'metal', repeats: [{rep: 1}]}]
  }, 'webgpu_example');
  assert.equal(normalized.normalization.status, 'blocked');
  assert.match(normalized.normalization.failures[0], /RGBA|metadata/u);
});

test('normalization parses explicit paths only', () => {
  assert.deepEqual(parseNormalizationArguments([
    'node', 'normalize', '--input', 'a.json', '--output', 'b.json', '--case-id', 'webgl_example'
  ]), {input: 'a.json', output: 'b.json', 'case-id': 'webgl_example'});
});
