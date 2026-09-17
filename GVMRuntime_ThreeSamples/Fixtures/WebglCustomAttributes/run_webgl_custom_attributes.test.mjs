import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  customAttributesParityDefinitions,
  customAttributesScenarios,
  inspectRgbaPixels,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_custom_attributes.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');

/** Returns one lowercase SHA-256 digest for immutable fixture evidence. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

/** Advances the exact typed-array random walk used by the upstream page. */
function simulateTypedArrayWalk(targetFrame) {
  let randomState = 0x18500010;
  let randomDrawCount = 0;
  const noise = new Float32Array(8385);
  const displacement = new Float32Array(8385);
  const nextRandom = () => {
    let value = randomState >>> 0;
    value ^= value << 13;
    value ^= value >>> 17;
    value ^= value << 5;
    randomState = value >>> 0;
    randomDrawCount += 1;
    return (randomState >>> 8) / 16777216;
  };
  // r185 consumes 100 module/object draws before noise, four Mesh UUID draws
  // before frame zero, and 36 lazy renderer draws after frame-zero's walk.
  for (let draw = 0; draw < 100; draw += 1) nextRandom();
  const preNoiseRandomState = randomState;
  for (let index = 0; index < noise.length; index += 1) noise[index] = nextRandom() * 5;
  const initializedRandomState = randomState;
  for (let draw = 0; draw < 4; draw += 1) nextRandom();
  const preFrameRandomState = randomState;
  let virtualTimeMilliseconds = 0;
  let frameZeroWalkRandomState = null;
  for (let frame = 0; frame <= targetFrame; frame += 1) {
    const timeValue = (1_700_000_000_000 + virtualTimeMilliseconds) * 0.01;
    for (let index = 0; index < displacement.length; index += 1) {
      displacement[index] = Math.sin(0.1 * index + timeValue);
      noise[index] += 0.5 * (0.5 - nextRandom());
      noise[index] = Math.max(-5, Math.min(5, noise[index]));
      displacement[index] += noise[index];
    }
    if (frame === 0) {
      frameZeroWalkRandomState = randomState;
      for (let draw = 0; draw < 36; draw += 1) nextRandom();
    }
    virtualTimeMilliseconds += 1000 / 60;
  }
  const probes = [0, 1, 4192, 8384];
  return {
    randomDrawCount,
    preNoiseRandomState,
    initializedRandomState,
    preFrameRandomState,
    frameZeroWalkRandomState,
    finalRandomState: randomState,
    noise: probes.map((index) => noise[index]),
    displacement: probes.map((index) => displacement[index])
  };
}

/** Builds the smallest generated-source fixture satisfying the ordinary indexed ABI. */
function makeGeneratedSource() {
  return [
    'class WebglCustomAttributesMainPass',
    'vertexState.buffers[0].arrayStride = 36;',
    'offsetof(WebglCustomAttributesVertex, position)',
    'offsetof(WebglCustomAttributesVertex, normal)',
    'offsetof(WebglCustomAttributesVertex, texCoord)',
    'offsetof(WebglCustomAttributesVertex, displacement)',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesIndexCount',
    'writeBuffer(BufferRange(vertexBuffer)',
    'writeTexture(',
    'renderPass("WebglCustomAttributesScene"'
  ].join('\n');
}

/** Builds a merged DSL header fixture with every locked source declaration. */
function makeSingleHeader() {
  return [
    'static const uint WebglCustomAttributesVertexCount = 8385u',
    'static const uint WebglCustomAttributesIndexCount = 48384u',
    'static const uint WebglCustomAttributesTextureMipCount = 10u',
    'class WebglCustomAttributesMainPass',
    'class WebglCustomAttributesRenderer'
  ].join('\n');
}

test('scenario matrix locks frame zero and sequential frame sixty', () => {
  assert.deepEqual(customAttributesScenarios.map(({ id, frame, frameAdvanceCount }) => ({
    id, frame, frameAdvanceCount
  })), [
    { id: 'initial', frame: 0, frameAdvanceCount: 1 },
    { id: 'animated', frame: 60, frameAdvanceCount: 61 }
  ]);
  assert.equal(customAttributesParityDefinitions.length, 4);
  assert.ok(!customAttributesScenarios[0].canonicalState.includes('time-zero'));
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
    customAttributesScenarios[1],
    'experimental',
    'vulkan',
    '/assets',
    artifacts);
  assert.deepEqual(args.slice(0, 10), [
    '--case-id', 'webgl_custom_attributes',
    '--scenario-id', 'animated',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '407896080'
  ]);
  assert.ok(args.includes('/assets'));
  assert.equal(args[args.indexOf('--frame') + 1], '60');
  assert.equal(args.includes('--input-replay'), false);
});

