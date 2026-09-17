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
const volumeRelativePath =
  path.join('textures', '3d', 'head256x256x109.zip');
const volumeSha256 =
  '7d6d9c15a24a043114a9a2d07380b1afe24f4c45aa0a0da0c55bf764cb2cf906';
const carbonRelativePath =
  path.join('textures', 'carbon', 'Carbon.png');
const carbonSha256 =
  '1d504adf20c83f26ce5519a19b9e33c2dc9ca6371a523a10869d308edd67f9e6';
const proceduralReplayRelativePath =
  path.join('inputs', 'webgpu_procedural_texture_gui.json');
const proceduralReplaySha256 =
  'cac972b7e86ea4bb862229bd8dff89346c6d0ad4ceb9547d075851e098082ada';
const panoramaRelativePath =
  path.join('textures', '2294472375_24a3b8ef46_o.jpg');
const panoramaSha256 =
  '3efa22071f3f84ec26248ee58aa05aba635a0a9d810bca93da5a94f33e907524';
const panoramaReplayRelativePath =
  path.join(
    'inputs',
    'webgl_panorama_equirectangular_pointer_wheel.json'
  );
const panoramaReplaySha256 =
  '07eeeb3e433a584521fe35f3ba59900fc3aee742965503f6aff35426ea2776d6';
const crateRelativePath = path.join('textures', 'crate.gif');
const crateSha256 =
  'a890f0a89eadc083cb39bfbe597c1395d7acf47a19f673b5643d4a9c174ea52f';
const webglAnisotropyReplayRelativePath =
  path.join('inputs', 'webgl_materials_texture_anisotropy_mouse.json');
const webglAnisotropyReplaySha256 =
  '0203538cfefd3a1061d277f17b1117e7b34b04348495ccfa78e59551e2b799e7';
const webgpuAnisotropyReplayRelativePath =
  path.join('inputs', 'webgpu_textures_anisotropy_pointer.json');
const webgpuAnisotropyReplaySha256 =
  'cfd8c91416741069adf21b0e359034c4f83f7264234d2a64f001cb7ebb809b8a';
const uvGridRelativePath = path.join('textures', 'uv_grid_opengl.jpg');
const uvGridSha256 =
  '909d9a1eb2a5d5de9d221a5e8de4e9119d409decddf522d48896bd51523d354d';
const caseConfigurations = Object.freeze([
  Object.freeze({
    caseId: 'webgl_texture2darray',
    shardName: 'Phase1WebglTexture2DArraySimple',
    passName: 'WebglTexture2DArrayMainPass',
    scenarioCount: 2
  }),
  Object.freeze({
    caseId: 'webgpu_textures_2d-array',
    shardName: 'Phase1WebgpuTextures2DArraySimple',
    passName: 'WebgpuTextures2DArrayMainPass',
    scenarioCount: 2
  }),
  Object.freeze({
    caseId: 'webgpu_textures_partialupdate',
    shardName: 'Phase1WebgpuTexturesPartialUpdateSimple',
    passName: 'WebgpuTexturesPartialUpdateMainPass',
    scenarioCount: 2
  }),
  Object.freeze({
    caseId: 'webgpu_procedural_texture',
    shardName: 'WebgpuProceduralTexture',
    passName: 'WebgpuProceduralTextureMainPass',
    scenarioCount: 3
  }),
  Object.freeze({
    caseId: 'webgl_panorama_equirectangular',
    shardName: 'Phase1WebglPanoramaEquirectangular',
    passName: 'WebglPanoramaEquirectangularMainPass',
    scenarioCount: 4
  }),
  Object.freeze({
    caseId: 'webgl_materials_texture_anisotropy',
    shardName: 'Phase1WebglMaterialsTextureAnisotropy',
    manifestShardName: 'Phase1MaterialsSimple',
    passName:
      'WebglMaterialsTextureAnisotropyLeftMaxAnisotropyPass',
    additionalPassNames: [
      'WebglMaterialsTextureAnisotropyRightAnisotropyOnePass'
    ],
    scenarioCount: 3
  }),
  Object.freeze({
    caseId: 'webgpu_textures_anisotropy',
    shardName: 'Phase1WebgpuTexturesAnisotropy',
    passName: 'WebgpuTexturesAnisotropyLeftPass',
    additionalPassNames: ['WebgpuTexturesAnisotropyRightPass'],
    scenarioCount: 2
  }),
  Object.freeze({
    caseId: 'webgpu_texturegrad',
    shardName: 'WebgpuTexturegrad',
    passName: 'WebgpuTexturegradWebgpuMainPass',
    additionalPassNames: ['WebgpuTexturegradWebglMainPass'],
    scenarioCount: 2
  })
]);

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

