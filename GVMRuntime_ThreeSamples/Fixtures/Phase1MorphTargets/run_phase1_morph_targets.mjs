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
const caseConfigurations = Object.freeze([
  Object.freeze({
    caseId: 'webgl_morphtargets',
    shardName: 'WebglMorphtargets',
    binaryName: 'WebglMorphtargets'
  }),
  Object.freeze({
    caseId: 'webgpu_morphtargets',
    shardName: 'WebgpuMorphtargets',
    binaryName: 'WebgpuMorphtargets'
  })
]);
const caseIds = Object.freeze(caseConfigurations.map((entry) => entry.caseId));
const inputFiles = Object.freeze([
  'webgl_morphtargets_gui.json',
  'webgl_morphtargets_orbit.json',
  'webgpu_morphtargets_gui.json',
  'webgpu_morphtargets_orbit.json'
]);

/** Parses unique value-bearing long options without implicit environment configuration. */
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
export function parseRepeatCount(value) {
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
    throw new Error(`--timeout-ms must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Loads both dedicated morph examples from the frozen 588-item manifest. */
async function loadSelectedExamples(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error('Morph fixture requires the frozen 588-item Three r185 Manifest.');
  }
  const examples = caseConfigurations.map((configuration) => {
    const example = manifest.examples.find((entry) => entry.id === configuration.caseId);
    if (example == null || example.status !== 'phase1_required'
      || example.dslShard !== configuration.shardName) {
      throw new Error(`${configuration.caseId}: Manifest selection or dedicated shard ownership differs from the locked plan.`);
    }
    return example;
  });
  return examples;
}

/** Stages the four immutable input replays below the generic runner's explicit asset root. */
async function stageFixtureAssets(outputDirectory) {
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const inputDirectory = path.join(assetRoot, 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  await Promise.all(inputFiles.map((fileName) => fs.copyFile(
    path.join(scriptDirectory, 'Inputs', fileName),
    path.join(inputDirectory, fileName)
  )));
  return assetRoot;
}

/** Converts generated lint violations into the shared pass/fail report shape. */
function createGeneratedArtifactReport(generatedRoot, manifestPath) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath,
    caseIds: [...caseIds],
    fixtureSelectors: [],
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Creates the independent host selection used by one dedicated example. */
function createCaseContext(baseContext, binaryRoot, configuration) {
  return {
    ...baseContext,
    profile: {
      buildDir: baseContext.runDir,
      hostCandidates: {
        legacy: [path.join(binaryRoot, `${configuration.binaryName}-legacy`)],
        experimental: [path.join(binaryRoot, `${configuration.binaryName}-experimental`)]
      }
    }
  };
}

/** Runs every scenario through the full pipeline/backend/repetition matrix. */
async function runMatrix(baseContext, binaryRoot, examples) {
  const quadrants = [];
  for (let repetition = 1; repetition <= baseContext.repetitions; repetition += 1) {
    for (const configuration of caseConfigurations) {
      const example = examples.find((entry) => entry.id === configuration.caseId);
      const context = createCaseContext(baseContext, binaryRoot, configuration);
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
            console.log(`${result.status.toUpperCase()} ${example.id}/${scenario.id} ${pipeline}/${backend} repeat-${repetition}`);
          }
        }
      }
    }
  }
  return quadrants;
}

/** Builds one independently gateable standardized case summary. */
export function buildCaseSummary(
  caseId,
  repetitions,
  quadrants,
  crossComparisons,
  stabilityComparisons,
  generatedArtifactLint,
  gpuBoundaryLint
) {
  const caseQuadrants = quadrants.filter((entry) => entry.caseId === caseId);
  const caseCrossComparisons = crossComparisons.filter((entry) => entry.caseId === caseId);
  const caseStabilityComparisons = stabilityComparisons.filter((entry) => entry.caseId === caseId);
  const status = caseQuadrants.length > 0
    && caseQuadrants.every((entry) => entry.status === 'pass')
    && caseCrossComparisons.every((entry) => entry.status === 'pass')
    && caseStabilityComparisons.every((entry) => entry.status === 'pass')
    && generatedArtifactLint.status === 'pass'
    && gpuBoundaryLint.status === 'pass'
    ? 'pass'
    : 'fail';
  return {
    schemaVersion: 1,
    status,
    selectedCases: [caseId],
    repeatCount: repetitions,
    runCount: caseQuadrants.length,
    expectedRunCount: caseQuadrants.length,
    pipelines: [...requiredPipelines],
    backends: [...requiredBackends],
    quadrants: caseQuadrants,
    crossComparisons: caseCrossComparisons,
    stabilityComparisons: caseStabilityComparisons,
    generatedArtifacts: [generatedArtifactLint],
    gpuBoundaryLint,
    thresholds: {
      normalizedDistancePixelRatio: 0.001,
      meanAbsoluteRgb: 2.0,
      p99AbsoluteRgb: 16,
      luminanceSsim: 0.995
    }
  };
}

/** Executes the strict dedicated morph fixture and writes standardized summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const outputDirectory = requirePathOption(options, 'output-dir');
  const manifestPath = path.resolve(
    options.manifest ?? path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json')
  );
  const repetitions = parseRepeatCount(options.repeat);
  const timeoutMs = parseTimeout(options['timeout-ms']);
  await fs.mkdir(outputDirectory, { recursive: true });
  const [examples, assetRoot, gpuBoundaryLint] = await Promise.all([
    loadSelectedExamples(manifestPath),
    stageFixtureAssets(outputDirectory),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = createGeneratedArtifactReport(generatedRoot, manifestPath);
  const baseContext = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions
  };
  const quadrants = await runMatrix(baseContext, binaryRoot, examples);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = examples.reduce((count, example) => (
    count + example.scenarios.length * requiredPipelines.length * requiredBackends.length * repetitions
  ), 0);
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
    selectedCases: [...caseIds],
    repeatCount: repetitions,
    runCount: quadrants.length,
    expectedRunCount,
    pipelines: [...requiredPipelines],
    backends: [...requiredBackends],
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
  for (const caseId of caseIds) {
    await writeJson(
      path.join(outputDirectory, 'cases', caseId, 'summary.json'),
      buildCaseSummary(
        caseId,
        repetitions,
        quadrants,
        crossComparisons,
        stabilityComparisons,
        generatedArtifactLint,
        gpuBoundaryLint
      )
    );
  }
  console.log(`Morph fixture ${status}: ${path.join(outputDirectory, 'summary.json')}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