test('typed arrays lock both float32 round points and all sequential random draws', () => {
  const initial = simulateTypedArrayWalk(0);
  assert.equal(initial.preNoiseRandomState, 2176856772);
  assert.equal(initial.initializedRandomState, 538802415);
  assert.equal(initial.preFrameRandomState, 1429354707);
  assert.equal(initial.frameZeroWalkRandomState, 1955797493);
  assert.equal(initial.finalRandomState, 3501288780);
  assert.equal(initial.randomDrawCount, 16910);
  assert.deepEqual(initial.noise, [
    2.3557260036468506,
    2.542635440826416,
    3.5297203063964844,
    0.6495635509490967
  ]);
  assert.deepEqual(initial.displacement, [
    1.9746644496917725,
    2.071176290512085,
    4.5120391845703125,
    0.6352694034576416
  ]);

  const frameOne = simulateTypedArrayWalk(1);
  assert.equal(frameOne.finalRandomState, 2689663534);
  assert.equal(frameOne.randomDrawCount, 25295);

  const animated = simulateTypedArrayWalk(60);
  assert.equal(animated.preNoiseRandomState, 2176856772);
  assert.equal(animated.initializedRandomState, 538802415);
  assert.equal(animated.preFrameRandomState, 1429354707);
  assert.equal(animated.frameZeroWalkRandomState, 1955797493);
  assert.equal(animated.finalRandomState, 2053689625);
  assert.equal(animated.randomDrawCount, 520010);
  assert.deepEqual(animated.noise, [
    3.899693489074707,
    2.956455707550049,
    4.497976779937744,
    2.572510004043579
  ]);
  assert.deepEqual(animated.displacement, [
    4.722405910491943,
    3.8318092823028564,
    3.7755894660949707,
    2.0405383110046387
  ]);
});

test('snapshot contract preserves geometry, NoColorSpace mips, and frame uploads', () => {
  const initial = buildExpectedSnapshot(customAttributesScenarios[0]);
  const animated = buildExpectedSnapshot(customAttributesScenarios[1]);
  assert.equal(initial.sphereVertexCount, 8385);
  assert.equal(initial.explicitIndexCount, 48384);
  assert.equal(initial.textureColorSpace, 'NoColorSpace');
  assert.equal(initial.textureMipLevelCount, 10);
  assert.equal(initial.projectionConvention, 'three-opengl-positive-y-negative-one-to-one');
  assert.equal(initial.dslClipConversion, 'y-negate-and-z-half-range-after-projection');
  assert.equal(initial.preNoiseRandomDrawCount, 100);
  assert.equal(initial.preNoiseRandomState, 2176856772);
  assert.equal(initial.preFrameRandomDrawCount, 4);
  assert.equal(initial.firstRenderLazyRandomDrawCount, 36);
  assert.equal(initial.totalRandomDrawCount, 16910);
  assert.equal(initial.shaderOutputTransfer, 'none-custom-shader-does-not-call-linearToOutputTexel');
  assert.equal(initial.dynamicVertexUploadCount, 1);
  assert.equal(animated.dynamicVertexUploadCount, 61);
  assert.equal(animated.totalRandomDrawCount, 520010);
  assert.equal(animated.finalRandomState, 2053689625);
  assert.deepEqual(animated.color, [1, 0.198996293361426, 0]);
});

test('clean Oracle digests and metadata are immutable', async () => {
  const oracleRoot = path.join(
    repositoryRoot,
    'build',
    'three-r185-reference-smoke',
    'webgl_custom_attributes');
  for (const scenario of customAttributesScenarios) {
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

test('upstream page and default NoColorSpace water asset remain pinned', async () => {
  const upstreamRoot = path.join(repositoryRoot, 'build', 'three-r185-upstream');
  assert.equal(
    await sha256File(path.join(upstreamRoot, 'examples', 'webgl_custom_attributes.html')),
    'ff29b98376651e2b6342f930e09c3c52ffc12cb4091158fe5cd436fa25ae2811');
  assert.equal(
    await sha256File(path.join(upstreamRoot, 'examples', 'textures', 'water.jpg')),
    '43847c5fff3f80551dda41c68bb8ebb00938e625c5acb2c11acc4e3221b17402');
});

test('generated ABI lint accepts matching Legacy and Experimental layouts', () => {
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

test('RGBA inspection rejects malformed input and reports background plus opacity', () => {
  assert.deepEqual(inspectRgbaPixels(Uint8Array.from([
    5, 5, 5, 255,
    255, 0, 0, 255
  ])), {
    uniqueRgbColorCount: 2,
    backgroundPixels: 1,
    nonOpaquePixels: 0
  });
  assert.throws(() => inspectRgbaPixels(Uint8Array.from([0, 0, 0])), /RGBA8/u);
});
