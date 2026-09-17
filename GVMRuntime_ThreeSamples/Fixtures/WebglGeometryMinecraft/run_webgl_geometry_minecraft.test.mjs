import assert from 'node:assert/strict';
import test from 'node:test';

import {
  createDiagnosticManifest,
  minecraftScenarios,
  parseArguments,
  parseRepeatCount,
  validateExpectedFields,
  validateStatusLockContract
} from './run_webgl_geometry_minecraft.mjs';

/** Builds one complete Minecraft manifest record for isolated contract tests. */
function makeMinecraftExample() {
  return {
    id: 'webgl_geometry_minecraft',
    status: 'phase1_required',
    dslShard: 'WebglGeometryMinecraft',
    renderSetPolicy: 'not-required',
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    renderSetType: null,
    scenarios: minecraftScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState
    }))
  };
}

/** Builds the authoritative Minecraft status-lock review. */
function makeMinecraftReview() {
  const { status, ...review } = makeMinecraftExample();
  return {
    ...review,
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    scenePasses: [{
      name: 'main-atlas-lambert',
      renderClass: 'WebglGeometryMinecraftMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: []
  };
}

/** Builds the authoritative status lock used by the isolated diagnostic. */
function makeStatusLock() {
  return {
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    phase1RequiredReviews: [makeMinecraftReview()]
  };
}

/** Builds a 588-entry manifest with one Minecraft record and inert fillers. */
function makeManifest() {
  const examples = [makeMinecraftExample()];
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

test('parseRepeatCount preserves the mandatory three-capture stability gate', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /at least 3/u);
  assert.throws(() => parseRepeatCount('1.5'), /positive integer/u);
});

test('minecraftScenarios lock the true r185 seed, timer, and replay camera', () => {
  assert.deepEqual(minecraftScenarios.map((scenario) => [scenario.id, scenario.frame]), [
    ['initial', 0],
    ['animated', 60],
    ['first-person-input', 61]
  ]);
  assert.match(minecraftScenarios[0].canonicalState, /0x12345678.*draw92309/u);
  assert.equal(minecraftScenarios[1].virtualTimeMs, 999.9999999999991);
  assert.equal(minecraftScenarios[2].inputReplay, 'inputs/webgl_geometry_minecraft_first_person.json');
  assert.deepEqual(
    minecraftScenarios[2].cameraPosition,
    [494.5197900233341, 100, -494.5197900233341]
  );
});

test('validateStatusLockContract rejects seed-42 and the obsolete shared shard', () => {
  const statusLock = makeStatusLock();
  assert.deepEqual(validateStatusLockContract(statusLock), []);
  statusLock.phase1RequiredReviews[0].dslShard = 'Phase1GeometrySimple';
  statusLock.phase1RequiredReviews[0].scenarios[0].canonicalState = 'seed-42-atlas-loaded';
  const failures = validateStatusLockContract(statusLock).join('\n');
  assert.match(failures, /dslShard/u);
  assert.match(failures, /seed-42/u);
});

test('createDiagnosticManifest preserves 588 entries and overlays canonical scenarios', () => {
  const manifest = makeManifest();
  manifest.examples[0].scenarios[1].canonicalState = 'seed-42-fixed-step-60hz';
  const diagnostic = createDiagnosticManifest(manifest, makeMinecraftReview());
  assert.equal(diagnostic.examples.length, 588);
  assert.deepEqual(
    diagnostic.examples.filter((example) => example.status === 'phase1_required')
      .map((example) => example.id),
    ['webgl_geometry_minecraft']
  );
  assert.equal(diagnostic.examples[0].dslShard, 'WebglGeometryMinecraft');
  assert.match(diagnostic.examples[0].scenarios[1].canonicalState, /0x12345678/u);
  assert.deepEqual(diagnostic.examples[0].scenarios[0].scenePassSequence, [{
    sceneRoot: 'scene',
    scenePass: 'main-atlas-lambert'
  }]);
});

test('validateExpectedFields reports nested deterministic metadata drift', () => {
  const failures = [];
  validateExpectedFields(
    { inputReplay: { eventCount: 3, captureFrame: 61 } },
    { inputReplay: { eventCount: 4, captureFrame: 61 } },
    'metadata',
    failures
  );
  assert.deepEqual(failures, [
    'metadata.inputReplay.eventCount=3, expected 4.'
  ]);
});
