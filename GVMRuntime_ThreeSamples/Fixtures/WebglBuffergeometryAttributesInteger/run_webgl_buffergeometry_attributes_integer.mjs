#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const caseName = 'WebglBuffergeometryAttributesInteger';
const caseId = 'webgl_buffergeometry_attributes_integer';
const manifestPath = path.join(
  repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
const passName = 'WebglBuffergeometryAttributesIntegerMainPass';
const randomSeed = 0x18500001;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: '5fa9764503bce8eef6f4e0346cfef0ab5b36ab99134ebda5c4b99ea99c2db937',
    json: 'f8d8803121cfb9c8a179580478e0f7f88ddb1f84218e92b1d01aabb7dc574f7c'
  }),
  animated: Object.freeze({
    rgba: '99c2e4e730b35ffe9eaab131077f009566f480b177fc85606c05efac46d12f37',
    json: 'a34ed41a47b91208137dd67120dc87df8000e3a00d780fa7bf30bae46e30ba80'
  })
});

export const integerAttributeScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    canonicalState: 'seed-0x18500001-time-zero-three-texture-selection',
    timeSeconds: 1_700_000_000,
    rotationX: 425_000_000,
    rotationY: 850_000_000
  }),
  Object.freeze({
    id: 'animated',
    frame: 60,
    canonicalState: 'seed-0x18500001-fixed-step-60hz-one-second',
    timeSeconds: 1_700_000_001,
    rotationX: 425_000_000.25,
    rotationY: 850_000_000.5
  })
]);

export const parityDefinitions = Object.freeze([
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'metal',
    rightPipeline: 'experimental',
    rightBackend: 'metal',
    relation: 'pipeline-parity-metal'
  }),
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'vulkan',
    rightPipeline: 'experimental',
    rightBackend: 'vulkan',
    relation: 'pipeline-parity-vulkan'
  }),
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'metal',
    rightPipeline: 'legacy',
    rightBackend: 'vulkan',
    relation: 'backend-parity-legacy'
  }),
  Object.freeze({
    leftPipeline: 'experimental',
    leftBackend: 'metal',
    rightPipeline: 'experimental',
    rightBackend: 'vulkan',
    relation: 'backend-parity-experimental'
  })
]);

/** Parses strict value-bearing long options accepted by this dedicated fixture. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) {
      throw new Error(`Duplicate --${name} option.`);
    }
    options[name] = value;
  }
  return options;
}

/** Resolves one required fixture path without consulting environment variables. */
function requirePathOption(options, name) {
  if (!options[name]) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(options[name]);
}

/** Parses one optional positive-integer watchdog value. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) return fallback;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Returns whether one resolved generated product or executable exists. */
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

/** Rejects an Oracle digest that differs from the immutable r185 capture. */
export function validateOracleSha256(scenarioId, extension, actualSha256) {
  const expectedSha256 = lockedOracleSha256[scenarioId]?.[extension];
  if (expectedSha256 === undefined) {
    throw new Error(`No locked Oracle SHA-256 for ${scenarioId}.${extension}.`);
  }
  return actualSha256 === expectedSha256
    ? []
    : [`oracle.${scenarioId}.${extension}.sha256=${actualSha256}, expected ${expectedSha256}.`];
}

