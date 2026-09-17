#!/usr/bin/env node

import { execFile } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { promisify } from 'node:util';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { executeWithWatchdog } from '../../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const execFileAsync = promisify(execFile);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const shardName = 'WebglCustomAttributesPoints';
const caseId = 'webgl_custom_attributes_points';
const randomSeed = 0x18500012;
const repeatCount = 3;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = '5316fb3913d1c75d097c1be1808d0ad6cfe3a093da4bd4a7e511f20794245445';
const sparkAssetSha256 = 'f79341aaf44ea6c1a912cf5b29e8bd1fc045376f77b8bde8637e07b179a7d67e';

export const customAttributesPointsScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    canonicalState: 'seed-0x18500012-time-zero-100000-noninstanced-spark-billboards-after-size-wave-update',
    virtualTimeMilliseconds: 0,
    nextFrameTimeMilliseconds: 1000 / 60,
    timeValue: 8_500_000_000,
    rotationZ: 85_000_000,
    frameAdvanceCount: 1,
    finalRandomState: 1274764516,
    sizeSha256: '8da63c3f661ea04611000a70ee66f408c0a258758fb3db7e59328718eae122dc'
  }),
  Object.freeze({
    id: 'animated',
    frame: 60,
    canonicalState: 'seed-0x18500012-fixed-step-60hz-one-second-size-wave-and-z-rotation',
    virtualTimeMilliseconds: 999.9999999999991,
    nextFrameTimeMilliseconds: 1016.6666666666657,
    timeValue: 8_500_000_005,
    rotationZ: 85_000_000.05,
    frameAdvanceCount: 61,
    finalRandomState: 1274764516,
    sizeSha256: 'd0c384965667d71149dcb1d1e671f9718a0316fece1eb4a7b291235b8d45f94f'
  })
]);

export const customAttributesPointsParityDefinitions = Object.freeze([
  Object.freeze({
    relation: 'pipeline-parity-metal',
    left: Object.freeze(['legacy', 'metal']),
    right: Object.freeze(['experimental', 'metal'])
  }),
  Object.freeze({
    relation: 'pipeline-parity-vulkan',
    left: Object.freeze(['legacy', 'vulkan']),
    right: Object.freeze(['experimental', 'vulkan'])
  }),
  Object.freeze({
    relation: 'backend-parity-legacy',
    left: Object.freeze(['legacy', 'metal']),
    right: Object.freeze(['legacy', 'vulkan'])
  }),
  Object.freeze({
    relation: 'backend-parity-experimental',
    left: Object.freeze(['experimental', 'metal']),
    right: Object.freeze(['experimental', 'vulkan'])
  })
]);

const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: '53fc74d14d6b36ad8eb4186343667d3dea156c5377ec54ae4bb2c6cf32ab8687',
    json: '668ab07aa0acbd64dc8c0fc0c1f903f0cedc7568ae8b848ee3579ffb6b034c95'
  }),
  animated: Object.freeze({
    rgba: '7db2e9701d6eb14459bd52edc44d7cf94486dad7663c3fd6af18c8c1f12c6f75',
    json: '13f1f9f70ac58b19a6db023096e0a129c0bd0637bdfa0fc567ba05c7c2c64726'
  })
});

