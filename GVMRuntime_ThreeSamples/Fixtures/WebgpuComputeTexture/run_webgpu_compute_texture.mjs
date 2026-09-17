#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import { defaultThreeRandomSeed } from '../../../tests/runners/three/node/determinism.mjs';
import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { buildStabilityComparisons } from '../../../tests/runners/three/node/runner.mjs';
import { scanSampleCppSource } from '../../Tools/lint_sample_gpu_boundary.mjs';

const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const caseName = 'WebgpuComputeTexture';
const caseId = 'webgpu_compute_texture';
const scenarioId = 'initial';

/** Parses strict value-bearing long-form options for the dedicated fixture. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  return options;
}

/** Resolves one required explicit path without consulting environment variables. */
function requirePathOption(options, name) {
  if (!options[name]) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(options[name]);
}

/** Parses one optional positive integer fixture setting. */
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

/** Parses a repeat count that preserves the mandatory three-run stability gate. */
function parseRepeatCount(value) {
  const repeatCount = parsePositiveInteger(value, 3, '--repeat');
  if (repeatCount < 3) {
    throw new Error(`--repeat must be at least 3; received '${repeatCount}'.`);
  }
  return repeatCount;
}

/** Returns whether one resolved artifact path currently exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Executes one host under a hard watchdog while retaining bounded diagnostics. */
async function executeHost(executable, argumentsList, sourceRoot, timeoutMs) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, argumentsList, {
      cwd: sourceRoot,
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

/** Returns deterministic output paths for one pipeline/backend quadrant. */
function makeArtifactPaths(outputRoot, pipeline, backend, repetition) {
  const artifactDirectory = path.join(outputRoot, pipeline, backend, `repeat-${repetition}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'initial.rgba'),
    metadataPath: path.join(artifactDirectory, 'initial.json'),
    snapshotPath: path.join(artifactDirectory, 'snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the complete explicit CLI contract for the locked initial frame. */
function buildHostArguments(pipeline, backend, artifacts) {
  return [
    '--case-id', caseId,
    '--scenario-id', scenarioId,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(defaultThreeRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', '0',
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
}

/** Returns the required Experimental UGLIR, MSL, and direct-SPIR-V products. */
function makeExperimentalProductPaths(generatedDirectory) {
  const artifactBases = [
    'WebgpuComputeTextureComputePass',
    'WebgpuComputeTexturePlanePass__vertex',
    'WebgpuComputeTexturePlanePass__fragment'
  ];
  return artifactBases.flatMap((artifactBase) => [
    path.join(generatedDirectory, 'uglir', `${artifactBase}.uglir.json`),
    path.join(generatedDirectory, 'msl', `${artifactBase}.msl`),
    path.join(generatedDirectory, 'spv', `${artifactBase}.raw.spv.txt`),
    path.join(generatedDirectory, 'spv', `${artifactBase}.raw.spvasm`)
  ]);
}

/** Validates the generated host ABI and Experimental shader products for one pipeline. */
async function validateGeneratedArtifacts(generatedRoot, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, caseName, 'UGLBin');
  const failures = [];
  const exportsPath = path.join(generatedDirectory, 'exports.hpp');
  const generatedHeaderPath = path.join(generatedDirectory, 'generate_result.hpp');
  if (!await pathExists(exportsPath) || !await pathExists(generatedHeaderPath)) {
    return {
      pipeline,
      generatedDirectory,
      status: 'fail',
      failures: [`${pipeline}: generated host artifacts are missing.`]
    };
  }

  const [exportsSource, generatedSource] = await Promise.all([
    fs.readFile(exportsPath, 'utf8'),
    fs.readFile(generatedHeaderPath, 'utf8')
  ]);
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${pipeline}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${pipeline}: the single-plane case must not declare a RenderSet.`);
  }
  for (const requiredToken of [
    'struct WebgpuComputeTextureStorageBindGroup',
    'struct WebgpuComputeTexturePlaneBindGroup',
    'class WebgpuComputeTextureComputePass',
    'class WebgpuComputeTexturePlanePass',
    'TextureFormat::RGBA8Unorm',
    'StorageBinding',
    'TextureBinding',
    'computePass("WebgpuComputeTextureCompute"',
    'renderPass("WebgpuComputeTexturePlane"'
  ]) {
    if (!generatedSource.includes(requiredToken)) {
      failures.push(`${pipeline}: generated host is missing '${requiredToken}'.`);
    }
  }
  if (generatedSource.includes('setVertexBuffer') || generatedSource.includes('setIndexBuffer')) {
    failures.push(`${pipeline}: the procedural plane unexpectedly binds standalone geometry buffers.`);
  }
  const computeOffset = generatedSource.indexOf('computePass("WebgpuComputeTextureCompute"');
  const planeOffset = generatedSource.indexOf('renderPass("WebgpuComputeTexturePlane"');
  if (computeOffset < 0 || planeOffset <= computeOffset) {
    failures.push(`${pipeline}: compute must be submitted before the plane render pass.`);
  }

  const products = pipeline === 'experimental'
    ? makeExperimentalProductPaths(generatedDirectory)
    : [];
  const productExistence = await Promise.all(products.map((productPath) => pathExists(productPath)));
  for (let productIndex = 0; productIndex < products.length; productIndex += 1) {
    const productPath = products[productIndex];
    if (!productExistence[productIndex]) {
      failures.push(`${pipeline}: missing ${path.relative(generatedDirectory, productPath)}.`);
    }
  }

  if (pipeline === 'experimental' && productExistence.every(Boolean)) {
    const computeUglirPath = path.join(
      generatedDirectory,
      'uglir',
      'WebgpuComputeTextureComputePass.uglir.json');
    const computeMslPath = path.join(
      generatedDirectory,
      'msl',
      'WebgpuComputeTextureComputePass.msl');
    const computeSpirvPath = path.join(
      generatedDirectory,
      'spv',
      'WebgpuComputeTextureComputePass.raw.spv.txt');
    const computeSpirvAssemblyPath = path.join(
      generatedDirectory,
      'spv',
      'WebgpuComputeTextureComputePass.raw.spvasm');
    const [uglirSource, mslSource, spirvSource, spirvAssemblySource] = await Promise.all([
      fs.readFile(computeUglirPath, 'utf8'),
      fs.readFile(computeMslPath, 'utf8'),
      fs.readFile(computeSpirvPath, 'utf8'),
      fs.readFile(computeSpirvAssemblyPath, 'utf8')
    ]);
    for (const [label, source, tokens] of [
      ['UGLIR', uglirSource, ['DispatchThreadID', 'storageTexture', 'sin', 'sqrt']],
      ['MSL', mslSource, ['kernel', 'thread_position_in_grid', 'storageTexture', 'sin', 'sqrt']],
      ['direct SPIR-V words', spirvSource, ['word_count', '0x07230203']],
      ['direct SPIR-V assembly', spirvAssemblySource, ['OpEntryPoint GLCompute', 'OpExecutionMode', 'LocalSize 8 8 1', 'OpImageWrite']]
    ]) {
      for (const token of tokens) {
        if (!source.includes(token)) {
          failures.push(`experimental ${label}: missing '${token}'.`);
        }
      }
    }
  }

  return {
    pipeline,
    generatedDirectory,
    products,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Scans the dedicated C++ adapter for GPU work that must remain in the DSL. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebgpuComputeTextureRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebgpuComputeTextureRuntimeAdapter.cpp')
  ];
  const violations = [];
  for (const filePath of files) {
    const source = await fs.readFile(filePath, 'utf8');
    violations.push(...scanSampleCppSource(
      source,
      path.relative(path.resolve(scriptDirectory, '../../..'), filePath)
    ));
  }
  return {
    status: violations.length === 0 ? 'pass' : 'fail',
    filesScanned: files.length,
    violationCount: violations.length,
    violations
  };
}

