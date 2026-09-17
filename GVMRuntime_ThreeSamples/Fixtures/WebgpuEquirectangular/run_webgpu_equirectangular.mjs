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
const caseId = 'webgpu_equirectangular';
const shardName = 'WebgpuEquirectangularScreen';
const screenPassName = 'WebgpuEquirectangularMainPass';
const panoramaRelativePath =
  path.join('textures', '2294472375_24a3b8ef46_o.jpg');
const panoramaSha256 =
  '3efa22071f3f84ec26248ee58aa05aba635a0a9d810bca93da5a94f33e907524';
const replayRelativePath =
  path.join('inputs', 'webgpu_equirectangular_gui_orbit.json');
const replaySourcePath =
  path.join(scriptDirectory, 'Inputs', path.basename(replayRelativePath));
const replaySha256 =
  'c8d708be0972f251595eadc7a1fe8085de863c5ea0eeac2e3cac7f3b7de828ba';

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

/** Returns the lowercase SHA-256 identity of one immutable byte sequence. */
function calculateSha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Loads the dedicated equirectangular example from the frozen Manifest. */
async function loadSelectedExample(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Equirectangular fixture requires the frozen 588-item Three r185 Manifest.');
  }
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (example?.status !== 'phase1_required'
      || example.renderSetPolicy !== 'not-required'
      || example.dslShard !== shardName
      || example.renderSetType !== null
      || example.scenarios?.length !== 3) {
    throw new Error(
      `${caseId} Manifest ownership differs from the locked contract.`);
  }
  return example;
}

/** Copies and verifies the pinned panorama and replay into the runner asset root. */
async function stageInputs(outputDirectory, assetSourceRoot) {
  const panoramaSourcePath =
    path.join(assetSourceRoot, panoramaRelativePath);
  const [panoramaBytes, replayBytes] = await Promise.all([
    fs.readFile(panoramaSourcePath),
    fs.readFile(replaySourcePath)
  ]);
  const actualPanoramaSha256 = calculateSha256(panoramaBytes);
  const actualReplaySha256 = calculateSha256(replayBytes);
  if (actualPanoramaSha256 !== panoramaSha256) {
    throw new Error(
      `${panoramaSourcePath}: panorama identity differs from Three r185.`);
  }
  if (actualReplaySha256 !== replaySha256) {
    throw new Error(
      `${replaySourcePath}: GUI and Orbit replay identity differs.`);
  }
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  for (const [relativePath, bytes] of [
    [panoramaRelativePath, panoramaBytes],
    [replayRelativePath, replayBytes]
  ]) {
    const destination = path.join(assetRoot, relativePath);
    await fs.mkdir(path.dirname(destination), { recursive: true });
    await fs.writeFile(destination, bytes);
  }
  return {
    assetRoot,
    identities: [
      {
        path: panoramaRelativePath,
        sha256: actualPanoramaSha256,
        byteCount: panoramaBytes.byteLength
      },
      {
        path: replayRelativePath,
        sha256: actualReplaySha256,
        byteCount: replayBytes.byteLength
      }
    ]
  };
}

/** Converts screen-pass generated lint violations into the shared report shape. */
function createGeneratedArtifactReport(generatedRoot) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: [`${screenPassName}:screen`],
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Runs every equirectangular scenario through the full strict matrix. */
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

/** Executes the strict dedicated equirectangular fixture and writes summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const assetSourceRoot = requirePathOption(options, 'asset-source-root');
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
  await fs.mkdir(outputDirectory, { recursive: true });
  const [example, inputs, gpuBoundaryLint] = await Promise.all([
    loadSelectedExample(manifestPath),
    stageInputs(outputDirectory, assetSourceRoot),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint =
    createGeneratedArtifactReport(generatedRoot);
  const context = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot: inputs.assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions,
    profile: {
      buildDir: outputDirectory,
      hostCandidates: {
        legacy: [path.join(binaryRoot, `${shardName}-legacy`)],
        experimental: [
          path.join(binaryRoot, `${shardName}-experimental`)
        ]
      }
    }
  };
  const quadrants = await runMatrix(context, example);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons =
    await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = example.scenarios.length
    * requiredPipelines.length
    * requiredBackends.length
    * repetitions;
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
    inputIdentities: inputs.identities,
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
    path.join(outputDirectory, 'cases', caseId, 'summary.json'),
    summary
  );
  console.log(
    `Equirectangular fixture ${status}: `
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
