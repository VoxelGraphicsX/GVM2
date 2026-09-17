import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  buildStabilityComparisons,
  buildHostArguments,
  calculateCoverage,
  compareRepetitionPair,
  expandSelector,
  normalizeStatus,
  scenarioRequiresSemanticSnapshot,
  selectCases,
  validateDeterministicCaptureMetadata,
  validateRenderSetSnapshot,
  validateSemanticSnapshot,
  validateStructuralSnapshot
} from '../runner.mjs';

/** Writes one minimal RGBA artifact pair used by repeat-stability tests. */
async function writeRgbaArtifact(directory, name, width, height, pixels) {
  const rgbaPath = path.join(directory, `${name}.rgba`);
  const metadataPath = path.join(directory, `${name}.json`);
  await Promise.all([
    fs.writeFile(rgbaPath, Uint8Array.from(pixels)),
    fs.writeFile(metadataPath, `${JSON.stringify({ width, height })}\n`, 'utf8')
  ]);
  return { rgbaPath, metadataPath };
}

/** Creates one passing quadrant record for repeat-stability tests. */
function makeRepeatQuadrant(repetition, artifacts) {
  return {
    caseId: 'repeat_case',
    scenarioId: 'initial-frame',
    pipeline: 'legacy',
    backend: 'metal',
    repetition,
    status: 'pass',
    artifacts
  };
}

/** Creates a structurally minimal 588-case manifest for selection tests. */
function makeManifest(status = 'excluded_upstream') {
  return {
    examples: Array.from({ length: 588 }, (_value, index) => ({
      id: `case_${index}`,
      status
    }))
  };
}

test('status and matrix selectors accept the public CLI spelling', () => {
  assert.equal(normalizeStatus('phase1-required'), 'phase1_required');
  assert.deepEqual(expandSelector('all', ['legacy', 'uglir'], 'pipeline'), ['legacy', 'uglir']);
  assert.throws(() => expandSelector('invalid', ['metal', 'vulkan'], 'backend'), /Unsupported backend/u);
});

test('required selection is blocked until every candidate has a locked audit result', () => {
  const manifest = makeManifest('excluded_upstream');
  manifest.examples[0].status = 'audit_pending';
  assert.throws(() => selectCases(manifest, 'phase1-required'), /audit is not locked/u);
});

test('host arguments carry all runtime configuration explicitly', () => {
  const args = buildHostArguments(
    { id: 'webgl_geometry_cube' },
    { id: 'initial-frame', frame: 0 },
    'legacy',
    'metal',
    '/assets',
    {
      rgbaPath: '/capture/final.rgba',
      metadataPath: '/capture/final.json',
      sceneSnapshotPath: '/capture/scene.json'
    }
  );
  assert.deepEqual(args.slice(0, 16), [
    '--case-id', 'webgl_geometry_cube',
    '--scenario-id', 'initial-frame',
    '--frame', '0',
    '--random-seed', '305419896',
    '--pipeline', 'legacy',
    '--backend', 'metal',
    '--asset-root', '/assets',
    '--width', '800'
  ]);
  assert.ok(args.includes('--capture-rgba'));
  assert.ok(args.includes('--scene-snapshot'));
  assert.ok(args.includes('--semantic-snapshot'));
});

test('case-specific random seeds are shared by host arguments and metadata validation', () => {
  const example = { id: 'seeded_case', randomSeed: 42 };
  const scenario = { id: 'initial', frame: 0 };
  const args = buildHostArguments(
    example,
    scenario,
    'legacy',
    'metal',
    '/assets',
    {
      rgbaPath: '/capture/final.rgba',
      metadataPath: '/capture/final.json',
      sceneSnapshotPath: '/capture/scene.json',
      semanticPath: '/capture/semantic.json'
    }
  );
  assert.equal(args[args.indexOf('--random-seed') + 1], '42');
  const metadata = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: 0,
    randomSeed: 42,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    }
  };
  assert.deepEqual(
    validateDeterministicCaptureMetadata(example, scenario, metadata, metadata),
    []
  );
});

