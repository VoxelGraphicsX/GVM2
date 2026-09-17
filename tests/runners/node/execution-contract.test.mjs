import test from 'node:test';
import { getTestRegistry } from './test-registry.mjs';
import assert from 'node:assert/strict';
import { inspectGoogleTestReport, inspectReadbackReport, requiredUglirTargets, validateUglirSelection } from './execution-contract.mjs';

test('UGLIR is required independently of the optional Legacy build', () => {
  const registry = requiredUglirTargets.map(target => ({ target, group: 'uglir' }));
  assert.doesNotThrow(() => validateUglirSelection(false, registry, registry, true));
  assert.throws(() => validateUglirSelection(true, registry, registry, true), /not registered.*legacy/);
  assert.throws(() => validateUglirSelection(false, registry.slice(1), registry, true), /not registered/);
  assert.throws(() => validateUglirSelection(false, registry, registry.slice(1), true), /omits required/);
  assert.throws(() => validateUglirSelection(false, registry, [], true), /no executable/);
  assert.throws(() => validateUglirSelection(false, [...registry, registry[0]], registry), /duplicate/);
});

test('missing, empty and incomplete GoogleTest reports cannot pass', () => {
  assert.throws(() => inspectGoogleTestReport(null), /did not report/);
  assert.throws(() => inspectGoogleTestReport({ tests: 0, testsuites: [] }), /did not report/);
  assert.throws(() => inspectGoogleTestReport({ tests: 1, testsuites: [] }), /count mismatch/);
});

test('execution counts distinguish passed, failed and skipped cases', () => {
  const report = { tests: 3, testsuites: [{ testsuite: [
    { status: 'RUN', result: 'COMPLETED' },
    { status: 'RUN', result: 'COMPLETED', failures: [{ failure: 'wrong value' }] },
    { status: 'RUN', result: 'SKIPPED' }
  ] }] };
  assert.deepEqual(inspectGoogleTestReport(report), { registered: 3, executed: 2, failures: 1, skipped: 1 });
});

test('sustained readback requires real duration, iterations, backend and correct values', () => {
  const valid = { status: 'RUN', result: 'COMPLETED', backend: 'metal', readback_duration_ms: '1800000', readback_iterations: '3000000' };
  const report = { tests: 1, testsuites: [{ testsuite: [valid] }] };
  assert.deepEqual(inspectReadbackReport(report, 'metal', 1800000), [valid]);
  for (const invalid of [
    { readback_duration_ms: undefined }, { readback_duration_ms: 'NaN' }, { readback_duration_ms: '1799999' },
    { readback_iterations: undefined }, { readback_iterations: 'NaN' }, { readback_iterations: '1' },
    { backend: 'vulkan' }, { result: 'SKIPPED' }, { failures: [{ failure: 'wrong result' }] }
  ]) {
    report.testsuites[0].testsuite = [{ ...valid, ...invalid }];
    assert.throws(() => inspectReadbackReport(report, 'metal', 1800000));
  }
});

/** Verifies that runtime backend selection reaches every backend-dependent executable as arguments. */
test('backend selection is passed explicitly to runtime executables', () => {
  for (const backend of ['metal', 'vulkan']) {
    const registry = getTestRegistry('/source', '/build', { backend });
    for (const entry of registry) {
      if (!entry.environment?.GVM_TEST_RHI_BACKEND) continue;
      const args = entry.args ?? [];
      const index = args.indexOf('--gvm-backend');
      assert.ok(index >= 0, entry.target);
      assert.equal(args[index + 1], backend, entry.target);
    }
  }
});
