import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
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
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_custom_attributes_points.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Returns one lowercase SHA-256 digest for immutable fixture evidence. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

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

/** Computes the host-native Float32 size-wave digest for one locked clock value. */
function calculateSizeWaveSha256(timeValue) {
  const sizes = new Float32Array(100_000);
  for (let index = 0; index < sizes.length; index += 1) {
    sizes[index] = 14 + 13 * Math.sin(0.1 * index + timeValue);
  }
  return createHash('sha256')
    .update(new Uint8Array(sizes.buffer, sizes.byteOffset, sizes.byteLength))
    .digest('hex');
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
  assert.equal(
    calculateSizeWaveSha256(customAttributesPointsScenarios[0].timeValue),
    customAttributesPointsScenarios[0].sizeSha256);
  assert.equal(
    calculateSizeWaveSha256(customAttributesPointsScenarios[1].timeValue),
    customAttributesPointsScenarios[1].sizeSha256);
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

test('clean Oracle digests and metadata are immutable', async () => {
  const oracleRoot = path.join(
    repositoryRoot,
    'build',
    'three-r185-reference-smoke',
    'webgl_custom_attributes_points');
  for (const scenario of customAttributesPointsScenarios) {
    const rgbaDigest = await sha256File(path.join(oracleRoot, `${scenario.id}.rgba`));
    const metadataPath = path.join(oracleRoot, `${scenario.id}.json`);
    const metadataDigest = await sha256File(metadataPath);
    assert.deepEqual(validateOracleSha256(scenario.id, 'rgba', rgbaDigest), []);
    assert.deepEqual(validateOracleSha256(scenario.id, 'json', metadataDigest), []);
    assert.deepEqual(validateOracleMetadata(
      JSON.parse(await fs.readFile(metadataPath, 'utf8')),
      scenario), []);
  }
  assert.equal(validateOracleSha256('initial', 'rgba', '0'.repeat(64)).length, 1);
});

test('upstream page and raw NoColorSpace spark asset remain pinned', async () => {
  assert.equal(
    await sha256File(path.join(
      repositoryRoot,
      'build',
      'three-r185-upstream',
      'examples',
      'webgl_custom_attributes_points.html')),
    '5316fb3913d1c75d097c1be1808d0ad6cfe3a093da4bd4a7e511f20794245445');
  assert.equal(
    await sha256File(path.join(
      repositoryRoot,
      'build',
      'three-r185-points-assets',
      'textures',
      'sprites',
      'spark1.png')),
    'f79341aaf44ea6c1a912cf5b29e8bd1fc045376f77b8bde8637e07b179a7d67e');
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