test('capture metadata rejects a random stream or scenario identity mismatch', () => {
  const example = { id: 'webgl_geometry_cube' };
  const scenario = { id: 'initial', frame: 0 };
  const correct = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: 0,
    randomSeed: 0x12345678,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    }
  };
  assert.deepEqual(validateDeterministicCaptureMetadata(example, scenario, correct, correct), []);
  const wrongOracle = { ...correct, randomSeed: 7, scenarioId: 'wrong' };
  const failures = validateDeterministicCaptureMetadata(example, scenario, correct, wrongOracle);
  assert.ok(failures.some((failure) => failure.includes('randomSeed')));
  assert.ok(failures.some((failure) => failure.includes('scenarioId')));
});

test('capture metadata requires the exact locked input replay identity', () => {
  const example = { id: 'paint_case' };
  const scenario = { id: 'painted', frame: 30, inputReplay: 'inputs/paint.json' };
  const base = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: scenario.frame,
    randomSeed: 0x12345678,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    samplePolicy: {
      mode: 'single-sample',
      msaaEnabled: false,
      simulateMsaa: false
    },
    inputReplay: {
      sha256: 'a'.repeat(64),
      caseId: example.id,
      scenarioId: scenario.id,
      captureFrame: scenario.frame,
      eventCount: 6,
      target: '#drawing-canvas'
    }
  };
  assert.deepEqual(validateDeterministicCaptureMetadata(example, scenario, base, base), []);
  const failures = validateDeterministicCaptureMetadata(
    example,
    scenario,
    { ...base, inputReplay: { ...base.inputReplay, sha256: 'b'.repeat(64) } },
    base
  );
  assert.ok(failures.some((failure) => failure.includes('inputReplay.sha256')));
});

test('scenario replay files resolve inside the explicit asset pack', () => {
  const args = buildHostArguments(
    { id: 'paint_case' },
    {
      id: 'painted',
      frame: 30,
      inputReplay: 'inputs/paint.json',
      canonicalState: 'fixed-state'
    },
    'uglir',
    'vulkan',
    '/locked-assets',
    {
      rgbaPath: '/capture/final.rgba',
      metadataPath: '/capture/final.json',
      sceneSnapshotPath: '/capture/scene.json'
    }
  );
  assert.equal(args[args.indexOf('--input-replay') + 1], '/locked-assets/inputs/paint.json');
  assert.equal(args[args.indexOf('--canonical-state') + 1], 'fixed-state');
  assert.throws(() => buildHostArguments(
    { id: 'bad' },
    { id: 'bad', frame: 0, inputReplay: '../escape.json' },
    'legacy',
    'metal',
    '/locked-assets',
    { rgbaPath: 'a', metadataPath: 'b', sceneSnapshotPath: 'c' }
  ), /escapes the locked asset pack/u);
});

test('runtime RenderSet snapshot rejects multiple sets and direct-draw fallback', () => {
  const example = {
    id: 'complex_scene',
    renderSetPolicy: 'required',
    sceneRoots: [{ name: 'main' }],
    componentSchema: [
      { name: 'vertices' },
      { name: 'indices' },
      { name: 'objects' }
    ],
    containsInstancing: true
  };
  const failures = validateRenderSetSnapshot(example, {
    sceneRoots: [{
      id: 'main',
      renderSetId: 'set-main',
      renderSetType: null,
      renderSetCount: 2,
      renderableObjectCount: 1,
      entityCount: 1,
      entities: [{ entityId: 0, logicalRenderableId: 'mesh', instanceCount: 1 }],
      componentSchema: [
        { name: 'vertices', kind: undefined, role: undefined },
        { name: 'wrong-indices', kind: undefined, role: undefined },
        { name: 'objects', kind: undefined, role: undefined }
      ],
      drawCommandCount: 1,
      scenePasses: [],
      directDrawFallback: true
    }]
  });
  assert.ok(failures.some((failure) => failure.includes('exactly one RenderSet')));
  assert.ok(failures.some((failure) => failure.includes('direct-draw fallback')));
  assert.ok(failures.some((failure) => failure.includes('no instanced entity')));
  assert.ok(failures.some((failure) => failure.includes('componentSchema differs')));
});