/** Compares expected JSON fields recursively while allowing diagnostic extensions. */
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
              Math.abs(actualElement - expectedElement) > 1e-7) {
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
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-7) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(
        `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Validates the formal Phase 1 classification and ordinary-Scene draw contract. */
export function validateManifestContract(manifest) {
  const failures = [];
  const example = manifest?.examples?.find((entry) => entry.id === caseId);
  if (!example) {
    return [`Formal manifest is missing ${caseId}.`];
  }
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_buffergeometry_attributes_integer.html',
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
      name: 'main-textured',
      renderClass: passName,
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: caseName,
    scenarios: integerAttributeScenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      inputReplay: null,
      canonicalState: scenario.canonicalState,
      scenePassInvocations: [{
        sceneRoot: 'scene',
        scenePass: 'main-textured',
        invocationCount: 1
      }]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const available = new Set(example.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'signed_integer_vertex_attribute',
    'flat_integer_varying',
    'texture2d_sampling',
    'three_texture_material_selection'
  ]) {
    if (!available.has(capability)) {
      failures.push(`manifest.example capabilityAudit is missing '${capability}'.`);
    }
  }
  return failures;
}

/** Builds the complete structural expectation for one locked integer-attribute scenario. */
export function buildExpectedSnapshot(scenario) {
  const scenePassSequence = [{
    sceneRoot: 'scene',
    scenePass: 'main-textured',
    entityOrdinal: 0
  }];
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
    drawCommandCount: 1,
    logicalDrawCommandCount: 1,
    computePassCount: 0,
    rasterSampleCount: 1,
    explicitVertexCount: 30_000,
    submittedVertexCount: 30_000,
    vertexStrideBytes: 24,
    standaloneGeometryBufferCount: 1,
    primitiveTopology: 'triangle-list',
    sourceIntegerAttributeType: 'int16',
    gpuIntegerAttributeType: 'sint32',
    integerVaryingInterpolation: 'flat',
    textureCount: 3,
    textureMipCounts: [9, 10, 12],
    seed: randomSeed,
    upstreamPreVertexRandomDrawCount: 88,
    finalGeometryRandomState: 2_464_553_194,
    finalPageRandomState: 3_968_022_076,
    timeSeconds: scenario.timeSeconds,
    rotationX: scenario.rotationX,
    rotationY: scenario.rotationY,
    cameraFovDegrees: 27,
    cameraNear: 1,
    cameraFar: 3500,
    cameraPositionZ: 2500,
    assetSha256: {
      'textures/crate.gif':
        'a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f',
      'textures/floors/FloorsCheckerboard_S_Diffuse.jpg':
        'd7547036c6221a840b80ce11138dc2c1a26df2630419217234c479b00e50a119',
      'textures/terrain/grasslight-big.jpg':
        '23bd506b94e40a9de435375c2be6280eb088dc637fb64c331e325c7472f47177'
    },
    scenePassSequence,
    sceneRoots: [{
      id: 'scene',
      renderSetCount: 0,
      renderSetId: null,
      renderSetType: null,
      renderableObjectCount: 1,
      entityCount: 0,
      entities: [],
      drawCommandCount: 1,
      logicalDrawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [{
        name: 'main-textured',
        renderClass: passName,
        renderSetId: null,
        renderSetBindingCount: 0,
        drawMode: 'explicit-nonindexed',
        invocationCount: 1,
        logicalInvocationCount: 1,
        drawCommandCount: 1,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }]
    }],
    gpuWorkDslOnly: true
  };
}

/** Builds the complete explicit host CLI for one pipeline/backend quadrant. */
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

/** Counts visible, alpha, and color properties needed to reject an empty capture. */
export function inspectRgbaPixels(pixels) {
  if (!(pixels instanceof Uint8Array) || pixels.byteLength === 0 ||
      pixels.byteLength % 4 !== 0) {
    throw new Error('Integer-attribute capture must be tightly packed RGBA8.');
  }
  let nonBackgroundPixels = 0;
  let nonOpaquePixels = 0;
  const uniqueRgbColors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const red = pixels[offset];
    const green = pixels[offset + 1];
    const blue = pixels[offset + 2];
    if (red !== 5 || green !== 5 || blue !== 5) nonBackgroundPixels += 1;
    if (pixels[offset + 3] !== 255) nonOpaquePixels += 1;
    uniqueRgbColors.add((red << 16) | (green << 8) | blue);
  }
  return {
    nonBackgroundPixels,
    nonOpaquePixels,
    uniqueRgbColorCount: uniqueRgbColors.size
  };
}

/** Extracts and validates the RenderSet-free integer host ABI from generated C++. */
export function validateGeneratedSourceContract(pipeline, generatedSource, exportsSource) {
  const failures = [];
  const label = `${caseName}/${pipeline}`;
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${label}: the single Mesh Scene must not declare a RenderSet.`);
  }
  for (const token of [
    `class ${passName}`,
    'WebglBuffergeometryAttributesIntegerVertexCount = 30000u',
    'GVM::RHI::VertexFormat::Float32x3',
    'GVM::RHI::VertexFormat::Float32x2',
    'GVM::RHI::VertexFormat::Sint32',
    'GVM::RHI::PrimitiveTopology::TriangleList',
    'GVM::RHI::TextureUsage::CopyDst',
    'GVM::RHI::TextureSampleType::Float',
    'renderPass("WebglBuffergeometryAttributesIntegerSample0"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated host is missing '${token}'.`);
    }
  }
  const strideMatch = generatedSource.match(
    /vertexState\.buffers\[0\]\.arrayStride = (\d+);/u);
  const vertexStrideBytes = strideMatch ? Number(strideMatch[1]) : null;
  if (vertexStrideBytes !== 24) {
    failures.push(`${label}: vertex stride is ${vertexStrideBytes}, expected 24.`);
  }
  const attributeFormats = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.format = GVM::RHI::VertexFormat::(\w+);/gu)]
    .map((match) => ({ index: Number(match[1]), format: match[2] }))
    .sort((left, right) => left.index - right.index);
  const expectedFormats = [
    { index: 0, format: 'Float32x3' },
    { index: 1, format: 'Float32x2' },
    { index: 2, format: 'Sint32' }
  ];
  if (JSON.stringify(attributeFormats) !== JSON.stringify(expectedFormats)) {
    failures.push(
      `${label}: vertex formats are ${JSON.stringify(attributeFormats)}, expected ${JSON.stringify(expectedFormats)}.`);
  }
  const vertexBindings = generatedSource.match(
    /mainPass\d->setVertexBuffer\(vertexBuffer\)/gu) ?? [];
  const drawCalls = generatedSource.match(
    /mainPass\d->run\(WebglBuffergeometryAttributesIntegerVertexCount, 1u, 0u, 0u\)/gu) ?? [];
  if (vertexBindings.length !== 1 || drawCalls.length !== 1 ||
      generatedSource.includes('setIndexBuffer')) {
    failures.push(
      `${label}: expected one single-sample draw on one standalone vertex buffer and no index buffer.`);
  }
  for (const textureName of ['crateTexture', 'floorTexture', 'grassTexture']) {
    if (!generatedSource.includes(`writeTexture(${textureName}`)) {
      failures.push(`${label}: ${textureName} is not uploaded through the generated DSL queue.`);
    }
  }
  const abiSignature = {
    renderSetExportCount: 0,
    vertexStrideBytes,
    vertexAttributes: attributeFormats,
    primitiveTopology: generatedSource.includes('GVM::RHI::PrimitiveTopology::TriangleList')
      ? 'TriangleList'
      : null,
    standaloneVertexBufferBindings: vertexBindings.length,
    indexed: generatedSource.includes('setIndexBuffer'),
    explicitVertexCount: drawCalls.length === 1 ? 30_000 : null,
    explicitInstanceCount: drawCalls.length === 1 ? 1 : null,
    sceneTextureBindingCount: 3,
    coverageDrawCount: drawCalls.length,
    resolveDispatchCount: 0
  };
  return {
    pipeline,
    abiSignature,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Validates Experimental UGLIR, MSL, and direct-SPIR-V semantic evidence. */
export function validateExperimentalProductSources(productSources) {
  const failures = [];
  for (const stage of ['vertex', 'fragment']) {
    const sources = productSources[stage];
    const label = `experimental ${stage}`;
    if (!sources) {
      failures.push(`${label}: products are missing.`);
      continue;
    }
    let uglirDocument;
    try {
      uglirDocument = JSON.parse(sources.uglirJson);
    } catch (error) {
      failures.push(`${label}: UGLIR JSON is invalid: ${error.message}`);
    }
    if (uglirDocument) {
      validateExpectedFields(uglirDocument, {
        schemaVersion: 1,
        name: `${passName}.${stage}`,
        reflection: { entryName: `${passName}.${stage}`, stage }
      }, `${label}.uglir`, failures);
    }
    if (!sources.uglirText.includes('textureIndex') ||
        !sources.uglirText.includes('type "i32"')) {
      failures.push(`${label}: UGLIR does not preserve the signed integer varying.`);
    }
    const mslEntry = stage === 'vertex' ? 'vertexMain' : 'fragmentMain';
    if (!sources.msl.includes(mslEntry) || !sources.msl.includes('int textureIndex')) {
      failures.push(`${label}: MSL does not preserve the integer stage interface.`);
    }
    if (!sources.spirvWords.includes('word_count') ||
        !sources.spirvWords.includes('0x07230203')) {
      failures.push(`${label}: direct-SPIR-V words are missing the module header.`);
    }
    const spirvEntry = stage === 'vertex' ? 'OpEntryPoint Vertex' : 'OpEntryPoint Fragment';
    if (!sources.spirvAssembly.includes(spirvEntry) ||
        !sources.spirvAssembly.includes('OpDecorate %textureIndex') ||
        !sources.spirvAssembly.includes(' Flat')) {
      failures.push(`${label}: direct-SPIR-V is missing the integer Flat decoration.`);
    }
  }
  return failures;
}

/** Confirms that Legacy and Experimental expose an identical generated host ABI. */
export function validateGeneratedAbiParity(entries) {
  const failures = [];
  const legacy = entries.find((entry) => entry.pipeline === 'legacy');
  const experimental = entries.find((entry) => entry.pipeline === 'experimental');
  if (!legacy || !experimental) {
    failures.push('Both Legacy and Experimental generated entries are required.');
  } else if (JSON.stringify(legacy.abiSignature) !==
             JSON.stringify(experimental.abiSignature)) {
    failures.push(
      `Legacy/Experimental ABI differs: legacy=${JSON.stringify(legacy.abiSignature)}, experimental=${JSON.stringify(experimental.abiSignature)}.`);
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    failures,
    legacy: legacy?.abiSignature ?? null,
    experimental: experimental?.abiSignature ?? null
  };
}

/** Reads all required Experimental stage products from one generated directory. */
async function readExperimentalProductSources(generatedDirectory, failures) {
  const productSources = {};
  const productPaths = [];
  for (const stage of ['vertex', 'fragment']) {
    const baseName = `${passName}__${stage}`;
    const paths = {
      uglirJson: path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
      uglirText: path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
      msl: path.join(generatedDirectory, 'msl', `${baseName}.msl`),
      spirvWords: path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
      spirvAssembly: path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
    };
    const sources = {};
    for (const [kind, productPath] of Object.entries(paths)) {
      productPaths.push(productPath);
      if (!await pathExists(productPath)) {
        failures.push(`${caseName}/experimental: missing ${productPath}.`);
        sources[kind] = '';
      } else {
        sources[kind] = await fs.readFile(productPath, 'utf8');
      }
    }
    productSources[stage] = sources;
  }
  return { productPaths, productSources };
}

/** Validates one isolated Legacy or Experimental generated-product directory. */
async function validateGeneratedPipeline(generatedRoot, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, caseName, 'UGLBin');
  const exportsPath = path.join(generatedDirectory, 'exports.hpp');
  const generatedHeaderPath = path.join(generatedDirectory, 'generate_result.hpp');
  if (!await pathExists(exportsPath) || !await pathExists(generatedHeaderPath)) {
    return {
      pipeline,
      generatedDirectory,
      abiSignature: null,
      status: 'fail',
      failures: [`${caseName}/${pipeline}: generated host products are missing.`]
    };
  }
  const [exportsSource, generatedSource] = await Promise.all([
    fs.readFile(exportsPath, 'utf8'),
    fs.readFile(generatedHeaderPath, 'utf8')
  ]);
  const result = {
    ...validateGeneratedSourceContract(pipeline, generatedSource, exportsSource),
    generatedDirectory,
    exportsPath,
    generatedHeaderPath,
    products: []
  };
  if (pipeline === 'experimental') {
    const experimental = await readExperimentalProductSources(
      generatedDirectory, result.failures);
    result.products = experimental.productPaths;
    result.failures.push(...validateExperimentalProductSources(experimental.productSources));
    result.status = result.failures.length === 0 ? 'pass' : 'fail';
  }
  return result;
}

