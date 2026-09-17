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
import {
  executeWithWatchdog,
  validateRenderSetSnapshot,
  validateStructuralSnapshot
} from '../../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const execFileAsync = promisify(execFile);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const shardName = 'WebglBuffergeometry';
const caseId = 'webgl_buffergeometry';
const randomSeed = 0x18500015;
const repeatCount = 3;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 =
  '7a4ac8162786e2a3e7ceefe0a07434f5c4d5afab790b5dd2462d68211fd0122d';

export const buffergeometryScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-seeded',
    frame: 0,
    canonicalState:
      'seed-0x18500015-epoch-1700000000000-transparent-double-side-back-then-front',
    virtualTimeMilliseconds: 0,
    nextFrameTimeMilliseconds: 16.666666666666668,
    timeSeconds: 1_700_000_000,
    rotationX: 425_000_000,
    rotationY: 850_000_000
  }),
  Object.freeze({
    id: 'fixed-rotation',
    frame: 120,
    canonicalState:
      'seed-0x18500015-fixed-step-120-epoch-transparent-double-side-back-then-front',
    virtualTimeMilliseconds: 2000.0000000000034,
    nextFrameTimeMilliseconds: 2016.6666666666702,
    timeSeconds: 1_700_000_002,
    rotationX: 425_000_000.5,
    rotationY: 850_000_001
  })
]);