test('scenario instancing override supports dynamic ordinary-renderable modes', () => {
  const failures = validateRenderSetSnapshot({
    id: 'dynamic_mode',
    renderSetPolicy: 'required',
    sceneRoots: [{ name: 'main' }],
    componentSchema: [],
    containsInstancing: true
  }, {
    sceneRoots: [{
      id: 'main',
      renderSetId: 'set-main',
      renderSetType: null,
      renderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      entities: [{ entityId: 0, logicalRenderableId: 'mesh', instanceCount: 1 }],
      componentSchema: [],
      drawCommandCount: 0,
      scenePasses: [],
      directDrawFallback: false
    }]
  }, {
    containsInstancing: false
  });
  assert.ok(!failures.some((failure) => failure.includes('no instanced entity')));
});

test('every snapshot locks capture identity, object count, DSL ownership, and RenderSet count', () => {
  const example = {
    id: 'simple_case',
    renderSetPolicy: 'not-required',
    sceneRoots: [{ name: 'scene' }],
    renderableObjectCount: 1,
    containsInstancing: false
  };
  const scenario = { id: 'initial-frame', frame: 0 };
  const snapshot = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: 0,
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    drawCommandCount: 1,
    gpuWorkDslOnly: true
  };
  assert.deepEqual(validateStructuralSnapshot(example, scenario, snapshot), []);
  const invalid = {
    ...snapshot,
    scenarioId: 'wrong',
    sceneRenderSetCount: 1,
    renderableObjectCount: 2,
    instanceCount: 3,
    drawCommandCount: 0,
    gpuWorkDslOnly: false
  };
  const failures = validateStructuralSnapshot(example, scenario, invalid);
  for (const field of [
    'scenarioId', 'sceneRenderSetCount', 'renderableObjectCount',
    'instanceCount', 'drawCommandCount', 'gpuWorkDslOnly'
  ]) {
    assert.ok(failures.some((failure) => failure.includes(field)), `missing ${field} failure`);
  }
});

test('ordinary Scene snapshots lock repeated pass counts and ordered invocations', () => {
  const example = {
    id: 'ordinary_repeated_scene',
    renderSetPolicy: 'not-required',
    sceneRoots: [{ name: 'scene', renderSetRuntimeInstanceCount: 0 }],
    renderableObjectCount: 1,
    containsInstancing: false,
    scenePasses: [{ name: 'main', renderClass: 'OrdinaryMainPass', sceneRoot: 'scene' }]
  };
  const scenePassSequence = Array.from({ length: 4 }, () => ({
    sceneRoot: 'scene',
    scenePass: 'main',
    entityOrdinal: 0
  }));
  const scenario = {
    id: 'four-sample',
    frame: 0,
    scenePassInvocations: [{ sceneRoot: 'scene', scenePass: 'main', invocationCount: 4 }],
    scenePassSequence
  };
  const snapshot = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: 0,
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    drawCommandCount: 4,
    scenePassCount: 4,
    scenePassSequence: structuredClone(scenePassSequence),
    gpuWorkDslOnly: true
  };
  assert.deepEqual(validateStructuralSnapshot(example, scenario, snapshot), []);
  const wrongCount = { ...snapshot, scenePassCount: 1 };
  assert.ok(validateStructuralSnapshot(example, scenario, wrongCount)
    .some((failure) => failure.includes('scenePassCount')));
  const wrongOrder = structuredClone(snapshot);
  wrongOrder.scenePassSequence[2].scenePass = 'wrong';
  assert.ok(validateStructuralSnapshot(example, scenario, wrongOrder)
    .some((failure) => failure.includes('ordered Scene pass sequence differs at index 2')));
});

test('screen-only snapshots do not require a synthetic instanceCount', () => {
  const example = {
    id: 'screen_only_case',
    renderSetPolicy: 'not-required',
    sceneRoots: [{ name: 'postScene', renderSetRuntimeInstanceCount: 0 }],
    renderableObjectCount: 0,
    containsInstancing: false,
    scenePasses: []
  };
  const scenario = { id: 'initial-screen', frame: 0 };
  const snapshot = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: 0,
    sceneRenderSetCount: 0,
    renderableObjectCount: 0,
    drawCommandCount: 1,
    gpuWorkDslOnly: true
  };
  assert.deepEqual(validateStructuralSnapshot(example, scenario, snapshot), []);
});

