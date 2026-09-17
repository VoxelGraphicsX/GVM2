import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  customAttributesPoints2ParityDefinitions,
  customAttributesPoints2Scenarios,
  inspectRgbaPixels,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_custom_attributes_points2.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Returns one lowercase SHA-256 digest for immutable fixture evidence. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

/** Advances the exact xorshift32 stream through all audited UUID and renderer draws. */
function simulatePointRandomStates() {
  let state = 0x18500013;
  let drawCount = 0;
  const next = () => {
    let value = state >>> 0;
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    state = value >>> 0;
    drawCount += 1;
  };
  const advance = (count) => {
    for (let index = 0; index < count; index += 1) next();
    return state;
  };
  return {
    moduleImports: advance(76),
    camera: advance(4),
    scene: advance(4),
    sphereGeometry: advance(4),
    boxGeometry: advance(4),
    sphereMerge: advance(4),
    boxMerge: advance(4),
    geometryMerge: advance(4),
    finalGeometry: advance(4),
    textureAndSource: advance(8),
    material: advance(4),
    points: advance(4),
    renderer: advance(36),
    drawCount
  };
}

/** Computes the host-native Float32 size-wave digest for one locked clock value. */
function calculateSizeWaveSha256(timeValue) {
  const sizes = new Float32Array(3120);
  sizes.fill(40);
  for (let index = 0; index < 2518; index += 1) {
    sizes[index] = 16 + 12 * Math.sin(0.1 * index + timeValue);
  }
  return createHash('sha256')
    .update(new Uint8Array(sizes.buffer, sizes.byteOffset, sizes.byteLength))
    .digest('hex');
}

/** Builds the smallest generated-source fixture satisfying the single-draw ABI. */
function makeGeneratedSource() {
  return [
    'class WebglCustomAttributesPoints2MainPass',
    'vertexState.buffers[0].arrayStride = 32;',
    'offsetof(WebglCustomAttributesPoints2Vertex, position)',
    'offsetof(WebglCustomAttributesPoints2Vertex, customColor)',
    'offsetof(WebglCustomAttributesPoints2Vertex, corner)',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesPoints2IndexCount',
    'writeBuffer(BufferRange(sizeBuffer)',
    'writeBuffer(BufferRange(indexBuffer)',
    'writeTexture(',
    'renderPass("WebglCustomAttributesPoints2Scene"',
    'mainPass->run(WebglCustomAttributesPoints2IndexCount, 1u'
  ].join('\n');
}

/** Builds a merged DSL fixture containing every locked point-count declaration. */
function makeSingleHeader() {
  return [
    'static const uint WebglCustomAttributesPoints2LogicalPointCount = 3120u;',
    'static const uint WebglCustomAttributesPoints2VertexCount =',
    'WebglCustomAttributesPoints2LogicalPointCount * 4u;',
    'static const uint WebglCustomAttributesPoints2IndexCount =',
    'WebglCustomAttributesPoints2LogicalPointCount * 6u;',
    'static const uint WebglCustomAttributesPoints2TextureMipCount = 6u;',
    'class WebglCustomAttributesPoints2MainPass',
    'class WebglCustomAttributesPoints2Renderer'
  ].join('\n');
}

test('scenario matrix locks frame zero, frame sixty, and all parity relations', () => {
  assert.deepEqual(customAttributesPoints2Scenarios.map(({ id, frame, frameAdvanceCount }) => ({
    id, frame, frameAdvanceCount
  })), [
    { id: 'initial', frame: 0, frameAdvanceCount: 1 },
    { id: 'animated-sorted-index', frame: 60, frameAdvanceCount: 61 }
  ]);
  assert.equal(customAttributesPoints2ParityDefinitions.length, 4);
  assert.match(customAttributesPoints2Scenarios[0].canonicalState, /time-zero/u);
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
    customAttributesPoints2Scenarios[1],
    'experimental',
    'vulkan',
    '/assets',
    artifacts);
  assert.deepEqual(args.slice(0, 10), [
    '--case-id', 'webgl_custom_attributes_points2',
    '--scenario-id', 'animated-sorted-index',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '407896083'
  ]);
  assert.equal(args[args.indexOf('--asset-root') + 1], '/assets');
  assert.equal(args[args.indexOf('--frame') + 1], '60');
  assert.equal(args.includes('--input-replay'), false);
});