/** Validates immutable Three r185 Oracle metadata for one scenario. */
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
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    referenceCaptureMode: 'single-canvas',
    inputReplay: null,
    externalAssetMap: null
  }, `oracle.${scenario.id}`, failures);
  return failures;
}

/** Loads and validates both immutable Oracle captures from the explicit root. */
async function loadOracleImages(oracleRoot) {
  const images = new Map();
  for (const scenario of integerAttributeScenarios) {
    const oracleBase = path.join(oracleRoot, caseId, scenario.id);
    const rgbaPath = `${oracleBase}.rgba`;
    const metadataPath = `${oracleBase}.json`;
    const [rgbaSha256, metadataSha256, image] = await Promise.all([
      sha256File(rgbaPath),
      sha256File(metadataPath),
      loadRgbaArtifact(rgbaPath, metadataPath)
    ]);
    const failures = validateOracleMetadata(image.metadata, scenario);
    failures.push(...validateOracleSha256(scenario.id, 'rgba', rgbaSha256));
    failures.push(...validateOracleSha256(scenario.id, 'json', metadataSha256));
    if (failures.length > 0) throw new Error(failures.join('\n'));
    images.set(scenario.id, {
      ...image,
      sha256: Object.freeze({ rgba: rgbaSha256, json: metadataSha256 })
    });
  }
  return images;
}