test('runtime RenderSet snapshot requires the exact manifest Scene root identities', () => {
  const failures = validateRenderSetSnapshot({
    id: 'two_scene_case',
    renderSetPolicy: 'required',
    sceneRoots: [
      { name: 'main', renderSetType: 'TwoSceneSet' },
      { name: 'reflection', renderSetType: 'TwoSceneSet' }
    ],
    componentSchema: [{ name: 'vertices' }],
    containsInstancing: false,
    renderableObjectCount: 2,
    scenePasses: [
      { name: 'main', renderClass: 'MainPass', sceneRoot: 'main' },
      { name: 'reflection', renderClass: 'ReflectionPass', sceneRoot: 'reflection' }
    ]
  }, {
    sceneRoots: [
      {
        id: 'main',
        renderSetId: 'main-set',
        renderSetType: 'TwoSceneSet',
        renderSetCount: 1,
        renderableObjectCount: 1,
        entityCount: 1,
        entities: [{ entityId: 0, logicalRenderableId: 'main-mesh', instanceCount: 1 }],
        componentSchema: [{ name: 'vertices', kind: undefined, role: undefined }],
        drawCommandCount: 1,
        directDrawFallback: false,
        scenePasses: [{
          name: 'main',
          renderClass: 'MainPass',
          renderSetId: 'main-set',
          renderSetBindingCount: 1,
          drawMode: 'render-set-indexed-indirect',
          invocationCount: 1,
          drawCommandCount: 1,
          usesStandaloneGeometry: false,
          usesExplicitDrawCount: false
        }]
      },
      {
        id: 'main',
        renderSetId: 'wrong-set',
        renderSetType: 'TwoSceneSet',
        renderSetCount: 1,
        renderableObjectCount: 1,
        entityCount: 1,
        entities: [{ entityId: 0, logicalRenderableId: 'wrong-mesh', instanceCount: 1 }],
        componentSchema: [{ name: 'vertices', kind: undefined, role: undefined }],
        drawCommandCount: 1,
        directDrawFallback: false,
        scenePasses: []
      }
    ]
  });
  assert.ok(failures.some((failure) => failure.includes('ids must be unique')));
  assert.ok(failures.some((failure) => failure.includes("missing Scene root 'reflection'")));
});

test('runtime RenderSet snapshot proves entity mapping and pass reuse of one Scene set', () => {
  const example = {
    id: 'instanced_shadow_scene',
    renderSetPolicy: 'required',
    sceneRoots: [{ name: 'scene', renderSetType: 'FixtureSceneSet' }],
    renderableObjectCount: 2,
    containsInstancing: true,
    componentSchema: [
      { name: 'vertices', kind: 'buffer', role: 'vertex' },
      { name: 'indices', kind: 'buffer', role: 'index' },
      { name: 'objects', kind: 'buffer', role: 'object' },
      { name: 'instances', kind: 'buffer', role: 'instance' },
      { name: 'materials', kind: 'buffer', role: 'material' }
    ],
    scenePasses: [
      { name: 'main', renderClass: 'FixtureMainPass', sceneRoot: 'scene' },
      { name: 'shadow-depth', renderClass: 'FixtureShadowPass', sceneRoot: 'scene' }
    ]
  };
  const root = {
    id: 'scene',
    renderSetId: 'scene-set-0',
    renderSetType: 'FixtureSceneSet',
    renderSetCount: 1,
    renderableObjectCount: 2,
    entityCount: 2,
    entities: [
      { entityId: 4, logicalRenderableId: 'floor', instanceCount: 1 },
      { entityId: 9, logicalRenderableId: 'boxes', instanceCount: 3 }
    ],
    componentSchema: example.componentSchema,
    drawCommandCount: 2,
    directDrawFallback: false,
    scenePasses: example.scenePasses.map((scenePass) => ({
      name: scenePass.name,
      renderClass: scenePass.renderClass,
      renderSetId: 'scene-set-0',
      renderSetBindingCount: 1,
      drawMode: 'render-set-indexed-indirect',
      invocationCount: 1,
      drawCommandCount: 1,
      usesStandaloneGeometry: false,
      usesExplicitDrawCount: false
    }))
  };
  assert.deepEqual(validateRenderSetSnapshot(example, { sceneRoots: [root] }), []);
  const splitPassSnapshot = structuredClone(root);
  splitPassSnapshot.scenePasses[1].renderSetId = 'different-instance';
  const failures = validateRenderSetSnapshot(example, { sceneRoots: [splitPassSnapshot] });
  assert.ok(failures.some((failure) => failure.includes("reuse the Scene's unique RenderSet")));
});

