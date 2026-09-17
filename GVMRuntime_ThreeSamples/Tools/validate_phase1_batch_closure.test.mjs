import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  parseBatchClosureArguments,
  validateBatchClosure,
  validateStrictCaseSummary
} from './validate_phase1_batch_closure.mjs';

function makeExecutions(caseId) {
  return ['legacy', 'experimental'].flatMap((pipeline) =>
    ['metal', 'vulkan'].flatMap((backend) =>
      [0, 1, 2].map((repetition) => ({ caseId, pipeline, backend, repetition, passed: true }))));
}

function makeStrictSummary(caseId) {
  return {
    caseId,
    gateKind: 'strict',
    strictGateEvidence: true,
    passed: true,
    oracleComparisonPassed: true,
    threeRunStabilityPassed: true,
    crossQuadrantComparisonPassed: true,
    lintPassed: true,
    executions: makeExecutions(caseId)
  };
}

function makeFormalStrictSummary(caseId) {
  const quadrants = makeExecutions(caseId).map((execution) => ({
    ...execution,
    status: 'pass',
    validation: {
      status: 'pass',
      oraclePaths: { rgbaPath: `/oracle/${caseId}.rgba` },
      metrics: {
        normalizedDistancePixelRatio: 0,
        meanAbsoluteRgb: 0,
        p99AbsoluteRgb: 0,
        luminanceSsim: 1
      }
    }
  }));
  return {
    schemaVersion: 1,
    status: 'pass',
    selectedCases: [caseId],
    repeatCount: 3,
    runCount: quadrants.length,
    expectedRunCount: quadrants.length,
    quadrants,
    crossComparisons: [{ status: 'pass' }],
    stabilityComparisons: [{ status: 'pass' }],
    generatedArtifacts: { status: 'pass' },
    gpuBoundaryLint: { status: 'pass' }
  };
}

test('argument parser requires explicit batch paths', () => {
  assert.deepEqual(parseBatchClosureArguments([
    'node', 'tool', '--batch', '/batch.json', '--summary-root', '/summaries', '--output', '/result.json'
  ]), {
    batch: '/batch.json',
    'summary-root': '/summaries',
    output: '/result.json'
  });
  assert.throws(() => parseBatchClosureArguments(['node', 'tool']), /Missing --batch/u);
});

test('implementation smoke can never satisfy a case closure', () => {
  const summary = makeStrictSummary('case-a');
  summary.gateKind = 'implementation-smoke';
  summary.strictGateEvidence = false;
  assert.match(validateStrictCaseSummary(summary, 'case-a').join('\n'), /implementation smoke/u);
});

test('strict case requires every quadrant and three repetitions', () => {
  const summary = makeStrictSummary('case-a');
  assert.deepEqual(validateStrictCaseSummary(summary, 'case-a'), []);
  summary.executions = summary.executions.filter((execution) =>
    !(execution.pipeline === 'experimental' && execution.backend === 'vulkan'));
  assert.match(validateStrictCaseSummary(summary, 'case-a').join('\n'), /experimental-vulkan/u);
});

test('formal runner schema proves Oracle, comparisons, lints, and repetitions', () => {
  const summary = makeFormalStrictSummary('case-a');
  assert.deepEqual(validateStrictCaseSummary(summary, 'case-a'), []);
  summary.quadrants[0].validation.oraclePaths = {};
  assert.match(
    validateStrictCaseSummary(summary, 'case-a').join('\n'),
    /Oracle evidence/u);
});

test('formal runner accepts the standard generated artifact report array', () => {
  const summary = makeFormalStrictSummary('case-a');
  summary.generatedArtifacts = [
    { status: 'pass', checkedPasses: ['FirstPass'] },
    { status: 'pass', checkedPasses: ['SecondPass'] }
  ];
  assert.deepEqual(validateStrictCaseSummary(summary, 'case-a'), []);
  summary.generatedArtifacts[1].status = 'fail';
  assert.ok(
    validateStrictCaseSummary(summary, 'case-a')
      .includes('generated artifact lint is not passed')
  );
});

test('batch closes only after twenty distinct strict summaries', () => {
  const caseIds = Array.from({ length: 21 }, (_, index) => `case-${index + 1}`);
  const summaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-three-batch-'));
  const batch = {
    schemaVersion: 1,
    batchId: 'batch-test',
    minimumStrictPassCount: 20,
    primaryCases: caseIds.slice(0, 20),
    fallbackCases: [caseIds[20]],
    blockedCases: []
  };
  for (const caseId of caseIds.slice(0, 19)) {
    const directory = path.join(summaryRoot, caseId);
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'summary.json'), JSON.stringify(makeStrictSummary(caseId)));
  }
  assert.equal(validateBatchClosure(batch, summaryRoot).passed, false);
  for (const caseId of caseIds.slice(19)) {
    const directory = path.join(summaryRoot, caseId);
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'summary.json'), JSON.stringify(makeStrictSummary(caseId)));
  }
  const result = validateBatchClosure(batch, summaryRoot);
  assert.equal(result.passed, true);
  assert.equal(result.strictPassCount, 20);
  assert.equal(result.strictCases.length, 20);
  fs.rmSync(summaryRoot, { recursive: true, force: true });
  assert.throws(() => validateBatchClosure({
    ...batch,
    primaryCases: [...batch.primaryCases, batch.fallbackCases[0]]
  }, '/missing'), /globally unique/u);
});

test('atomic schema rejects baseline IDs and publishes selection diagnostics', () => {
  const summaryRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-three-atomic-'));
  const batch = {
    schemaVersion: 2,
    batchId: 'atomic-test',
    minimumNetNewCaseCount: 20,
    baselineStrictCaseIds: ['case-1'],
    activeCases: Array.from({ length: 20 }, (_, index) => ({ caseId: `case-${index + 1}` }))
  };
  for (const caseId of batch.activeCases.map((entry) => entry.caseId)) {
    const directory = path.join(summaryRoot, caseId);
    fs.mkdirSync(directory, { recursive: true });
    fs.writeFileSync(path.join(directory, 'summary.json'), JSON.stringify(makeStrictSummary(caseId)));
  }
  const result = validateBatchClosure(batch, summaryRoot);
  assert.equal(result.passed, false);
  assert.deepEqual(result.baselineConflicts, ['case-1']);
  assert.match(result.selectionFailures.join('\n'), /strict baseline/u);
  assert.equal(result.strictPassCount, 19);
  fs.rmSync(summaryRoot, { recursive: true, force: true });
});

test('formal atomic closure checks every immutable image metric', () => {
  const summary = makeFormalStrictSummary('case-a');
  summary.quadrants[0].validation.metrics.p99AbsoluteRgb = 17;
  assert.match(
    validateStrictCaseSummary(summary, 'case-a').join('\n'),
    /Oracle evidence/u);
});