/** Loads every dedicated texture-family case from the frozen Manifest. */
async function loadSelectedExamples(manifestPath, configurations) {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (!Array.isArray(manifest.examples) || manifest.examples.length !== 588) {
    throw new Error(
      'Texture-array fixture requires the frozen 588-item r185 Manifest.');
  }
  return configurations.map((configuration) => {
    const example = manifest.examples.find(
      (entry) => entry.id === configuration.caseId);
    if (example?.status !== 'phase1_required'
        || example.renderSetPolicy !== 'not-required'
        || example.dslShard
          !== (configuration.manifestShardName
            ?? configuration.shardName)
        || example.renderSetType !== null
        || example.scenarios?.length !== configuration.scenarioCount) {
      throw new Error(
        `${configuration.caseId} Manifest ownership differs from the locked contract.`);
    }
    return example;
  });
}

/** Copies and verifies every pinned r185 texture-family source asset. */
async function stageTextureAssets(outputDirectory, assetSourceRoot) {
  const assetRoot = path.join(outputDirectory, 'locked-assets');
  const identities = [];
  for (const [relativePath, expectedSha256] of [
    [volumeRelativePath, volumeSha256],
    [carbonRelativePath, carbonSha256],
    [panoramaRelativePath, panoramaSha256],
    [crateRelativePath, crateSha256],
    [uvGridRelativePath, uvGridSha256]
  ]) {
    const sourcePath = path.join(assetSourceRoot, relativePath);
    const bytes = await fs.readFile(sourcePath);
    const actualSha256 = calculateSha256(bytes);
    if (actualSha256 !== expectedSha256) {
      throw new Error(
        `${sourcePath}: texture identity differs from Three r185.`);
    }
    const destination = path.join(assetRoot, relativePath);
    await fs.mkdir(path.dirname(destination), { recursive: true });
    await fs.writeFile(destination, bytes);
    identities.push({
      path: relativePath,
      sha256: actualSha256,
      byteCount: bytes.byteLength
    });
  }
  for (const [relativePath, expectedSha256] of [
    [proceduralReplayRelativePath, proceduralReplaySha256],
    [panoramaReplayRelativePath, panoramaReplaySha256],
    [
      webglAnisotropyReplayRelativePath,
      webglAnisotropyReplaySha256
    ],
    [
      webgpuAnisotropyReplayRelativePath,
      webgpuAnisotropyReplaySha256
    ]
  ]) {
    const replaySource = path.join(
      repositoryRoot,
      'GVMRuntime_ThreeSamples',
      relativePath
    );
    const replayBytes = await fs.readFile(replaySource);
    const replayIdentity = calculateSha256(replayBytes);
    if (replayIdentity !== expectedSha256) {
      throw new Error(
        `${replaySource}: replay identity differs from the locked contract.`);
    }
    const replayDestination = path.join(assetRoot, relativePath);
    await fs.mkdir(path.dirname(replayDestination), { recursive: true });
    await fs.writeFile(replayDestination, replayBytes);
    identities.push({
      path: relativePath,
      sha256: replayIdentity,
      byteCount: replayBytes.byteLength
    });
  }
  return {
    assetRoot,
    identities
  };
}

/** Converts dedicated screen-pass lint results into the shared report shape. */
function createGeneratedArtifactReport(generatedRoot, configurations) {
  const result = lintGeneratedArtifacts({
    legacyRoot: path.join(generatedRoot, 'legacy'),
    experimentalRoot: path.join(generatedRoot, 'experimental'),
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: configurations.flatMap(
      ({ passName, additionalPassNames = [] }) =>
        [passName, ...additionalPassNames].map(
          (name) => `${name}:screen`)),
    instancingPasses: []
  });
  return {
    status: result.errors.length === 0 ? 'pass' : 'fail',
    checkedPasses: result.checkedPasses,
    failures: result.errors
  };
}

