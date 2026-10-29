import assert from 'node:assert/strict';
import test from 'node:test';

import {
  createDiagnosticManifest,
  parseArguments,
  parseRepeatCount,
  validateExpectedFields,
  validateStatusLockContract,
  waterScenarios
} from './run_webgl_gpgpu_water.mjs';

const testComponentSchema = [
  { name: 'vertices', kind: 'buffer', role: 'vertex' },
  { name: 'indices', kind: 'buffer', role: 'index' },
  { name: 'objects', kind: 'buffer', role: 'object' },
  { name: 'instances', kind: 'buffer', role: 'instance' },
  { name: 'materials', kind: 'buffer', role: 'material' },
  {
    name: 'renderFlags',
    kind: 'buffer',
    role: 'phase-visibility-shadow-wireframe-texture'
  },
  {
    name: 'textures',
    kind: 'texture',
    role: 'fixed-per-entity-static-material-texture'
  }
];

const testScenePasses = [
  ['directional-shadow-depth', 'WebglGpgpuWaterShadowPass'],
  ['main-opaque-pbr', 'WebglGpgpuWaterOpaquePass'],
  ['main-transparent-water-back', 'WebglGpgpuWaterBackPass'],
  ['main-transparent-water-front', 'WebglGpgpuWaterFrontPass'],
  ['main-wireframe-phase', 'WebglGpgpuWaterWireframePass']
];

/** Builds one complete water manifest record for isolated contract tests. */
function makeWaterExample() {
  return {
    id: 'webgl_gpgpu_water',
    status: 'phase1_required',
    dslShard: 'WebglGpgpuWater',
    renderSetPolicy: 'required',
    renderableObjectCount: 14,
    loaderRenderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: true,
    containsMultipleMaterials: true,
    renderSetType: 'WebglGpgpuWaterSceneRenderSet',
    componentSchema: structuredClone(testComponentSchema),
    scenarios: waterScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState
    }))
  };
}

/** Builds the authoritative five-pass one-RenderSet water review. */
function makeWaterReview() {
  const { status, ...review } = makeWaterExample();
  return {
    ...review,
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 1,
      renderSetType: 'WebglGpgpuWaterSceneRenderSet'
    }],
    scenePasses: testScenePasses.map(([name, renderClass]) => ({
      name,
      renderClass,
      sceneRoot: 'scene',
      renderSetBindingCount: 1,
      usesStandaloneGeometry: false,
      usesExplicitDrawCount: false
    }))
  };
}

/** Builds the isolated authoritative status lock used by the water unit tests. */
function makeStatusLock() {
  return { phase1RequiredReviews: [makeWaterReview()] };
}

/** Builds a 588-entry inventory with one water record and inert fillers. */
function makeManifest() {
  const examples = [makeWaterExample()];
  for (let index = 1; index < 588; index += 1) {
    examples.push({ id: `filler-${index}`, status: 'audit_pending' });
  }
  return { examples };
}

test('parseArguments accepts explicit roots and rejects duplicate options', () => {
  const options = parseArguments([
    'node',
    'runner.mjs',
    '--binary-root', '/binaries',
    '--asset-root', '/assets',
    '--repeat', '3'
  ]);
  assert.equal(options['binary-root'], '/binaries');
  assert.equal(options['asset-root'], '/assets');
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

test('waterScenarios lock loader semantics and frame-120 pointer state', () => {
  assert.deepEqual(waterScenarios.map((scenario) => [scenario.id, scenario.frame]), [
    ['initial-assets-loaded', 0],
    ['canonical-loader-snapshot', 0],
    ['pointer-viscosity-shadow', 120]
  ]);
  assert.equal(waterScenarios[1].kind, 'loader-snapshot');
  assert.equal(waterScenarios[2].inputReplay, 'inputs/webgl_gpgpu_water_pointer_viscosity_shadow.json');
  assert.equal(
    waterScenarios[2].canonicalState,
    'inputs/webgl_gpgpu_water_pointer_viscosity_shadow_state.js'
  );
  assert.equal(waterScenarios[2].simulationTickCount, 60);
  assert.equal(waterScenarios[2].shadowEnabled, true);
  assert.equal(waterScenarios[2].viscosity, 0.97);
});

test('validateStatusLockContract rejects collapsed transparency and stale component ABI', () => {
  const statusLock = makeStatusLock();
  assert.deepEqual(validateStatusLockContract(statusLock), []);
  statusLock.phase1RequiredReviews[0].scenePasses.splice(2, 2, {
    name: 'main-transparent-water',
    renderClass: 'WebglGpgpuWaterTransparentPass',
    sceneRoot: 'scene',
    renderSetBindingCount: 1,
    usesStandaloneGeometry: false,
    usesExplicitDrawCount: false
  });
  statusLock.phase1RequiredReviews[0].componentSchema[5].name = 'waterState';
  const failures = validateStatusLockContract(statusLock).join('\n');
  assert.match(failures, /main-transparent-water-back/u);
  assert.match(failures, /renderFlags/u);
});

test('createDiagnosticManifest isolates water and overlays the state-script scenario', () => {
  const manifest = makeManifest();
  manifest.examples[0].scenarios[2].canonicalState = 'obsolete-symbolic-state';
  const diagnostic = createDiagnosticManifest(manifest, makeWaterReview());
  assert.equal(diagnostic.examples.length, 588);
  assert.deepEqual(
    diagnostic.examples.filter((example) => example.status === 'phase1_required')
      .map((example) => example.id),
    ['webgl_gpgpu_water']
  );
  const pointer = diagnostic.examples[0].scenarios.find((scenario) => (
    scenario.id === 'pointer-viscosity-shadow'
  ));
  assert.equal(pointer.canonicalState, 'inputs/webgl_gpgpu_water_pointer_viscosity_shadow_state.js');
  assert.deepEqual(pointer.scenePassInvocations.map((invocation) => invocation.scenePass), [
    'directional-shadow-depth',
    'main-opaque-pbr',
    'main-transparent-water-back',
    'main-transparent-water-front',
    'main-wireframe-phase'
  ]);
});

test('validateExpectedFields reports nested deterministic metadata drift', () => {
  const failures = [];
  validateExpectedFields(
    { simulation: { tickCount: 59, currentIndex: 0 } },
    { simulation: { tickCount: 60, currentIndex: 0 } },
    'metadata',
    failures
  );
  assert.deepEqual(failures, [
    'metadata.simulation.tickCount=59, expected 60.'
  ]);
});
