#!/usr/bin/env node

import childProcess from 'node:child_process';
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
const sourceRelativePath = path.join('textures', 'spiritedaway.ktx2');
const sourceSha256 =
  'afab737973d40b8b2df1deda7204284e8a1b4271e1417c740a7de19254257f4b';
const decodedLayerSha256 = Object.freeze([
  '986448d4fcd79b9f7cc60b9f2f9e54d2a67b82a58d7fdc25162df244d6267ecc',
  '4f3a2649b84aac45b3f0489a2dce33d47d40f8d6f5d0c6cadbab5c6c669c67d3',
  '46d797704adae32da9b96988dd5ce0e5467efa46b0c28594862ab43c1adbc279',
  '6a956062790277c42e110ff7da4a16e3c3de7ec26f596ad8bd5c0bf27b92ade0',
  '7fc97349fbd3f36805bf5bc979aa1517e61c2393e672c2d89afe754cd0627a9a',
  '6d4347b1fe149c636bdb5f670164f44610017911a9af4bb55c89f446830dbb33'
]);
const configurations = Object.freeze([
  Object.freeze({
    caseId: 'webgl_texture2darray_compressed',
    shardName: 'Phase1WebglTexture2DArrayCompressedSimple',
    passName: 'WebglTexture2DArrayCompressedMainPass'
  }),
  Object.freeze({
    caseId: 'webgpu_textures_2d-array_compressed',
    shardName: 'Phase1WebgpuTextures2DArrayCompressedSimple',
    passName: 'WebgpuTextures2DArrayCompressedMainPass'
  })
]);
const caseIds = Object.freeze(
  configurations.map((entry) => entry.caseId)
);

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

/** Returns the lowercase SHA-256 identity of one immutable byte sequence. */
function calculateSha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Loads the two dedicated compressed-array contracts from the frozen Manifest. */
async function loadSelectedExamples(manifestPath) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Compressed-array fixture requires the frozen 588-item r185 Manifest.');
  }
  return configurations.map((configuration) => {
    const example = manifest.examples.find(
      (entry) => entry.id === configuration.caseId);
    if (example?.status !== 'phase1_required'
        || example.renderSetPolicy !== 'not-required'
        || example.dslShard !== configuration.shardName
        || example.renderSetType !== null
        || example.renderableObjectCount !== 1
        || example.scenePasses?.length !== 1
        || example.scenePasses[0]?.renderClass !== configuration.passName
        || example.scenarios?.length !== 2) {
      throw new Error(
        `${configuration.caseId}: compressed-array Manifest contract changed.`);
    }
    return example;
  });
}

/** Stages and validates the source KTX2 plus deterministic RGBA8 UASTC decode. */
async function stageCompressedArrayAssets(
  outputDirectory,
  assetSourceRoot,
  ktxExecutable
) {
  const sourcePath = path.join(assetSourceRoot, sourceRelativePath);
  const sourceBytes = await fs.readFile(sourcePath);
  if (calculateSha256(sourceBytes) !== sourceSha256) {
    throw new Error(`${sourcePath}: KTX2 identity differs from Three r185.`);
  }
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const stagedSourcePath = path.join(assetRoot, sourceRelativePath);
  const decodedDirectory = path.join(assetRoot, 'decoded');
  const extractionDirectory = path.join(outputDirectory, 'ktx-extract');
  await fs.mkdir(path.dirname(stagedSourcePath), { recursive: true });
  await fs.mkdir(decodedDirectory, { recursive: true });
  await fs.mkdir(extractionDirectory, { recursive: true });
  await fs.copyFile(sourcePath, stagedSourcePath);
  const extraction = childProcess.spawnSync(
    ktxExecutable,
    [
      'extract',
      '--transcode', 'rgba8',
      '--all',
      '--raw',
      stagedSourcePath,
      extractionDirectory
    ],
    { encoding: 'utf8' }
  );
  if (extraction.status !== 0) {
    throw new Error(
      `KTX2 RGBA8 extraction failed: ${extraction.stderr || extraction.stdout}`);
  }
  const decodedLayers = [];
  for (let layer = 0; layer < decodedLayerSha256.length; layer += 1) {
    const extractedPath = path.join(
      extractionDirectory,
      `output_layer${layer}.raw`);
    const bytes = await fs.readFile(extractedPath);
    const sha256 = calculateSha256(bytes);
    if (bytes.byteLength !== 496 * 260 * 4
        || sha256 !== decodedLayerSha256[layer]) {
      throw new Error(
        `${extractedPath}: decoded layer differs from the locked UASTC result.`);
    }
    const destination = path.join(
      decodedDirectory,
      `spiritedaway_layer${layer}.rgba`);
    await fs.copyFile(extractedPath, destination);
    decodedLayers.push({
      layer,
      path: path.relative(assetRoot, destination),
      sha256,
      byteCount: bytes.byteLength
    });
  }
  return {
    assetRoot,
    source: {
      path: sourceRelativePath,
      sha256: sourceSha256,
      byteCount: sourceBytes.byteLength
    },
    decodedLayers
  };
}

