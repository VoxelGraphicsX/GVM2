#!/usr/bin/env node

import { existsSync, promises as fs, readFileSync } from 'node:fs';
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
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseDefinitions = Object.freeze([
  {
    caseId: 'webgl_loader_texture_hdr',
    shardName: 'Phase1LoaderTextureHdrSimple',
    passes: ['WebglLoaderTextureHdrQuadPass']
  },
  {
    caseId: 'webgl_loader_vox',
    shardName: 'Phase1LoaderVoxSimple',
    passes: ['WebglLoaderVoxMeshPass', 'WebglLoaderVoxResolvePass']
  }
]);

/** Parses unique long-form options and rejects implicit runtime configuration. */
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

/** Resolves one mandatory explicit path option. */
function requirePath(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name}.`);
  return path.resolve(options[name]);
}

/** Loads and validates the two frozen Loader examples from the 588-item Manifest. */
async function loadExamples(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error('Loader wave requires the frozen 588-item Three r185 Manifest.');
  }
  return caseDefinitions.map((definition) => {
    const example = manifest.examples.find((entry) => entry.id === definition.caseId);
    if (example?.status !== 'phase1_required'
        || example.dslShard !== definition.shardName
        || example.renderSetPolicy !== 'not-required'
        || example.scenarios?.length !== 3) {
      throw new Error(`${definition.caseId}: Manifest ownership or scenario contract drifted.`);
    }
    return example;
  });
}

/** Verifies dual generated headers and every Experimental UGLIR/MSL/SPIR-V stage. */
export function lintGeneratedArtifacts(generatedRoot) {
  const failures = [];
  for (const definition of caseDefinitions) {
    for (const pipeline of requiredPipelines) {
      const directory = path.join(generatedRoot, pipeline, definition.shardName, 'UGLBin');
      const generatedPath = path.join(directory, 'generate_result.hpp');
      const dslPath = path.join(directory, 'dsl_single_header.hpp');
      if (!existsSync(generatedPath) || !existsSync(dslPath)) {
        failures.push(`${definition.caseId}/${pipeline}: missing generated headers.`);
        continue;
      }
      const generated = readFileSync(generatedPath, 'utf8');
      const dsl = readFileSync(dslPath, 'utf8');
      for (const passName of definition.passes) {
        if (!generated.includes(passName) || !dsl.includes(passName)) {
          failures.push(`${definition.caseId}/${pipeline}: missing ${passName}.`);
        }
        if (pipeline === 'experimental') {
          for (const stage of ['vertex', 'fragment']) {
            const stem = `${passName}__${stage}`;
            for (const relativePath of [
              `uglir/${stem}.uglir.json`,
              `msl/${stem}.msl`,
              `spv/${stem}.raw.spvasm`,
              `spv/${stem}.spvasm`
            ]) {
              if (!existsSync(path.join(directory, relativePath))) {
                failures.push(`${definition.caseId}: missing Experimental ${relativePath}.`);
              }
            }
          }
        }
      }
      if (dsl.includes('Phase1BatchSimpleRenderer')
          || dsl.includes('Phase1BatchRenderSetRenderer')) {
        failures.push(`${definition.caseId}/${pipeline}: shared placeholder renderer leaked.`);
      }
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    checkedCases: caseDefinitions.map((definition) => definition.caseId),
    failures
  };
}

/** Runs all six Loader scenarios through four quadrants and three repetitions. */
async function runMatrix(context, examples, repetitions) {
  const quadrants = [];
  for (let repetition = 1; repetition <= repetitions; repetition += 1) {
    for (const example of examples) {
      const definition = caseDefinitions.find((entry) => entry.caseId === example.id);
      context.profile.hostCandidates = {
        legacy: [path.join(context.binaryRoot, `${definition.shardName}-legacy`)],
        experimental: [path.join(context.binaryRoot, `${definition.shardName}-experimental`)]
      };
      for (const scenario of example.scenarios) {
        for (const pipeline of requiredPipelines) {
          for (const backend of requiredBackends) {
            const result = await runQuadrant(
              context, example, scenario, pipeline, backend, repetition);
            quadrants.push(result);
            console.log(
              `${result.status.toUpperCase()} ${example.id}/${scenario.id} `
              + `${pipeline}/${backend} repeat-${repetition}`);
          }
        }
      }
    }
  }
  return quadrants;
}

/** Executes the repeatable strict Loader wave and writes one standardized group report. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePath(options, 'binary-root');
  const generatedRoot = requirePath(options, 'generated-root');
  const assetRoot = requirePath(options, 'asset-root');
  const oracleRoot = requirePath(options, 'oracle-root');
  const outputDirectory = requirePath(options, 'output-dir');
  const manifestPath = path.resolve(
    options.manifest
      ?? path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest',
        'three-r185-manifest.json'));
  const repetitions = Number(options.repeat ?? 3);
  const timeoutMs = Number(options['timeout-ms'] ?? 60_000);
  if (!Number.isInteger(repetitions) || repetitions < 3
      || !Number.isInteger(timeoutMs) || timeoutMs < 1) {
    throw new Error('Loader wave requires --repeat >= 3 and a positive --timeout-ms.');
  }
  await fs.mkdir(outputDirectory, { recursive: true });
  const [examples, gpuBoundaryLint] = await Promise.all([
    loadExamples(manifestPath),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = lintGeneratedArtifacts(generatedRoot);
  const context = {
    profile: {
      buildDir: outputDirectory
    },
    binaryRoot,
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs
  };
  const quadrants = await runMatrix(context, examples, repetitions);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = examples.reduce(
    (sum, example) => sum + example.scenarios.length, 0)
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
    selectedCases: examples.map((example) => example.id),
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
      meanAbsoluteRgb: 2,
      p99AbsoluteRgb: 16,
      luminanceSsim: 0.995
    }
  };
  await writeJson(path.join(outputDirectory, 'summary.json'), summary);
  for (const example of examples) {
    await writeJson(path.join(outputDirectory, 'cases', example.id, 'summary.json'), summary);
  }
  console.log(`Loader wave ${status}: ${path.join(outputDirectory, 'summary.json')}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
