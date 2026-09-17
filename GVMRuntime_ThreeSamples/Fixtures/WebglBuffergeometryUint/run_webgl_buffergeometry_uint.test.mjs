import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  parseArguments,
  uintParityDefinitions,
  uintScenarios,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleSha256
} from './run_webgl_buffergeometry_uint.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Builds the smallest generated source satisfying the packed ordinary ABI. */
function makeGeneratedSource() {
  return [
    'class WebglBuffergeometryUintMainPass',
    'vertexState.buffers[0].arrayStride = 24;',
    'offsetof(WebglBuffergeometryUintVertex, position)',
    'offsetof(WebglBuffergeometryUintVertex, packedNormalSnorm16x3)',
    'offsetof(WebglBuffergeometryUintVertex, packedColorUnorm8x3)',
    'setVertexBuffer(vertexBuffer)',
    'mainPass0->run(WebglBuffergeometryUintVertexCount, 1u, 0u, 0u)',
    'renderPass("WebglBuffergeometryUintSample0"'
  ].join('\n');
}

/** Builds a merged DSL header fixture with the locked decoders and classes. */
function makeSingleHeader() {
  return [
    'static const uint WebglBuffergeometryUintVertexCount = 3000000u',
    'webglBuffergeometryUintDecodeSnorm16',
    'webglBuffergeometryUintDecodeNormal',
    'webglBuffergeometryUintDecodeColor',
    'class WebglBuffergeometryUintMainPass'
  ].join('\n');
}

test('scenario matrix locks initial and fixed nonzero animation captures', () => {
  assert.deepEqual(uintScenarios.map(({ id, frame }) => ({ id, frame })), [
    { id: 'initial', frame: 0 },
    { id: 'animated', frame: 60 }
  ]);
  assert.equal(uintParityDefinitions.length, 4);
});

test('argument parser rejects incomplete and duplicate value pairs', () => {
  assert.deepEqual(parseArguments(['node', 'fixture', '--output-dir', 'out']), {
    'output-dir': 'out'
  });
  assert.throws(
    () => parseArguments(['node', 'fixture', '--output-dir']),
    /Expected --option value pair/u);
  assert.throws(
    () => parseArguments([
      'node', 'fixture', '--output-dir', 'a', '--output-dir', 'b'
    ]),
    /Duplicate --output-dir/u);
});

test('host arguments lock seed, extent, frame, and three output artifacts', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/scene.json'
  };
  const argumentsList = buildHostArguments(
    uintScenarios[1], 'experimental', 'vulkan', '/assets', artifacts);
  assert.ok(argumentsList.includes('407896079'));
  assert.deepEqual(
    argumentsList.slice(argumentsList.indexOf('--frame'), argumentsList.indexOf('--frame') + 2),
    ['--frame', '60']);
  assert.equal(argumentsList.includes('--input-replay'), false);
  assert.ok(argumentsList.includes('/out/capture.rgba'));
  assert.ok(argumentsList.includes('/out/capture.json'));
  assert.ok(argumentsList.includes('/out/scene.json'));
});

test('formal manifest locks one ordinary packed Phong Scene pass', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find((entry) => entry.id === 'webgl_buffergeometry_uint');
  assert.deepEqual(validateManifestContract(example), []);
});

test('snapshot contract preserves packed raw attributes and DSL normalization', () => {
  const initial = buildExpectedSnapshot(uintScenarios[0]);
  const animated = buildExpectedSnapshot(uintScenarios[1]);
  assert.equal(initial.sourceTriangleCount, 500_000);
  assert.equal(initial.expandedVertexCount, 3_000_000);
  assert.equal(initial.vertexStrideBytes, 24);
  assert.equal(initial.normalizationStage, 'dsl-vertex');
  assert.equal(initial.doubleSideStrategy, 'dual-winding-negated-normal-cull-back');
  assert.equal(initial.preGeometryRandomDrawCount, 116);
  assert.equal(initial.sceneRenderSetCount, 0);
  assert.equal(animated.timeSeconds, 1_700_000_001);
});

test('clean Oracle digests are immutable and reject one-bit drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial',
    'rgba',
    'd136fa5d39610132cc0f37790095eeb217f0419b6544471d17bd8dc92efee13c'), []);
  assert.equal(validateOracleSha256('initial', 'rgba', '0'.repeat(64)).length, 1);
  assert.equal(validateOracleSha256('unknown', 'rgba', '0'.repeat(64)).length, 1);
});

test('generated ABI lint accepts identical Legacy and Experimental packed layouts', () => {
  const generated = makeGeneratedSource();
  const singleHeader = makeSingleHeader();
  const legacy = validateGeneratedSourceContract('legacy', generated, singleHeader);
  const experimental = validateGeneratedSourceContract(
    'experimental', generated, singleHeader);
  assert.equal(legacy.status, 'pass');
  assert.equal(experimental.status, 'pass');
  assert.equal(validateGeneratedAbiParity([legacy, experimental]).status, 'pass');
  assert.equal(validateGeneratedSourceContract(
    'legacy',
    generated.replace('arrayStride = 24', 'arrayStride = 28'),
    singleHeader).status, 'fail');
});

test('Experimental product lint covers vertex, fragment, and compute stages', () => {
  const products = [
    { name: 'main-vertex', stage: 'vertex' },
    { name: 'main-fragment', stage: 'fragment' },
    { name: 'resolve', stage: 'compute' }
  ].map(({ name, stage }) => ({
    name,
    stage,
    uglirJson: JSON.stringify({ reflection: { stage } }),
    uglirText: `stage ${stage}`,
    msl: stage === 'compute' ? 'kernel void main()' : `${stage} void main()`,
    spirvWords: '0x07230203',
    spirvAssembly:
      `OpEntryPoint ${stage === 'compute' ? 'GLCompute' : stage[0].toUpperCase() + stage.slice(1)}`
  }));
  assert.deepEqual(validateExperimentalProductSources(products), []);
  products[0].spirvWords = '';
  assert.equal(validateExperimentalProductSources(products).length, 1);
});

test('RGBA inspection rejects malformed input and reports opaque color diversity', () => {
  assert.deepEqual(inspectRgbaPixels(Uint8Array.from([
    5, 5, 5, 255,
    255, 0, 0, 255
  ])), {
    uniqueRgbColorCount: 2,
    nonOpaquePixels: 0
  });
  assert.throws(() => inspectRgbaPixels(Uint8Array.from([0, 0, 0])), /RGBA8/u);
});
