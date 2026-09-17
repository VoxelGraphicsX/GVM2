import assert from 'node:assert/strict';
import test from 'node:test';

import {
  digestCaseIds,
  findSingleSampleSourceViolations,
  isRunComparisonReport,
  isCompleteSingleCaseDiagnosticReport,
  normalizeCaseEvidence,
  validateBatchLocks,
  validateImplementationInventory,
  validateNetNewBaseline
} from './verify_strict_example_batch.mjs';

const baselineStrictCaseIds = Object.freeze(['case_a', 'case_b']);

/** Creates one valid schema-2 batch contract for a supplied active case list. */
function createBatch(activeCaseIds) {
  return {
    schemaVersion: 2,
    minimumCaseCount: 20,
    minimumNetNewCaseCount: 20,
    baselineStrictCaseIds: [...baselineStrictCaseIds],
    baselineStrictCaseIdsSha256: digestCaseIds(baselineStrictCaseIds),
    cases: activeCaseIds.map((caseId) => ({caseId}))
  };
}

test('schema-2 batch accepts twenty cases outside the locked strict baseline', () => {
  const caseIds = Array.from({length: 20}, (_, index) => `new_case_${index}`);
  assert.deepEqual(validateNetNewBaseline(createBatch(caseIds), caseIds), {
    baselineCaseCount: 2,
    netNewCaseCount: 20
  });
});

test('schema-2 batch rejects baseline overlap and baseline digest drift', () => {
  const caseIds = Array.from({length: 20}, (_, index) => `new_case_${index}`);
  assert.throws(
    () => validateNetNewBaseline(createBatch([...caseIds.slice(0, 19), 'case_a']),
      [...caseIds.slice(0, 19), 'case_a']),
    /repeats baseline cases/u
  );
  const drifted = createBatch(caseIds);
  drifted.baselineStrictCaseIdsSha256 = '0'.repeat(64);
  assert.throws(() => validateNetNewBaseline(drifted, caseIds), /differs from the locked/u);
});

test('schema-2 batch rejects fewer than twenty net-new cases', () => {
  const caseIds = Array.from({length: 19}, (_, index) => `new_case_${index}`);
  assert.throws(() => validateNetNewBaseline(createBatch(caseIds), caseIds),
    /at least 20 are required/u);
});

test('strict batch validates unique asset and Oracle SHA-256 locks', () => {
  const locks = validateBatchLocks({
    assetLocks: [
      { path: 'textures/source.hdr', sha256: 'a'.repeat(64) }
    ],
    oracleLocks: [
      { caseId: 'case-a', scenarioId: 'initial', sha256: 'b'.repeat(64) }
    ]
  });
  assert.deepEqual(locks, {
    assetLockCount: 1,
    inputLockCount: 0,
    oracleLockCount: 1
  });
  assert.throws(
    () => validateBatchLocks({
      oracleLocks: [
        { caseId: 'case-a', scenarioId: 'initial', sha256: 'invalid' }
      ]
    }),
    /invalid SHA-256/u
  );
});

test('strict batch requires an explicit strict implementation inventory', () => {
  const batchCase = {
    caseId: 'webgl_example',
    dslEntry: 'WebglExampleRenderer',
    hostTarget: 'WebglExample'
  };
  const inventory = {
    caseId: batchCase.caseId,
    implementationLevel: 'strict-pass',
    dslEntry: batchCase.dslEntry,
    hostTarget: batchCase.hostTarget,
    scenePasses: ['WebglExampleMainPass'],
    renderSetType: 'WebglExampleSceneRenderSet',
    reportPath: 'build/example/summary.json'
  };
  assert.deepEqual(validateImplementationInventory(batchCase, inventory), []);
  assert.match(
    validateImplementationInventory(batchCase, {
      ...inventory,
      implementationLevel: 'semantic-complete',
      dslEntry: 'OtherRenderer'
    }).join('\n'),
    /不是 strict-pass/u
  );
  assert.match(
    validateImplementationInventory(batchCase, inventory, true).join('\n'),
    /多个 implemented-cases/u
  );
});

test('screen-only strict inventory may omit scenePasses when RenderSet is not applicable', () => {
  const batchCase = {
    caseId: 'misc_uv_tests',
    dslEntry: 'MiscUvTests',
    hostTarget: 'MiscUvTests'
  };
  assert.deepEqual(validateImplementationInventory(batchCase, {
    caseId: batchCase.caseId,
    implementationLevel: 'strict-pass',
    dslEntry: batchCase.dslEntry,
    hostTarget: batchCase.hostTarget,
    scenePasses: [],
    screenPasses: ['MiscUvTestsMainPass'],
    renderSetType: null,
    strictReport: 'build/misc-uv/summary.json'
  }), []);
});