/** Validates the fixed host metadata, structural snapshot, and non-clear plane footprint. */
async function validateQuadrantArtifacts(pipeline, backend, artifacts) {
  const [image, snapshotText] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8')
  ]);
  const snapshot = JSON.parse(snapshotText);
  const failures = [];
  const expectedMetadata = {
    schemaVersion: 1,
    source: 'gvm-three-r185',
    caseId,
    scenarioId,
    pipeline,
    backend,
    randomSeed: defaultThreeRandomSeed,
    frame: 0,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  };
  for (const [name, expected] of Object.entries(expectedMetadata)) {
    if (image.metadata?.[name] !== expected) {
      failures.push(`metadata ${name}=${JSON.stringify(image.metadata?.[name])}, expected ${JSON.stringify(expected)}.`);
    }
  }

  const expectedSnapshot = {
    schemaVersion: 1,
    caseId,
    scenarioId,
    frame: 0,
    upstreamRevision: 'r185',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    materialCount: 1,
    storageTextureFormat: 'rgba8unorm',
    storageTextureWidth: 512,
    storageTextureHeight: 512,
    computeInvocationCount: 262144,
    computePassCount: 1,
    scenePassCount: 1,
    screenPassCount: 0,
    drawCommandCount: 1,
    drawVertexCount: 6,
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'compute-texture-display',
      entityOrdinal: 0
    }],
    gpuWorkDslOnly: true
  };
  for (const [name, expected] of Object.entries(expectedSnapshot)) {
    const actual = snapshot?.[name];
    const differs = expected !== null && typeof expected === 'object'
      ? JSON.stringify(actual) !== JSON.stringify(expected)
      : actual !== expected;
    if (differs) {
      failures.push(`snapshot ${name}=${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}.`);
    }
  }

  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const uniqueRgbColors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    const alpha = image.pixels[offset + 3];
    if (red !== 0 || green !== 0 || blue !== 0) {
      nonBlackPixels += 1;
    }
    if (alpha !== 255) {
      nonOpaquePixels += 1;
    }
    uniqueRgbColors.add((red << 16) | (green << 8) | blue);
  }
  if (nonBlackPixels !== 62_500) {
    failures.push(`rendered plane contains ${nonBlackPixels} non-black pixels; expected 62500.`);
  }
  if (nonOpaquePixels !== 0) {
    failures.push(`capture contains ${nonOpaquePixels} non-opaque pixels.`);
  }

  return {
    failures,
    image,
    snapshot,
    imageInspection: {
      nonBlackPixels,
      nonOpaquePixels,
      uniqueRgbColorCount: uniqueRgbColors.size
    }
  };
}