/** Converts generated artifact lint violations into the shared report shape. */
function createGeneratedArtifactReport(generatedRoot) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: configurations.map(
      ({ passName }) => `${passName}:screen`),
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Creates the independently generated host selection for one example. */
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

/** Runs every compressed-array scenario through the strict execution matrix. */
async function runMatrix(baseContext, binaryRoot, examples) {
  const quadrants = [];
  for (let repetition = 1;
       repetition <= baseContext.repetitions;
       repetition += 1) {
    for (const configuration of configurations) {
      const example = examples.find(
        (entry) => entry.id === configuration.caseId);
      const context = createCaseContext(
        baseContext,
        binaryRoot,
        configuration);
      for (const scenario of example.scenarios) {
        for (const pipeline of requiredPipelines) {
          for (const backend of requiredBackends) {
            const result = await runQuadrant(
              context,
              example,
              scenario,
              pipeline,
              backend,
              repetition);
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

/** Builds one independently gateable strict case summary. */
function buildCaseSummary(summary, caseId, expectedRunCount) {
  const quadrants = summary.quadrants.filter(
    (entry) => entry.caseId === caseId);
  const crossComparisons = summary.crossComparisons.filter(
    (entry) => entry.caseId === caseId);
  const stabilityComparisons = summary.stabilityComparisons.filter(
    (entry) => entry.caseId === caseId);
  return {
    ...summary,
    selectedCases: [caseId],
    status: quadrants.length === expectedRunCount
      && quadrants.every((entry) => entry.status === 'pass')
      && crossComparisons.every((entry) => entry.status === 'pass')
      && stabilityComparisons.every((entry) => entry.status === 'pass')
      && summary.generatedArtifacts.every(
        (entry) => entry.status === 'pass')
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

/** Executes the strict compressed-array fixture and writes aggregate reports. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const outputDirectory = requirePathOption(options, 'output-dir');
  const assetSourceRoot =
    requirePathOption(options, 'asset-source-root');
  const ktxExecutable = requirePathOption(options, 'ktx');
  const manifestPath = path.resolve(
    options.manifest ?? path.join(
      repositoryRoot,
      'GVMRuntime_ThreeSamples',
      'Manifest',
      'three-r185-manifest.json'));
  const repetitions = parseRepeatCount(options.repeat);
  const timeoutMs = parseTimeout(options['timeout-ms']);
  await fs.mkdir(outputDirectory, { recursive: true });
  const [examples, stagedAssets, gpuBoundaryLint] = await Promise.all([
    loadSelectedExamples(manifestPath),
    stageCompressedArrayAssets(
      outputDirectory,
      assetSourceRoot,
      ktxExecutable),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint =
    createGeneratedArtifactReport(generatedRoot);
  const baseContext = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    assetRoot: stagedAssets.assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions
  };
  const quadrants = await runMatrix(
    baseContext,
    binaryRoot,
    examples);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons =
    await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = examples.reduce(
    (count, example) =>
      count +
      example.scenarios.length *
        requiredPipelines.length *
        requiredBackends.length *
        repetitions,
    0);
  const summary = {
    schemaVersion: 1,
    status: quadrants.length === expectedRunCount
      && quadrants.every((entry) => entry.status === 'pass')
      && crossComparisons.every((entry) => entry.status === 'pass')
      && stabilityComparisons.every((entry) => entry.status === 'pass')
      && generatedArtifactLint.status === 'pass'
      && gpuBoundaryLint.status === 'pass'
      ? 'pass'
      : 'fail',
    selectedCases: [...caseIds],
    repeatCount: repetitions,
    runCount: quadrants.length,
    expectedRunCount,
    pipelines: [...requiredPipelines],
    backends: [...requiredBackends],
    stagedAssets,
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
  await writeJson(
    path.join(outputDirectory, 'summary.json'),
    summary);
  const expectedCaseRunCount =
    2 *
    requiredPipelines.length *
    requiredBackends.length *
    repetitions;
  for (const caseId of caseIds) {
    await writeJson(
      path.join(
        outputDirectory,
        'cases',
        caseId,
        'summary.json'),
      buildCaseSummary(summary, caseId, expectedCaseRunCount));
  }
  console.log(
    `Compressed-array fixture ${summary.status}: `
    + path.join(outputDirectory, 'summary.json'));
  if (summary.status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(
      error instanceof Error
        ? error.stack ?? error.message
        : String(error));
    process.exitCode = 1;
  });
}