/** Runs all selected cases through every scenario, quadrant, and repetition. */
async function runMatrix(context, examples, configurations) {
  const quadrants = [];
  for (let repetition = 1;
       repetition <= context.repetitions;
       repetition += 1) {
    for (let caseIndex = 0;
         caseIndex < examples.length;
         caseIndex += 1) {
      const example = examples[caseIndex];
      const configuration = configurations[caseIndex];
      const caseContext = {
        ...context,
        profile: {
          buildDir: context.runDir,
          hostCandidates: {
            legacy: [
              path.join(
                context.binaryRoot,
                `${configuration.shardName}-legacy`)
            ],
            experimental: [
              path.join(
                context.binaryRoot,
                `${configuration.shardName}-experimental`)
            ]
          }
        }
      };
      for (const scenario of example.scenarios) {
        for (const pipeline of requiredPipelines) {
          for (const backend of requiredBackends) {
            const result = await runQuadrant(
              caseContext,
              example,
              scenario,
              pipeline,
              backend,
              repetition
            );
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

/** Returns one complete case-local strict summary from the aggregate evidence. */
function createCaseSummary(summary, caseId, selectedCaseCount) {
  const quadrants = summary.quadrants.filter(
    (entry) => entry.caseId === caseId);
  const crossComparisons = summary.crossComparisons.filter(
    (entry) => entry.caseId === caseId);
  const stabilityComparisons = summary.stabilityComparisons.filter(
    (entry) => entry.caseId === caseId);
  const expectedRunCount =
    summary.expectedRunCount / selectedCaseCount;
  return {
    ...summary,
    selectedCases: [caseId],
    runCount: quadrants.length,
    expectedRunCount,
    quadrants,
    crossComparisons,
    stabilityComparisons
  };
}

/** Executes the strict texture-family fixture and writes aggregate summaries. */
async function main() {
  const options = parseArguments(process.argv);
  const binaryRoot = requirePathOption(options, 'binary-root');
  const generatedRoot = requirePathOption(options, 'generated-root');
  const oracleRoot = requirePathOption(options, 'oracle-root');
  const assetSourceRoot =
    requirePathOption(options, 'asset-source-root');
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
  const selectedConfigurations = options['case-id']
    ? caseConfigurations.filter(
      (configuration) => configuration.caseId === options['case-id'])
    : [...caseConfigurations];
  if (selectedConfigurations.length === 0) {
    throw new Error(
      `--case-id '${options['case-id']}' is not a texture-family case.`);
  }
  await fs.mkdir(outputDirectory, { recursive: true });
  const [examples, inputs, gpuBoundaryLint] = await Promise.all([
    loadSelectedExamples(manifestPath, selectedConfigurations),
    stageTextureAssets(outputDirectory, assetSourceRoot),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint =
    createGeneratedArtifactReport(
      generatedRoot,
      selectedConfigurations);
  const context = {
    sourceDir: repositoryRoot,
    runDir: outputDirectory,
    binaryRoot,
    assetRoot: inputs.assetRoot,
    oracleRoot,
    timeoutMs,
    repetitions,
    profile: {}
  };
  const quadrants = await runMatrix(
    context,
    examples,
    selectedConfigurations);
  const crossComparisons = await buildCrossComparisons(quadrants);
  const stabilityComparisons =
    await buildStabilityComparisons(quadrants, repetitions);
  const expectedRunCount = examples.reduce(
    (sum, example) => sum + example.scenarios.length,
    0
  ) * requiredPipelines.length
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
    selectedCases:
      selectedConfigurations.map(({ caseId }) => caseId),
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
  for (const { caseId } of selectedConfigurations) {
    await writeJson(
      path.join(outputDirectory, 'cases', caseId, 'summary.json'),
      createCaseSummary(
        summary,
        caseId,
        selectedConfigurations.length)
    );
  }
  console.log(
    `Texture-family fixture ${status}: `
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
