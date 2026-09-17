import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  indexedParityDefinitions,
  indexedScenarios,
  inspectRgbaPixels,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleSha256
} from './run_webgl_buffergeometry_indexed.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Builds the smallest generated-source fixture satisfying the ordinary indexed ABI. */
function makeGeneratedSource() {
  return [
    'class WebglBuffergeometryIndexedFilledPass',
    'class WebglBuffergeometryIndexedWireframePass',
    'vertexState.buffers[0].arrayStride = 36;',
    'vertexState.buffers[0].arrayStride = 56;',
    'offsetof(WebglBuffergeometryIndexedVertex, position)',
    'offsetof(WebglBuffergeometryIndexedVertex, normal)',
    'offsetof(WebglBuffergeometryIndexedVertex, color)',
    'offsetof(WebglBuffergeometryIndexedWireVertex, startPosition)',
    'offsetof(WebglBuffergeometryIndexedWireVertex, endPosition)',
    'offsetof(WebglBuffergeometryIndexedWireVertex, startColor)',
    'offsetof(WebglBuffergeometryIndexedWireVertex, endColor)',
    'offsetof(WebglBuffergeometryIndexedWireVertex, lineCoordinate)',
    'setIndexBuffer(indexBuffer)',
    'setVertexBuffer(wireVertexBuffer)',
    'WebglBuffergeometryIndexedIndexCount',
    'WebglBuffergeometryIndexedWireVertexCount',
    'computePass("WebglBuffergeometryIndexedResolve"',
    ...Array.from({ length: 4 }, (_, index) =>
      `WebglBuffergeometryIndexedFilledSample${index}`),
    ...Array.from({ length: 4 }, (_, index) =>
      `WebglBuffergeometryIndexedWireSample${index}`)
  ].join('\n');
}

/** Builds a merged DSL header fixture with all locked classes and source counts. */
function makeSingleHeader() {
  return [
    'static const uint WebglBuffergeometryIndexedVertexCount = 121u',
    'static const uint WebglBuffergeometryIndexedIndexCount = 600u',
    'static const uint WebglBuffergeometryIndexedWireVertexCount = 3600u',
    'class WebglBuffergeometryIndexedFilledPass',
    'class WebglBuffergeometryIndexedWireframePass'
  ].join('\n');
}

test('scenario matrix locks initial, animated, and GUI wireframe captures', () => {
  assert.deepEqual(indexedScenarios.map(({ id, frame, replay, wireframe }) => ({
    id, frame, replay, wireframe
  })), [
    { id: 'initial-filled', frame: 0, replay: false, wireframe: false },
    { id: 'animated-filled', frame: 60, replay: false, wireframe: false },
    { id: 'wireframe', frame: 61, replay: true, wireframe: true }
  ]);
  assert.equal(indexedParityDefinitions.length, 4);
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

test('host arguments add replay only for the wireframe GUI scenario', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/scene.json'
  };
  const filled = buildHostArguments(
    indexedScenarios[0], 'legacy', 'metal', '/assets', '/replay.json', artifacts);
  const wireframe = buildHostArguments(
    indexedScenarios[2], 'experimental', 'vulkan', '/assets', '/replay.json', artifacts);
  assert.equal(filled.includes('--input-replay'), false);
  assert.deepEqual(
    wireframe.slice(wireframe.indexOf('--input-replay')),
    ['--input-replay', '/replay.json']);
  assert.ok(filled.includes('407896069'));
});

test('formal manifest locks one ordinary object and mutually exclusive Scene passes', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find((entry) => entry.id === 'webgl_buffergeometry_indexed');
  assert.deepEqual(validateManifestContract(example), []);
});

test('snapshot contract preserves shared source indices and static wire expansion', () => {
  const filled = buildExpectedSnapshot(indexedScenarios[0]);
  const wireframe = buildExpectedSnapshot(indexedScenarios[2]);
  assert.equal(filled.sourceVertexCount, 121);
  assert.equal(filled.sourceIndexCount, 600);
  assert.equal(filled.wireExpandedVertexCount, 3600);
  assert.equal(filled.scenePassSequence[0].scenePass, 'main-filled');
  assert.equal(wireframe.scenePassSequence[0].scenePass, 'main-wireframe');
  assert.equal(wireframe.wireframe, true);
});

test('clean Oracle digests are immutable and reject one-bit drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial-filled',
    'rgba',
    '23780c1a8d9c5322dca050fa6e0cb5ca07e001de4f49dd716470e4116996dd0d'), []);
  assert.equal(validateOracleSha256('initial-filled', 'rgba', '0'.repeat(64)).length, 1);
  assert.equal(validateOracleSha256('unknown', 'rgba', '0'.repeat(64)).length, 1);
});

test('replay bytes retain the SHA consumed by browser and native hosts', async () => {
  const replayPath = path.join(
    scriptDirectory,
    'Inputs',
    'webgl_buffergeometry_indexed_wireframe.json');
  const digest = createHash('sha256').update(await fs.readFile(replayPath)).digest('hex');
  assert.equal(digest, 'ddb31fa3f0adaad97b5b644f14020d00be88f1537d1b23ce0be0bf2442b55398');
});

test('generated ABI lint accepts identical Legacy and Experimental layouts', () => {
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
    generated.replace('setIndexBuffer(indexBuffer)', ''),
    singleHeader).status, 'fail');
});

test('Experimental stage lint covers vertex, fragment, and compute products', () => {
  const products = [
    { name: 'filled-vertex', stage: 'vertex' },
    { name: 'filled-fragment', stage: 'fragment' },
    { name: 'wire-vertex', stage: 'vertex' },
    { name: 'wire-fragment', stage: 'fragment' },
    { name: 'resolve', stage: 'compute' }
  ].map(({ name, stage }) => ({
    name,
    stage,
    uglirJson: JSON.stringify({ reflection: { stage } }),
    uglirText: `stage ${stage}`,
    msl: stage === 'compute' ? 'kernel void main()' : `${stage} void main()`,
    spirvWords: '0x07230203',
    spirvAssembly: `OpEntryPoint ${stage === 'compute' ? 'GLCompute' : stage[0].toUpperCase() + stage.slice(1)}`
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
