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
const caseId = 'webgl_postprocessing_sobel';
const shardName = 'Phase1WebglPostprocessingSobel';
const replayDefinitions = Object.freeze([
  {
    relativePath: path.join('inputs', 'webgl_postprocessing_sobel_disabled.json'),
    sha256: '769e432194f7047ac906dd0ef607542bf8dada1b80deb45faeea9aa9b178cb76'
  },
  {
    relativePath: path.join('inputs', 'webgl_postprocessing_sobel_orbit.json'),
    sha256: 'a6c977403217c3ae680778975a6e07b7eb9b75d0b40cd8214c8bc8ba4952710e'
  }
]);
const generatedPasses = Object.freeze([
  'WebglPostprocessingSobelMainPass',
  'WebglPostprocessingSobelLuminancePass',
  'WebglPostprocessingSobelOperatorPass',
  'WebglPostprocessingSobelOutputPass'
]);

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

/** Loads the dedicated Sobel example from the frozen Manifest. */
async function loadSelectedExample(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Sobel fixture requires the frozen 588-item Three r185 Manifest.');
  }
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (example?.status !== 'phase1_required'
      || example.renderSetPolicy !== 'not-required'
      || example.dslShard !== shardName
      || example.renderSetType !== null
      || example.scenarios?.length !== 3) {
    throw new Error(`${caseId} Manifest ownership differs from the locked contract.`);
  }
  return example;
}

/** Copies and verifies both deterministic Sobel replays into the runner asset root. */
async function stageInputs(outputDirectory) {
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const identities = [];
  for (const definition of replayDefinitions) {
    const sourcePath = path.join(
      scriptDirectory,
      'Inputs',
      path.basename(definition.relativePath)
    );
    const bytes = await fs.readFile(sourcePath);
    const sha256 = calculateSha256(bytes);
    if (sha256 !== definition.sha256) {
      throw new Error(`${sourcePath}: replay identity differs from the locked contract.`);
    }
    const destination = path.join(assetRoot, definition.relativePath);
    await fs.mkdir(path.dirname(destination), { recursive: true });
    await fs.writeFile(destination, bytes);
    identities.push({
      path: definition.relativePath,
      sha256,
      byteCount: bytes.byteLength
    });
  }
  return { assetRoot, identities };
}

/** Converts explicit-draw generated lint violations into the shared report shape. */
function createGeneratedArtifactReport(generatedRoot) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: generatedPasses.map((name) => `${name}:screen`),
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Runs every Sobel scenario through the full strict matrix. */
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

/** Executes the strict dedicated Sobel fixture and writes canonical summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
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
    stageInputs(outputDirectory),
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
        experimental: [path.join(binaryRoot, `${shardName}-experimental`)]
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
    `Sobel fixture ${status}: ${path.join(outputDirectory, 'summary.json')}`);
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
