#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { defaultThreeRandomSeed } from '../../../tests/runners/three/node/determinism.mjs';
import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const caseName = 'WebglBuffergeometryLines';
const caseId = 'webgl_buffergeometry_lines';
const passName = 'WebglBuffergeometryLinesMainPass';
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: 'fda2d1a8cb074fe6c744eec2ee5f3314f132cef36643499f0ea0e330e154bdf4',
    json: 'b61ef8c55d1e01a5005d1aeeb9dc339cb5e077de2ae6355e002ea2d419e0cf91'
  }),
  'animated-morph': Object.freeze({
    rgba: '25cb24b5c4d913759b331aa00347ed950e7a2ede0f64e6462cf0988fcbc9c523',
    json: '91ea59ef2d75762d10174463ab83ee3294fd160faa6e8da540b14586bdb22d17'
  })
});

export const lineScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    canonicalState: 'seed-0x12345678-morph-weight-zero',
    timeSeconds: 0,
    rotationX: 0,
    rotationY: 0,
    morphWeight: 0
  }),
  Object.freeze({
    id: 'animated-morph',
    frame: 60,
    canonicalState: 'seed-0x12345678-fixed-step-60hz-morph-and-rotation',
    timeSeconds: 1,
    rotationX: 0.25,
    rotationY: 0.5,
    morphWeight: Math.abs(Math.sin(0.5))
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

/** Parses strict value-bearing long options accepted by the line fixture. */
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

/** Resolves one required explicit fixture path without environment fallback. */
function requirePathOption(options, name) {
  if (!options[name]) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(options[name]);
}

/** Parses one optional positive-integer watchdog value. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) {
    return fallback;
  }
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Returns whether one resolved generated-product or executable path exists. */
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

/** Rejects any line Oracle digest that differs from the immutable r185 capture. */
export function validateOracleSha256(scenarioId, extension, actualSha256) {
  const expectedSha256 = lockedOracleSha256[scenarioId]?.[extension];
  if (expectedSha256 === undefined) {
    throw new Error(`No locked Oracle SHA-256 for ${scenarioId}.${extension}.`);
  }
  return actualSha256 === expectedSha256
    ? []
    : [
      `oracle.${scenarioId}.${extension}.sha256=${actualSha256}, expected ${expectedSha256}.`
    ];
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
          if (typeof actualElement !== 'number' || Math.abs(actualElement - expectedElement) > 1e-7) {
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

/** Builds the complete structural expectation for one locked morph scenario. */
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
    explicitVertexCount: 10_000,
    vertexStrideBytes: 36,
    standaloneGeometryBufferCount: 1,
    primitiveTopology: 'line-strip',
    morphTargetCount: 1,
    morphTargetsRelative: false,
    seed: defaultThreeRandomSeed,
    upstreamPreVertexRandomDrawCount: 92,
    upstreamPostVertexRandomDrawCount: 40,
    finalRandomState: 1_523_321_029,
    timeSeconds: scenario.timeSeconds,
    rotationX: scenario.rotationX,
    rotationY: scenario.rotationY,
    morphWeight: scenario.morphWeight,
    cameraFovDegrees: 27,
    cameraNear: 1,
    cameraFar: 4000,
    cameraPositionZ: 2750,
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main-line-strip',
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
        name: 'main-line-strip',
        renderClass: passName,
        renderSetId: null,
        renderSetBindingCount: 0,
        drawMode: 'explicit-nonindexed',
        invocationCount: 1,
        drawCommandCount: 1,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }]
    }],
    gpuWorkDslOnly: true
  };
}