test('scenario pass invocations distinguish generated definitions from repeated and inactive draws', () => {
  const example = {
    id: 'temporal_scene',
    renderSetPolicy: 'required',
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 1,
      renderSetType: 'TemporalSceneSet'
    }],
    renderableObjectCount: 1,
    containsInstancing: false,
    componentSchema: [{ name: 'vertices', kind: 'buffer', role: 'vertex' }],
    scenePasses: [
      { name: 'temporal-sample', renderClass: 'TemporalSamplePass', sceneRoot: 'scene' },
      { name: 'inactive-variant', renderClass: 'InactiveVariantPass', sceneRoot: 'scene' }
    ]
  };
  const scenario = {
    id: 'settled',
    frame: 8,
    scenePassInvocations: [
      { sceneRoot: 'scene', scenePass: 'temporal-sample', invocationCount: 8 },
      { sceneRoot: 'scene', scenePass: 'inactive-variant', invocationCount: 0 }
    ],
    scenePassSequence: Array.from({ length: 8 }, (_, entityOrdinal) => ({
      sceneRoot: 'scene',
      scenePass: 'temporal-sample',
      entityOrdinal
    }))
  };
  const snapshot = {
    scenePassSequence: structuredClone(scenario.scenePassSequence),
    sceneRoots: [{
      id: 'scene',
      renderSetId: 'temporal-set',
      renderSetType: 'TemporalSceneSet',
      renderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      entities: [{ entityId: 0, logicalRenderableId: 'mesh', instanceCount: 1 }],
      componentSchema: example.componentSchema,
      drawCommandCount: 8,
      directDrawFallback: false,
      scenePasses: [
        {
          name: 'temporal-sample',
          renderClass: 'TemporalSamplePass',
          renderSetId: 'temporal-set',
          renderSetBindingCount: 1,
          drawMode: 'render-set-indexed-indirect',
          invocationCount: 8,
          drawCommandCount: 8,
          usesStandaloneGeometry: false,
          usesExplicitDrawCount: false
        },
        {
          name: 'inactive-variant',
          renderClass: 'InactiveVariantPass',
          renderSetId: 'temporal-set',
          renderSetBindingCount: 1,
          drawMode: 'render-set-indexed-indirect',
          invocationCount: 0,
          drawCommandCount: 0,
          usesStandaloneGeometry: false,
          usesExplicitDrawCount: false
        }
      ]
    }]
  };
  assert.deepEqual(validateRenderSetSnapshot(example, snapshot, scenario), []);
  [snapshot.scenePassSequence[0], snapshot.scenePassSequence[1]] = [
    snapshot.scenePassSequence[1], snapshot.scenePassSequence[0]
  ];
  assert.ok(validateRenderSetSnapshot(example, snapshot, scenario)
    .some((failure) => failure.includes('ordered Scene pass sequence differs at index')));
  snapshot.scenePassSequence = structuredClone(scenario.scenePassSequence);
  snapshot.sceneRoots[0].scenePasses[0].drawCommandCount = 1;
  assert.ok(validateRenderSetSnapshot(example, snapshot, scenario)
    .some((failure) => failure.includes('8 RenderSet-only indexed-indirect draw invocation')));
});