/** Parses strict unique value-bearing long options accepted by this fixture. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = value;
  }
  return options;
}

/** Returns one required absolute path option without environment fallbacks. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one optional positive integer with an explicit fallback. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) return fallback;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Returns whether one resolved artifact exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Returns one explicit file's lowercase SHA-256 digest. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

/** Compares expected nested fields while allowing additional diagnostics. */
export function validateExpectedFields(actual, expected, label, failures) {
  for (const [name, expectedValue] of Object.entries(expected)) {
    const actualValue = actual?.[name];
    const fieldLabel = `${label}.${name}`;
    if (Array.isArray(expectedValue)) {
      if (!Array.isArray(actualValue) || actualValue.length !== expectedValue.length) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
        continue;
      }
      for (let index = 0; index < expectedValue.length; index += 1) {
        const expectedElement = expectedValue[index];
        const actualElement = actualValue[index];
        if (expectedElement !== null && typeof expectedElement === 'object') {
          validateExpectedFields(actualElement, expectedElement, `${fieldLabel}[${index}]`, failures);
        } else if (typeof expectedElement === 'number' && !Number.isInteger(expectedElement)) {
          if (typeof actualElement !== 'number' || Math.abs(actualElement - expectedElement) > 1e-6) {
            failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${expectedElement}.`);
          }
        } else if (actualElement !== expectedElement) {
          failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${JSON.stringify(expectedElement)}.`);
        }
      }
    } else if (expectedValue !== null && typeof expectedValue === 'object') {
      if (actualValue === null || typeof actualValue !== 'object' || Array.isArray(actualValue)) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected an object.`);
      } else {
        validateExpectedFields(actualValue, expectedValue, fieldLabel, failures);
      }
    } else if (typeof expectedValue === 'number' && !Number.isInteger(expectedValue)) {
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-6) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Verifies the formal ordinary indexed RenderClass and sequential-frame contract. */
export function validateManifestContract(example) {
  const failures = [];
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_custom_attributes_points.html',
    status: 'phase1_required',
    capabilityAudit: {
      state: 'supported',
      gpuWorkDslOnly: true,
      requiresNewPublicCapability: false,
      missingCapabilities: []
    },
    renderSetPolicy: 'not-required',
    renderSetReasons: [],
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    scenePasses: [{
      name: 'main-spark-points',
      renderClass: 'WebglCustomAttributesPointsMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: shardName,
    scenarios: customAttributesPointsScenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      inputReplay: null,
      scenePassInvocations: [{
        sceneRoot: 'scene',
        scenePass: 'main-spark-points',
        invocationCount: 1
      }],
      scenePassSequence: [{
        sceneRoot: 'scene',
        scenePass: 'main-spark-points',
        entityOrdinal: 0
      }]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const available = new Set(example?.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'sample_private_dsl_shader_rewrite',
    'ordinary_indexed_triangle_rendering',
    'dynamic_vertex_buffer_update',
    'texture2d_sampling',
    'noninstanced_triangle_billboard_point_expansion',
    'depth_attenuated_billboard_size_math',
    'local_billboard_uv',
    'additive_blending',
    'depth_test_disable',
    'deterministic_cpu_random_stream'
  ]) {
    if (!available.has(capability)) {
      failures.push(`manifest.example capabilityAudit is missing '${capability}'.`);
    }
  }
  return failures;
}

/** Builds the exact host CLI for one deterministic quadrant repetition. */
export function buildHostArguments(scenario, pipeline, backend, assetRoot, artifacts) {
  return [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(randomSeed),
    '--asset-root', assetRoot,
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
}

/** Builds the runtime structural fields fixed for one canonical scenario. */
export function buildExpectedSnapshot(scenario) {
  return {
    schemaVersion: 1,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    upstreamRevision: 'r185',
    canonicalState: scenario.canonicalState,
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    scenePassCount: 1,
    logicalScenePassCount: 1,
    screenPassCount: 0,
    computePassCount: 0,
    drawCommandCount: 1,
    logicalDrawCommandCount: 1,
    physicalCoverageDrawCount: 1,
    logicalPointCount: 100000,
    expandedVertexCount: 400000,
    explicitIndexCount: 600000,
    expandedTriangleCount: 200000,
    standaloneGeometryBufferCount: 2,
    dynamicSizeBufferCount: 1,
    primitiveTopology: 'triangle-list',
    pointExpansion: 'four-corners-six-indices-noninstanced',
    projectionConvention: 'three-opengl-positive-y-negative-one-to-one',
    dslClipConversion: 'y-negate-and-z-half-range-after-billboard-expansion',
    antialias: false,
    dynamicAttribute: 'size',
    dynamicSizeUploadCount: scenario.frameAdvanceCount,
    sequentialFrameAdvanceCount: scenario.frameAdvanceCount,
    renderedVirtualTimeMs: scenario.virtualTimeMilliseconds,
    timeValue: scenario.timeValue,
    rotationZ: scenario.rotationZ,
    sizeSha256: scenario.sizeSha256,
    positionSha256: '7cc037d18d7e4574f1e9fed3da1190962ce414c64ea175d94f02a6ea06861ca9',
    colorSha256: 'd47d91e3f1b739b0bd8b0b799c43964e56686d67ebb3056faf593e9ba100fd56',
    seed: randomSeed,
    prePositionRandomDrawCount: 84,
    prePositionRandomState: 2815147167,
    positionRandomDrawCount: 300000,
    postPositionRandomState: 124731722,
    postPositionObjectRandomDrawCount: 20,
    postObjectRandomState: 3825047168,
    firstRenderLazyRandomDrawCount: 36,
    totalRandomDrawCount: 300140,
    finalRandomState: scenario.finalRandomState,
    texturePath: 'textures/sprites/spark1.png',
    textureAssetSha256: sparkAssetSha256,
    textureColorSpace: 'NoColorSpace',
    textureFormat: 'rgba8unorm',
    textureExtent: 32,
    textureMipLevelCount: 6,
    textureBaseGpuBytesSha256: '645fc710eba86744a7cba29ca6bcfcd7bafceab2612901bd50beecf74a48c16e',
    textureFlipY: true,
    textureHiddenRgbPreserved: true,
    mipmapGeneration: 'explicit-cpu-raw-unorm-box-filter',
    samplerAddressMode: 'clamp-to-edge',
    samplerMinMagFilter: 'linear',
    samplerMipmapFilter: 'linear',
    blendColor: 'src-alpha-plus-one',
    blendAlpha: 'one-plus-one',
    depthTest: false,
    depthWrite: false,
    rendererOutputColorSpace: 'srgb',
    shaderOutputTransfer: 'none-custom-shader-does-not-call-linearToOutputTexel',
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main-spark-points',
      entityOrdinal: 0
    }],
    gpuWorkDslOnly: true
  };
}

/** Validates clean Oracle metadata against one locked deterministic scenario. */
export function validateOracleMetadata(metadata, scenario) {
  const failures = [];
  validateExpectedFields(metadata, {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    randomSeed,
    randomState: scenario.finalRandomState,
    virtualTimeMs: scenario.virtualTimeMilliseconds,
    nextFrameTimeMs: scenario.nextFrameTimeMilliseconds,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: null
  }, 'oracle.metadata', failures);
  return failures;
}

/** Rejects an Oracle digest that differs from the locked clean capture. */
export function validateOracleSha256(scenarioId, extension, actualSha256) {
  const expectedSha256 = lockedOracleSha256[scenarioId]?.[extension];
  if (expectedSha256 === undefined) {
    return [`No locked Oracle SHA-256 exists for ${scenarioId}.${extension}.`];
  }
  return actualSha256 === expectedSha256
    ? []
    : [`Oracle ${scenarioId}.${extension} SHA-256=${actualSha256}, expected ${expectedSha256}.`];
}

/** Extracts the ordinary indexed point-expansion ABI exposed by one pipeline. */
function extractGeneratedAbiSignature(generatedSource) {
  return {
    vertexStrideCount: (generatedSource.match(/arrayStride\s*=\s*32;/gu) ?? []).length,
    attributeOffsets: ['position', 'customColor', 'corner'].map((name) =>
      generatedSource.includes(`offsetof(WebglCustomAttributesPointsVertex, ${name})`)),
    hasIndexBinding: generatedSource.includes('setIndexBuffer(indexBuffer)'),
    hasVertexBinding: generatedSource.includes('setVertexBuffer(vertexBuffer)'),
    hasDynamicSizeUpload: /writeBuffer\((?:GVM::RHI::)?BufferRange\(sizeBuffer\)/u.test(generatedSource),
    explicitIndexedDrawCount: (
      generatedSource.match(/mainPass->run\(WebglCustomAttributesPointsIndexCount,\s*1u/gu) ?? []
    ).length,
    sceneRenderPassCount: (
      generatedSource.match(/renderPass\("WebglCustomAttributesPointsScene"/gu) ?? []
    ).length,
    computePassCount: (generatedSource.match(/->computePass\(/gu) ?? []).length
  };
}

/** Lints one generated pipeline for the frozen ordinary indexed material ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, singleHeader) {
  const failures = [];
  const label = `${shardName}/${pipeline}`;
  for (const token of [
    'class WebglCustomAttributesPointsMainPass',
    'arrayStride = 32',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesPointsIndexCount',
    'writeTexture(',
    'renderPass("WebglCustomAttributesPointsScene"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated source omits '${token}'.`);
    }
  }
  if (!/writeBuffer\((?:GVM::RHI::)?BufferRange\(sizeBuffer\)/u.test(generatedSource)) {
    failures.push(`${label}: generated source omits the dynamic logical-size upload.`);
  }
  for (const forbidden of [
    'createRenderSet<',
    'drawIndexedIndirect(',
    'mRenderSetBindGroupIndex',
    '->computePass('
  ]) {
    if (generatedSource.includes(forbidden)) {
      failures.push(`${label}: ordinary Scene unexpectedly contains '${forbidden}'.`);
    }
  }
  for (const token of [
    'static const uint WebglCustomAttributesPointsLogicalPointCount = 100000u;',
    'WebglCustomAttributesPointsLogicalPointCount * 4u;',
    'WebglCustomAttributesPointsLogicalPointCount * 6u;',
    'static const uint WebglCustomAttributesPointsTextureMipCount = 6u;',
    'class WebglCustomAttributesPointsMainPass',
    'class WebglCustomAttributesPointsRenderer'
  ]) {
    if (!singleHeader.includes(token)) {
      failures.push(`${label}: merged DSL omits '${token}'.`);
    }
  }
  const abiSignature = extractGeneratedAbiSignature(generatedSource);
  if (abiSignature.vertexStrideCount !== 1 ||
      abiSignature.attributeOffsets.includes(false) ||
      !abiSignature.hasIndexBinding || !abiSignature.hasVertexBinding ||
      !abiSignature.hasDynamicSizeUpload ||
      abiSignature.explicitIndexedDrawCount !== 1 ||
      abiSignature.sceneRenderPassCount !== 1 ||
      abiSignature.computePassCount !== 0) {
    failures.push(`${label}: generated vertex-layout ABI is incomplete.`);
  }
  return {
    pipeline,
    status: failures.length === 0 ? 'pass' : 'fail',
    abiSignature,
    failures
  };
}

