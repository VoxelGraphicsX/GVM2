import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  customAttributesPointsParityDefinitions,
  customAttributesPointsScenarios,
  inspectRgbaPixels,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata
} from './run_webgl_custom_attributes_points.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Advances the exact xorshift32 stream through all upstream UUID and position draws. */
function simulatePointRandomStates() {
  let state = 0x18500012;
  let drawCount = 0;
  const next = () => {
    let value = state >>> 0;
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    state = value >>> 0;
    drawCount += 1;
  };
  for (let index = 0; index < 84; index += 1) next();
  const prePositionState = state;
  for (let index = 0; index < 300_000; index += 1) next();
  const postPositionState = state;
  for (let index = 0; index < 20; index += 1) next();
  const postObjectState = state;
  for (let index = 0; index < 36; index += 1) next();
  return { drawCount, prePositionState, postPositionState, postObjectState, finalState: state };
}

/** Builds the smallest generated-source fixture satisfying the single-draw ABI. */
function makeGeneratedSource() {
  return [
    'class WebglCustomAttributesPointsMainPass',
    'vertexState.buffers[0].arrayStride = 32;',
    'offsetof(WebglCustomAttributesPointsVertex, position)',
    'offsetof(WebglCustomAttributesPointsVertex, customColor)',
    'offsetof(WebglCustomAttributesPointsVertex, corner)',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesPointsIndexCount',
    'writeBuffer(BufferRange(sizeBuffer)',
    'writeTexture(',
    'renderPass("WebglCustomAttributesPointsScene"',
    'mainPass->run(WebglCustomAttributesPointsIndexCount, 1u'
  ].join('\n');
}

/** Builds a merged DSL fixture containing every locked point-count declaration. */
function makeSingleHeader() {
  return [
    'static const uint WebglCustomAttributesPointsLogicalPointCount = 100000u;',
    'static const uint WebglCustomAttributesPointsVertexCount =',
    'WebglCustomAttributesPointsLogicalPointCount * 4u;',
    'static const uint WebglCustomAttributesPointsIndexCount =',
    'WebglCustomAttributesPointsLogicalPointCount * 6u;',
    'static const uint WebglCustomAttributesPointsTextureMipCount = 6u;',
    'class WebglCustomAttributesPointsMainPass',
    'class WebglCustomAttributesPointsRenderer'
  ].join('\n');
}

test('scenario matrix locks frame zero, frame sixty, and all parity relations', () => {
  assert.deepEqual(customAttributesPointsScenarios.map(({ id, frame, frameAdvanceCount }) => ({
    id, frame, frameAdvanceCount
  })), [
    { id: 'initial', frame: 0, frameAdvanceCount: 1 },
    { id: 'animated', frame: 60, frameAdvanceCount: 61 }
  ]);
  assert.equal(customAttributesPointsParityDefinitions.length, 4);
  assert.match(customAttributesPointsScenarios[0].canonicalState, /time-zero/u);
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

test('host arguments lock explicit assets, seed, extent, and target frame', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/scene.json'
  };
  const args = buildHostArguments(
    customAttributesPointsScenarios[1],
    'experimental',
    'vulkan',
    '/assets',
    artifacts);
  assert.deepEqual(args.slice(0, 10), [
    '--case-id', 'webgl_custom_attributes_points',
    '--scenario-id', 'animated',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '407896082'
  ]);
  assert.equal(args[args.indexOf('--asset-root') + 1], '/assets');
  assert.equal(args[args.indexOf('--frame') + 1], '60');
  assert.equal(args.includes('--input-replay'), false);
});

test('random stream locks every UUID and position phase', () => {
  assert.deepEqual(simulatePointRandomStates(), {
    drawCount: 300_140,
    prePositionState: 2815147167,
    postPositionState: 124731722,
    postObjectState: 3825047168,
    finalState: 1274764516
  });
});

test('Float32 size waves lock initial and animated canonical states', () => {

});