test('scenario renderable count tracks deterministic dynamic removal', () => {
  const example = {
    id: 'dynamic_scene',
    renderSetPolicy: 'required',
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 1,
      renderSetType: 'DynamicSceneSet'
    }],
    renderableObjectCount: 2,
    containsInstancing: false,
    componentSchema: [{ name: 'vertices', kind: 'buffer', role: 'vertex' }],
    scenePasses: [{ name: 'main', renderClass: 'DynamicMainPass', sceneRoot: 'scene' }]
  };
  const scenario = { id: 'object-removed', frame: 1, renderableObjectCount: 1 };
  const runtimePass = {
    name: 'main',
    renderClass: 'DynamicMainPass',
    renderSetId: 'dynamic-set',
    renderSetBindingCount: 1,
    drawMode: 'render-set-indexed-indirect',
    invocationCount: 1,
    drawCommandCount: 1,
    usesStandaloneGeometry: false,
    usesExplicitDrawCount: false
  };
  const snapshot = {
    caseId: example.id,
    scenarioId: scenario.id,
    frame: scenario.frame,
    sceneRenderSetCount: 1,
    renderableObjectCount: 1,
    drawCommandCount: 1,
    gpuWorkDslOnly: true,
    sceneRoots: [{
      id: 'scene',
      renderSetId: 'dynamic-set',
      renderSetType: 'DynamicSceneSet',
      renderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      entities: [{ entityId: 1, logicalRenderableId: 'remaining', instanceCount: 1 }],
      componentSchema: example.componentSchema,
      drawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [runtimePass]
    }]
  };
  assert.deepEqual(validateStructuralSnapshot(example, scenario, snapshot), []);
  assert.deepEqual(validateRenderSetSnapshot(example, snapshot, scenario), []);
});

test('mixed examples validate one complex root and one independent simple root', () => {
  const componentSchema = [
    { name: 'vertices', kind: 'buffer', role: 'vertex' },
    { name: 'indices', kind: 'buffer', role: 'index' },
    { name: 'objects', kind: 'buffer', role: 'object' },
    { name: 'instances', kind: 'buffer', role: 'instance' },
    { name: 'materials', kind: 'buffer', role: 'material' }
  ];
  const example = {
    id: 'mixed_roots',
    renderSetPolicy: 'required',
    renderSetType: 'MixedSceneSet',
    sceneRoots: [
      {
        name: 'complex',
        renderSetRuntimeInstanceCount: 1,
        renderSetType: 'MixedSceneSet'
      },
      {
        name: 'simple',
        renderSetRuntimeInstanceCount: 0,
        renderSetType: null
      }
    ],
    renderableObjectCount: 2,
    containsInstancing: false,
    componentSchema,
    scenePasses: [
      { name: 'complex-main', renderClass: 'MixedComplexPass', sceneRoot: 'complex' },
      { name: 'simple-main', renderClass: 'MixedSimplePass', sceneRoot: 'simple' }
    ]
  };
  const snapshot = {
    sceneRoots: [
      {
        id: 'complex',
        renderSetId: 'complex-set',
        renderSetType: 'MixedSceneSet',
        renderSetCount: 1,
        renderableObjectCount: 1,
        entityCount: 1,
        entities: [{ entityId: 0, logicalRenderableId: 'complex-object', instanceCount: 1 }],
        componentSchema,
        drawCommandCount: 1,
        directDrawFallback: false,
        scenePasses: [{
          name: 'complex-main',
          renderClass: 'MixedComplexPass',
          renderSetId: 'complex-set',
          renderSetBindingCount: 1,
          drawMode: 'render-set-indexed-indirect',
          invocationCount: 1,
          drawCommandCount: 1,
          usesStandaloneGeometry: false,
          usesExplicitDrawCount: false
        }]
      },
      {
        id: 'simple',
        renderSetCount: 0,
        renderableObjectCount: 1
      }
    ]
  };
  assert.deepEqual(validateRenderSetSnapshot(example, snapshot), []);
  assert.deepEqual(validateStructuralSnapshot(
    example,
    { id: 'initial-frame', frame: 0 },
    {
      caseId: example.id,
      scenarioId: 'initial-frame',
      frame: 0,
      sceneRenderSetCount: 1,
      renderableObjectCount: 2,
      drawCommandCount: 2,
      gpuWorkDslOnly: true
    }
  ), []);
});