/** Returns deterministic output paths for one scenario and matrix quadrant. */
function makeArtifactPaths(outputRoot, scenario, pipeline, backend) {
  const artifactDirectory = path.join(outputRoot, scenario.id, pipeline, backend);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Executes one generated host under a hard watchdog with bounded output. */
async function executeHost(executable, argumentsList, timeoutMs) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, argumentsList, {
      cwd: repositoryRoot,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    child.stdout.on('data', (chunk) => {
      stdout = `${stdout}${chunk.toString('utf8')}`.slice(-64 * 1024);
    });
    child.stderr.on('data', (chunk) => {
      stderr = `${stderr}${chunk.toString('utf8')}`.slice(-64 * 1024);
    });
    child.on('error', reject);
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.on('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({ exitCode, signal, timedOut, stdout, stderr });
    });
  });
}

/** Validates metadata, structural state, and visible textured pixels. */
async function validateQuadrantArtifacts(scenario, pipeline, backend, artifacts) {
  const [image, snapshotText] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8')
  ]);
  const snapshot = JSON.parse(snapshotText);
  const failures = [];
  validateExpectedFields(image.metadata, {
    schemaVersion: 1,
    source: 'gvm-three-r185',
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    randomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);
  const imageInspection = inspectRgbaPixels(image.pixels);
  if (imageInspection.nonBackgroundPixels < 10_000 ||
      imageInspection.uniqueRgbColorCount < 100) {
    failures.push('capture is empty or lacks the three authored texture families.');
  }
  if (imageInspection.nonOpaquePixels !== 0) {
    failures.push(`capture contains ${imageInspection.nonOpaquePixels} non-opaque pixels.`);
  }
  return { image, snapshot, imageInspection, failures };
}

