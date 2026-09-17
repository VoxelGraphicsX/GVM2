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
const shardName = 'WebglCustomAttributesPoints2';
const caseId = 'webgl_custom_attributes_points2';
const randomSeed = 0x18500013;
const repeatCount = 3;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = 'f8550792d1801617a32520bfbeb22d443dcca9ce8143c6b1e0e98c9199eaaf51';
const discAssetSha256 = '5429d0ce673fe08a0dc8bc852728ebd624bf8677650fa74f2c98c861e0de3721';

export const customAttributesPoints2Scenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    canonicalState: 'seed-0x18500013-time-zero-3120-disc-billboards-pre-render-identity-matrices-index-sha-eb906a9f26f4-size-sha-5494433543fb-color-sha-52972aa0b087',
    virtualTimeMilliseconds: 0,
    nextFrameTimeMilliseconds: 1000 / 60,
    timeValue: 0,
    drawRotation: 0,
    sortRotation: 0,
    sortMatrixLagFrames: 0,
    sortUsesIdentityMatrices: true,
    frameAdvanceCount: 1,
    finalRandomState: 2509245698,
    sizeSha256: '5494433543fb44b1a1ac074700d3a56a5e65b43f53e322a383163e846d9069ff',
    logicalIndexSha256: 'eb906a9f26f4b171010ab45bc9d828c451721f380acf3e90509e9f64ad67ea05',
    expandedIndexSha256: '3801c9fd75312ac6ed377786531b90f8e39ca1530f294035d8d660110cf9b6b4'
  }),
  Object.freeze({
    id: 'animated-sorted-index',
    frame: 60,
    canonicalState: 'seed-0x18500013-fixed-step-60hz-one-second-frame59-matrix-index-sha-2d52c3c9f029-size-sha-ffdf685b2888-color-sha-52972aa0b087',
    virtualTimeMilliseconds: 999.9999999999991,
    nextFrameTimeMilliseconds: 1016.6666666666657,
    timeValue: 4.999999999999996,
    drawRotation: 0.09999999999999991,
    sortRotation: 0.09833333333333326,
    sortMatrixLagFrames: 1,
    sortUsesIdentityMatrices: false,
    frameAdvanceCount: 61,
    finalRandomState: 2509245698,
    sizeSha256: 'ffdf685b288850dd8074ee84de9f9c95ba43dc17492d8019393885fbd2d3e1c2',
    logicalIndexSha256: '2d52c3c9f02934b06e69c8714039efc527519769188804a95c1d64cfd1ac2d33',
    expandedIndexSha256: 'b76c2055b4b63f47e9f1dae47785aebbec7878507e562288136a634251370a84'
  })
]);

