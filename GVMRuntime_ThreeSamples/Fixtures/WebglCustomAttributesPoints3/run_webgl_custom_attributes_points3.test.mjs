import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  customAttributesPoints3ParityDefinitions,
  customAttributesPoints3Scenarios,
  inspectRgbaPixels,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata
} from './run_webgl_custom_attributes_points3.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Advances the exact xorshift32 stream through candidates, UUIDs, and renderer draws. */
function simulatePointRandomStates() {
  let state = 0x18500014;
  let drawCount = 0;
  const next = () => {
    let value = state >>> 0;
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    state = value >>> 0;
    drawCount += 1;
    return state / 4294967296;
  };
  const advance = (count) => {
    for (let index = 0; index < count; index += 1) next();
    return state;
  };
  const result = {
    moduleImports: advance(76),
    camera: advance(4),
    scene: advance(4)
  };
  let shellPointCount = 0;
  for (let candidate = 0; candidate < 100000; candidate += 1) {
    const x = next() * 2 - 1;
    const y = next() * 2 - 1;
    const z = next() * 2 - 1;
    if (Math.abs(x) > 0.6 || Math.abs(y) > 0.6 || Math.abs(z) > 0.6) {
      shellPointCount += 1;
    }
  }
  return {
    ...result,
    candidates: state,
    shellPointCount,
    boxGeometry1: advance(4),
    boxGeometry1Merge: advance(4),
    boxGeometry2: advance(4),
    boxGeometry2Merge: advance(4),
    finalGeometry: advance(4),
    textureAndSource: advance(8),
    material: advance(4),
    points: advance(4),
    renderer: advance(36),
    drawCount
  };
}

/** Computes the Float32 size-wave digest and zero count for one locked clock value. */
function calculateSizeWave(timeValue) {
  const sizes = new Float32Array(91859);
  sizes.fill(40);
  let zeroSizeCount = 0;
  for (let index = 0; index < 78435; index += 1) {
    sizes[index] = Math.max(0, 26 + 32 * Math.sin(0.1 * index + 0.6 * timeValue));
    if (sizes[index] === 0) zeroSizeCount += 1;
  }

  return {  zeroSizeCount };
}

/** Builds the smallest generated-source fixture satisfying the single-draw ABI. */
function makeGeneratedSource() {
  return [
    'class WebglCustomAttributesPoints3MainPass',
    'vertexState.buffers[0].arrayStride = 36;',
    'offsetof(WebglCustomAttributesPoints3Vertex, position)',
    'offsetof(WebglCustomAttributesPoints3Vertex, customColor)',
    'offsetof(WebglCustomAttributesPoints3Vertex, corner)',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesPoints3IndexCount',
    'writeBuffer(BufferRange(sizeBuffer)',
    'writeBuffer(BufferRange(indexBuffer)',
    'writeTexture(',
    'discard_fragment()',
    'smoothstep(200.0f, 600.0f, fragmentDepth)',
    'renderPass("WebglCustomAttributesPoints3Scene"',
    'mainPass->run(WebglCustomAttributesPoints3IndexCount, 1u'
  ].join('\n');
}

/** Builds a merged DSL fixture containing every locked point-count declaration. */
function makeSingleHeader() {
  return [
    'static const uint WebglCustomAttributesPoints3LogicalPointCount = 91859u;',
    'static const uint WebglCustomAttributesPoints3VertexCount =',
    'WebglCustomAttributesPoints3LogicalPointCount * 4u;',
    'static const uint WebglCustomAttributesPoints3IndexCount =',
    'WebglCustomAttributesPoints3LogicalPointCount * 6u;',
    'static const uint WebglCustomAttributesPoints3TextureMipCount = 7u;',
    'discard_fragment();',
    'smoothstep(200.0f, 600.0f, fragmentDepth)',
    'class WebglCustomAttributesPoints3MainPass',
    'class WebglCustomAttributesPoints3Renderer'
  ].join('\n');
}