test('Loader and Exporter semantic sidecars require exact locked results', () => {
  const loaderExample = {
    id: 'webgl_loader_example',
    loaderRenderableObjectCount: 3,
    renderableObjectCount: 3,
    sceneRoots: [{ name: 'scene' }]
  };
  const loaderScenario = {
    id: 'canonical-loader',
    kind: 'loader-snapshot',
    frame: 0,
    canonicalState: 'asset-loaded'
  };
  const loaderDocument = {
    schemaVersion: 1,
    caseId: loaderExample.id,
    scenarioId: loaderScenario.id,
    frame: 0,
    kind: 'loader-snapshot',
    canonicalState: 'asset-loaded',
    result: {
      canonicalSceneSha256: 'a'.repeat(64),
      renderableObjectCount: 3,
      sceneRootCount: 1
    }
  };
  assert.equal(scenarioRequiresSemanticSnapshot(loaderScenario), true);
  assert.deepEqual(
    validateSemanticSnapshot(loaderExample, loaderScenario, loaderDocument, structuredClone(loaderDocument)),
    []
  );
  const changedLoader = structuredClone(loaderDocument);
  changedLoader.result.renderableObjectCount = 2;
  const loaderFailures = validateSemanticSnapshot(
    loaderExample,
    loaderScenario,
    changedLoader,
    loaderDocument
  );
  assert.ok(loaderFailures.some((failure) => failure.includes('differs from the locked Oracle')));
  assert.ok(loaderFailures.some((failure) => failure.includes('renderableObjectCount')));

  const exporterScenario = {
    id: 'round-trip',
    kind: 'export-round-trip',
    frame: 2,
    canonicalState: 'canonical-export'
  };
  const exporterDocument = {
    schemaVersion: 1,
    caseId: 'webgl_exporter_example',
    scenarioId: 'round-trip',
    frame: 2,
    kind: 'export-round-trip',
    canonicalState: 'canonical-export',
    result: {
      canonicalOutputSha256: 'b'.repeat(64),
      roundTripEquivalent: true,
      sourceSemanticSha256: 'c'.repeat(64),
      reimportedSemanticSha256: 'c'.repeat(64)
    }
  };
  assert.deepEqual(validateSemanticSnapshot(
    { id: 'webgl_exporter_example' },
    exporterScenario,
    exporterDocument,
    structuredClone(exporterDocument)
  ), []);
  const invalidRoundTrip = structuredClone(exporterDocument);
  invalidRoundTrip.result.reimportedSemanticSha256 = 'd'.repeat(64);
  assert.ok(validateSemanticSnapshot(
    { id: 'webgl_exporter_example' },
    exporterScenario,
    invalidRoundTrip,
    invalidRoundTrip
  ).some((failure) => failure.includes('round-trip')));
});

test('partial pipeline or backend diagnostics never count toward the P/P gate', () => {
  const example = {
    id: 'required_case',
    status: 'phase1_required',
    scenarios: [{ id: 'initial-frame', frame: 0 }]
  };
  const manifest = {
    examples: [
      example,
      ...Array.from({ length: 587 }, (_value, index) => ({
        id: `excluded_${index}`,
        status: 'excluded_upstream'
      }))
    ]
  };
  const quadrant = {
    caseId: example.id,
    scenarioId: 'initial-frame',
    repetition: 1,
    pipeline: 'legacy',
    backend: 'metal',
    status: 'pass'
  };
  const coverage = calculateCoverage(
    manifest,
    [example],
    [quadrant],
    [],
    ['legacy'],
    ['metal'],
    1
  );
  assert.equal(coverage.matrixComplete, false);
  assert.equal(coverage.passingRequired, 0);
  assert.equal(coverage.gatePassRate, 0);
});

