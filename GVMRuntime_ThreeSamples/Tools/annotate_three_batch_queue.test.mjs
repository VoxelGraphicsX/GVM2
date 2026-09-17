import test from 'node:test';
import assert from 'node:assert/strict';
import { annotateThreeBatchQueue } from './annotate_three_batch_queue.mjs';

function makeQueue() {
  return {
    schemaVersion: 1,
    activeCases: Array.from({ length: 20 }, (_, index) => ({
      caseId: `case-${index}`,
      dslShard: `Shard${index}`
    }))
  };
}

function makeSummary(caseCount = 20) {
  return {
    status: 'fail',
    generatedAt: '2026-08-25T15:00:00.000Z',
    repeatCount: 1,
    totals: { scenarios: 20, quadrants: 80, crossComparisons: 80, stabilityComparisons: 0 },
    singleSampleSourceLint: { status: 'pass' },
    gpuBoundaryLint: { status: 'pass' },
    generatedArtifactLint: { status: 'fail' },
    caseResults: Array.from({ length: caseCount }, (_, index) => ({
      caseId: `case-${index}`,
      status: index === 0 ? 'pass' : 'fail',
      reportPath: `build/cases/case-${index}/summary.json`,
      scenarioCount: 1,
      quadrantCount: 4,
      crossComparisonCount: 4,
      stabilityComparisonCount: 0,
      failures: index === 0 ? [] : ['oracle mismatch']
    }))
  };
}

test('attaches latest batch evidence without changing queue membership', () => {
  const queue = makeQueue();
  const summary = makeSummary();
  const annotated = annotateThreeBatchQueue(queue, summary, 'build/summary.json');
  assert.equal(annotated.activeCases.length, 20);
  assert.equal(annotated.lastRun.status, 'fail');
  assert.equal(annotated.lastRun.generatedArtifactLint, 'fail');
  assert.equal(annotated.activeCases[0].lastRun.status, 'pass');
  assert.equal(annotated.activeCases[1].lastRun.failures[0], 'oracle mismatch');
  assert.equal(queue.activeCases[0].lastRun, undefined);
});

test('rejects a summary that does not cover all active cases', () => {
  assert.throws(
    () => annotateThreeBatchQueue(makeQueue(), makeSummary(19), 'build/summary.json'),
    /missing queue cases/);
});