test('random stream locks every UUID and renderer phase', () => {
  assert.deepEqual(simulatePointRandomStates(), {
    moduleImports: 2099579701,
    camera: 1729371842,
    scene: 2893702495,
    sphereGeometry: 2970501421,
    boxGeometry: 4173600148,
    sphereMerge: 2818997995,
    boxMerge: 830588404,
    geometryMerge: 3791256602,
    finalGeometry: 775777594,
    textureAndSource: 3480931377,
    material: 1806173057,
    points: 1907300212,
    renderer: 2509245698,
    drawCount: 160
  });
});

test('Float32 size waves lock initial and animated canonical states', () => {
  assert.equal(
    calculateSizeWaveSha256(customAttributesPoints2Scenarios[0].timeValue),
    customAttributesPoints2Scenarios[0].sizeSha256);
  assert.equal(
    calculateSizeWaveSha256(customAttributesPoints2Scenarios[1].timeValue),
    customAttributesPoints2Scenarios[1].sizeSha256);
});

test('snapshot contract preserves expansion, single sampling, disc mips, and stale sorting', () => {
  const initial = buildExpectedSnapshot(customAttributesPoints2Scenarios[0]);
  const animated = buildExpectedSnapshot(customAttributesPoints2Scenarios[1]);
  assert.equal(initial.logicalPointCount, 3120);
  assert.equal(initial.expandedVertexCount, 12480);
  assert.equal(initial.explicitIndexCount, 18720);
  assert.equal(initial.scenePassCount, 1);
  assert.equal(initial.screenPassCount, 0);
  assert.equal(initial.drawCommandCount, 1);
  assert.equal(initial.physicalCoverageDrawCount, 1);
  assert.equal(initial.antialias, false);
  assert.equal(initial.textureColorSpace, 'NoColorSpace');
  assert.equal(initial.textureMipLevelCount, 6);
  assert.equal(initial.projectionConvention, 'three-opengl-positive-y-negative-one-to-one');
  assert.equal(
    initial.dslClipConversion,
    'y-negate-and-z-half-range-after-point-expansion');
  assert.equal(initial.nativePointSubpixelBits, 4);
  assert.equal(initial.coverageSampleCount, 1);
  assert.equal(initial.internalCoverageResolve, false);
  assert.equal(initial.totalRandomDrawCount, 160);
  assert.equal(initial.dynamicSizeUploadCount, 1);
  assert.equal(initial.dynamicIndexUploadCount, 1);
  assert.equal(initial.sortUsesIdentityCameraMatrix, true);
  assert.equal(initial.sortMatrixLagFrames, 0);
  assert.equal(animated.dynamicSizeUploadCount, 61);
  assert.equal(animated.dynamicIndexUploadCount, 61);
  assert.equal(animated.sortMatrixLagFrames, 1);
  assert.equal(animated.finalRandomState, 2509245698);
});

test('formal manifest locks the ordinary single-pass RenderClass policy', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find(({ id }) => id === 'webgl_custom_attributes_points2');
  assert.deepEqual(validateManifestContract(example), []);
  assert.deepEqual(example.screenPasses, []);
});

test('clean Oracle digests and metadata are immutable', async () => {
  const oracleRoot = path.join(
    repositoryRoot,
    'build',
    'three-r185-reference-smoke',
    'webgl_custom_attributes_points2');
  for (const scenario of customAttributesPoints2Scenarios) {
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

test('upstream page and raw NoColorSpace disc asset remain pinned', async () => {
  assert.equal(
    await sha256File(path.join(
      repositoryRoot,
      'build',
      'three-r185-upstream',
      'examples',
      'webgl_custom_attributes_points2.html')),
    'f8550792d1801617a32520bfbeb22d443dcca9ce8143c6b1e0e98c9199eaaf51');
  assert.equal(
    await sha256File(path.join(
      repositoryRoot,
      'build',
      'three-r185-points-upstream-probe',
      'examples',
      'textures',
      'sprites',
      'disc.png')),
    '5429d0ce673fe08a0dc8bc852728ebd624bf8677650fa74f2c98c861e0de3721');
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
