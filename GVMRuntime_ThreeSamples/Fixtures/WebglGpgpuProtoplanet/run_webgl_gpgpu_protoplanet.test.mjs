import assert from 'node:assert/strict';
import test from 'node:test';

import {
  createDiagnosticManifest,
  parseArguments,
  parseRepeatCount,
  protoplanetScenarios,
  validateExpectedFields,
  validateStatusLockContract
} from './run_webgl_gpgpu_protoplanet.mjs';

/** Builds one complete protoplanet manifest record for static contract tests. */
function makeProtoplanetExample() {
  return {
    id: 'webgl_gpgpu_protoplanet',
    status: 'phase1_required',
    dslShard: 'WebglGpgpuProtoplanet',
    renderSetPolicy: 'not-required',
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    renderSetType: null,
    scenarios: protoplanetScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState
    }))
  };
}

/** Builds the authoritative status-lock review without a derived manifest status. */
function makeProtoplanetReview() {
  const { status, ...review } = makeProtoplanetExample();
  return {
    ...review,
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    scenePasses: [{
      name: 'main-expanded-circular-particles',
      renderClass: 'WebglGpgpuProtoplanetMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: []
  };
}

/** Builds the authoritative status lock used by the isolated protoplanet diagnostic. */
function makeStatusLock() {
  return {
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    phase1RequiredReviews: [makeProtoplanetReview()]
  };
}

/** Builds a 588-entry manifest with one protoplanet record and inert filler examples. */
function makeManifest() {
  const examples = [makeProtoplanetExample()];
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

test('protoplanetScenarios lock initial, frame 60, and canonical-state restart', () => {
  assert.deepEqual(protoplanetScenarios.map((scenario) => [scenario.id, scenario.frame]), [
    ['initial-seeded-disc', 0],
    ['fixed-nbody-step', 60],
    ['gui-restart', 60]
  ]);
  assert.equal(protoplanetScenarios[1].simulationStepCount, 61);
  assert.equal(protoplanetScenarios[1].simulationFragmentPassCount, 122);
  assert.equal(protoplanetScenarios[2].inputReplay, null);
  assert.equal(
    protoplanetScenarios[2].canonicalState,
    'inputs/webgl_gpgpu_protoplanet_restart_gui_state.js'
  );
});

test('validateStatusLockContract rejects treating the canonical-state script as an input replay', () => {
  const statusLock = makeStatusLock();
  assert.deepEqual(validateStatusLockContract(statusLock), []);
  statusLock.phase1RequiredReviews[0].scenarios[2].inputReplay =
    'inputs/webgl_gpgpu_protoplanet_restart_gui_state.js';
  assert.match(validateStatusLockContract(statusLock).join('\n'), /gui-restart\.inputReplay/u);
});

test('createDiagnosticManifest preserves inventory and overlays the canonical-state script', () => {
  const manifest = makeManifest();
  manifest.examples[0].scenarios[2].inputReplay = 'inputs/obsolete.json';
  const diagnostic = createDiagnosticManifest(manifest, makeProtoplanetReview());
  assert.equal(diagnostic.examples.length, 588);
  assert.deepEqual(
    diagnostic.examples.filter((example) => example.status === 'phase1_required').map((example) => example.id),
    ['webgl_gpgpu_protoplanet']
  );
  const restart = diagnostic.examples[0].scenarios.find((scenario) => scenario.id === 'gui-restart');
  assert.equal(restart.inputReplay, null);
  assert.equal(restart.canonicalState, 'inputs/webgl_gpgpu_protoplanet_restart_gui_state.js');
  assert.deepEqual(restart.scenePassSequence, [{
    sceneRoot: 'scene',
    scenePass: 'main-expanded-circular-particles'
  }]);
});

test('validateExpectedFields reports nested deterministic metadata drift', () => {
  const failures = [];
  validateExpectedFields(
    { simulation: { stepCount: 60, pingPongIndex: 1 } },
    { simulation: { stepCount: 61, pingPongIndex: 1 } },
    'metadata',
    failures
  );
  assert.deepEqual(failures, [
    'metadata.simulation.stepCount=60, expected 61.'
  ]);
});