/** Verifies Legacy and Experimental expose identical ordinary vertex layouts. */
export function validateGeneratedAbiParity(entries) {
  const failures = [];
  if (entries.length !== 2 || entries.some((entry) => entry.status !== 'pass')) {
    failures.push('Both generated pipelines must pass before ABI comparison.');
  } else if (JSON.stringify(entries[0].abiSignature) !==
             JSON.stringify(entries[1].abiSignature)) {
    failures.push('Legacy and Experimental custom-attribute-point ABI signatures differ.');
  }
  return { status: failures.length === 0 ? 'pass' : 'fail', failures };
}

/** Validates one Experimental UGLIR, MSL, and direct-SPIR-V product family. */
export function validateExperimentalProductSources(products) {
  const failures = [];
  for (const product of products) {
    let uglir = null;
    try {
      uglir = JSON.parse(product.uglirJson);
    } catch {
      failures.push(`${product.name}: UGLIR JSON is invalid.`);
    }
    if (uglir?.reflection?.stage !== product.stage) {
      failures.push(`${product.name}: UGLIR reflection stage differs.`);
    }
    if (!product.uglirText.includes(`stage ${product.stage}`)) {
      failures.push(`${product.name}: UGLIR text stage is missing.`);
    }
    if (!product.msl.includes(`${product.stage} `)) {
      failures.push(`${product.name}: MSL ${product.stage} entry is missing.`);
    }
    if (!product.spirvWords.includes('0x07230203')) {
      failures.push(`${product.name}: direct-SPIR-V word magic is missing.`);
    }
    const spirvStage = product.stage[0].toUpperCase() + product.stage.slice(1);
    if (!product.spirvAssembly.includes(`OpEntryPoint ${spirvStage}`)) {
      failures.push(`${product.name}: SPIR-V ${product.stage} entry is missing.`);
    }
  }
  return failures;
}

