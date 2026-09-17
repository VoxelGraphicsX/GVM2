#!/usr/bin/env node

import { createHash } from 'node:crypto';
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
const caseId = 'webgl_geometry_terrain_raycast';
const shardName = 'WebglGeometryTerrainRaycast';
const replayRelativePath = path.join(
  'inputs', 'webgl_geometry_terrain_raycast_pointer.json');
const replaySourcePath = path.join(
  repositoryRoot, 'GVMRuntime_ThreeSamples', replayRelativePath);
const replaySha256 =
  'ecfcf5e158794095f23cf3bdb38998083ca3d90a4d254df2cb2e6050e1ca69ef';

/** Parses unique value-bearing options without environment configuration. */
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

/** Resolves one mandatory explicit filesystem path. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses the mandatory strict stability repetition count. */
function parseRepeatCount(value) {
  const parsed = value == null ? 3 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 3) {
    throw new Error(`--repeat must be an integer of at least 3; received '${value}'.`);
  }
  return parsed;
}

/** Parses one positive per-capture watchdog duration. */
function parseTimeout(value) {
  const parsed = value == null ? 60_000 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`--timeout-ms must be positive; received '${value}'.`);
  }
  return parsed;
}

/** Loads and validates the frozen terrain raycast Manifest contract. */
async function loadSelectedExample(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error('Terrain raycast fixture requires the frozen 588-item Manifest.');
  }
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (example?.status !== 'phase1_required'
      || example.renderSetPolicy !== 'required'
      || example.renderSetType !== 'WebglGeometryTerrainRaycastSceneRenderSet'
      || example.renderableObjectCount !== 2
      || example.scenarios?.length !== 3) {
    throw new Error(`${caseId} Manifest ownership differs from the locked contract.`);
  }
  return example;
}

/** Stages and verifies the canonical post-first-frame pointer replay. */
async function stageReplay(outputDirectory) {
  const bytes = await fs.readFile(replaySourcePath);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  if (sha256 !== replaySha256) {
    throw new Error(`${replaySourcePath}: pointer replay identity differs.`);
  }
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const destination = path.join(assetRoot, replayRelativePath);
  await fs.mkdir(path.dirname(destination), { recursive: true });
  await fs.writeFile(destination, bytes);
  return {
    assetRoot,
    identity: {
      path: replayRelativePath,
      sha256,
      byteCount: bytes.byteLength
    }
  };
}

/** Lints both generated pipelines as one RenderSet-only Scene pass. */
function createGeneratedArtifactReport(generatedRoot) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: ['WebglGeometryTerrainRaycastMainPass'],
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Runs all scenarios through Legacy and Experimental on both backends. */
async function runMatrix(context, example) {
  const quadrants = [];
  for (let repetition = 1; repetition <= context.repetitions; repetition += 1) {
    for (const scenario of example.scenarios) {
      for (const pipeline of requiredPipelines) {
        for (const backend of requiredBackends) {
          const result = await runQuadrant(
            context, example, scenario, pipeline, backend, repetition);
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

/** Executes the strict terrain raycast gate and writes canonical summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const outputDirectory = requirePathOption(options, 'output-dir');
  const manifestPath = path.resolve(
    options.manifest ?? path.join(
      repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest',
      'three-r185-manifest.json'));
  const repetitions = parseRepeatCount(options.repeat);
  const timeoutMs = parseTimeout(options['timeout-ms']);
  await fs.mkdir(outputDirectory, { recursive: true });
  const [example, replay, gpuBoundaryLint] = await Promise.all([
    loadSelectedExample(manifestPath),
    stageReplay(outputDirectory),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = createGeneratedArtifactReport(generatedRoot);
  const context = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot: replay.assetRoot,
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
  const expectedRunCount = example.scenarios.length
    * requiredPipelines.length * requiredBackends.length * repetitions;
  const status = quadrants.length === expectedRunCount
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass')
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
    replayIdentity: replay.identity,
    quadrants,
    crossComparisons,
    stabilityComparisons,
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
    path.join(outputDirectory, 'cases', caseId, 'summary.json'), summary);
  console.log(
    `Terrain raycast fixture ${status}: ${path.join(outputDirectory, 'summary.json')}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
