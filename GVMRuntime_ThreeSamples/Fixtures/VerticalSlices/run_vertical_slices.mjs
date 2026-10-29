#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  compareThreeCaptures,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { defaultThreeRandomSeed } from '../../../tests/runners/three/node/determinism.mjs';

const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const sliceDefinitions = Object.freeze([
  {
    name: 'SimpleRenderClass',
    caseId: 'simple-render-class',
    scenarioId: 'single-object',
    passNames: ['SimpleRenderClassScenePass'],
    expectedSnapshot: {
      sceneRenderSetCount: 0,
      renderableObjectCount: 1,
      instanceCount: 1,
      containsHierarchy: false,
      materialCount: 1,
      sceneRenderClassCount: 1,
      scenePassCount: 1,
      screenPassCount: 0,
      drawCommandCount: 1,
      explicitVertexCount: 3,
      gpuWorkDslOnly: true
    }
  },
  {
    name: 'FullscreenPostprocess',
    caseId: 'fullscreen-postprocess',
    scenarioId: 'offscreen-screen-pass',
    passNames: ['FullscreenPostprocessScenePass', 'FullscreenPostprocessScreenPass'],
    expectedSnapshot: {
      sceneRenderSetCount: 0,
      screenPassRenderSetCount: 0,
      renderableObjectCount: 1,
      instanceCount: 1,
      sceneRenderClassCount: 1,
      screenRenderClassCount: 1,
      scenePassCount: 1,
      screenPassCount: 1,
      drawCommandCount: 2,
      sceneOffscreenOutput: true,
      fullscreenTriangle: true,
      gpuWorkDslOnly: true
    }
  }
]);

/** Parses strict value-bearing long-form options for the vertical-slice fixture. */
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

/** Resolves one required explicit path option without consulting environment variables. */
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

/** Returns whether one explicitly resolved path exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Runs one host under a hard watchdog and captures bounded diagnostics. */
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