/** Loads and lints both generated families plus both Experimental shader stages. */
async function lintGeneratedArtifacts(context) {
  const entries = [];
  for (const pipeline of pipelines) {
    const root = path.join(context.generatedRoot, pipeline, shardName, 'UGLBin');
    const generatedPath = path.join(root, 'generate_result.hpp');
    const singleHeaderPath = path.join(root, 'dsl_single_header.hpp');
    let entry;
    if (await pathExists(generatedPath) && await pathExists(singleHeaderPath)) {
      entry = validateGeneratedSourceContract(
        pipeline,
        await fs.readFile(generatedPath, 'utf8'),
        await fs.readFile(singleHeaderPath, 'utf8'));
    } else {
      entry = {
        pipeline,
        status: 'fail',
        abiSignature: {},
        failures: [`Missing generated artifacts under ${root}.`]
      };
    }
    entry.generatedRoot = root;
    if (pipeline === 'experimental') {
      const products = [];
      for (const [name, stage] of [
        ['WebglCustomAttributesPointsMainPass__vertex', 'vertex'],
        ['WebglCustomAttributesPointsMainPass__fragment', 'fragment']
      ]) {
        const paths = {
          uglirJson: path.join(root, 'uglir', `${name}.uglir.json`),
          uglirText: path.join(root, 'uglir', `${name}.uglir.txt`),
          msl: path.join(root, 'msl', `${name}.msl`),
          spirvWords: path.join(root, 'spv', `${name}.raw.spv.txt`),
          spirvAssembly: path.join(root, 'spv', `${name}.raw.spvasm`)
        };
        const product = { name, stage };
        for (const [field, targetPath] of Object.entries(paths)) {
          product[field] = await pathExists(targetPath)
            ? await fs.readFile(targetPath, 'utf8')
            : '';
        }
        products.push(product);
      }
      entry.failures.push(...validateExperimentalProductSources(products));
      entry.experimentalProductCount = products.length;
      entry.status = entry.failures.length === 0 ? 'pass' : 'fail';
    }
    entries.push(entry);
  }
  const abiParity = validateGeneratedAbiParity(entries);
  const failures = [...entries.flatMap((entry) => entry.failures), ...abiParity.failures];
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    pipelines: entries,
    abiParity,
    failures
  };
}