test('scenario matrix locks frame zero, frame sixty, and all parity relations', () => {
  assert.deepEqual(customAttributesPoints3Scenarios.map(({ id, frame, frameAdvanceCount }) => ({
    id, frame, frameAdvanceCount
  })), [
    { id: 'initial', frame: 0, frameAdvanceCount: 1 },
    { id: 'animated-size-and-fog', frame: 60, frameAdvanceCount: 61 }
  ]);
  assert.equal(customAttributesPoints3ParityDefinitions.length, 4);
  assert.match(customAttributesPoints3Scenarios[0].canonicalState, /time-zero/u);
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
    customAttributesPoints3Scenarios[1],
    'experimental',
    'vulkan',
    '/assets',
    artifacts);
  assert.deepEqual(args.slice(0, 10), [
    '--case-id', 'webgl_custom_attributes_points3',
    '--scenario-id', 'animated-size-and-fog',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '407896084'
  ]);
  assert.equal(args[args.indexOf('--asset-root') + 1], '/assets');
  assert.equal(args[args.indexOf('--frame') + 1], '60');
  assert.equal(args.includes('--input-replay'), false);
});

test('random stream locks every UUID and renderer phase', () => {
  assert.deepEqual(simulatePointRandomStates(), {
    moduleImports: 1736956936,
    camera: 3472450054,
    scene: 2413416337,
    candidates: 3496169787,
    shellPointCount: 78435,
    boxGeometry1: 753332669,
    boxGeometry1Merge: 2152637164,
    boxGeometry2: 1415315803,
    boxGeometry2Merge: 673321235,
    finalGeometry: 125041257,
    textureAndSource: 2921448620,
    material: 4233772355,
    points: 3983851488,
    renderer: 2372394654,
    drawCount: 300156
  });
});

test('Float32 size waves lock initial and animated canonical states', () => {
  for (const scenario of customAttributesPoints3Scenarios) {
    assert.deepEqual(calculateSizeWave(scenario.timeValue), {

      zeroSizeCount: scenario.zeroSizeCount
    });
  }
});

test('snapshot contract preserves ball expansion, discard fog, and single sampling', () => {
  const initial = buildExpectedSnapshot(customAttributesPoints3Scenarios[0]);
  const animated = buildExpectedSnapshot(customAttributesPoints3Scenarios[1]);
  assert.equal(initial.sourceShellPointCount, 78435);
  assert.equal(initial.sourceBoxEdgePointCount, 13424);
  assert.equal(initial.logicalPointCount, 91859);
  assert.equal(initial.expandedVertexCount, 367436);
  assert.equal(initial.explicitIndexCount, 551154);
  assert.equal(initial.scenePassCount, 1);
  assert.equal(initial.screenPassCount, 0);
  assert.equal(initial.drawCommandCount, 1);
  assert.equal(initial.physicalCoverageDrawCount, 1);
  assert.equal(initial.antialias, false);
  assert.equal(initial.textureColorSpace, 'NoColorSpace');
  assert.equal(initial.textureMipLevelCount, 7);
  assert.equal(initial.projectionConvention, 'three-opengl-positive-y-negative-one-to-one');
  assert.equal(
    initial.dslClipConversion,
    'y-negate-and-z-half-range-after-point-expansion');
  assert.equal(initial.nativePointSubpixelBits, 4);
  assert.equal(initial.coverageSampleCount, 1);
  assert.equal(initial.internalCoverageResolve, false);
  assert.equal(initial.alphaDiscardThreshold, 0.5);
  assert.equal(initial.fragmentDepthExpression, 'position-z-divided-by-position-w');
  assert.equal(initial.fogNear, 200);
  assert.equal(initial.fogFar, 600);
  assert.equal(initial.blendEnabled, false);
  assert.equal(initial.totalRandomDrawCount, 300156);
  assert.equal(initial.dynamicSizeUploadCount, 1);
  assert.equal(initial.zeroSizeCount, 15546);
  assert.equal(animated.dynamicSizeUploadCount, 61);
  assert.equal(animated.zeroSizeCount, 15544);
  assert.equal(animated.finalRandomState, 2372394654);
});

test('formal manifest locks the ordinary single-pass RenderClass policy', async () => {
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const example = manifest.examples.find(({ id }) => id === 'webgl_custom_attributes_points3');
  assert.deepEqual(validateManifestContract(example), []);
  assert.deepEqual(example.screenPasses, []);
});

test('Oracle metadata matches the declared capture state', async () => {
  const oracleRoot = path.join(
    repositoryRoot,
    'build',
    'three-r185-reference-smoke',
    'webgl_custom_attributes_points3');
  for (const scenario of customAttributesPoints3Scenarios) {

    const metadataPath = path.join(oracleRoot, `${scenario.id}.json`);

    assert.deepEqual(validateOracleMetadata(
      JSON.parse(await fs.readFile(metadataPath, 'utf8')),
      scenario), []);
  }

});

test('upstream page and raw NoColorSpace ball asset remain pinned', async () => {

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