export const customAttributesPoints2ParityDefinitions = Object.freeze([
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
    rgba: '776213d2dcc57fcbe946552e1726953d3102b3a7b9382039028a1931f3ac7dc3',
    json: '10b56dffa607a019aad4a483afd20000446d71016737a19c0d550d596d00b9e5'
  }),
  'animated-sorted-index': Object.freeze({
    rgba: '22c11ec921e06d2208c9faf15e086f567006fe24aa0e9769da4b64d4a8a60df9',
    json: '41996ce5fedfa9ac92f6458a2cc126e98864f471f5f8806e91b650920c4a6756'
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
    upstreamPath: 'examples/webgl_custom_attributes_points2.html',
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
    containsGeometryGroups: false,
    scenePasses: [{
      name: 'main-sorted-disc-points',
      renderClass: 'WebglCustomAttributesPoints2MainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: shardName,
    scenarios: customAttributesPoints2Scenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      inputReplay: null,
      scenePassInvocations: [{
        sceneRoot: 'scene',
        scenePass: 'main-sorted-disc-points',
        invocationCount: 1
      }],
      scenePassSequence: [{
        sceneRoot: 'scene',
        scenePass: 'main-sorted-disc-points',
        entityOrdinal: 0
      }]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const available = new Set(example?.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'sample_private_dsl_shader_rewrite',
    'ordinary_indexed_triangle_rendering',
    'noninstanced_triangle_billboard_point_expansion',
    'cpu_geometry_merge_and_vertex_deduplication',
    'cpu_back_to_front_point_sort',
    'dynamic_index_buffer_update',
    'dynamic_vertex_size_update',
    'depth_attenuated_billboard_size_math',
    'texture2d_sampling',
    'src_alpha_blending'
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
    screenPassCount: 0,
    drawCommandCount: 1,
    physicalCoverageDrawCount: 1,
    sourceSpherePointCount: 2518,
    sourceBoxPointCount: 602,
    logicalPointCount: 3120,
    expandedVertexCount: 12480,
    explicitIndexCount: 18720,
    expandedTriangleCount: 6240,
    vertexStrideBytes: 32,
    standaloneGeometryBufferCount: 2,
    dynamicSizeBufferCount: 1,
    primitiveTopology: 'triangle-list',
    pointExpansion: 'four-corners-six-indices-noninstanced',
    nativePointSubpixelBits: 4,
    nativePointSizeRange: [1, 511],
    projectionConvention: 'three-opengl-positive-y-negative-one-to-one',
    dslClipConversion: 'y-negate-and-z-half-range-after-point-expansion',
    pointCoordOrigin: 'upper-left',
    antialias: false,
    coverageSampleCount: 1,
    internalCoverageResolve: false,
    dynamicSizeUploadCount: scenario.frameAdvanceCount,
    dynamicIndexUploadCount: scenario.frameAdvanceCount,
    renderedVirtualTimeMs: scenario.virtualTimeMilliseconds,
    dateNowSource: 'performance.now-fixed-step',
    timeValue: scenario.timeValue,
    drawRotationY: scenario.drawRotation,
    drawRotationZ: scenario.drawRotation,
    sortRotationY: scenario.sortRotation,
    sortRotationZ: scenario.sortRotation,
    sortUsesIdentityCameraMatrix: scenario.sortUsesIdentityMatrices,
    sortUsesIdentityObjectMatrix: scenario.sortUsesIdentityMatrices,
    sortMatrixLagFrames: scenario.sortMatrixLagFrames,
    drawCameraPositionZ: 300,
    drawUsesCurrentCameraMatrix: true,
    drawUsesCurrentObjectMatrix: true,
    sizeSha256: scenario.sizeSha256,
    logicalIndexSha256: scenario.logicalIndexSha256,
    expandedIndexSha256: scenario.expandedIndexSha256,
    positionSha256: '8e24230550f6602ca179173649a5c4061c9b92a2a1f96504dc5aac63e1475999',
    colorSha256: '52972aa0b0876d91e234e953f3220f777bf83ff17591d589e2b3e94d7e540f35',
    seed: randomSeed,
    moduleImportRandomDrawCount: 76,
    cameraRandomDrawCount: 4,
    sceneRandomDrawCount: 4,
    geometryUuidRandomDrawCount: 24,
    textureSourceRandomDrawCount: 8,
    materialRandomDrawCount: 4,
    pointsRandomDrawCount: 4,
    rendererConstructorRandomDrawCount: 36,
    firstRenderLazyRandomDrawCount: 0,
    subsequentFrameRandomDrawCount: 0,
    totalRandomDrawCount: 160,
    finalRandomState: scenario.finalRandomState,
    texturePath: 'textures/sprites/disc.png',
    textureAssetSha256: discAssetSha256,
    textureColorSpace: 'NoColorSpace',
    textureFormat: 'rgba8unorm',
    textureExtent: 32,
    textureMipLevelCount: 6,
    textureBaseGpuBytesSha256: 'bdd23b9074323ec6ca8ec3c685374797ba2bf2bdedb945745a3befee6d5be2e0',
    textureFlipY: true,
    textureHiddenRgbPreserved: true,
    mipmapGeneration: 'explicit-cpu-raw-unorm-box-filter',
    samplerAddressMode: 'repeat',
    samplerMinMagFilter: 'linear',
    samplerMipmapFilter: 'linear',
    blendColor: 'src-alpha-one-minus-src-alpha',
    blendAlpha: 'one-one-minus-src-alpha',
    depthTest: true,
    depthWrite: true,
    depthCompare: 'less-equal',
    rendererOutputColorSpace: 'srgb',
    shaderOutputTransfer: 'none-custom-shader',
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main-sorted-disc-points',
      entityOrdinal: 0
    }],
    sceneRoots: [{
      id: 'scene',
      renderSetCount: 0,
      renderSetId: null,
      renderSetType: null,
      renderableObjectCount: 1,
      entityCount: 0,
      entities: [],
      drawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [{
        name: 'main-sorted-disc-points',
        renderClass: 'WebglCustomAttributesPoints2MainPass',
        renderSetId: null,
        renderSetBindingCount: 0,
        drawMode: 'explicit-indexed',
        invocationCount: 1,
        drawCommandCount: 1,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }]
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
      generatedSource.includes(`offsetof(WebglCustomAttributesPoints2Vertex, ${name})`)),
    hasIndexBinding: generatedSource.includes('setIndexBuffer(indexBuffer)'),
    hasVertexBinding: generatedSource.includes('setVertexBuffer(vertexBuffer)'),
    hasDynamicSizeUpload: /writeBuffer\((?:GVM::RHI::)?BufferRange\(sizeBuffer\)/u.test(generatedSource),
    hasDynamicIndexUpload: /writeBuffer\((?:GVM::RHI::)?BufferRange\(indexBuffer\)/u.test(generatedSource),
    explicitIndexedDrawCount: (
      generatedSource.match(/mainPass->run\(WebglCustomAttributesPoints2IndexCount,\s*1u/gu) ?? []
    ).length,
    sceneRenderPassCount: (
      generatedSource.match(/renderPass\("WebglCustomAttributesPoints2Scene"/gu) ?? []
    ).length,
    computePassCount: (generatedSource.match(/->computePass\(/gu) ?? []).length
  };
}

/** Lints one generated pipeline for the frozen ordinary indexed material ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, singleHeader) {
  const failures = [];
  const label = `${shardName}/${pipeline}`;
  for (const token of [
    'class WebglCustomAttributesPoints2MainPass',
    'arrayStride = 32',
    'setVertexBuffer(vertexBuffer)',
    'setIndexBuffer(indexBuffer)',
    'WebglCustomAttributesPoints2IndexCount',
    'writeTexture(',
    'renderPass("WebglCustomAttributesPoints2Scene"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated source omits '${token}'.`);
    }
  }
  if (!/writeBuffer\((?:GVM::RHI::)?BufferRange\(sizeBuffer\)/u.test(generatedSource)) {
    failures.push(`${label}: generated source omits the dynamic logical-size upload.`);
  }
  if (!/writeBuffer\((?:GVM::RHI::)?BufferRange\(indexBuffer\)/u.test(generatedSource)) {
    failures.push(`${label}: generated source omits the dynamic stable-sort index upload.`);
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
    'static const uint WebglCustomAttributesPoints2LogicalPointCount = 3120u;',
    'WebglCustomAttributesPoints2LogicalPointCount * 4u;',
    'WebglCustomAttributesPoints2LogicalPointCount * 6u;',
    'static const uint WebglCustomAttributesPoints2TextureMipCount = 6u;',
    'class WebglCustomAttributesPoints2MainPass',
    'class WebglCustomAttributesPoints2Renderer'
  ]) {
    if (!singleHeader.includes(token)) {
      failures.push(`${label}: merged DSL omits '${token}'.`);
    }
  }
  const abiSignature = extractGeneratedAbiSignature(generatedSource);
  if (abiSignature.vertexStrideCount !== 1 ||
      abiSignature.attributeOffsets.includes(false) ||
      !abiSignature.hasIndexBinding || !abiSignature.hasVertexBinding ||
      !abiSignature.hasDynamicSizeUpload || !abiSignature.hasDynamicIndexUpload ||
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
        ['WebglCustomAttributesPoints2MainPass__vertex', 'vertex'],
        ['WebglCustomAttributesPoints2MainPass__fragment', 'fragment']
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
  for (const definition of customAttributesPoints2ParityDefinitions) {
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

/** Locks source, disc asset, executables, and both clean Oracle artifacts. */
async function validateInputs(context, manifest) {
  const failures = [];
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  failures.push(...validateManifestContract(example));
  const pendingSharedLockUpdates = customAttributesPoints2Scenarios.flatMap((scenario) => {
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
    context.upstreamRoot, 'examples', 'webgl_custom_attributes_points2.html');
  if (!await pathExists(upstreamSourcePath)) {
    failures.push(`Missing upstream source: ${upstreamSourcePath}.`);
  } else if (await sha256File(upstreamSourcePath) !== upstreamSourceSha256) {
    failures.push('Pinned upstream source SHA-256 differs.');
  }
  const discAssetPath = path.join(
    context.assetRoot, 'textures', 'sprites', 'disc.png');
  if (!await pathExists(discAssetPath)) {
    failures.push(`Missing pinned disc asset: ${discAssetPath}.`);
  } else if (await sha256File(discAssetPath) !== discAssetSha256) {
    failures.push('Pinned disc asset SHA-256 differs.');
  }
  const oracles = [];
  for (const scenario of customAttributesPoints2Scenarios) {
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
      requiredFiles: [{ path: discAssetPath, sha256: discAssetSha256 }],
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
    for (const scenario of customAttributesPoints2Scenarios) {
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
    customAttributesPoints2Scenarios.length * pipelines.length * backends.length * repeatCount;
  const expectedStabilityCount =
    customAttributesPoints2Scenarios.length * pipelines.length * backends.length;
  const expectedCrossComparisonCount =
    customAttributesPoints2Scenarios.length * customAttributesPoints2ParityDefinitions.length;
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
    gate: 'three-r185-webgl-custom-attribute-points2',
    caseId,
    status,
    comparisonThresholds,
    matrix: {
      pipelines,
      backends,
      scenarios: customAttributesPoints2Scenarios.map((scenario) => scenario.id),
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
  console.log(`webgl_custom_attributes_points2 report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