export const buffergeometryParityDefinitions = Object.freeze([
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
  'initial-seeded': Object.freeze({
    rgba: '8c2314a7eda5ed4d303225dc097facea010b908e95c7e21295ef8127b05d87fc',
    json: 'a036bc6461a937ee3738a307f6634c88e26503a20a27401c670517f3ceaa0344'
  }),
  'fixed-rotation': Object.freeze({
    rgba: 'ebdf53c29961db02d4936e4e16b9f07929a79b302922827029e3585a00b098f2',
    json: '3c10df060620cafbcabfda00fa8217610f3681cec60be1bc62e166e7744fcf19'
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

/** Resolves one mandatory path option without environment-variable fallbacks. */
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

/** Compares expected nested fields while allowing diagnostic extensions. */
export function validateExpectedFields(actual, expected, label, failures) {
  for (const [name, expectedValue] of Object.entries(expected)) {
    const actualValue = actual?.[name];
    const fieldLabel = `${label}.${name}`;
    if (Array.isArray(expectedValue)) {
      if (!Array.isArray(actualValue) || actualValue.length !== expectedValue.length) {
        failures.push(
          `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
        continue;
      }
      for (let index = 0; index < expectedValue.length; index += 1) {
        const expectedElement = expectedValue[index];
        const actualElement = actualValue[index];
        if (expectedElement !== null && typeof expectedElement === 'object') {
          validateExpectedFields(
            actualElement, expectedElement, `${fieldLabel}[${index}]`, failures);
        } else if (typeof expectedElement === 'number' && !Number.isInteger(expectedElement)) {
          if (typeof actualElement !== 'number' ||
              Math.abs(actualElement - expectedElement) > 1e-6) {
            failures.push(
              `${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${expectedElement}.`);
          }
        } else if (actualElement !== expectedElement) {
          failures.push(
            `${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${JSON.stringify(expectedElement)}.`);
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
      failures.push(
        `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Verifies the formal one-object ordinary-RenderClass manifest contract. */
export function validateManifestContract(example) {
  const failures = [];
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_buffergeometry.html',
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
    loaderRenderableObjectCount: 0,
    scenePasses: [
      {
        name: 'main-transparent-phong-back-side',
        renderClass: 'WebglBuffergeometryBackSidePass',
        sceneRoot: 'scene',
        renderSetBindingCount: 0,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      },
      {
        name: 'main-transparent-phong-front-side',
        renderClass: 'WebglBuffergeometryFrontSidePass',
        sceneRoot: 'scene',
        renderSetBindingCount: 0,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }
    ],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: shardName,
    scenarios: buffergeometryScenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      inputReplay: null,
      canonicalState: scenario.canonicalState,
      scenePassInvocations: [
        {
          sceneRoot: 'scene',
          scenePass: 'main-transparent-phong-back-side',
          invocationCount: 1
        },
        {
          sceneRoot: 'scene',
          scenePass: 'main-transparent-phong-front-side',
          invocationCount: 1
        }
      ],
      scenePassSequence: [
        {
          sceneRoot: 'scene',
          scenePass: 'main-transparent-phong-back-side',
          entityOrdinal: 0
        },
        {
          sceneRoot: 'scene',
          scenePass: 'main-transparent-phong-front-side',
          entityOrdinal: 0
        }
      ]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const available = new Set(example?.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'seeded_cpu_buffergeometry_generation',
    'ordinary_single_object_render_class',
    'rgba_vertex_attributes',
    'private_dsl_phong_lighting_and_fog',
    'double_sided_triangle_rendering',
    'alpha_blending_and_depth',
    'fixed_time_object_animation'
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

/** Builds the runtime structural fields locked for one transparent Float32 scenario. */
export function buildExpectedSnapshot(scenario) {
  return {
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    renderSetPolicy: 'not-required',
    gpuWorkDslOnly: true,
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    scenePassCount: 2,
    screenPassCount: 0,
    screenPasses: [],
    drawCommandCount: 2,
    physicalCoverageDrawCount: 2,
    scenePassSequence: [
      {
        sceneRoot: 'scene',
        scenePass: 'main-transparent-phong-back-side',
        entityOrdinal: 0
      },
      {
        sceneRoot: 'scene',
        scenePass: 'main-transparent-phong-front-side',
        entityOrdinal: 0
      }
    ],
    sceneRoots: [{
      id: 'scene',
      renderSetCount: 0,
      renderSetId: null,
      renderSetType: null,
      renderableObjectCount: 1,
      entityCount: 0,
      entities: [],
      drawCommandCount: 2,
      directDrawFallback: false,
      scenePasses: [
        {
          name: 'main-transparent-phong-back-side',
          renderClass: 'WebglBuffergeometryBackSidePass',
          renderSetId: null,
          renderSetBindingCount: 0,
          drawMode: 'explicit-non-indexed-triangle-list',
          invocationCount: 1,
          drawCommandCount: 1,
          usesStandaloneGeometry: true,
          usesExplicitDrawCount: true
        },
        {
          name: 'main-transparent-phong-front-side',
          renderClass: 'WebglBuffergeometryFrontSidePass',
          renderSetId: null,
          renderSetBindingCount: 0,
          drawMode: 'explicit-non-indexed-triangle-list',
          invocationCount: 1,
          drawCommandCount: 1,
          usesStandaloneGeometry: true,
          usesExplicitDrawCount: true
        }
      ]
    }],
    sourceTriangleCount: 160_000,
    sourceVertexCount: 480_000,
    vertexStrideBytes: 40,
    sourcePositionFormat: 'float32x3',
    sourceNormalFormat: 'float32x3',
    sourceColorFormat: 'float32x4',
    doubleSideStrategy: 'transparent-back-side-then-front-side',
    blendColor: 'src-alpha-one-minus-src-alpha',
    blendAlpha: 'one-one-minus-src-alpha',
    depthWriteEnabled: true,
    depthCompare: 'less-equal',
    preGeometryRandomDrawCount: 116,
    geometryRandomDrawCount: 2_080_000,
    postGeometryRandomDrawCount: 52,
    totalReferenceRandomDrawCount: 2_080_168,
    geometryFinalRandomState: 2_315_725_594,
    referenceFinalRandomState: 4_038_982_052,
    referenceRandomAudit: {
      moduleImport: { drawCount: 76, state: 1_057_064_717 },
      camera: { drawCount: 4, state: 2_007_558_102 },
      scene: { drawCount: 4, state: 2_221_413_969 },
      ambientLight: { drawCount: 4, state: 3_793_295_688 },
      directionalLight1: { drawCount: 12, state: 1_669_311_323 },
      directionalLight2: { drawCount: 12, state: 1_990_145_794 },
      geometryObject: { drawCount: 4, state: 3_108_691_359 },
      geometryValues: { drawCount: 2_080_000, state: 2_315_725_594 },
      material: { drawCount: 4, state: 2_913_495_208 },
      mesh: { drawCount: 4, state: 3_864_669_586 },
      rendererConstructor: { drawCount: 36, state: 1_193_638_393 },
      firstRender: { drawCount: 8, state: 4_038_982_052 },
      subsequentFrames: { drawCount: 0, state: 4_038_982_052 }
    },
    virtualTimeMilliseconds: scenario.virtualTimeMilliseconds,
    timeSeconds: scenario.timeSeconds,
    rotationX: scenario.rotationX,
    rotationY: scenario.rotationY,
    cameraFovDegrees: 27,
    cameraNear: 1,
    cameraFar: 3500,
    cameraPositionZ: 2750,
    fogType: 'linear',
    fogNear: 2000,
    fogFar: 3500,
    materialColorHex: 'd5d5d5',
    shininess: 250,
    transparent: true,
    antialiasResolve: 'disabled-single-sample'
  };
}

/** Validates immutable Oracle identity and deterministic virtual timing. */
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
    randomState: 4_038_982_052,
    virtualTimeMs: scenario.virtualTimeMilliseconds,
    nextFrameTimeMs: scenario.nextFrameTimeMilliseconds,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    canvasBackingWidth: 800,
    canvasBackingHeight: 500,
    referenceCaptureMode: 'single-canvas',
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

/** Extracts both transparent RenderClass Float32 vertex-layout ABIs from generated C++. */
function extractGeneratedAbiSignature(generatedSource) {
  return {
    stride40Count: (generatedSource.match(/arrayStride\s*=\s*40;/gu) ?? []).length,
    attributeOffsetCounts: ['position', 'normal', 'color'].map((name) =>
      (generatedSource.match(new RegExp(
        `offsetof\\(WebglBuffergeometryVertex, ${name}\\)`, 'gu')) ?? []).length)
  };
}

/** Lints one generated pipeline for the frozen transparent ordinary-render ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, singleHeader) {
  const failures = [];
  const label = `${shardName}/${pipeline}`;
  for (const token of [
    'class WebglBuffergeometryBackSidePass',
    'class WebglBuffergeometryFrontSidePass',
    'arrayStride = 40',
    'offsetof(WebglBuffergeometryVertex, position)',
    'offsetof(WebglBuffergeometryVertex, normal)',
    'offsetof(WebglBuffergeometryVertex, color)',
    'setVertexBuffer(vertexBuffer)',
    'backSidePass0->run(WebglBuffergeometryVertexCount, 1u, 0u, 0u)',
    'frontSidePass0->run(WebglBuffergeometryVertexCount, 1u, 0u, 0u)',
    'computePass("WebglBuffergeometryResolve"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated source omits '${token}'.`);
    }
  }
  for (let sampleIndex = 0; sampleIndex < 4; sampleIndex += 1) {
    const token = `renderPass("WebglBuffergeometrySample${sampleIndex}"`;
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated source omits coverage pass ${sampleIndex}.`);
    }
  }
  for (const forbidden of [
    'createRenderSet<',
    'drawIndexedIndirect(',
    'mRenderSetBindGroupIndex'
  ]) {
    if (generatedSource.includes(forbidden)) {
      failures.push(`${label}: ordinary Scene unexpectedly contains '${forbidden}'.`);
    }
  }
  for (const token of [
    'static const uint WebglBuffergeometryVertexCount = 480000u',
    'webglBuffergeometryShade',
    'class WebglBuffergeometryBackSidePass',
    'class WebglBuffergeometryFrontSidePass'
  ]) {
    if (!singleHeader.includes(token)) {
      failures.push(`${label}: merged DSL omits '${token}'.`);
    }
  }
  const abiSignature = extractGeneratedAbiSignature(generatedSource);
  if (abiSignature.stride40Count !== 2 ||
      abiSignature.attributeOffsetCounts.some((count) => count !== 2)) {
    failures.push(`${label}: both generated Float32 vertex-layout ABIs are required.`);
  }
  return {
    pipeline,
    status: failures.length === 0 ? 'pass' : 'fail',
    abiSignature,
    failures
  };
}

/** Verifies Legacy and Experimental expose identical transparent vertex layouts. */
export function validateGeneratedAbiParity(entries) {
  const failures = [];
  if (entries.length !== 2 || entries.some((entry) => entry.status !== 'pass')) {
    failures.push('Both generated pipelines must pass before ABI comparison.');
  } else if (JSON.stringify(entries[0].abiSignature) !==
             JSON.stringify(entries[1].abiSignature)) {
    failures.push('Legacy and Experimental Float32 vertex-layout ABI signatures differ.');
  }
  return { status: failures.length === 0 ? 'pass' : 'fail', failures };
}

/** Validates one Experimental UGLIR/MSL/direct-SPIR-V product family. */
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
    const mslEntryToken = product.stage === 'compute' ? 'kernel ' : `${product.stage} `;
    if (!product.msl.includes(mslEntryToken)) {
      failures.push(`${product.name}: MSL ${product.stage} entry is missing.`);
    }
    if (!product.spirvWords.includes('0x07230203')) {
      failures.push(`${product.name}: direct-SPIR-V word magic is missing.`);
    }
    const spirvStage = product.stage === 'compute'
      ? 'GLCompute'
      : product.stage[0].toUpperCase() + product.stage.slice(1);
    if (!product.spirvAssembly.includes(`OpEntryPoint ${spirvStage}`)) {
      failures.push(`${product.name}: SPIR-V ${product.stage} entry is missing.`);
    }
  }
  return failures;
}

/** Loads and lints both generated output families plus three Experimental stages. */
async function lintGeneratedArtifacts(context) {
  const entries = [];
  for (const pipeline of pipelines) {
    const root = path.join(context.generatedRoot, pipeline, shardName, 'UGLBin');
    const generatedPath = path.join(root, 'generate_result.hpp');
    const singleHeaderPath = path.join(root, 'dsl_single_header.hpp');
    const missing = [];
    for (const targetPath of [generatedPath, singleHeaderPath]) {
      if (!await pathExists(targetPath)) missing.push(`Missing generated artifact ${targetPath}.`);
    }
    let entry;
    if (missing.length === 0) {
      entry = validateGeneratedSourceContract(
        pipeline,
        await fs.readFile(generatedPath, 'utf8'),
        await fs.readFile(singleHeaderPath, 'utf8'));
    } else {
      entry = { pipeline, status: 'fail', abiSignature: {}, failures: missing };
    }
    entry.generatedRoot = root;
    if (pipeline === 'experimental') {
      const stageDefinitions = [
        ['WebglBuffergeometryBackSidePass__vertex', 'vertex'],
        ['WebglBuffergeometryBackSidePass__fragment', 'fragment'],
        ['WebglBuffergeometryFrontSidePass__vertex', 'vertex'],
        ['WebglBuffergeometryFrontSidePass__fragment', 'fragment']
      ];
      const products = [];
      for (const [name, stage] of stageDefinitions) {
        const productPaths = {
          uglirJson: path.join(root, 'uglir', `${name}.uglir.json`),
          uglirText: path.join(root, 'uglir', `${name}.uglir.txt`),
          msl: path.join(root, 'msl', `${name}.msl`),
          spirvWords: path.join(root, 'spv', `${name}.spv.txt`),
          spirvAssembly: path.join(root, 'spv', `${name}.spvasm`)
        };
        const product = { name, stage };
        for (const [field, targetPath] of Object.entries(productPaths)) {
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
  const failures = [
    ...entries.flatMap((entry) => entry.failures),
    ...abiParity.failures
  ];
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
  const colors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    colors.add((pixels[offset] << 16) | (pixels[offset + 1] << 8) | pixels[offset + 2]);
    if (pixels[offset + 3] !== 255) nonOpaquePixels += 1;
  }
  return { uniqueRgbColorCount: colors.size, nonOpaquePixels };
}

/** Locks source, executables, empty asset root, and both clean Oracle artifacts. */
async function validateInputs(context, manifest) {
  const manifestFailures = [];
  const executionFailures = [];
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  manifestFailures.push(...validateManifestContract(example));
  const { stdout: currentCommit } = await execFileAsync(
    'git', ['-C', context.upstreamRoot, 'rev-parse', 'HEAD']);
  if (currentCommit.trim() !== upstreamCommit) {
    executionFailures.push(
      `Upstream commit=${currentCommit.trim()}, expected ${upstreamCommit}.`);
  }
  const upstreamSourcePath = path.join(
    context.upstreamRoot, 'examples', 'webgl_buffergeometry.html');
  if (!await pathExists(upstreamSourcePath)) {
    executionFailures.push(`Missing upstream source: ${upstreamSourcePath}.`);
  } else if (await sha256File(upstreamSourcePath) !== upstreamSourceSha256) {
    executionFailures.push('Pinned upstream source SHA-256 differs.');
  }
  if (!await pathExists(context.assetRoot)) {
    executionFailures.push(`Explicit asset root is missing: ${context.assetRoot}.`);
  }
  const oracles = [];
  for (const scenario of buffergeometryScenarios) {
    const rgbaPath = path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`);
    const metadataPath = path.join(context.oracleRoot, caseId, `${scenario.id}.json`);
    if (!await pathExists(rgbaPath) || !await pathExists(metadataPath)) {
      executionFailures.push(`Missing locked Oracle for ${scenario.id}.`);
      continue;
    }
    const rgbaDigest = await sha256File(rgbaPath);
    const metadataDigest = await sha256File(metadataPath);
    executionFailures.push(...validateOracleSha256(scenario.id, 'rgba', rgbaDigest));
    executionFailures.push(...validateOracleSha256(scenario.id, 'json', metadataDigest));
    const metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    executionFailures.push(...validateOracleMetadata(metadata, scenario));
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
    if (!await pathExists(executable)) {
      executionFailures.push(`Missing ${pipeline} host: ${executable}.`);
    }
  }
  const failures = [...manifestFailures, ...executionFailures];
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    executionReady: executionFailures.length === 0,
    example,
    upstreamCommit,
    upstreamSourcePath,
    upstreamSourceSha256,
    assets: { requiredFiles: [], requiredFileCount: 0 },
    oracles,
    manifestFailures,
    executionFailures,
    failures
  };
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

/** Validates one successful host's metadata, structure, and Oracle distance. */
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
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    frame: scenario.frame,
    randomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: null
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);
  if (context.manifestContractPassed) {
    const manifestScenario = context.manifestScenarios.get(scenario.id);
    failures.push(...validateStructuralSnapshot(context.example, manifestScenario, snapshot));
    failures.push(...validateRenderSetSnapshot(context.example, snapshot, manifestScenario));
  }
  const inspection = inspectRgbaPixels(actual.pixels);
  if (inspection.uniqueRgbColorCount < 2) failures.push('Capture is clear-only.');
  if (inspection.nonOpaquePixels !== 0) {
    failures.push(`Capture contains ${inspection.nonOpaquePixels} non-opaque pixels.`);
  }
  const oracleComparison = compareThreeCaptures(oracle, actual);
  failures.push(...oracleComparison.failures.map((failure) => `Oracle: ${failure}`));
  return {
    inspection,
    oracleComparison,
    manifestValidatorsApplied: context.manifestContractPassed,
    failures
  };
}

/** Runs and validates one scenario/pipeline/backend repetition. */
async function runQuadrantRepetition(
  context,
  scenario,
  pipeline,
  backend,
  repetition
) {
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
  if (processResult.timedOut) failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  if (processResult.exitCode !== 0) {
    failures.push(`Host exited code=${processResult.exitCode} signal=${processResult.signal}.`);
  }
  let validation = null;
  if (failures.length === 0) {
    try {
      validation = await validateRunArtifacts(
        context, scenario, pipeline, backend, artifacts);
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
    manifestValidatorsApplied: validation?.manifestValidatorsApplied ?? false,
    failures
  };
}

/** Loads one successful repetition image. */
async function loadRunImage(run) {
  return loadRgbaArtifact(run.artifacts.rgbaPath, run.artifacts.metadataPath);
}

/** Requires all three RGBA, metadata, and snapshot repetitions to be byte exact. */
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
  for (const definition of buffergeometryParityDefinitions) {
    const left = canonicalRuns.get(definition.left.join('/'));
    const right = canonicalRuns.get(definition.right.join('/'));
    const failures = [];
    let metrics = null;
    if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
      failures.push('Both canonical quadrants must pass before parity comparison.');
    } else {
      const comparison = compareThreeCaptures(
        await loadRunImage(left),
        await loadRunImage(right));
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

/** Executes the complete 24-run transparent Float32 strict gate and writes one report. */
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
  const manifestPath = path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const inputs = await validateInputs(context, manifest);
  context.example = inputs.example;
  context.manifestContractPassed = inputs.manifestFailures.length === 0;
  context.manifestScenarios = new Map(
    inputs.example?.scenarios?.map((scenario) => [scenario.id, scenario]) ?? []);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    lintGeneratedArtifacts(context),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const runs = [];
  const stability = [];
  const crossComparisons = [];
  if (inputs.executionReady &&
      generatedArtifacts.status === 'pass' &&
      gpuBoundaryLint.status === 'pass') {
    for (const scenario of buffergeometryScenarios) {
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
    buffergeometryScenarios.length * pipelines.length * backends.length * repeatCount;
  const expectedStabilityCount = buffergeometryScenarios.length * pipelines.length * backends.length;
  const expectedCrossComparisonCount = buffergeometryScenarios.length * buffergeometryParityDefinitions.length;
  const status = inputs.status === 'pass' &&
    generatedArtifacts.status === 'pass' &&
    gpuBoundaryLint.status === 'pass' &&
    runs.length === expectedRunCount &&
    runs.every((run) => run.status === 'pass') &&
    stability.length === expectedStabilityCount &&
    stability.every((entry) => entry.status === 'pass') &&
    crossComparisons.length === expectedCrossComparisonCount &&
    crossComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-buffergeometry',
    caseId,
    status,
    comparisonThresholds,
    matrix: {
      pipelines,
      backends,
      scenarios: buffergeometryScenarios.map((scenario) => scenario.id),
      repeatCount,
      expectedRunCount,
      passingRunCount: runs.filter((run) => run.status === 'pass').length,
      expectedStabilityCount,
      passingStabilityCount: stability.filter((entry) => entry.status === 'pass').length,
      expectedCrossComparisonCount,
      passingCrossComparisonCount:
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
  console.log(`webgl_buffergeometry report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
