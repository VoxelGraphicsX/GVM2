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
    caseId: 'webgl_volume_perlin',
    shardName: 'Phase1WebglVolumePerlinRenderSet',
    scenePass: 'WebglVolumePerlinScenePass',
    renderSetType: 'WebglVolumePerlinSceneRenderSet'
  }),
  Object.freeze({
    caseId: 'webgpu_volume_perlin',
    shardName: 'Phase1WebgpuVolumePerlinRenderSet',
    scenePass: 'WebgpuVolumePerlinScenePass',
    renderSetType: 'WebgpuVolumePerlinSceneRenderSet'
  })
]);
const caseIds = Object.freeze(
  caseConfigurations.map((entry) => entry.caseId)
);

/** Parses unique value-bearing long options without environment configuration. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(
        `Expected --option value pair near '${option ?? '<end>'}'.`
      );
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
      `--repeat must be an integer of at least 3; received '${value}'.`
    );
  }
  return parsed;
}

/** Parses one positive per-capture watchdog duration. */
function parseTimeout(value) {
  const parsed = value == null ? 60_000 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(
      `--timeout-ms must be positive; received '${value}'.`
    );
  }
  return parsed;
}

/** Loads and validates both dedicated volume examples from the frozen Manifest. */
async function loadSelectedExamples(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Volume fixture requires the frozen 588-item Three r185 Manifest.'
    );
  }
  return caseConfigurations.map((configuration) => {
    const example = manifest.examples.find(
      (entry) => entry.id === configuration.caseId
    );
    if (example?.status !== 'phase1_required'
        || example.renderSetPolicy !== 'required'
        || example.dslShard !== configuration.shardName
        || example.renderSetType !== configuration.renderSetType
        || example.renderableObjectCount !== 1
        || example.scenePasses?.length !== 1
        || example.scenePasses[0]?.renderClass !== configuration.scenePass
        || example.scenarios?.length !== 2) {
      throw new Error(
        `${configuration.caseId}: dedicated volume Manifest contract changed.`
      );
    }
    return example;
  });
}

/** Stages immutable volume input replays below the explicit asset root. */
async function stageFixtureAssets(outputDirectory) {
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const inputDirectory = path.join(assetRoot, 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  for (const caseId of caseIds) {
    await fs.copyFile(
      path.join(
        repositoryRoot,
        'GVMRuntime_ThreeSamples',
        'inputs',
        `${caseId}_settings_orbit.json`
      ),
      path.join(inputDirectory, `${caseId}_settings_orbit.json`)
    );
  }
  return assetRoot;
}

/** Converts generated RenderSet lint violations into the shared report shape. */
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

/** Creates the independent generated-host selection for one volume example. */
function createCaseContext(baseContext, binaryRoot, configuration) {
  return {
    ...baseContext,
    profile: {
      buildDir: baseContext.runDir,
      hostCandidates: {
        legacy: [
          path.join(binaryRoot, `${configuration.shardName}-legacy`)
        ],
        experimental: [
          path.join(binaryRoot, `${configuration.shardName}-experimental`)
        ]
      }
    }
  };
}

/** Runs all volume scenarios through every pipeline, backend, and repetition. */
async function runMatrix(baseContext, binaryRoot, examples) {
  const quadrants = [];
  for (let repetition = 1;
       repetition <= baseContext.repetitions;
       repetition += 1) {
    for (const configuration of caseConfigurations) {
      const example = examples.find(
        (entry) => entry.id === configuration.caseId
      );
      const context = createCaseContext(
        baseContext,
        binaryRoot,
        configuration
      );
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
              `${result.status.toUpperCase()} ${example.id}/${scenario.id} `
              + `${pipeline}/${backend} repeat-${repetition}`
            );
          }
        }
      }
    }
  }
  return quadrants;
}

/** Builds one independently gateable strict summary for a volume example. */
function buildCaseSummary(summary, caseId, expectedRunCount) {
  const quadrants = summary.quadrants.filter(
    (entry) => entry.caseId === caseId
  );
  const crossComparisons = summary.crossComparisons.filter(
    (entry) => entry.caseId === caseId
  );
  const stabilityComparisons = summary.stabilityComparisons.filter(
    (entry) => entry.caseId === caseId
  );
  return {
    ...summary,
    selectedCases: [caseId],
    status: quadrants.length === expectedRunCount
      && quadrants.every((entry) => entry.status === 'pass')
      && crossComparisons.every((entry) => entry.status === 'pass')
      && stabilityComparisons.every((entry) => entry.status === 'pass')
      && summary.generatedArtifacts.every(
        (entry) => entry.status === 'pass'
      )
      && summary.gpuBoundaryLint.status === 'pass'
      ? 'pass'
      : 'fail',
    runCount: quadrants.length,
    expectedRunCount,
    quadrants,
    crossComparisons,
    stabilityComparisons
  };
}

/** Executes the strict dedicated volume fixture and writes aggregate summaries. */
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
  const [examples, assetRoot, gpuBoundaryLint] = await Promise.all([
    loadSelectedExamples(manifestPath),
    stageFixtureAssets(outputDirectory),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = createGeneratedArtifactReport(
    generatedRoot,
    manifestPath
  );
  const baseContext = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions
  };
  const quadrants = await runMatrix(
    baseContext,
    binaryRoot,
    examples
  );
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(
    quadrants,
    repetitions
  );
  const perCaseExpectedRunCount =
    2 * requiredPipelines.length * requiredBackends.length * repetitions;
  const expectedRunCount =
    perCaseExpectedRunCount * caseConfigurations.length;
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
      buildCaseSummary(summary, caseId, perCaseExpectedRunCount)
    );
  }
  console.log(
    `Volume fixture ${status}: ${path.join(outputDirectory, 'summary.json')}`
  );
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(
      error instanceof Error ? error.stack ?? error.message : String(error)
    );
    process.exitCode = 1;
  });
}