test('repeat stability requires byte-exact RGBA output and matching dimensions', async (context) => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-repeat-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const baselineArtifacts = await writeRgbaArtifact(directory, 'baseline', 1, 1, [4, 8, 12, 255]);
  const identicalArtifacts = await writeRgbaArtifact(directory, 'identical', 1, 1, [4, 8, 12, 255]);
  const changedArtifacts = await writeRgbaArtifact(directory, 'changed', 1, 1, [4, 9, 12, 255]);
  const wrongDimensions = await writeRgbaArtifact(directory, 'wrong-dimensions', 2, 1, [4, 8, 12, 255, 4, 8, 12, 255]);
  const baseline = makeRepeatQuadrant(1, baselineArtifacts);

  const identical = await compareRepetitionPair(baseline, makeRepeatQuadrant(2, identicalArtifacts));
  assert.equal(identical.status, 'pass');
  assert.equal(identical.differingBytes, 0);

  const changed = await compareRepetitionPair(baseline, makeRepeatQuadrant(2, changedArtifacts));
  assert.equal(changed.status, 'fail');
  assert.equal(changed.differingBytes, 1);
  assert.match(changed.failures.join('\n'), /differs in 1 RGBA bytes/u);

  const resized = await compareRepetitionPair(baseline, makeRepeatQuadrant(2, wrongDimensions));
  assert.equal(resized.status, 'fail');
  assert.match(resized.failures.join('\n'), /dimensions differ/u);
});

test('three-run stability compares repetitions two and three against repetition one', async (context) => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'gvm-three-stability-'));
  context.after(() => fs.rm(directory, { recursive: true, force: true }));
  const artifacts = await Promise.all([
    writeRgbaArtifact(directory, 'repeat-1', 1, 1, [1, 2, 3, 255]),
    writeRgbaArtifact(directory, 'repeat-2', 1, 1, [1, 2, 3, 255]),
    writeRgbaArtifact(directory, 'repeat-3', 1, 1, [1, 2, 3, 255])
  ]);
  const comparisons = await buildStabilityComparisons(
    artifacts.map((artifact, index) => makeRepeatQuadrant(index + 1, artifact)),
    3
  );
  assert.deepEqual(comparisons.map((entry) => entry.candidateRepetition), [2, 3]);
  assert.ok(comparisons.every((entry) => entry.status === 'pass'));
});

test('missing one of three required repetitions cannot count as a passing required case', () => {
  const example = {
    id: 'required_case',
    status: 'phase1_required',
    scenarios: [{ id: 'initial-frame', frame: 0 }]
  };
  const manifest = {
    examples: [
      example,
      ...Array.from({ length: 587 }, (_value, index) => ({
        id: `excluded_${index}`,
        status: 'excluded_upstream'
      }))
    ]
  };
  const quadrants = [];
  const comparisons = [];
  const stability = [];
  for (const repetition of [1, 2]) {
    for (const pipeline of ['legacy', 'uglir']) {
      for (const backend of ['metal', 'vulkan']) {
        quadrants.push({
          caseId: example.id,
          scenarioId: 'initial-frame',
          repetition,
          pipeline,
          backend,
          status: 'pass'
        });
      }
    }
    for (const relation of [
      'pipeline-parity-metal',
      'pipeline-parity-vulkan',
      'backend-parity-legacy',
      'backend-parity-experimental'
    ]) {
      comparisons.push({
        caseId: example.id,
        scenarioId: 'initial-frame',
        repetition,
        relation,
        status: 'pass'
      });
    }
  }
  for (const pipeline of ['legacy', 'uglir']) {
    for (const backend of ['metal', 'vulkan']) {
      stability.push({
        caseId: example.id,
        scenarioId: 'initial-frame',
        pipeline,
        backend,
        candidateRepetition: 2,
        status: 'pass'
      });
    }
  }
  const coverage = calculateCoverage(
    manifest,
    [example],
    quadrants,
    comparisons,
    ['legacy', 'uglir'],
    ['metal', 'vulkan'],
    3,
    stability
  );
  assert.equal(coverage.diagnosticPassingRequired, 0);
  assert.equal(coverage.passingRequired, 0);
  assert.equal(coverage.gateComplete, false);
});