/** Inspects one RGBA8 image for visible opaque output. */
export function inspectRgbaPixels(pixels) {
  if (!(pixels instanceof Uint8Array) || pixels.length % 4 !== 0) {
    throw new Error('Capture is not tightly packed RGBA8.');
  }
  let nonOpaquePixels = 0;
  let backgroundPixels = 0;
  const colors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const packed = (pixels[offset] << 16) | (pixels[offset + 1] << 8) | pixels[offset + 2];
    colors.add(packed);
    if (packed === 0x000000) backgroundPixels += 1;
    if (pixels[offset + 3] !== 255) nonOpaquePixels += 1;
  }
  return { uniqueRgbColorCount: colors.size, backgroundPixels, nonOpaquePixels };
}

/** Creates deterministic artifact paths for one quadrant repetition. */
function makeArtifactPaths(context, scenario, pipeline, backend, repetition) {
  const artifactDirectory = path.join(
    context.outputRoot,
    'artifacts',
    scenario.id,
    pipeline,
    backend,
    `run-${repetition}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Validates one successful host's metadata, structure, image, and Oracle distance. */
async function validateRunArtifacts(context, scenario, pipeline, backend, artifacts) {
  const failures = [];
  const [actual, snapshot, oracle] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8').then(JSON.parse),
    loadRgbaArtifact(
      path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      path.join(context.oracleRoot, caseId, `${scenario.id}.json`))
  ]);
  validateExpectedFields(actual.metadata, {
    schemaVersion: 1,
    source: 'gvm-three-r185',
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    randomSeed,
    frame: scenario.frame,
    virtualTimeMs: scenario.virtualTimeMilliseconds,
    nextFrameTimeMs: scenario.nextFrameTimeMilliseconds,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);
  const inspection = inspectRgbaPixels(actual.pixels);
  if (inspection.uniqueRgbColorCount < 2) failures.push('Capture is clear-only.');
  if (inspection.backgroundPixels === 0) failures.push('Capture omits the black Scene background.');
  if (inspection.nonOpaquePixels !== 0) {
    failures.push(`Capture contains ${inspection.nonOpaquePixels} non-opaque pixels.`);
  }
  const oracleComparison = compareThreeCaptures(oracle, actual);
  failures.push(...oracleComparison.failures.map((failure) => `Oracle: ${failure}`));
  return { inspection, oracleComparison, failures };
}

/** Runs and validates one scenario/pipeline/backend repetition. */
async function runQuadrantRepetition(context, scenario, pipeline, backend, repetition) {
  const artifacts = makeArtifactPaths(context, scenario, pipeline, backend, repetition);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
  const args = buildHostArguments(scenario, pipeline, backend, context.assetRoot, artifacts);
  const processResult = await executeWithWatchdog(executable, args, {
    cwd: repositoryRoot,
    timeoutMs: context.timeoutMs
  });
  await fs.writeFile(
    artifacts.hostLogPath,
    `${JSON.stringify({ executable, args, ...processResult }, null, 2)}\n`);
  const failures = [];
  if (processResult.timedOut) {
    failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  } else if (processResult.exitCode !== 0) {
    failures.push(`Host exited with code ${processResult.exitCode}: ${processResult.stderr}`);
  }
  let validation = null;
  if (failures.length === 0) {
    try {
      validation = await validateRunArtifacts(context, scenario, pipeline, backend, artifacts);
      failures.push(...validation.failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  const artifactSha256 = failures.length === 0 ? {
    rgba: await sha256File(artifacts.rgbaPath),
    metadata: await sha256File(artifacts.metadataPath),
    snapshot: await sha256File(artifacts.snapshotPath)
  } : null;
  return {
    scenarioId: scenario.id,
    pipeline,
    backend,
    repetition,
    status: failures.length === 0 ? 'pass' : 'fail',
    executable,
    arguments: args,
    process: processResult,
    artifacts,
    artifactSha256,
    inspection: validation?.inspection ?? null,
    oracleMetrics: validation?.oracleComparison.metrics ?? null,
    failures
  };
}

/** Loads one successful repetition image. */
async function loadRunImage(run) {
  return loadRgbaArtifact(run.artifacts.rgbaPath, run.artifacts.metadataPath);
}

/** Requires all RGBA, metadata, and snapshot repetitions to be byte exact. */
async function compareStability(runs) {
  const failures = [];
  const comparisons = [];
  if (runs.length !== repeatCount || runs.some((run) => run.status !== 'pass')) {
    failures.push('Three successful repetitions are required for stability.');
  } else {
    const baseline = await loadRunImage(runs[0]);
    for (let index = 1; index < runs.length; index += 1) {
      const candidate = await loadRunImage(runs[index]);
      const imageComparison = compareThreeCaptures(baseline, candidate);
      const digestEquality = {
        rgba: runs[index].artifactSha256.rgba === runs[0].artifactSha256.rgba,
        metadata: runs[index].artifactSha256.metadata === runs[0].artifactSha256.metadata,
        snapshot: runs[index].artifactSha256.snapshot === runs[0].artifactSha256.snapshot
      };
      for (const [artifact, equal] of Object.entries(digestEquality)) {
        if (!equal) failures.push(`Run ${index + 1} ${artifact} SHA differs from run 1.`);
      }
      failures.push(...imageComparison.failures.map(
        (failure) => `run1-vs-run${index + 1}: ${failure}`));
      comparisons.push({
        relation: `run-1-vs-run-${index + 1}`,
        status: Object.values(digestEquality).every(Boolean) &&
          imageComparison.failures.length === 0 ? 'pass' : 'fail',
        digestEquality,
        metrics: imageComparison.metrics
      });
    }
  }
  return {
    scenarioId: runs[0]?.scenarioId,
    pipeline: runs[0]?.pipeline,
    backend: runs[0]?.backend,
    status: failures.length === 0 ? 'pass' : 'fail',
    comparisons,
    failures
  };
}

/** Compares the four mandatory pipeline/backend relations for one scenario. */
async function compareScenarioParity(scenario, canonicalRuns) {
  const comparisons = [];
  for (const definition of customAttributesPointsParityDefinitions) {
    const left = canonicalRuns.get(definition.left.join('/'));
    const right = canonicalRuns.get(definition.right.join('/'));
    const failures = [];
    let metrics = null;
    if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
      failures.push('Both canonical quadrants must pass before parity comparison.');
    } else {
      const comparison = compareThreeCaptures(await loadRunImage(left), await loadRunImage(right));
      metrics = comparison.metrics;
      failures.push(...comparison.failures);
    }
    comparisons.push({
      scenarioId: scenario.id,
      relation: definition.relation,
      status: failures.length === 0 ? 'pass' : 'fail',
      metrics,
      failures
    });
  }
  return comparisons;
}

/** Locks source, spark asset, executables, and both clean Oracle artifacts. */
async function validateInputs(context, manifest) {
  const failures = [];
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  failures.push(...validateManifestContract(example));
  const pendingSharedLockUpdates = customAttributesPointsScenarios.flatMap((scenario) => {
    const lockedScenario = example?.scenarios?.find((candidate) => candidate.id === scenario.id);
    return lockedScenario?.canonicalState === scenario.canonicalState
      ? []
      : [{
          path: `examples.${caseId}.scenarios.${scenario.id}.canonicalState`,
          currentValue: lockedScenario?.canonicalState ?? null,
          requiredValue: scenario.canonicalState
        }];
  });
  const { stdout: currentCommit } = await execFileAsync(
    'git', ['-C', context.upstreamRoot, 'rev-parse', 'HEAD']);
  if (currentCommit.trim() !== upstreamCommit) {
    failures.push(`Upstream commit=${currentCommit.trim()}, expected ${upstreamCommit}.`);
  }
  const upstreamSourcePath = path.join(
    context.upstreamRoot, 'examples', 'webgl_custom_attributes_points.html');
  if (!await pathExists(upstreamSourcePath)) {
    failures.push(`Missing upstream source: ${upstreamSourcePath}.`);
  } else if (await sha256File(upstreamSourcePath) !== upstreamSourceSha256) {
    failures.push('Pinned upstream source SHA-256 differs.');
  }
  const sparkAssetPath = path.join(
    context.assetRoot, 'textures', 'sprites', 'spark1.png');
  if (!await pathExists(sparkAssetPath)) {
    failures.push(`Missing pinned spark asset: ${sparkAssetPath}.`);
  } else if (await sha256File(sparkAssetPath) !== sparkAssetSha256) {
    failures.push('Pinned spark asset SHA-256 differs.');
  }
  const oracles = [];
  for (const scenario of customAttributesPointsScenarios) {
    const rgbaPath = path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`);
    const metadataPath = path.join(context.oracleRoot, caseId, `${scenario.id}.json`);
    if (!await pathExists(rgbaPath) || !await pathExists(metadataPath)) {
      failures.push(`Missing locked Oracle for ${scenario.id}.`);
      continue;
    }
    const rgbaDigest = await sha256File(rgbaPath);
    const metadataDigest = await sha256File(metadataPath);
    failures.push(...validateOracleSha256(scenario.id, 'rgba', rgbaDigest));
    failures.push(...validateOracleSha256(scenario.id, 'json', metadataDigest));
    failures.push(...validateOracleMetadata(
      JSON.parse(await fs.readFile(metadataPath, 'utf8')),
      scenario));
    oracles.push({
      scenarioId: scenario.id,
      rgbaPath,
      metadataPath,
      rgbaSha256: rgbaDigest,
      metadataSha256: metadataDigest
    });
  }
  for (const pipeline of pipelines) {
    const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
    if (!await pathExists(executable)) failures.push(`Missing ${pipeline} host: ${executable}.`);
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    example,
    pendingSharedLockUpdates,
    upstreamCommit,
    upstreamSourcePath,
    upstreamSourceSha256,
    assets: {
      requiredFiles: [{ path: sparkAssetPath, sha256: sparkAssetSha256 }],
      requiredFileCount: 1
    },
    oracles,
    failures
  };
}

