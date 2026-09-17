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
  buffergeometryParityDefinitions,
  buffergeometryScenarios,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleSha256
} from './run_webgl_buffergeometry.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Builds the smallest generated source satisfying both transparent ordinary ABIs. */
function makeGeneratedSource() {
  return [
    'class WebglBuffergeometryBackSidePass',
    'class WebglBuffergeometryFrontSidePass',
    'vertexState.buffers[0].arrayStride = 40;',
    'vertexState.buffers[0].arrayStride = 40;',
    'offsetof(WebglBuffergeometryVertex, position)',
    'offsetof(WebglBuffergeometryVertex, position)',
    'offsetof(WebglBuffergeometryVertex, normal)',
    'offsetof(WebglBuffergeometryVertex, normal)',
    'offsetof(WebglBuffergeometryVertex, color)',
    'offsetof(WebglBuffergeometryVertex, color)',
    'setVertexBuffer(vertexBuffer)',
    'backSidePass0->run(WebglBuffergeometryVertexCount, 1u, 0u, 0u)',
    'frontSidePass0->run(WebglBuffergeometryVertexCount, 1u, 0u, 0u)',
    'computePass("WebglBuffergeometryResolve"',
    ...Array.from({ length: 4 }, (_, index) =>
      `renderPass("WebglBuffergeometrySample${index}"`)
  ].join('\n');
}

/** Builds a merged DSL header fixture with the locked shader and two side classes. */
function makeSingleHeader() {
  return [
    'static const uint WebglBuffergeometryVertexCount = 480000u',
    'webglBuffergeometryShade',
    'class WebglBuffergeometryBackSidePass',
    'class WebglBuffergeometryFrontSidePass'
  ].join('\n');
}

test('scenario matrix locks initial and fixed nonzero animation captures', () => {
  assert.deepEqual(buffergeometryScenarios.map(({ id, frame }) => ({ id, frame })), [
    { id: 'initial-seeded', frame: 0 },
    { id: 'fixed-rotation', frame: 120 }
  ]);
  assert.equal(buffergeometryParityDefinitions.length, 4);
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
    buffergeometryScenarios[1], 'experimental', 'vulkan', '/assets', artifacts);
  assert.ok(argumentsList.includes('407896085'));
  assert.deepEqual(
    argumentsList.slice(argumentsList.indexOf('--frame'), argumentsList.indexOf('--frame') + 2),
    ['--frame', '120']);
  assert.equal(argumentsList.includes('--input-replay'), false);
  assert.ok(argumentsList.includes('/out/capture.rgba'));
  assert.ok(argumentsList.includes('/out/capture.json'));
  assert.ok(argumentsList.includes('/out/scene.json'));
});

test('formal manifest locks the two ordered transparent ordinary Scene passes', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find((entry) => entry.id === 'webgl_buffergeometry');
  assert.deepEqual(validateManifestContract(example), []);
});

test('snapshot contract preserves Float32 RGBA and CDP-audited random states', () => {
  const initial = buildExpectedSnapshot(buffergeometryScenarios[0]);
  const fixedRotation = buildExpectedSnapshot(buffergeometryScenarios[1]);
  assert.equal(initial.sourceTriangleCount, 160_000);
  assert.equal(initial.sourceVertexCount, 480_000);
  assert.equal(initial.vertexStrideBytes, 40);
  assert.equal(initial.sourceColorFormat, 'float32x4');
  assert.equal(initial.doubleSideStrategy, 'transparent-back-side-then-front-side');
  assert.equal(initial.preGeometryRandomDrawCount, 116);
  assert.equal(initial.geometryRandomDrawCount, 2_080_000);
  assert.equal(initial.postGeometryRandomDrawCount, 52);
  assert.equal(initial.referenceRandomAudit.rendererConstructor.drawCount, 36);
  assert.equal(initial.referenceRandomAudit.firstRender.drawCount, 8);
  assert.equal(initial.referenceRandomAudit.subsequentFrames.drawCount, 0);
  assert.equal(initial.referenceFinalRandomState, 4_038_982_052);
  assert.equal(initial.sceneRenderSetCount, 0);
  assert.equal(initial.scenePassCount, 2);
  assert.equal(initial.physicalCoverageDrawCount, 2);
  assert.equal(fixedRotation.virtualTimeMilliseconds, 2000.0000000000034);
  assert.equal(fixedRotation.timeSeconds, 1_700_000_002);
});

test('clean Oracle digests are immutable and reject one-bit drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial-seeded',
    'rgba',
    '8c2314a7eda5ed4d303225dc097facea010b908e95c7e21295ef8127b05d87fc'), []);
  assert.equal(
    validateOracleSha256('initial-seeded', 'rgba', '0'.repeat(64)).length,
    1);
  assert.equal(validateOracleSha256('unknown', 'rgba', '0'.repeat(64)).length, 1);
});

test('generated ABI lint accepts identical Legacy and Experimental Float32 layouts', () => {
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
    generated.replaceAll('arrayStride = 40', 'arrayStride = 44'),
    singleHeader).status, 'fail');
});

test('Experimental product lint covers vertex, fragment, and compute stages', () => {
  const products = [
    { name: 'back-vertex', stage: 'vertex' },
    { name: 'back-fragment', stage: 'fragment' },
    { name: 'front-vertex', stage: 'vertex' },
    { name: 'front-fragment', stage: 'fragment' },
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