/** Returns deterministic output paths for one slice pipeline/backend quadrant. */
function makeArtifactPaths(outputRoot, slice, pipeline, backend) {
  const artifactDirectory = path.join(outputRoot, slice.name, pipeline, backend);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'final.rgba'),
    metadataPath: path.join(artifactDirectory, 'final.json'),
    snapshotPath: path.join(artifactDirectory, 'snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the fully explicit CLI contract for one static vertical-slice capture. */
function buildHostArguments(slice, pipeline, backend, artifacts) {
  return [
    '--case-id', slice.caseId,
    '--scenario-id', slice.scenarioId,
    '--pipeline', pipeline,
    '--backend', backend,
    '--width', '800',
    '--height', '500',
    '--frame', '0',
    '--random-seed', String(defaultThreeRandomSeed),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
}

/** Validates generated products for explicit draws and an empty RenderSet export namespace. */
async function validateGeneratedArtifacts(generatedRoot, slice, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, slice.name, 'UGLBin');
  const failures = [];
  const exportsPath = path.join(generatedDirectory, 'exports.hpp');
  const generatedHeaderPath = path.join(generatedDirectory, 'generate_result.hpp');
  if (!await pathExists(exportsPath) || !await pathExists(generatedHeaderPath)) {
    return [`${slice.name}/${pipeline}: generated host artifacts are missing.`];
  }
  const [exportsSource, generatedSource] = await Promise.all([
    fs.readFile(exportsPath, 'utf8'),
    fs.readFile(generatedHeaderPath, 'utf8')
  ]);
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${slice.name}/${pipeline}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${slice.name}/${pipeline}: generated vertical slice unexpectedly declares a RenderSet.`);
  }
  if (generatedSource.includes('setVertexBuffer') || generatedSource.includes('setIndexBuffer')) {
    failures.push(`${slice.name}/${pipeline}: procedural slice unexpectedly binds standalone geometry buffers.`);
  }
  const explicitDrawCount = [...generatedSource.matchAll(/->run\(3u, 1u, 0u, 0u\)/gu)].length;
  if (explicitDrawCount !== slice.passNames.length) {
    failures.push(`${slice.name}/${pipeline}: expected ${slice.passNames.length} explicit three-vertex draws; observed ${explicitDrawCount}.`);
  }
  for (const passName of slice.passNames) {
    if (!generatedSource.includes(`class ${passName}`)) {
      failures.push(`${slice.name}/${pipeline}: generated host is missing ${passName}.`);
    }
    if (pipeline === 'experimental') {
      for (const stage of ['vertex', 'fragment']) {
        const requiredProducts = [
          path.join(generatedDirectory, 'uglir', `${passName}__${stage}.uglir.json`),
          path.join(generatedDirectory, 'msl', `${passName}__${stage}.msl`),
          path.join(generatedDirectory, 'spv', `${passName}__${stage}.raw.spv.txt`)
        ];
        for (const productPath of requiredProducts) {
          if (!await pathExists(productPath)) {
            failures.push(`${slice.name}/${pipeline}: missing ${path.relative(generatedDirectory, productPath)}.`);
          }
        }
      }
    }
  }
  if (slice.name === 'FullscreenPostprocess') {
    const sceneOffset = generatedSource.indexOf('renderPass("FullscreenPostprocessScene"');
    const screenOffset = generatedSource.indexOf('renderPass("FullscreenPostprocessScreen"');
    if (sceneOffset < 0 || screenOffset <= sceneOffset) {
      failures.push(`${slice.name}/${pipeline}: screen pass must follow the offscreen scene pass.`);
    }
  }
  return failures;
}

/** Validates fixed metadata, non-clear image content, and the structural snapshot. */
async function validateQuadrantArtifacts(slice, pipeline, backend, artifacts) {
  const [image, snapshotText] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8')
  ]);
  const snapshot = JSON.parse(snapshotText);
  const failures = [];
  const expectedMetadata = {
    caseId: slice.caseId,
    scenarioId: slice.scenarioId,
    pipeline,
    backend,
    frame: 0,
    randomSeed: defaultThreeRandomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  };
  for (const [name, expected] of Object.entries(expectedMetadata)) {
    if (image.metadata?.[name] !== expected) {
      failures.push(`${slice.name}/${backend}: metadata ${name}=${JSON.stringify(image.metadata?.[name])}, expected ${JSON.stringify(expected)}.`);
    }
  }
  for (const [name, expected] of Object.entries(slice.expectedSnapshot)) {
    if (snapshot?.[name] !== expected) {
      failures.push(`${slice.name}/${backend}: snapshot ${name}=${JSON.stringify(snapshot?.[name])}, expected ${JSON.stringify(expected)}.`);
    }
  }

  const firstPixel = image.pixels.slice(0, 4);
  let nonUniformPixels = 0;
  let maximumRgb = 0;
  const uniqueColors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    if (red !== firstPixel[0] || green !== firstPixel[1] || blue !== firstPixel[2]) {
      nonUniformPixels += 1;
    }
    uniqueColors.add((red << 16) | (green << 8) | blue);
    maximumRgb = Math.max(maximumRgb, red, green, blue);
  }
  if (nonUniformPixels < 10_000 || uniqueColors.size < 512 || maximumRgb < 128) {
    failures.push(`${slice.name}/${backend}: capture is clear-only or lacks rendered scene content.`);
  }
  return {
    failures,
    snapshot,
    imageValidation: {
      firstPixelRgba: [...firstPixel],
      nonUniformPixels,
      uniqueRgbColorCount: uniqueColors.size,
      maximumRgb
    }
  };
}

/** Runs and validates one Legacy/Experimental and Metal/Vulkan quadrant. */
async function runQuadrant(context, slice, pipeline, backend) {
  const artifacts = makeArtifactPaths(context.outputRoot, slice, pipeline, backend);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${slice.name}-${pipeline}`);
  const argumentsList = buildHostArguments(slice, pipeline, backend, artifacts);
  const result = {
    slice: slice.name,
    pipeline,
    backend,
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
      const validation = await validateQuadrantArtifacts(slice, pipeline, backend, artifacts);
      result.validation = validation;
      result.failures.push(...validation.failures);
    } catch (error) {
      result.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Builds the mandatory same-backend and same-pipeline comparison pairs for one slice. */
async function compareSliceQuadrants(slice, quadrants) {
  const byKey = new Map(quadrants.map((quadrant) => [`${quadrant.pipeline}|${quadrant.backend}`, quadrant]));
  const definitions = [
    ['legacy', 'metal', 'experimental', 'metal', 'pipeline-parity-metal'],
    ['legacy', 'vulkan', 'experimental', 'vulkan', 'pipeline-parity-vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy'],
    ['experimental', 'metal', 'experimental', 'vulkan', 'backend-parity-experimental']
  ];
  const comparisons = [];
  for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of definitions) {
    const left = byKey.get(`${leftPipeline}|${leftBackend}`);
    const right = byKey.get(`${rightPipeline}|${rightBackend}`);
    const comparison = { slice: slice.name, relation, status: 'fail', failures: [] };
    if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
      comparison.failures.push('Both required quadrants must pass before image comparison.');
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

/** Executes both vertical slices through the full four-quadrant runtime gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    sourceRoot: path.resolve(scriptDirectory, '..', '..', '..'),
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

  const generatedArtifacts = [];
  const quadrants = [];
  const comparisons = [];
  for (const slice of sliceDefinitions) {
    for (const pipeline of pipelines) {
      const failures = await validateGeneratedArtifacts(context.generatedRoot, slice, pipeline);
      generatedArtifacts.push({ slice: slice.name, pipeline, status: failures.length === 0 ? 'pass' : 'fail', failures });
    }
    const sliceQuadrants = [];
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const result = await runQuadrant(context, slice, pipeline, backend);
        sliceQuadrants.push(result);
        quadrants.push(result);
        console.log(`[${result.status.toUpperCase()}] ${slice.name} ${pipeline}/${backend}`);
        for (const failure of result.failures) {
          console.log(`  ${failure}`);
        }
      }
    }
    comparisons.push(...await compareSliceQuadrants(slice, sliceQuadrants));
  }

  const status = generatedArtifacts.every((entry) => entry.status === 'pass')
    && quadrants.every((entry) => entry.status === 'pass')
    && comparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = { schemaVersion: 1, gate: 'three-phase2-vertical-slices', status, generatedArtifacts, quadrants, comparisons };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  for (const comparison of comparisons) {
    console.log(`[${comparison.status.toUpperCase()}] ${comparison.slice} ${comparison.relation}`);
  }
  console.log(`Vertical-slice report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
