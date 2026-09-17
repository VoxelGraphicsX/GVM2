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
const caseId = 'webgl_multiple_scenes_comparison';
const shardName = 'WebglMultipleScenesComparison';
const replayNames = Object.freeze([
  'webgl_multiple_scenes_comparison_slider.json',
  'webgl_multiple_scenes_comparison_orbit.json'
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

/** Parses the mandatory minimum-three stability repetition count. */
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

/** Loads the exact required example from the frozen 588-item Manifest. */
async function loadSelectedExample(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error('Multiple-scenes fixture requires the frozen 588-item Three r185 Manifest.');
  }
  const example = manifest.examples.find((entry) => entry.id === caseId);
  if (example == null || example.status !== 'phase1_required'
    || example.dslShard !== shardName || example.renderSetPolicy !== 'not-required') {
    throw new Error('Multiple-scenes fixture Manifest ownership or RenderSet policy differs from the locked plan.');
  }
  return example;
}

/** Stages both immutable pointer replays below the generic runner's explicit asset root. */
async function stageFixtureAssets(outputDirectory) {
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const inputDirectory = path.join(assetRoot, 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  for (const replayName of replayNames) {
    await fs.copyFile(
      path.join(scriptDirectory, 'Inputs', replayName),
      path.join(inputDirectory, replayName)
    );
  }
  return assetRoot;
}

/** Verifies both ordinary Scene passes and the screen Compute pass across generated pipelines. */
export function createGeneratedArtifactReport(generatedRoot) {
  const failures = [];
  const checkedPasses = [
    'WebglMultipleScenesComparisonLeftPass',
    'WebglMultipleScenesComparisonRightPass',
    'WebglMultipleScenesComparisonResolvePass'
  ];
  const artifacts = {};
  for (const pipeline of requiredPipelines) {
    const artifactDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const generatedHeaderPath = path.join(artifactDirectory, 'generate_result.hpp');
    const dslHeaderPath = path.join(artifactDirectory, 'dsl_single_header.hpp');
    for (const requiredPath of [generatedHeaderPath, dslHeaderPath]) {
      if (!existsSync(requiredPath)) failures.push(`缺少生成产物：${requiredPath}`);
    }
    if (existsSync(generatedHeaderPath) && existsSync(dslHeaderPath)) {
      artifacts[pipeline] = {
        artifactDirectory,
        generated: readFileSync(generatedHeaderPath, 'utf8'),
        dsl: readFileSync(dslHeaderPath, 'utf8')
      };
    }
  }
  for (const [pipeline, artifact] of Object.entries(artifacts)) {
    for (const passName of checkedPasses) {
      if (!artifact.dsl.includes(passName) || !artifact.generated.includes(passName)) {
        failures.push(`${pipeline} 未生成 ${passName}。`);
      }
    }
    if (artifact.dsl.includes('RenderSet<') || artifact.dsl.includes('[[RenderEntity')) {
      failures.push(`${pipeline} 的两个简单 Scene 不得生成 RenderSet 或 RenderEntity builtin。`);
    }
    for (const requiredCall of [
      'leftPass->setVertexBuffer(solidVertexBuffer)',
      'leftPass->setIndexBuffer(solidIndexBuffer)',
      'leftPass->run(WebglMultipleScenesSolidIndexCount, 1u, 0u, 0, 0u)',
      'rightPass->setVertexBuffer(wireVertexBuffer)',
      'rightPass->setIndexBuffer(wireIndexBuffer)',
      'rightPass->run(WebglMultipleScenesWireIndexCount, 1u, 0u, 0, 0u)',
      'resolvePass->run(WebglMultipleScenesOutputWidth, WebglMultipleScenesOutputHeight, 1u)'
    ]) {
      if (!artifact.generated.includes(requiredCall)) {
        failures.push(`${pipeline} 缺少普通 RenderClass/ComputeClass 调用：${requiredCall}`);
      }
    }
  }
  const experimentalDirectory = artifacts.experimental?.artifactDirectory;
  if (experimentalDirectory !== undefined) {
    const renderStages = [
      'WebglMultipleScenesComparisonLeftPass__vertex',
      'WebglMultipleScenesComparisonLeftPass__fragment',
      'WebglMultipleScenesComparisonRightPass__vertex',
      'WebglMultipleScenesComparisonRightPass__fragment'
    ];
    for (const stem of renderStages) {
      for (const relativePath of [
        `uglir/${stem}.uglir.json`,
        `msl/${stem}.msl`,
        `spv/${stem}.raw.spvasm`,
        `spv/${stem}.spvasm`
      ]) {
        const artifactPath = path.join(experimentalDirectory, relativePath);
        if (!existsSync(artifactPath)) failures.push(`Experimental 缺少 ${relativePath}。`);
      }
    }
    for (const relativePath of [
      'uglir/WebglMultipleScenesComparisonResolvePass.uglir.json',
      'msl/WebglMultipleScenesComparisonResolvePass.msl',
      'spv/WebglMultipleScenesComparisonResolvePass.raw.spvasm',
      'spv/WebglMultipleScenesComparisonResolvePass.spvasm'
    ]) {
      const artifactPath = path.join(experimentalDirectory, relativePath);
      if (!existsSync(artifactPath)) failures.push(`Experimental 缺少 ${relativePath}。`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    checkedPasses,
    failures
  };
}

/** Runs all three canonical snapshots through every pipeline/backend/repetition quadrant. */
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
          console.log(`${result.status.toUpperCase()} ${caseId}/${scenario.id} ${pipeline}/${backend} repeat-${repetition}`);
        }
      }
    }
  }
  return quadrants;
}

/** Builds the single independently gateable standardized fixture summary. */
export function buildSummary(
  repetitions,
  expectedRunCount,
  quadrants,
  crossComparisons,
  stabilityComparisons,
  generatedArtifactLint,
  gpuBoundaryLint
) {
  const status = quadrants.length === expectedRunCount
    && quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.every((entry) => entry.status === 'pass')
    && stabilityComparisons.every((entry) => entry.status === 'pass')
    && generatedArtifactLint.status === 'pass'
    && gpuBoundaryLint.status === 'pass'
    ? 'pass'
    : 'fail';
  return {
    schemaVersion: 1,
    status,
    selectedCases: [caseId],
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
}

/** Executes the strict two-Scene fixture and writes its standardized summary. */
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
  const [example, assetRoot, gpuBoundaryLint] = await Promise.all([
    loadSelectedExample(manifestPath),
    stageFixtureAssets(outputDirectory),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = createGeneratedArtifactReport(generatedRoot);
  const context = {
    profile: {
      buildDir: outputDirectory,
      hostCandidates: {
        legacy: [path.join(binaryRoot, `${shardName}-legacy`)],
        experimental: [path.join(binaryRoot, `${shardName}-experimental`)]
      }
    },
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions
  };
  const quadrants = await runMatrix(context, example);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons = await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = example.scenarios.length
    * requiredPipelines.length * requiredBackends.length * repetitions;
  const summary = buildSummary(
    repetitions,
    expectedRunCount,
    quadrants,
    crossComparisons,
    stabilityComparisons,
    generatedArtifactLint,
    gpuBoundaryLint
  );
  await writeJson(path.join(outputDirectory, 'summary.json'), summary);
  await writeJson(path.join(outputDirectory, 'cases', caseId, 'summary.json'), summary);
  console.log(`Multiple-scenes fixture ${summary.status}: ${path.join(outputDirectory, 'summary.json')}`);
  if (summary.status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
