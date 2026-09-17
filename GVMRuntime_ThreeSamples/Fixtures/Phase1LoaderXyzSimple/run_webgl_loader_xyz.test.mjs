import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import test from 'node:test';

import {
  buildHostArguments,
  parseArguments,
  parseRepeatCount,
  validateExperimentalProductSources,
  validateFormalManifestEntry,
  validateGeneratedSourceContract,
  validateRepeatStability,
  validateStructuralSnapshot
} from './run_webgl_loader_xyz.mjs';

test('parseArguments accepts explicit paths and rejects duplicate options', () => {
  assert.deepEqual(parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/assets', '--oracle-root', '/oracles'
  ]), {
    'asset-root': '/assets',
    'oracle-root': '/oracles'
  });
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/one', '--asset-root', '/two'
  ]), /Duplicate --asset-root/u);
});

test('parseRepeatCount enforces at least three deterministic repetitions', () => {
  assert.equal(parseRepeatCount(undefined), 3);
  assert.equal(parseRepeatCount('4'), 4);
  assert.throws(() => parseRepeatCount('2'), /greater than or equal to 3/u);
  assert.throws(() => parseRepeatCount('3.5'), /greater than or equal to 3/u);
});

test('validateRepeatStability requires byte-exact RGBA, metadata, and snapshots', () => {
  const runs = [1, 2, 3].map((repetition) => ({
    scenario: 'animated',
    pipeline: 'experimental',
    backend: 'vulkan',
    repetition,
    status: 'pass',
    artifactSha256: { rgba: 'rgba', metadata: 'metadata', snapshot: 'snapshot' }
  }));
  const stable = validateRepeatStability(runs, 3);
  assert.equal(stable.status, 'pass');
  assert.equal(stable.byteExact, true);
  assert.equal(stable.artifacts.rgba.byteExact, true);
  runs[2].artifactSha256.snapshot = 'different';
  const unstable = validateRepeatStability(runs, 3);
  assert.equal(unstable.status, 'fail');
  assert.match(unstable.failures.join('\n'), /snapshot SHA-256/u);
});

test('buildHostArguments uses only explicit CLI state for the locked animated frame', () => {
  const argumentsList = buildHostArguments(
    { id: 'animated', frame: 120 },
    'experimental',
    'vulkan',
    { rgbaPath: '/a.rgba', metadataPath: '/a.json', snapshotPath: '/a.snapshot.json' },
    '/assets');
  assert.deepEqual(argumentsList.slice(0, 8), [
    '--case-id', 'webgl_loader_xyz',
    '--scenario-id', 'animated',
    '--pipeline', 'experimental',
    '--backend', 'vulkan'
  ]);
  assert.equal(argumentsList[argumentsList.indexOf('--frame') + 1], '120');
  assert.equal(argumentsList[argumentsList.indexOf('--asset-root') + 1], '/assets');
});

test('the repository manifest preserves the audited one-object non-RenderSet policy', async () => {
  const manifest = JSON.parse(await fs.readFile(
    new URL('../../Manifest/three-r185-manifest.json', import.meta.url),
    'utf8'));
  assert.deepEqual(validateFormalManifestEntry(manifest), []);
});