/** Executes the complete two-scenario, three-repeat strict matrix and writes one report. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  const manifest = JSON.parse(await fs.readFile(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'), 'utf8'));
  const inputs = await validateInputs(context, manifest);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    lintGeneratedArtifacts(context),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const runs = [];
  const stability = [];
  const crossComparisons = [];
  if (inputs.status === 'pass' &&
      generatedArtifacts.status === 'pass' &&
      gpuBoundaryLint.status === 'pass') {
    for (const scenario of customAttributesPointsScenarios) {
      const canonicalRuns = new Map();
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          const quadrantRuns = [];
          for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
            const run = await runQuadrantRepetition(
              context, scenario, pipeline, backend, repetition);
            runs.push(run);
            quadrantRuns.push(run);
            console.log(
              `[${run.status.toUpperCase()}] ${caseId}/${scenario.id} ` +
              `${pipeline}/${backend} run=${repetition}`);
            for (const failure of run.failures) console.log(`  ${failure}`);
          }
          stability.push(await compareStability(quadrantRuns));
          canonicalRuns.set(`${pipeline}/${backend}`, quadrantRuns[0]);
        }
      }
      crossComparisons.push(...await compareScenarioParity(scenario, canonicalRuns));
    }
  }
  const expectedRunCount =
    customAttributesPointsScenarios.length * pipelines.length * backends.length * repeatCount;
  const expectedStabilityCount =
    customAttributesPointsScenarios.length * pipelines.length * backends.length;
  const expectedCrossComparisonCount =
    customAttributesPointsScenarios.length * customAttributesPointsParityDefinitions.length;
  const status = inputs.status === 'pass' &&
    generatedArtifacts.status === 'pass' &&
    gpuBoundaryLint.status === 'pass' &&
    runs.length === expectedRunCount && runs.every((run) => run.status === 'pass') &&
    stability.length === expectedStabilityCount &&
    stability.every((entry) => entry.status === 'pass') &&
    crossComparisons.length === expectedCrossComparisonCount &&
    crossComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-custom-attribute-points',
    caseId,
    status,
    comparisonThresholds,
    matrix: {
      pipelines,
      backends,
      scenarios: customAttributesPointsScenarios.map((scenario) => scenario.id),
      repeatCount,
      expectedRunCount,
      passingRunCount: runs.filter((run) => run.status === 'pass').length,
      expectedStabilityCount,
      passingStabilityCount: stability.filter((entry) => entry.status === 'pass').length,
      expectedCrossComparisonCount,
      passingCrossComparisonCount:
        crossComparisons.filter((entry) => entry.status === 'pass').length,
      expectedTotalChecks:
        expectedRunCount + expectedStabilityCount + expectedCrossComparisonCount,
      passingTotalChecks:
        runs.filter((run) => run.status === 'pass').length +
        stability.filter((entry) => entry.status === 'pass').length +
        crossComparisons.filter((entry) => entry.status === 'pass').length
    },
    inputs,
    generatedArtifacts,
    gpuBoundaryLint,
    runs,
    stability,
    crossComparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(`webgl_custom_attributes_points report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