/** Runs and validates one Legacy/Experimental and Metal/Vulkan quadrant. */
async function runQuadrant(context, pipeline, backend, repetition, oracleImage) {
  const artifacts = makeArtifactPaths(context.outputRoot, pipeline, backend, repetition);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${caseName}-${pipeline}`);
  const argumentsList = buildHostArguments(pipeline, backend, artifacts);
  const result = {
    caseId,
    scenarioId,
    pipeline,
    backend,
    repetition,
    status: 'fail',
    artifacts,
    failures: []
  };
  if (!await pathExists(executable)) {
    result.failures.push(`Missing host executable: ${executable}.`);
    return result;
  }

  const execution = await executeHost(executable, argumentsList, context.sourceRoot, context.timeoutMs);
  result.host = { executable, arguments: argumentsList, ...execution };
  await fs.writeFile(artifacts.hostLogPath, `${JSON.stringify(result.host, null, 2)}\n`, 'utf8');
  if (execution.timedOut) {
    result.failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  } else if (execution.exitCode !== 0) {
    result.failures.push(`Host exited with code ${execution.exitCode}${execution.signal ? ` (${execution.signal})` : ''}.`);
  } else {
    try {
      const validation = await validateQuadrantArtifacts(pipeline, backend, artifacts);
      result.validation = {
        snapshot: validation.snapshot,
        imageInspection: validation.imageInspection
      };
      result.failures.push(...validation.failures);
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

/** Builds the mandatory same-backend and same-pipeline comparison pairs. */
async function compareQuadrants(quadrants, repeatCount) {
  const byKey = new Map(quadrants.map((quadrant) => [
    `${quadrant.repetition}|${quadrant.pipeline}|${quadrant.backend}`,
    quadrant
  ]));
  const definitions = [
    ['legacy', 'metal', 'experimental', 'metal', 'pipeline-parity-metal'],
    ['legacy', 'vulkan', 'experimental', 'vulkan', 'pipeline-parity-vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy'],
    ['experimental', 'metal', 'experimental', 'vulkan', 'backend-parity-experimental']
  ];
  const comparisons = [];
  for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
    for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of definitions) {
      const left = byKey.get(`${repetition}|${leftPipeline}|${leftBackend}`);
      const right = byKey.get(`${repetition}|${rightPipeline}|${rightBackend}`);
      const comparison = { caseId, scenarioId, repetition, relation, status: 'fail', failures: [] };
      if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
        comparison.failures.push('Both required quadrants must pass before parity comparison.');
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
  }
  return comparisons;
}

/** Validates the locked r185 oracle identity before executing GVM quadrants. */
function validateOracle(oracleImage) {
  const expected = {
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId,
    scenarioId,
    frame: 0,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  };
  for (const [name, value] of Object.entries(expected)) {
    if (oracleImage.metadata?.[name] !== value) {
      throw new Error(`Oracle metadata ${name}=${JSON.stringify(oracleImage.metadata?.[name])}, expected ${JSON.stringify(value)}.`);
    }
  }
}

/** Executes the real r185 case through generated-product and four-quadrant gates. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    sourceRoot: path.resolve(scriptDirectory, '..', '..', '..'),
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRgbaPath: requirePathOption(options, 'oracle-rgba'),
    oracleMetadataPath: requirePathOption(options, 'oracle-metadata'),
    repeatCount: parseRepeatCount(options.repeat),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

  const oracleImage = await loadRgbaArtifact(context.oracleRgbaPath, context.oracleMetadataPath);
  validateOracle(oracleImage);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    Promise.all(pipelines.map(
      (pipeline) => validateGeneratedArtifacts(context.generatedRoot, pipeline)
    )),
    validateGpuBoundary()
  ]);
  const quadrants = [];
  for (let repetition = 1; repetition <= context.repeatCount; repetition += 1) {
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const result = await runQuadrant(context, pipeline, backend, repetition, oracleImage);
        quadrants.push(result);
        console.log(`[${result.status.toUpperCase()}] ${caseId} ${pipeline}/${backend} repeat=${repetition}`);
        for (const failure of result.failures) {
          console.log(`  ${failure}`);
        }
      }
    }
  }
  const [crossComparisons, stabilityComparisons] = await Promise.all([
    compareQuadrants(quadrants, context.repeatCount),
    buildStabilityComparisons(quadrants, context.repeatCount)
  ]);
  const expectedRunCount = pipelines.length * backends.length * context.repeatCount;
  const expectedCrossComparisonCount = 4 * context.repeatCount;
  const expectedStabilityComparisonCount = pipelines.length * backends.length
    * (context.repeatCount - 1);
  const status = generatedArtifacts.every((entry) => entry.status === 'pass')
    && gpuBoundaryLint.status === 'pass'
    && quadrants.length === expectedRunCount
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.length === expectedCrossComparisonCount
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.length === expectedStabilityComparisonCount
    && stabilityComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgpu-compute-texture',
    caseId,
    scenarioId,
    status,
    comparisonThresholds,
    repeatCount: context.repeatCount,
    runCount: quadrants.length,
    expectedRunCount,
    oracle: {
      rgbaPath: context.oracleRgbaPath,
      metadataPath: context.oracleMetadataPath,
      metadata: oracleImage.metadata
    },
    generatedArtifacts,
    gpuBoundaryLint,
    quadrants,
    crossComparisons,
    stabilityComparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  for (const comparison of crossComparisons) {
    console.log(`[${comparison.status.toUpperCase()}] ${caseId} ${comparison.relation}`);
    for (const failure of comparison.failures) {
      console.log(`  ${failure}`);
    }
  }
  for (const generated of generatedArtifacts) {
    console.log(`[${generated.status.toUpperCase()}] ${caseId} ${generated.pipeline} generated products`);
    for (const failure of generated.failures) {
      console.log(`  ${failure}`);
    }
  }
  console.log(`webgpu_compute_texture report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