/** Runs one scenario through one Legacy/Experimental and Metal/Vulkan quadrant. */
async function runQuadrant(context, scenario, pipeline, backend, oracleImage) {
  const artifacts = makeArtifactPaths(context.outputRoot, scenario, pipeline, backend);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${caseName}-${pipeline}`);
  const argumentsList = buildHostArguments(
    scenario, pipeline, backend, context.assetRoot, artifacts);
  const result = {
    scenarioId: scenario.id,
    frame: scenario.frame,
    pipeline,
    backend,
    captureStatus: 'fail',
    status: 'fail',
    artifacts,
    failures: []
  };
  if (!await pathExists(executable)) {
    result.failures.push(`Missing host executable: ${executable}.`);
    return result;
  }
  const execution = await executeHost(executable, argumentsList, context.timeoutMs);
  result.host = { executable, arguments: argumentsList, ...execution };
  await fs.writeFile(
    artifacts.hostLogPath, `${JSON.stringify(result.host, null, 2)}\n`, 'utf8');
  if (execution.timedOut) {
    result.failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  } else if (execution.exitCode !== 0) {
    result.failures.push(
      `Host exited with code ${execution.exitCode}${execution.signal ? ` (${execution.signal})` : ''}.`);
  } else {
    try {
      const validation = await validateQuadrantArtifacts(
        scenario, pipeline, backend, artifacts);
      result.validation = {
        snapshot: validation.snapshot,
        imageInspection: validation.imageInspection
      };
      result.failures.push(...validation.failures);
      result.captureStatus = validation.failures.length === 0 ? 'pass' : 'fail';
      const oracleComparison = compareThreeCaptures(oracleImage, validation.image);
      result.oracleComparison = oracleComparison;
      result.failures.push(...oracleComparison.failures);
    } catch (error) {
      result.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Executes all four mandatory cross-quadrant comparisons for one scenario. */
async function compareScenarioQuadrants(scenario, quadrants) {
  const byKey = new Map(
    quadrants.map((quadrant) => [`${quadrant.pipeline}|${quadrant.backend}`, quadrant]));
  const comparisons = [];
  for (const definition of parityDefinitions) {
    const left = byKey.get(`${definition.leftPipeline}|${definition.leftBackend}`);
    const right = byKey.get(`${definition.rightPipeline}|${definition.rightBackend}`);
    const comparison = {
      scenarioId: scenario.id,
      relation: definition.relation,
      status: 'fail',
      failures: []
    };
    if (!left || !right || left.captureStatus !== 'pass' || right.captureStatus !== 'pass') {
      comparison.failures.push(
        'Both quadrants must pass capture validation before parity comparison.');
    } else {
      const [leftImage, rightImage] = await Promise.all([
        loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
        loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
      ]);
      const imageComparison = compareThreeCaptures(leftImage, rightImage);
      comparison.metrics = imageComparison.metrics;
      comparison.failures.push(...imageComparison.failures);
    }
    comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
    comparisons.push(comparison);
  }
  return comparisons;
}

/** Executes the dedicated integer-attribute fixture through every mandatory gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const manifestFailures = validateManifestContract(manifest);
  if (manifestFailures.length > 0) throw new Error(manifestFailures.join('\n'));
  const oracleImages = await loadOracleImages(context.oracleRoot);
  const generatedEntries = [];
  for (const pipeline of pipelines) {
    generatedEntries.push(await validateGeneratedPipeline(context.generatedRoot, pipeline));
  }
  const generatedAbiParity = validateGeneratedAbiParity(generatedEntries);
  const generatedArtifacts = {
    status: generatedEntries.every((entry) => entry.status === 'pass') &&
      generatedAbiParity.status === 'pass'
      ? 'pass'
      : 'fail',
    entries: generatedEntries,
    abiParity: generatedAbiParity
  };

  const quadrants = [];
  const comparisons = [];
  for (const scenario of integerAttributeScenarios) {
    const scenarioQuadrants = [];
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const result = await runQuadrant(
          context, scenario, pipeline, backend, oracleImages.get(scenario.id));
        quadrants.push(result);
        scenarioQuadrants.push(result);
        console.log(
          `[${result.status.toUpperCase()}] ${caseId}/${scenario.id} ${pipeline}/${backend}`);
        for (const failure of result.failures) console.log(`  ${failure}`);
      }
    }
    comparisons.push(...await compareScenarioQuadrants(scenario, scenarioQuadrants));
  }

  const status = generatedArtifacts.status === 'pass' &&
    quadrants.length === integerAttributeScenarios.length * pipelines.length * backends.length &&
    quadrants.every((entry) => entry.status === 'pass') &&
    comparisons.length === integerAttributeScenarios.length * parityDefinitions.length &&
    comparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-buffergeometry-attributes-integer',
    caseId,
    status,
    comparisonThresholds,
    manifest: {
      path: manifestPath,
      status: 'pass',
      failures: manifestFailures
    },
    oracles: integerAttributeScenarios.map((scenario) => ({
      scenarioId: scenario.id,
      rgbaPath: path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      metadataPath: path.join(context.oracleRoot, caseId, `${scenario.id}.json`),
      sha256: oracleImages.get(scenario.id).sha256,
      metadata: oracleImages.get(scenario.id).metadata
    })),
    generatedArtifacts,
    quadrants,
    comparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');

  for (const entry of generatedEntries) {
    console.log(`[${entry.status.toUpperCase()}] ${caseId} ${entry.pipeline} generated products`);
    for (const failure of entry.failures) console.log(`  ${failure}`);
  }
  console.log(`[${generatedAbiParity.status.toUpperCase()}] ${caseId} Legacy/Experimental ABI`);
  for (const comparison of comparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${caseId}/${comparison.scenarioId} ${comparison.relation}`);
    for (const failure of comparison.failures) console.log(`  ${failure}`);
  }
  console.log(`webgl_buffergeometry_attributes_integer report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
