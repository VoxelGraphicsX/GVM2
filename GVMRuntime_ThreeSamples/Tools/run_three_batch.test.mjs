import test from 'node:test';
import assert from 'node:assert/strict';

import {
  createSingleCaseManifest,
  summarizeCaseRun,
  validateBatchSelection
} from './run_three_batch.mjs';

/** Creates a complete-sized test manifest with a configurable active prefix. */
function makeManifest(count = 20) {
  return {
    examples: Array.from({ length: 588 }, (_, index) => index < count
      ? {
        id: `webgl_case_${index}`,
        status: 'phase1_required',
        scenarios: [{ id: 'initial' }]
      }
      : { id: `excluded_case_${index}`, status: 'excluded_upstream', scenarios: [] })
  };
}

/** Creates a frozen single-sample batch selection for runner contract tests. */
function makeBatch(count = 20) {
  return {
    batchId: 'test-batch',
    minimumNetNewCaseCount: 20,
    baselineStrictCaseIds: [],
    selectionPolicy: { msaaEnabled: false, simulateMsaa: false },
    upstreamMsaaPolicy: { simulateMsaa: false },
    activeCases: Array.from({ length: count }, (_, index) => ({
      caseId: `webgl_case_${index}`,
      dslEntry: `Case${index}Renderer`,
      hostTarget: `Case${index}`
    }))
  };
}

test('batch selection enforces twenty net-new single-sample cases', () => {
  const selection = validateBatchSelection(makeManifest(), makeBatch());
  assert.equal(selection.activeCases.length, 20);
  assert.equal(selection.selectedExamples.length, 20);
});

test('batch selection rejects a strict baseline overlap', () => {
  const batch = makeBatch();
  batch.baselineStrictCaseIds = ['webgl_case_0'];
  assert.throws(
    () => validateBatchSelection(makeManifest(), batch),
    /overlaps the strict baseline/
  );
});

test('batch selection rejects a short active list', () => {
  assert.throws(
    () => validateBatchSelection(makeManifest(19), makeBatch(19)),
    /requires at least 20 active cases/
  );
});

test('single-case manifest excludes every other source example', () => {
  const source = makeManifest();
  const selected = source.examples[3];
  const view = createSingleCaseManifest(source, selected);
  assert.equal(view.examples.filter((entry) => entry.status === 'phase1_required').length, 1);
  assert.equal(view.examples[3].id, selected.id);
  assert.equal(view.examples[0].status, 'excluded_upstream');
});

test('case summary preserves all runner evidence cardinalities', () => {
  const example = { id: 'webgl_case_0', scenarios: [{ id: 'initial' }] };
  const report = {
    status: 'pass',
    quadrants: Array.from({ length: 12 }, () => ({ caseId: example.id, status: 'pass', failures: [] })),
    crossComparisons: Array.from({ length: 12 }, () => ({ caseId: example.id, status: 'pass', failures: [] })),
    stabilityComparisons: Array.from({ length: 8 }, () => ({ caseId: example.id, status: 'pass', failures: [] }))
  };
  const summary = summarizeCaseRun(example, report, 'build/test/summary.json');
  assert.equal(summary.status, 'pass');
  assert.equal(summary.quadrantCount, 12);
  assert.equal(summary.crossComparisonCount, 12);
  assert.equal(summary.stabilityComparisonCount, 8);
});