/** Builds the complete explicit host CLI for one pipeline/backend quadrant. */
export function buildHostArguments(scenario, pipeline, backend, artifacts) {
  return [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(defaultThreeRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
}

/** Counts visible and alpha properties needed to reject an empty line capture. */
export function inspectRgbaPixels(pixels) {
  if (!(pixels instanceof Uint8Array) || pixels.byteLength === 0 || pixels.byteLength % 4 !== 0) {
    throw new Error('Line capture pixels must be a non-empty tightly packed RGBA8 array.');
  }
  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const uniqueRgbColors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const red = pixels[offset];
    const green = pixels[offset + 1];
    const blue = pixels[offset + 2];
    if (red !== 0 || green !== 0 || blue !== 0) {
      nonBlackPixels += 1;
    }
    if (pixels[offset + 3] !== 255) {
      nonOpaquePixels += 1;
    }
    uniqueRgbColors.add((red << 16) | (green << 8) | blue);
  }
  return {
    nonBlackPixels,
    nonOpaquePixels,
    uniqueRgbColorCount: uniqueRgbColors.size
  };
}

/** Extracts and validates the RenderSet-free line host ABI from generated C++. */
export function validateGeneratedSourceContract(pipeline, generatedSource, exportsSource) {
  const failures = [];
  const label = `${caseName}/${pipeline}`;
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${label}: the single Line Scene must not declare a RenderSet.`);
  }
  if (!generatedSource.includes(`class ${passName}`)) {
    failures.push(`${label}: generated host is missing ${passName}.`);
  }
  for (const token of [
    'WebglBuffergeometryLinesVertexCount = 10000u',
    'GVM::RHI::BufferUsage::Vertex',
    'GVM::RHI::BufferUsage::CopyDst',
    'GVM::RHI::BufferBindingType::Uniform',
    'GVM::RHI::PrimitiveTopology::LineStrip',
    'renderPass("main-line-strip"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated host is missing '${token}'.`);
    }
  }

  const vertexBufferBindings = generatedSource.match(
    /mainPass->setVertexBuffer\(vertexBuffer\)/gu) ?? [];
  if (vertexBufferBindings.length !== 1 || generatedSource.includes('setIndexBuffer')) {
    failures.push(`${label}: line rendering must bind one standalone vertex buffer and no index buffer.`);
  }
  const drawCalls = generatedSource.match(
    /mainPass->run\(WebglBuffergeometryLinesVertexCount, 1u, 0u, 0u\)/gu) ?? [];
  if (drawCalls.length !== 1) {
    failures.push(`${label}: expected one explicit 10,000-vertex draw, observed ${drawCalls.length}.`);
  }

  const strideMatch = generatedSource.match(
    /vertexState\.buffers\[0\]\.arrayStride = (\d+);/u);
  const vertexStrideBytes = strideMatch ? Number(strideMatch[1]) : null;
  if (vertexStrideBytes !== 36) {
    failures.push(`${label}: vertex stride is ${vertexStrideBytes}, expected 36.`);
  }

  const attributeFormats = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.format = GVM::RHI::VertexFormat::(\w+);/gu)]
    .map((match) => ({ index: Number(match[1]), format: match[2] }))
    .sort((left, right) => left.index - right.index);
  const attributeOffsets = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.offset = offsetof\(WebglBuffergeometryLinesVertex, (\w+)\);/gu)]
    .map((match) => ({ index: Number(match[1]), field: match[2] }))
    .sort((left, right) => left.index - right.index);
  const expectedFormats = [
    { index: 0, format: 'Float32x3' },
    { index: 1, format: 'Float32x3' },
    { index: 2, format: 'Float32x3' }
  ];
  const expectedOffsets = [
    { index: 0, field: 'position' },
    { index: 1, field: 'morphPosition' },
    { index: 2, field: 'color' }
  ];
  if (JSON.stringify(attributeFormats) !== JSON.stringify(expectedFormats)) {
    failures.push(
      `${label}: vertex formats are ${JSON.stringify(attributeFormats)}, expected ${JSON.stringify(expectedFormats)}.`);
  }
  if (JSON.stringify(attributeOffsets) !== JSON.stringify(expectedOffsets)) {
    failures.push(
      `${label}: vertex offsets are ${JSON.stringify(attributeOffsets)}, expected ${JSON.stringify(expectedOffsets)}.`);
  }

  const abiSignature = {
    renderSetExportCount: 0,
    uniformBinding: 0,
    vertexStrideBytes,
    vertexAttributes: attributeFormats.map((entry, index) => ({
      ...entry,
      field: attributeOffsets[index]?.index === entry.index
        ? attributeOffsets[index].field
        : null
    })),
    primitiveTopology: generatedSource.includes('GVM::RHI::PrimitiveTopology::LineStrip')
      ? 'LineStrip'
      : null,
    standaloneVertexBufferCount: vertexBufferBindings.length,
    indexed: generatedSource.includes('setIndexBuffer'),
    explicitVertexCount: drawCalls.length === 1 ? 10_000 : null,
    explicitInstanceCount: drawCalls.length === 1 ? 1 : null
  };
  return {
    pipeline,
    abiSignature,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Validates Experimental UGLIR, MSL, and direct-SPIR-V stage contents. */
export function validateExperimentalProductSources(productSources) {
  const failures = [];
  for (const stage of ['vertex', 'fragment']) {
    const sources = productSources[stage];
    const label = `experimental ${stage}`;
    if (!sources) {
      failures.push(`${label}: product sources are missing.`);
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
        reflection: {
          entryName: `${passName}.${stage}`,
          stage
        }
      }, `${label}.uglir`, failures);
    }
    for (const token of [
      `module "${passName}.${stage}"`,
      `stage ${stage}`
    ]) {
      if (!sources.uglirText.includes(token)) {
        failures.push(`${label}: UGLIR text is missing '${token}'.`);
      }
    }
    const mslEntry = stage === 'vertex' ? 'vertexMain' : 'fragmentMain';
    if (!sources.msl.includes(stage) || !sources.msl.includes(mslEntry)) {
      failures.push(`${label}: MSL is missing the ${stage} entry point.`);
    }
    if (!sources.spirvWords.includes('word_count') ||
        !sources.spirvWords.includes('0x07230203')) {
      failures.push(`${label}: direct-SPIR-V words are missing the module header.`);
    }
    const spirvEntry = stage === 'vertex' ? 'OpEntryPoint Vertex' : 'OpEntryPoint Fragment';
    if (!sources.spirvAssembly.includes(spirvEntry)) {
      failures.push(`${label}: direct-SPIR-V assembly is missing '${spirvEntry}'.`);
    }
  }
  if (productSources.vertex &&
      (!productSources.vertex.uglirText.includes('morphPosition') ||
       !productSources.vertex.uglirText.includes('intrinsic math_lerp'))) {
    failures.push('experimental vertex: UGLIR does not preserve the absolute morph-position blend.');
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
  } else if (JSON.stringify(legacy.abiSignature) !== JSON.stringify(experimental.abiSignature)) {
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

/** Executes one generated line host under a hard watchdog with bounded output. */
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

/** Reads all required Experimental products for both programmable stages. */
async function readExperimentalProductSources(generatedDirectory, failures) {
  const productSources = {};
  const productPaths = [];
  for (const stage of ['vertex', 'fragment']) {
    const baseName = `${passName}__${stage}`;
    const stagePaths = {
      uglirJson: path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
      uglirText: path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
      msl: path.join(generatedDirectory, 'msl', `${baseName}.msl`),
      spirvWords: path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
      spirvAssembly: path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
    };
    const stageSources = {};
    for (const [kind, productPath] of Object.entries(stagePaths)) {
      productPaths.push(productPath);
      if (!await pathExists(productPath)) {
        failures.push(
          `WebglBuffergeometryLines/experimental: missing ${path.relative(generatedDirectory, productPath)}.`);
        stageSources[kind] = '';
      } else {
        stageSources[kind] = await fs.readFile(productPath, 'utf8');
      }
    }
    productSources[stage] = stageSources;
  }
  return { productPaths, productSources };
}

/** Validates one isolated Legacy or Experimental generated-product directory. */
async function validateGeneratedPipeline(generatedRoot, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, caseName, 'UGLBin');
  const exportsPath = path.join(generatedDirectory, 'exports.hpp');
  const generatedHeaderPath = path.join(generatedDirectory, 'generate_result.hpp');
  const missingHostProducts = [];
  for (const targetPath of [exportsPath, generatedHeaderPath]) {
    if (!await pathExists(targetPath)) {
      missingHostProducts.push(targetPath);
    }
  }
  if (missingHostProducts.length > 0) {
    return {
      pipeline,
      generatedDirectory,
      abiSignature: null,
      status: 'fail',
      failures: missingHostProducts.map(
        (targetPath) => `${caseName}/${pipeline}: missing ${targetPath}.`)
    };
  }

  const [exportsSource, generatedSource] = await Promise.all([
    fs.readFile(exportsPath, 'utf8'),
    fs.readFile(generatedHeaderPath, 'utf8')
  ]);
  const validation = validateGeneratedSourceContract(
    pipeline, generatedSource, exportsSource);
  const result = {
    ...validation,
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

/** Validates the immutable Three r185 Oracle metadata for one scenario. */
export function validateOracleMetadata(metadata, scenario) {
  const failures = [];
  validateExpectedFields(metadata, {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    randomSeed: defaultThreeRandomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: null
  }, `oracle.${scenario.id}`, failures);
  return failures;
}

/** Loads and validates both immutable Oracle captures from the explicit root. */
async function loadOracleImages(oracleRoot) {
  const images = new Map();
  for (const scenario of lineScenarios) {
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
    if (failures.length > 0) {
      throw new Error(failures.join('\n'));
    }
    images.set(scenario.id, {
      ...image,
      sha256: Object.freeze({ rgba: rgbaSha256, json: metadataSha256 })
    });
  }
  return images;
}

/** Validates metadata, structural state, morph state, and visible line pixels. */
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
    randomSeed: defaultThreeRandomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);

  const imageInspection = inspectRgbaPixels(image.pixels);
  if (imageInspection.nonBlackPixels === 0 || imageInspection.uniqueRgbColorCount < 2) {
    failures.push('capture is empty or clear-only; expected visible line-strip pixels.');
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
  const argumentsList = buildHostArguments(scenario, pipeline, backend, artifacts);
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
        'Both required quadrants must pass host and structural validation before parity comparison.');
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

/** Executes the dedicated r185 line fixture through all generated and image gates. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

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
  for (const scenario of lineScenarios) {
    const scenarioQuadrants = [];
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const result = await runQuadrant(
          context, scenario, pipeline, backend, oracleImages.get(scenario.id));
        quadrants.push(result);
        scenarioQuadrants.push(result);
        console.log(
          `[${result.status.toUpperCase()}] ${caseId}/${scenario.id} ${pipeline}/${backend}`);
        for (const failure of result.failures) {
          console.log(`  ${failure}`);
        }
      }
    }
    comparisons.push(...await compareScenarioQuadrants(scenario, scenarioQuadrants));
  }

  const status = generatedArtifacts.status === 'pass' &&
    quadrants.length === lineScenarios.length * pipelines.length * backends.length &&
    quadrants.every((entry) => entry.status === 'pass') &&
    comparisons.length === lineScenarios.length * parityDefinitions.length &&
    comparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-buffergeometry-lines',
    caseId,
    status,
    comparisonThresholds,
    oracles: lineScenarios.map((scenario) => ({
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
    for (const failure of entry.failures) {
      console.log(`  ${failure}`);
    }
  }
  console.log(`[${generatedAbiParity.status.toUpperCase()}] ${caseId} Legacy/Experimental ABI`);
  for (const comparison of comparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${caseId}/${comparison.scenarioId} ${comparison.relation}`);
    for (const failure of comparison.failures) {
      console.log(`  ${failure}`);
    }
  }
  console.log(`webgl_buffergeometry_lines report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