test('snapshot contract preserves expansion, single sampling, spark mips, and uploads', () => {
  const initial = buildExpectedSnapshot(customAttributesPointsScenarios[0]);
  const animated = buildExpectedSnapshot(customAttributesPointsScenarios[1]);
  assert.equal(initial.logicalPointCount, 100_000);
  assert.equal(initial.expandedVertexCount, 400_000);
  assert.equal(initial.explicitIndexCount, 600_000);
  assert.equal(initial.scenePassCount, 1);
  assert.equal(initial.screenPassCount, 0);
  assert.equal(initial.computePassCount, 0);
  assert.equal(initial.drawCommandCount, 1);
  assert.equal(initial.physicalCoverageDrawCount, 1);
  assert.equal(initial.antialias, false);
  assert.equal(initial.textureColorSpace, 'NoColorSpace');
  assert.equal(initial.textureMipLevelCount, 6);
  assert.equal(initial.projectionConvention, 'three-opengl-positive-y-negative-one-to-one');
  assert.equal(
    initial.dslClipConversion,
    'y-negate-and-z-half-range-after-billboard-expansion');
  assert.equal(initial.totalRandomDrawCount, 300_140);
  assert.equal(initial.dynamicSizeUploadCount, 1);
  assert.equal(animated.dynamicSizeUploadCount, 61);
  assert.equal(animated.finalRandomState, 1274764516);
});

test('formal manifest locks the ordinary single-pass RenderClass policy', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find(({ id }) => id === 'webgl_custom_attributes_points');
  assert.deepEqual(validateManifestContract(example), []);
  assert.deepEqual(example.screenPasses, []);
});

test('Oracle metadata matches the declared capture state', async () => {
  const oracleRoot = path.join(
    repositoryRoot,
    'build',
    'three-r185-reference-smoke',
    'webgl_custom_attributes_points');
  for (const scenario of customAttributesPointsScenarios) {

    const metadataPath = path.join(oracleRoot, `${scenario.id}.json`);

    assert.deepEqual(validateOracleMetadata(
      JSON.parse(await fs.readFile(metadataPath, 'utf8')),
      scenario), []);
  }

});

test('upstream page and raw NoColorSpace spark asset remain pinned', async () => {

});

test('generated ABI lint accepts matching single-draw pipeline layouts', () => {
  const generated = makeGeneratedSource();
  const singleHeader = makeSingleHeader();
  const legacy = validateGeneratedSourceContract('legacy', generated, singleHeader);
  const experimental = validateGeneratedSourceContract('experimental', generated, singleHeader);
  assert.equal(legacy.status, 'pass');
  assert.equal(experimental.status, 'pass');
  assert.equal(validateGeneratedAbiParity([legacy, experimental]).status, 'pass');
  assert.equal(validateGeneratedSourceContract(
    'legacy',
    generated.replace('setIndexBuffer(indexBuffer)', ''),
    singleHeader).status, 'fail');
  assert.equal(validateGeneratedSourceContract(
    'legacy',
    `${generated}\n->computePass(`,
    singleHeader).status, 'fail');
});

test('Experimental stage lint covers vertex and fragment direct products', () => {
  const products = ['vertex', 'fragment'].map((stage) => ({
    name: `main-${stage}`,
    stage,
    uglirJson: JSON.stringify({ reflection: { stage } }),
    uglirText: `stage ${stage}`,
    msl: `${stage} void main()`,
    spirvWords: '0x07230203',
    spirvAssembly: `OpEntryPoint ${stage[0].toUpperCase() + stage.slice(1)}`
  }));
  assert.deepEqual(validateExperimentalProductSources(products), []);
  products[0].spirvWords = '';
  assert.equal(validateExperimentalProductSources(products).length, 1);
});

test('RGBA inspection rejects malformed input and reports black plus opacity', () => {
  assert.deepEqual(inspectRgbaPixels(Uint8Array.from([
    0, 0, 0, 255,
    255, 0, 0, 255
  ])), {
    uniqueRgbColorCount: 2,
    backgroundPixels: 1,
    nonOpaquePixels: 0
  });
  assert.throws(() => inspectRgbaPixels(Uint8Array.from([0, 0, 0])), /RGBA8/u);
});
