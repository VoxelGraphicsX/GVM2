#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  buildCrossComparisons,
  buildStabilityComparisons,
  requiredBackends,
  requiredPipelines,
  runQuadrant,
  writeJson
} from '../../../tests/runners/three/node/runner.mjs';
import { lintGeneratedArtifacts } from '../../Tools/lint_generated_artifacts.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgpu_compute_texture_pingpong';
const shardName = 'WebgpuComputeTexturePingpong';

/** Parses unique value-bearing options without environment configuration. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(
        `Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) {
      throw new Error(`Duplicate --${name}.`);
    }
    options[name] = value;
  }
  return options;
}

/** Resolves one mandatory explicit filesystem path. */
function requirePathOption(options, name) {
  if (!options[name]) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(options[name]);
}

/** Parses the mandatory strict stability repetition count. */
function parseRepeatCount(value) {
  const parsed = value == null ? 3 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 3) {
    throw new Error(
      `--repeat must be an integer of at least 3; received '${value}'.`);
  }
  return parsed;
}

/** Parses one positive per-capture watchdog duration. */
function parseTimeout(value) {
  const parsed = value == null ? 60_000 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(
      `--timeout-ms must be positive; received '${value}'.`);
  }
  return parsed;
}

/** Loads the dedicated ping-pong example from the frozen Manifest. */
async function loadSelectedExample(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Ping-pong fixture requires the frozen 588-item Three r185 Manifest.');
  }
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (example?.status !== 'phase1_required'
      || example.renderSetPolicy !== 'not-required'
      || example.dslShard !== shardName
      || example.scenarios?.length !== 3
      || example.scenePasses?.length !== 1
      || example.scenePasses[0].renderClass
        !== 'WebgpuComputeTexturePingpongMainPass') {
    throw new Error(
      `${caseId} Manifest ownership differs from the locked contract.`);
  }
  return example;
}

/** Converts generated lint violations into the shared report shape. */
function createGeneratedArtifactReport(generatedRoot, manifestPath) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath,
    caseIds: [caseId],
    fixtureSelectors: ['WebgpuComputeTexturePingpongMainPass:screen'],
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Returns whether two scalar arrays have exactly equal ordered contents. */
function arraysEqual(left, right) {
  return Array.isArray(left)
    && left.length === right.length
    && left.every((value, index) => value === right[index]);
}

/** Validates the dedicated texture, dispatch, and draw contract in every run. */
function validateScenarioSnapshots(quadrants) {
  const failures = [];
  for (const quadrant of quadrants) {
    const snapshot = quadrant.validation?.snapshot;
    const frame = Number(snapshot?.frame);
    const expectedPresentTexture = frame % 2 === 0 ? 'pong' : 'ping';
    if (snapshot?.caseId !== caseId
        || snapshot.renderSetPolicy !== 'not-required'
        || snapshot.sceneRenderSetCount !== 0
        || snapshot.renderableObjectCount !== 1
        || snapshot.vertexCount !== 4
        || snapshot.indexCount !== 6
        || snapshot.scenePassCount !== 1
        || snapshot.drawCommandCount !== 1
        || snapshot.directDrawFallback !== false
        || snapshot.computePassCount !== frame * 2 + 3
        || snapshot.resetCount !== 1
        || snapshot.presentTexture !== expectedPresentTexture
        || !arraysEqual(
          snapshot.storageTextureFormats,
          ['rgba16float', 'rgba16float', 'rgba16float'])
        || !arraysEqual(snapshot.storageTextureDimensions, [512, 512])
        || !arraysEqual(snapshot.mipTextureDimensions, [256, 256])
        || !arraysEqual(snapshot.computeDispatchThreads, [512, 512, 1])
        || !arraysEqual(snapshot.mipDispatchThreads, [256, 256, 1])
        || !arraysEqual(
          snapshot.attachmentFormats,
          ['rgba8unorm', 'depth32float'])) {
      failures.push(
        `${quadrant.scenarioId}/${quadrant.pipeline}/${quadrant.backend}/`
        + `repeat-${quadrant.repetition}: scene contract mismatch.`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    checkedSnapshots: quadrants.length,
    failures
  };
}

/** Runs every ping-pong scenario through the full strict matrix. */
async function runMatrix(context, example) {
  const quadrants = [];
  for (let repetition = 1; repetition <= context.repetitions; repetition += 1) {
    for (const scenario of example.scenarios) {
      for (const pipeline of requiredPipelines) {
        for (const backend of requiredBackends) {
          const result = await runQuadrant(
            context,
            example,
            scenario,
            pipeline,
            backend,
            repetition
          );
          quadrants.push(result);
          console.log(
            `${result.status.toUpperCase()} ${caseId}/${scenario.id} `
            + `${pipeline}/${backend} repeat-${repetition}`);
        }
      }
    }
  }
  return quadrants;
}

/** Executes the strict dedicated ping-pong fixture and writes summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const assetRoot = requirePathOption(options, 'asset-root');
  const outputDirectory = requirePathOption(options, 'output-dir');
  const manifestPath = path.resolve(
    options.manifest ?? path.join(
      repositoryRoot,
      'GVMRuntime_ThreeSamples',
      'Manifest',
      'three-r185-manifest.json'
    )
  );
  const repetitions = parseRepeatCount(options.repeat);
  const timeoutMs = parseTimeout(options['timeout-ms']);
  await fs.rm(outputDirectory, { recursive: true, force: true });
  await fs.mkdir(outputDirectory, { recursive: true });
  const [example, gpuBoundaryLint] = await Promise.all([
    loadSelectedExample(manifestPath),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint =
    createGeneratedArtifactReport(generatedRoot, manifestPath);
  const context = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions,
    profile: {
      buildDir: outputDirectory,
      hostCandidates: {
        legacy: [path.join(binaryRoot, `${shardName}-legacy`)],
        experimental: [path.join(binaryRoot, `${shardName}-experimental`)]
      }
    }
  };
  const quadrants = await runMatrix(context, example);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons =
    await buildStabilityComparisons(quadrants, repetitions);
  const scenarioContract = validateScenarioSnapshots(quadrants);
  const expectedRunCount = example.scenarios.length
    * requiredPipelines.length
    * requiredBackends.length
    * repetitions;
  const status = quadrants.length === expectedRunCount
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioContract.status === 'pass'
    && generatedArtifactLint.status === 'pass'
    && gpuBoundaryLint.status === 'pass'
    ? 'pass'
    : 'fail';
  const summary = {
    schemaVersion: 1,
    status,
    selectedCases: [caseId],
    repeatCount: repetitions,
    runCount: quadrants.length,
    expectedRunCount,
    pipelines: [...requiredPipelines],
    backends: [...requiredBackends],
    inputIdentities: [],
    quadrants,
    crossComparisons,
    stabilityComparisons,
    scenarioContract,
    generatedArtifacts: [generatedArtifactLint],
    gpuBoundaryLint,
    thresholds: {
      normalizedDistancePixelRatio: 0.001,
      meanAbsoluteRgb: 2.0,
      p99AbsoluteRgb: 16,
      luminanceSsim: 0.995
    }
  };
  await writeJson(path.join(outputDirectory, 'summary.json'), summary);
  await writeJson(
    path.join(outputDirectory, 'cases', caseId, 'summary.json'),
    summary
  );
  console.log(
    `Ping-pong fixture ${status}: `
    + path.join(outputDirectory, 'summary.json'));
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(
      error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
