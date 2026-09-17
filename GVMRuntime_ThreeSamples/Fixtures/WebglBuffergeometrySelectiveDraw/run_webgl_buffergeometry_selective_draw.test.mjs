import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  parseArguments,
  selectiveDrawParityDefinitions,
  selectiveDrawScenarios,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleSha256
} from './run_webgl_buffergeometry_selective_draw.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Builds the smallest generated source satisfying the expanded-line ABI. */
function makeGeneratedSource() {
  return [
    'class WebglBuffergeometrySelectiveDrawMainPass',
    'vertexState.buffers[0].arrayStride = 60;',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, startPosition)',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, endPosition)',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, startColor)',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, endColor)',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, lineCoordinate)',
    'offsetof(WebglBuffergeometrySelectiveDrawVertex, visible)',
    'setVertexBuffer(vertexBuffer)',
    'updateVisibilityVertices(',
    'WebglBuffergeometrySelectiveDrawExpandedVertexCount',
    'renderPass("WebglBuffergeometrySelectiveDrawSample0"'
  ].join('\n');
}

/** Builds a merged DSL header fixture with all source counts and shader classes. */
function makeSingleHeader() {
  return [
    'static const uint WebglBuffergeometrySelectiveDrawSourceLineCount = 20000u',
    'static const uint WebglBuffergeometrySelectiveDrawSourceVertexCount = 40000u',
    'static const uint WebglBuffergeometrySelectiveDrawExpandedVertexCount = 120000u',
    'class WebglBuffergeometrySelectiveDrawMainPass',
    'discard_fragment();'
  ].join('\n');
}

test('scenario matrix locks visible, animated, and hide-click captures', () => {
  assert.deepEqual(selectiveDrawScenarios.map(
    ({ id, frame, replay, culled, visibleLineCount, culledLineCount }) => ({
      id, frame, replay, culled, visibleLineCount, culledLineCount
    })), [
    {
      id: 'initial-all-visible',
      frame: 0,
      replay: false,
      culled: false,
      visibleLineCount: 20000,
      culledLineCount: 0
    },
    {
      id: 'animated',
      frame: 60,
      replay: false,
      culled: false,
      visibleLineCount: 20000,
      culledLineCount: 0
    },
    {
      id: 'culled',
      frame: 61,
      replay: true,
      culled: true,
      visibleLineCount: 15014,
      culledLineCount: 4986
    }
  ]);
  assert.equal(selectiveDrawParityDefinitions.length, 4);
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

test('host arguments add replay only for the culled scenario', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/scene.json'
  };
  const visible = buildHostArguments(
    selectiveDrawScenarios[0],
    'legacy',
    'metal',
    '/assets',
    '/replay.json',
    artifacts);
  const culled = buildHostArguments(
    selectiveDrawScenarios[2],
    'experimental',
    'vulkan',
    '/assets',
    '/replay.json',
    artifacts);
  assert.equal(visible.includes('--input-replay'), false);
  assert.deepEqual(
    culled.slice(culled.indexOf('--input-replay')),
    ['--input-replay', '/replay.json']);
  assert.ok(visible.includes('407896078'));
});

test('formal manifest locks one ordinary expanded-line Scene pass', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find(
    (entry) => entry.id === 'webgl_buffergeometry_selective_draw');
  assert.deepEqual(validateManifestContract(example), []);
});

test('snapshot contract preserves 20,000 lines and deterministic visibility', () => {
  const visible = buildExpectedSnapshot(selectiveDrawScenarios[0]);
  const culled = buildExpectedSnapshot(selectiveDrawScenarios[2]);
  assert.equal(visible.sourceLineCount, 20000);
  assert.equal(visible.sourceVertexCount, 40000);
  assert.equal(visible.expandedVertexCount, 120000);
  assert.equal(visible.scenePassSequence[0].scenePass, 'main-selective-lines');
  assert.equal(culled.visibleLineCount, 15014);
  assert.equal(culled.culledLineCount, 4986);
  assert.equal(culled.preGeometryRandomCallCount, 88);
  assert.equal(culled.canonicalHidePreVisibilityRandomCallCount, 44);
  assert.equal(culled.canonicalHidePreVisibilityRandomState, 2347915795);
  assert.equal(culled.canonicalHidePostVisibilityRandomState, 248151130);
  assert.equal(visible.vertexBufferUploadCount, 1);
  assert.equal(culled.vertexBufferUploadCount, 2);
  assert.equal(culled.initialVisibilityUploadAllVisible, true);
  assert.equal(culled.replayVisibilityUpdateApplied, true);
  assert.equal(culled.visibilityReplayApplied, true);
});

test('clean Oracle digests are immutable and reject one-bit drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial-all-visible',
    'rgba',
    'd95ba8e02d159d14ed7de83b8bcaa0e827f1e9e65590a31f95ffbe5279861404'), []);
  assert.equal(validateOracleSha256(
    'initial-all-visible', 'rgba', '0'.repeat(64)).length, 1);
  assert.equal(validateOracleSha256(
    'unknown', 'rgba', '0'.repeat(64)).length, 1);
});

test('hide replay and state script retain browser/native SHA provenance', async () => {
  const replayPath = path.join(
    scriptDirectory,
    'Inputs',
    'webgl_buffergeometry_selective_draw_hide.json');
  const stateScriptPath = path.join(
    scriptDirectory,
    'Inputs',
    'webgl_buffergeometry_selective_draw_hide_state.js');
  const replayDigest = createHash('sha256')
    .update(await fs.readFile(replayPath)).digest('hex');
  const stateScriptDigest = createHash('sha256')
    .update(await fs.readFile(stateScriptPath)).digest('hex');
  assert.equal(
    replayDigest,
    '6d38e4787716906c619f68ccfc21b24880ca38d86229e7fc421d656e0792d7b2');
  assert.equal(
    stateScriptDigest,
    'f3be5c6fcbf5bf21fb69248f5369f7bfe76492692ab888f5a61e6968404edf53');
});

test('generated ABI lint accepts identical Legacy and Experimental layouts', () => {
  const generated = makeGeneratedSource();
  const singleHeader = makeSingleHeader();
  const legacy = validateGeneratedSourceContract(
    'legacy', generated, singleHeader);
  const experimental = validateGeneratedSourceContract(
    'experimental', generated, singleHeader);
  assert.equal(legacy.status, 'pass');
  assert.equal(experimental.status, 'pass');
  assert.equal(validateGeneratedAbiParity([legacy, experimental]).status, 'pass');
  assert.equal(validateGeneratedSourceContract(
    'legacy',
    generated.replace('setVertexBuffer(vertexBuffer)', ''),
    singleHeader).status, 'fail');
});

test('Experimental stage lint covers vertex, fragment, and compute products', () => {
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
      `OpEntryPoint ${stage === 'compute'
        ? 'GLCompute'
        : stage[0].toUpperCase() + stage.slice(1)}`
  }));
  assert.deepEqual(validateExperimentalProductSources(products), []);
  products[0].spirvWords = '';
  assert.equal(validateExperimentalProductSources(products).length, 1);
});

test('RGBA inspection rejects malformed input and reports opaque diversity', () => {
  assert.deepEqual(inspectRgbaPixels(Uint8Array.from([
    0, 0, 0, 255,
    255, 0, 0, 255
  ])), {
    uniqueRgbColorCount: 2,
    nonOpaquePixels: 0
  });
  assert.throws(
    () => inspectRgbaPixels(Uint8Array.from([0, 0, 0])),
    /RGBA8/u);
});
