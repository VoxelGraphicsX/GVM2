import assert from 'node:assert/strict';
import test from 'node:test';

import {
  birdsScenarios,
  createDiagnosticManifest,
  parseArguments,
  parseRepeatCount,
  validateExpectedFields,
  validateStatusLockContract
} from './run_webgl_gpgpu_birds.mjs';

/** Builds one complete birds manifest record for static contract tests. */
function makeBirdsExample() {
  return {
    id: 'webgl_gpgpu_birds',
    status: 'phase1_required',
    dslShard: 'WebglGpgpuBirds',
    renderSetPolicy: 'not-required',
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    renderSetType: null,
    scenarios: birdsScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState
    }))
  };
}

/** Builds the authoritative status-lock review without a derived manifest status. */
function makeBirdsReview() {
  const { status, ...review } = makeBirdsExample();
  return {
    ...review,
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    scenePasses: [{
      name: 'main-double-sided-flock',
      renderClass: 'WebglGpgpuBirdsMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }]
  };
}

/** Builds the authoritative status lock used by the isolated birds diagnostic. */
function makeStatusLock() {
  return {
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    phase1RequiredReviews: [makeBirdsReview()]
  };
}

/** Builds a 588-entry manifest with one birds record and inert filler examples. */
function makeManifest() {
  const examples = [makeBirdsExample()];
  for (let index = 1; index < 588; index += 1) {
    examples.push({ id: `filler-${index}`, status: 'audit_pending' });
  }
  return { examples };
}

test('parseArguments accepts explicit paths and rejects duplicate options', () => {
  const options = parseArguments([
    'node',
    'runner.mjs',
    '--binary-root', '/binaries',
    '--generated-root', '/generated',
    '--repeat', '3'
  ]);
  assert.equal(options['binary-root'], '/binaries');
  assert.equal(options.repeat, '3');
  assert.throws(() => parseArguments([
    'node', 'runner.mjs', '--repeat', '3', '--repeat', '4'
  ]), /Duplicate --repeat/u);
});

test('parseRepeatCount requires at least three deterministic captures', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
  assert.throws(() => parseRepeatCount('1.5'), /positive integer/u);
});

test('birdsScenarios lock initial, fixed frame one, and pointer frame one', () => {
  assert.deepEqual(birdsScenarios.map((scenario) => [scenario.id, scenario.frame]), [
    ['initial-seeded-flock', 0],
    ['fixed-flock-step', 1],
    ['pointer-and-gui', 1]
  ]);
  assert.equal(birdsScenarios[1].simulationStepCount, 2);
  assert.equal(birdsScenarios[1].simulationRenderPassCount, 4);
  assert.equal(birdsScenarios[1].currentPingPongIndex, 0);
  assert.match(birdsScenarios[2].canonicalState, /gui-preframe-28-17-31-pointer-frame1/u);
  assert.doesNotMatch(birdsScenarios[2].canonicalState, /frame60|121-dispatches|pingpong-index-1/u);
});

test('validateStatusLockContract rejects stale frame 120 birds scenarios', () => {
  const statusLock = makeStatusLock();
  assert.deepEqual(validateStatusLockContract(statusLock), []);
  statusLock.phase1RequiredReviews[0].scenarios[1].frame = 120;
  assert.match(validateStatusLockContract(statusLock).join('\n'), /fixed-flock-step\.frame=120/u);
});

test('createDiagnosticManifest preserves inventory and overlays the locked birds review', () => {
  const manifest = makeManifest();
  manifest.examples[0].scenarios[1].frame = 120;
  const diagnostic = createDiagnosticManifest(manifest, makeBirdsReview());
  assert.equal(diagnostic.examples.length, 588);
  assert.deepEqual(
    diagnostic.examples.filter((example) => example.status === 'phase1_required').map((example) => example.id),
    ['webgl_gpgpu_birds']
  );
  const runtimePointer = diagnostic.examples[0].scenarios.find(
    (scenario) => scenario.id === 'pointer-and-gui');
  assert.equal(runtimePointer.frame, 1);
  assert.equal(runtimePointer.inputReplay, 'inputs/webgl_gpgpu_birds_pointer_gui.json');
  assert.equal(runtimePointer.canonicalState, 'inputs/webgl_gpgpu_birds_pointer_gui_state.js');
  assert.equal(diagnostic.examples[0].scenarios[1].frame, 1);
});

test('validateExpectedFields reports nested deterministic metadata drift', () => {
  const failures = [];
  validateExpectedFields(
    { inputReplay: { eventCount: 2, captureFrame: 1 } },
    { inputReplay: { eventCount: 1, captureFrame: 1 } },
    'metadata',
    failures
  );
  assert.deepEqual(failures, [
    'metadata.inputReplay.eventCount=2, expected 1.'
  ]);
});
