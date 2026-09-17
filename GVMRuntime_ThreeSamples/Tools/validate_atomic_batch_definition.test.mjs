import assert from 'node:assert/strict';
import test from 'node:test';

import {
  computeCaseIdSha256,
  parseArguments,
  validateAtomicBatchDefinition
} from './validate_atomic_batch_definition.mjs';

/** Builds one minimal valid twenty-case definition for validator tests. */
function makeFixture() {
  const baselineIds = ['webgl_existing'];
  const cases = Array.from({ length: 20 }, (_, index) => ({
    caseId: `webgl_new_${index}`,
    implementationLevel: 'scaffolded'
  }));
  const digest = computeCaseIdSha256(baselineIds);
  return {
    batch: {
      publicationMode: 'atomic',
      minimumCaseCount: 20,
      minimumNetNewCaseCount: 20,
      baselineStrictCaseCount: 1,
      baselineStrictCaseIdsSha256: digest,
      upstreamMsaaPolicy: {
        mode: 'single-sample-rendering',
        excludeExamples: false,
        simulateMsaa: false,
        requireMsaaParity: false
      },
      cases
    },
    baseline: {
      strictCaseCount: 1,
      strictCaseIdsSha256: digest,
      strictCaseIds: baselineIds
    },
    manifest: {
      examples: cases.map(({ caseId }) => ({
        id: caseId,
        status: 'phase1_required'
      }))
    }
  };
}

test('atomic batch validator accepts exactly twenty net-new required cases', () => {
  const fixture = makeFixture();
  assert.deepEqual(validateAtomicBatchDefinition(
    fixture.batch, fixture.baseline, fixture.manifest), []);
});

test('atomic batch validator rejects old cases and MSAA simulation', () => {
  const fixture = makeFixture();
  fixture.batch.cases[0].caseId = 'webgl_existing';
  fixture.manifest.examples.push({
    id: 'webgl_existing',
    status: 'phase1_required'
  });
  fixture.batch.upstreamMsaaPolicy.simulateMsaa = true;
  const failures = validateAtomicBatchDefinition(
    fixture.batch, fixture.baseline, fixture.manifest);
  assert.ok(failures.some((failure) => failure.includes('not net-new')));
  assert.ok(failures.some((failure) => failure.includes('without simulation')));
});

test('atomic batch validator requires canonical strict report paths', () => {
  const fixture = makeFixture();
  fixture.batch.cases[0].implementationLevel = 'strict-pass';
  fixture.batch.cases[0].strictReport = 'obsolete/summary.json';
  const failures = validateAtomicBatchDefinition(
    fixture.batch,
    fixture.baseline,
    fixture.manifest);
  assert.ok(failures.some((failure) => failure.includes('must declare reportPath')));
  assert.ok(failures.some((failure) => failure.includes('obsolete strictReport')));

  delete fixture.batch.cases[0].strictReport;
  fixture.batch.cases[0].reportPath = 'build/example/summary.json';
  assert.deepEqual(validateAtomicBatchDefinition(
    fixture.batch,
    fixture.baseline,
    fixture.manifest), []);
});

test('atomic batch validator parses explicit batch selection paths', () => {
  assert.deepEqual(parseArguments([
    'node',
    'validate_atomic_batch_definition.mjs',
    '--batch',
    'Manifest/three-r185-batch20-wave6.json',
    '--manifest',
    'Manifest/three-r185-manifest.json'
  ]), {
    batch: 'Manifest/three-r185-batch20-wave6.json',
    manifest: 'Manifest/three-r185-manifest.json'
  });
  assert.throws(
    () => parseArguments(['node', 'tool.mjs', '--batch']),
    /Expected --option value pair/);
  assert.throws(
    () => parseArguments(['node', 'tool.mjs', '--batch', 'a', '--batch', 'b']),
    /Duplicate --batch/);
});