test('single-sample source lint rejects antialias emulation but accepts algorithm sample loops', () => {
  const violatingSource = `
    // Resolves one supersampled scene without public MSAA.
    const char* mode = "antialias-resolve";
    const char* pattern = "rotated sample grid";
  `;
  assert.equal(findSingleSampleSourceViolations(violatingSource, 'scene.hpp').length, 3);
  assert.deepEqual(findSingleSampleSourceViolations(`
    const uint environmentSampleCount = 256u;
    const uint radialBlurSamples = 32u;
  `), []);
});

test('dedicated run/comparison reports normalize into strict matrix evidence', () => {
  const report = {
    executionCount: 1,
    oracleComparisonCount: 1,
    crossQuadrantComparisonCount: 1,
    stabilityComparisonCount: 1,
    runs: [{
      scenario: 'initial',
      pipeline: 'legacy',
      backend: 'metal',
      repetition: 1,
      rgbaPath: '/tmp/final.rgba',
      metadataPath: '/tmp/final.json'
    }],
    comparisons: [
      { kind: 'oracle', failures: [] },
      { kind: 'cross-quadrant', failures: [] },
      { kind: 'stability', failures: [] }
    ]
  };
  assert.equal(isRunComparisonReport(report), true);
  assert.deepEqual(normalizeCaseEvidence(report, 'webgpu_example'), {
    quadrants: [{
      caseId: 'webgpu_example',
      scenarioId: 'initial',
      pipeline: 'legacy',
      backend: 'metal',
      repetition: 1,
      status: 'pass',
      artifacts: {
        rgbaPath: '/tmp/final.rgba',
        metadataPath: '/tmp/final.json'
      }
    }],
    crossComparisons: [{
      kind: 'cross-quadrant', failures: [], caseId: 'webgpu_example', status: 'pass'
    }],
    stabilityComparisons: [{
      kind: 'stability', failures: [], caseId: 'webgpu_example', status: 'pass'
    }]
  });
});

test('formal quadrant reports preserve capturePath as RGBA artifact evidence', () => {
  const normalized = normalizeCaseEvidence({
    quadrants: [{
      caseId: 'webgpu_example',
      scenarioId: 'initial',
      pipeline: 'legacy',
      backend: 'metal',
      repetition: 1,
      status: 'pass',
      capturePath: 'captures/legacy/metal/initial/repeat-1/final.rgba',
      metadataPath: 'captures/legacy/metal/initial/repeat-1/final.json'
    }]
  }, 'webgpu_example');
  assert.equal(normalized.quadrants[0].artifacts.rgbaPath,
    'captures/legacy/metal/initial/repeat-1/final.rgba');
  assert.equal(normalized.quadrants[0].artifacts.metadataPath,
    'captures/legacy/metal/initial/repeat-1/final.json');
});

test('strict-case-closure reports validate their retained comparison metrics', () => {
  const report = {
    evidenceType: 'strict-case-closure',
    comparisons: [{
      kind: 'oracle',
      metrics: {
        normalizedDistancePixelRatio: 0,
        meanAbsoluteRgb: 0,
        p99AbsoluteRgb: 0,
        luminanceSsim: 1
      }
    }]
  };
  assert.equal(isRunComparisonReport(report), false);
});

test('complete single-case diagnostic reports expose the strict matrix contract', () => {
  const example = {
    id: 'webgl_math_orientation_transform',
    scenarios: [{id: 'initial'}, {id: 'animated'}, {id: 'orbit'}, {id: 'look-at'}]
  };
  const passEntries = (count) => Array.from({length: count}, () => ({status: 'pass'}));
  const report = {
    status: 'diagnostic-pass',
    selectedCases: [example.id],
    samplePolicy: {mode: 'single-sample', msaaEnabled: false, simulateMsaa: false},
    coverage: {
      caseId: example.id,
      scenarioCount: 4,
      quadrantCount: 48,
      crossComparisonCount: 48,
      stabilityComparisonCount: 32,
      oracleFailures: 0,
      crossFailures: 0,
      stabilityFailures: 0
    },
    quadrants: passEntries(48),
    crossComparisons: passEntries(48),
    stabilityComparisons: passEntries(32),
    generatedArtifacts: [{status: 'pass'}],
    gpuBoundaryLint: {status: 'pass'}
  };
  assert.equal(isCompleteSingleCaseDiagnosticReport(report, example), true);
  report.coverage.quadrantCount = 47;
  assert.equal(isCompleteSingleCaseDiagnosticReport(report, example), false);
});