test('validateGeneratedSourceContract recognizes one indexed single-sample draw', () => {
  const generatedSource = `
    class WebglLoaderXyzPointPass {};
    static const uint WebglLoaderXyzPointCount = 201u;
    auto a = GVM::RHI::PrimitiveTopology::TriangleList;
    auto b = GVM::RHI::BufferUsage::Vertex;
    auto c = GVM::RHI::BufferUsage::Index;
    auto d = GVM::RHI::BufferBindingType::Uniform;
    vertexState.buffers[0].arrayStride = 20;
    vertexState.buffers[0].attributes[0].format = GVM::RHI::VertexFormat::Float32x3;
    vertexState.buffers[0].attributes[1].format = GVM::RHI::VertexFormat::Float32x2;
    vertexState.buffers[0].attributes[0].offset = offsetof(WebglLoaderXyzVertex, position);
    vertexState.buffers[0].attributes[1].offset = offsetof(WebglLoaderXyzVertex, corner);
    pointPass0->setVertexBuffer(vertexBuffer); pointPass0->setIndexBuffer(indexBuffer);
    pointPass0->run(WebglLoaderXyzIndexCount, 1u, 0u, 0, 0u);
  `;
  const result = validateGeneratedSourceContract(
    'legacy', generatedSource, 'namespace ExportedRenderSet {};');
  assert.equal(result.status, 'pass');
  assert.deepEqual(result.failures, []);
  assert.equal(result.abiSignature.expandedIndexCount, 1206);
});

test('validateExperimentalProductSources covers vertex and fragment products', () => {
  const makeProduct = (name, stage, extra = '') => ({
    uglirJson: JSON.stringify({ schemaVersion: 1, name, reflection: { stage } }),
    uglirText: `module "${name}"\nstage ${stage}\n${extra}`,
    msl: `${stage} entry`,
    spirvWords: 'word_count = 12\n0x07230203',
    spirvAssembly: `OpEntryPoint ${stage === 'vertex' ? 'Vertex' : stage === 'fragment' ? 'Fragment' : 'GLCompute'}`
  });
  const failures = validateExperimentalProductSources({
    WebglLoaderXyzPointPass__vertex:
      makeProduct('WebglLoaderXyzPointPass.vertex', 'vertex', 'pointSizePixels'),
    WebglLoaderXyzPointPass__fragment:
      makeProduct('WebglLoaderXyzPointPass.fragment', 'fragment')
  });
  assert.deepEqual(failures, []);
});

test('validateStructuralSnapshot enforces loader counts and fixed animation state', () => {
  const snapshot = {
    schemaVersion: 1,
    caseId: 'webgl_loader_xyz',
    scenarioId: 'animated',
    frame: 120,
    canonicalState: 'fixed-step-two-seconds-x-y-rotation',
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    pointCount: 201,
    expandedVertexCount: 804,
    expandedIndexCount: 1206,
    vertexStrideBytes: 20,
    standaloneGeometryBufferCount: 2,
    sourcePrimitiveTopology: 'points',
    expandedPrimitiveTopology: 'triangle-list',
    logicalScenePassCount: 1,
    logicalDrawCommandCount: 1,
    coverageSampleCount: 1,
    physicalSceneDrawCommandCount: 1,
    screenPassCount: 0,
    assetPath: 'models/xyz/helix_201.xyz',
    assetSha256: '489c27c4b619c9a47c15df62ebb7a5474791a7ae85f0c9c3f8323a9504288519',
    geometryHasColor: false,
    geometryCenter: [0.171785474, 0, 0],
    pointsMaterialSize: 0.1,
    perspectiveSizeScale: 250,
    timeSeconds: 2,
    rotationX: 0.4,
    rotationY: 1,
    scenePassSequence: [{
      sceneRoot: 'scene', scenePass: 'point-billboards', entityOrdinal: 0
    }],
    sceneRoots: [{
      id: 'scene', renderSetCount: 0, renderSetId: null, renderSetType: null,
      renderableObjectCount: 1, entityCount: 0, entities: [], drawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [{
        name: 'point-billboards', renderClass: 'WebglLoaderXyzPointPass',
        renderSetId: null, renderSetBindingCount: 0, drawMode: 'explicit-indexed',
        invocationCount: 1, drawCommandCount: 1, usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }]
    }],
    screenPasses: [],
    gpuWorkDslOnly: true
  };
  assert.deepEqual(validateStructuralSnapshot(snapshot, {
    id: 'animated',
    frame: 120,
    canonicalState: 'fixed-step-two-seconds-x-y-rotation'
  }), []);
});
