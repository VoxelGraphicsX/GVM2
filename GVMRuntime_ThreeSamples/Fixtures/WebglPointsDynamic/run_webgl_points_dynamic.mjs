#!/usr/bin/env node

import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

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
const caseId = 'webgl_points_dynamic';

/** Parses explicit value-bearing command-line options for the dynamic point gate. */
function parseArguments(argv) {
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

/** Resolves one required path option without environment-variable fallbacks. */
function requirePath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name}.`);
  return path.resolve(options[name]);
}

/** Runs the complete three-scenario, four-quadrant, three-repeat closure. */
async function main() {
  const options = parseArguments(process.argv);
  const buildDir = requirePath(options, 'build-dir');
  const binaryRoot = requirePath(options, 'binary-root');
  const generatedRoot = requirePath(options, 'generated-root');
  const assetRoot = requirePath(options, 'asset-root');
  const oracleRoot = requirePath(options, 'oracle-root');
  const outputDir = requirePath(options, 'output-dir');
  const repetitions = Number(options.repeat ?? 3);
  const timeoutMs = Number(options['timeout-ms'] ?? 60_000);
  if (!Number.isInteger(repetitions) || repetitions < 3) throw new Error('--repeat must be at least 3.');
  if (!Number.isInteger(timeoutMs) || timeoutMs < 1) throw new Error('--timeout-ms must be positive.');
  await fs.mkdir(outputDir, { recursive: true });
  const manifestPath = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (!example || example.status !== 'phase1_required') throw new Error(`${caseId} is not a required manifest example.`);
  const generatedArtifacts = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath,
    caseIds: [caseId],
    fixtureSelectors: [],
    instancingPasses: []
  });
  const gpuBoundary = await lintSampleGpuBoundary(repositoryRoot);
  const context = {
    profile: {
      buildDir,
      hosts: {
        legacy: path.join(binaryRoot, 'WebglPointsDynamic-legacy'),
        experimental: path.join(binaryRoot, 'WebglPointsDynamic-experimental')
      }
    },
    sourceDir: repositoryRoot,
    runDir: outputDir,
    assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions
  };
  const quadrants = [];
  for (let repetition = 1; repetition <= repetitions; repetition += 1) {
    for (const scenario of example.scenarios) {
      for (const pipeline of requiredPipelines) {
        for (const backend of requiredBackends) {
          const result = await runQuadrant(context, example, scenario, pipeline, backend, repetition);
          quadrants.push(result);
          console.log(`${result.status.toUpperCase()} ${caseId}/${scenario.id} ${pipeline}/${backend} repeat-${repetition}`);
          for (const failure of result.failures) console.log(`  ${failure}`);
        }
      }
    }
  }
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(quadrants, repetitions);
  const status = generatedArtifacts.errors.length === 0
    && gpuBoundary.status === 'pass'
    && quadrants.length === example.scenarios.length * 4 * repetitions
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass')
    ? 'pass' : 'fail';
  await writeJson(path.join(outputDir, 'summary.json'), {
    schemaVersion: 1,
    gate: 'three-r185-webgl-points-dynamic',
    status,
    selectedCases: [caseId],
    repeatCount: repetitions,
    runCount: quadrants.length,
    expectedRunCount: example.scenarios.length * 4 * repetitions,
    quadrants,
    crossComparisons,
    stabilityComparisons,
    generatedArtifacts: { status: generatedArtifacts.errors.length === 0 ? 'pass' : 'fail', failures: generatedArtifacts.errors },
    gpuBoundaryLint: gpuBoundary,
    thresholds: { normalizedDistancePixelRatio: 0.001, meanAbsoluteRgb: 2, p99AbsoluteRgb: 16, luminanceSsim: 0.995 }
  });
  if (status !== 'pass') process.exitCode = 1;
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack : String(error));
  process.exitCode = 1;
});
