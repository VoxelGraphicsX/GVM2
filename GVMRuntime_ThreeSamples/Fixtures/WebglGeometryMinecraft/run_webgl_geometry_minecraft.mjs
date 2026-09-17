#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  compareQuadrantPair,
  loadJson,
  requiredBackends,
  requiredPipelines,
  runThreeMatrix
} from '../../../tests/runners/three/node/runner.mjs';
import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { scanSampleCppSource } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgl_geometry_minecraft';
const shardName = 'WebglGeometryMinecraft';
const randomSeed = 0x12345678;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = '354832a79eb1831cd756798913cef299b64d3f234058813ef241e2096916c1ac';
const atlasSha256 = '9e7a2ed78c02db3d54c2082534745797dde51f4b2b6bdcfed139dfe0a4abb7ef';
const replaySha256 = '40998e57ee419db255557a0e32e35bb07321a1dfe2fe8d54859cbf68325246b1';
const replayAssetPath = 'inputs/webgl_geometry_minecraft_first_person.json';
const geometrySha256 = '44028fa9693fed6cb902c50151ac22a4351c4fe7113cf891476454481bc545a9';

export const minecraftScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    kind: 'initial-frame',
    frame: 0,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-rng-draw92309-frame0-atlas-loaded-one-merged-mesh',
    virtualTimeMs: 0,
    timerDeltaSeconds: 0,
    cameraPosition: Object.freeze([0, 100, 0]),
    controlsVelocity: Object.freeze([0, 0, 0])
  }),
  Object.freeze({
    id: 'animated',
    kind: 'fixed-frame',
    frame: 60,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-rng-draw92309-frame60-static-camera-fixed-timer',
    virtualTimeMs: 999.9999999999991,
    timerDeltaSeconds: 0.01666666666666663,
    cameraPosition: Object.freeze([0, 100, 0]),
    controlsVelocity: Object.freeze([0, 0, 0])
  }),
  Object.freeze({
    id: 'first-person-input',
    kind: 'input-replay',
    frame: 61,
    inputReplay: replayAssetPath,
    canonicalState: 'seed-0x12345678-rng-draw92309-key-w-d-frames1-through30-camera-494.5197900233341-100-minus494.5197900233341',
    virtualTimeMs: 1016.6666666666657,
    timerDeltaSeconds: 0.01666666666666663,
    cameraPosition: Object.freeze([494.5197900233341, 100, -494.5197900233341]),
    controlsVelocity: Object.freeze([36.534733177771585, 0, -36.534733177771585])
  })
]);

const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: '0d87750ff3187b835651167d251bcd15e89114061dc4e15f71787c2d56d99c2d',
    json: '37c382488984cf444bcfa9b1c0d9067e5a71014ec8c4093f1593bfb41f8f4114'
  }),
  animated: Object.freeze({
    rgba: '0d87750ff3187b835651167d251bcd15e89114061dc4e15f71787c2d56d99c2d',
    json: 'cb8ccb7e68a919cda452d126fd99b47d564858a787a3e56774a5a2a3dae50eba'
  }),
  'first-person-input': Object.freeze({
    rgba: '988c78d23753f98ff9f50c589066398afea8c03de187134fc8704228a28546d5',
    json: 'e3675624ee55c085627636d687cc4ae554fb0aef1f045338bacb8135375e95f1'
  })
});

const diagonalDefinitions = Object.freeze([
  Object.freeze({
    relation: 'diagonal-legacy-metal-experimental-vulkan',
    left: Object.freeze(['legacy', 'metal']),
    right: Object.freeze(['experimental', 'vulkan'])
  }),
  Object.freeze({
    relation: 'diagonal-legacy-vulkan-experimental-metal',
    left: Object.freeze(['legacy', 'vulkan']),
    right: Object.freeze(['experimental', 'metal'])
  })
]);

/** Parses strict unique long-form option pairs without environment fallbacks. */
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

/** Parses a repeat count while preserving the mandatory three-run stability gate. */
export function parseRepeatCount(value) {
  const parsed = value == null ? 3 : Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`--repeat must be a positive integer; received '${value}'.`);
  }
  if (parsed < 3) throw new Error(`--repeat must be at least 3; received '${parsed}'.`);
  return parsed;
}

/** Resolves one mandatory explicit path option. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one optional positive watchdog duration. */
function parseTimeout(value) {
  if (value == null) return 300_000;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`--timeout-ms must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Returns a file's lowercase SHA-256 identity. */
async function sha256File(filePath) {
  return createHash('sha256').update(await fs.readFile(filePath)).digest('hex');
}

/** Validates selected nested fields while allowing diagnostic extensions. */
export function validateExpectedFields(actual, expected, label, failures) {
  for (const [name, expectedValue] of Object.entries(expected)) {
    const actualValue = actual?.[name];
    const fieldLabel = `${label}.${name}`;
    if (expectedValue !== null && typeof expectedValue === 'object') {
      if (actualValue === null || typeof actualValue !== 'object') {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected an object.`);
      } else {
        validateExpectedFields(actualValue, expectedValue, fieldLabel, failures);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Builds one generic-runner scenario for the logical single Scene pass. */
function makeRuntimeScenario(scenario) {
  return {
    id: scenario.id,
    kind: scenario.kind,
    frame: scenario.frame,
    inputReplay: scenario.inputReplay,
    canonicalState: scenario.canonicalState,
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main-atlas-lambert'
    }]
  };
}

/** Verifies the authoritative status-lock contract for this one ordinary Mesh. */
export function validateStatusLockContract(statusLock) {
  const failures = [];
  if (statusLock?.upstreamCommit !== upstreamCommit) {
    failures.push(`Status-lock upstream revision ${statusLock?.upstreamCommit} differs from ${upstreamCommit}.`);
  }
  const reviews = (statusLock?.phase1RequiredReviews ?? []).filter((candidate) => candidate.id === caseId);
  if (reviews.length !== 1) {
    failures.push(`Status lock must contain exactly one ${caseId} required review; observed ${reviews.length}.`);
  }
  const review = reviews[0];
  validateExpectedFields(review, {
    dslShard: shardName,
    renderSetPolicy: 'not-required',
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    renderSetType: null,
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    scenePasses: [{
      name: 'main-atlas-lambert',
      renderClass: 'WebglGeometryMinecraftMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: []
  }, `statusLock.${caseId}`, failures);
  const lockedScenarios = new Map((review?.scenarios ?? []).map((scenario) => [scenario.id, scenario]));
  if (lockedScenarios.size !== minecraftScenarios.length) {
    failures.push(`statusLock.${caseId}.scenarios has ${lockedScenarios.size} entries; expected ${minecraftScenarios.length}.`);
  }
  for (const scenario of minecraftScenarios) {
    validateExpectedFields(lockedScenarios.get(scenario.id), {
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState
    }, `statusLock.${caseId}.scenarios.${scenario.id}`, failures);
  }
  return failures;
}

/** Creates a 588-entry diagnostic manifest with only this example selected. */
export function createDiagnosticManifest(manifest, minecraftReview) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error(`Manifest inventory must contain 588 examples; observed ${manifest?.examples?.length ?? 0}.`);
  }
  const examples = manifest.examples.map((example) => {
    if (example.id !== caseId) return { ...example, status: 'excluded_upstream' };
    return {
      ...example,
      ...minecraftReview,
      status: 'phase1_required',
      scenarios: minecraftScenarios.map(makeRuntimeScenario)
    };
  });
  if (!examples.some((example) => example.id === caseId)) {
    throw new Error(`Manifest inventory does not contain ${caseId}.`);
  }
  return { ...manifest, examples };
}

/** Validates immutable source, atlas, replay, and all three Oracle identities. */
async function validateInputs(context, statusLock) {
  const failures = validateStatusLockContract(statusLock);
  let revision = '';
  try {
    revision = execFileSync('git', ['-C', context.upstreamRoot, 'rev-parse', 'HEAD'], {
      encoding: 'utf8'
    }).trim();
  } catch (error) {
    failures.push(`Could not read the pinned upstream revision: ${error.message}.`);
  }
  if (revision !== upstreamCommit) failures.push(`Upstream revision ${revision} differs from ${upstreamCommit}.`);
  const sourcePath = path.join(context.upstreamRoot, 'examples', 'webgl_geometry_minecraft.html');
  const atlasPath = path.join(context.upstreamRoot, 'examples', 'textures', 'minecraft', 'atlas.png');
  try {
    if (await sha256File(sourcePath) !== upstreamSourceSha256) failures.push('Pinned upstream source SHA-256 differs.');
    if (await sha256File(atlasPath) !== atlasSha256) failures.push('Pinned atlas.png SHA-256 differs.');
    if (await sha256File(context.replayPath) !== replaySha256) failures.push('Canonical FirstPersonControls replay SHA-256 differs.');
  } catch (error) {
    failures.push(error instanceof Error ? error.message : String(error));
  }
  const oracles = [];
  for (const scenario of minecraftScenarios) {
    const basePath = path.join(context.oracleRoot, caseId, scenario.id);
    const rgbaPath = `${basePath}.rgba`;
    const metadataPath = `${basePath}.json`;
    try {
      const [rgbaIdentity, metadataIdentity, metadata] = await Promise.all([
        sha256File(rgbaPath),
        sha256File(metadataPath),
        loadJson(metadataPath)
      ]);
      const locked = lockedOracleSha256[scenario.id];
      if (rgbaIdentity !== locked.rgba) failures.push(`${scenario.id} Oracle RGBA SHA-256 differs.`);
      if (metadataIdentity !== locked.json) failures.push(`${scenario.id} Oracle metadata SHA-256 differs.`);
      validateExpectedFields(metadata, {
        caseId,
        scenarioId: scenario.id,
        frame: scenario.frame,
        randomSeed,
        randomState: 124295903,
        width: 800,
        height: 500,
        inputReplay: scenario.inputReplay == null ? null : {
          sha256: replaySha256,
          eventCount: 4,
          lastEventFrame: 31,
          target: '#container > canvas'
        }
      }, `oracle.${scenario.id}`, failures);
      oracles.push({ scenarioId: scenario.id, rgbaPath, metadataPath, rgbaIdentity, metadataIdentity });
    } catch (error) {
      failures.push(`${scenario.id} Oracle: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  if (lockedOracleSha256.initial.rgba !== lockedOracleSha256.animated.rgba) {
    failures.push('Initial and no-input frame-60 Oracle captures must remain byte-identical.');
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    upstreamCommit,
    upstreamSourceSha256,
    atlasSha256,
    replaySha256,
    oracles,
    failures
  };
}

/** Stages the locked atlas and replay into the explicit runtime asset pack. */
async function stageAssetPack(context) {
  const assetRoot = path.join(context.outputRoot, 'locked-asset-pack');
  const atlasDirectory = path.join(assetRoot, 'textures', 'minecraft');
  const inputDirectory = path.join(assetRoot, 'inputs');
  await Promise.all([
    fs.mkdir(atlasDirectory, { recursive: true }),
    fs.mkdir(inputDirectory, { recursive: true })
  ]);
  await Promise.all([
    fs.copyFile(
      path.join(context.upstreamRoot, 'examples', 'textures', 'minecraft', 'atlas.png'),
      path.join(atlasDirectory, 'atlas.png')),
    fs.copyFile(
      context.replayPath,
      path.join(inputDirectory, 'webgl_geometry_minecraft_first_person.json'))
  ]);
  return assetRoot;
}

/** Counts non-overlapping occurrences of one generated host fragment. */
function countOccurrences(source, fragment) {
  let count = 0;
  let offset = 0;
  while ((offset = source.indexOf(fragment, offset)) >= 0) {
    count += 1;
    offset += fragment.length;
  }
  return count;
}

/** Validates dual generated ABI, four Scene invocations, and Experimental products. */
export async function validateGeneratedArtifacts(generatedRoot) {
  const records = [];
  const passNames = ['WebglGeometryMinecraftMainPass'];
  for (const pipeline of requiredPipelines) {
    const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    try {
      const [exportsSource, generatedSource] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8')
      ]);
      if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
        failures.push('ExportedRenderSet must remain empty for the one ordinary Mesh.');
      }
      for (const passName of passNames) {
        if (!generatedSource.includes(`class ${passName}`)) failures.push(`Generated host is missing ${passName}.`);
        if (pipeline === 'experimental') {
          for (const stage of ['vertex', 'fragment']) {
            for (const relativePath of [
              path.join('uglir', `${passName}__${stage}.uglir.json`),
              path.join('msl', `${passName}__${stage}.msl`),
              path.join('spv', `${passName}__${stage}.raw.spv.txt`)
            ]) {
              try {
                await fs.access(path.join(generatedDirectory, relativePath));
              } catch {
                failures.push(`Missing Experimental product ${relativePath}.`);
              }
            }
          }
        }
      }
      const mainRun = 'run(WebglGeometryMinecraftIndexCount, 1u, 0u, 0, 0u)';
      if (countOccurrences(generatedSource, mainRun) !== 1) {
        failures.push('The ordinary Scene must issue exactly one single-sample indexed draw.');
      }
      if (!generatedSource.includes('main-atlas-lambert')) {
        failures.push('Generated host is missing the single-sample Scene pass.');
      }
      for (let mipLevel = 0; mipLevel < 6; mipLevel += 1) {
        if (!generatedSource.includes(`atlasMipOffsets[${mipLevel}u]`)) {
          failures.push(`Generated host is missing explicit atlas mip ${mipLevel}.`);
        }
      }
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({ pipeline, generatedDirectory, status: failures.length === 0 ? 'pass' : 'fail', failures });
  }
  return records;
}

/** Scans only this sample's C++ boundary for prohibited direct GPU work. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebglGeometryMinecraftRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebglGeometryMinecraftRuntimeAdapter.cpp')
  ];
  const violations = [];
  for (const filePath of files) {
    const source = await fs.readFile(filePath, 'utf8');
    violations.push(...scanSampleCppSource(source, path.relative(repositoryRoot, filePath)));
  }
  return {
    status: violations.length === 0 ? 'pass' : 'fail',
    filesScanned: files.length,
    violationCount: violations.length,
    violations
  };
}

/** Adds the two diagonal relations so all six pairs among four quadrants are checked. */
async function buildDiagonalComparisons(quadrants) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.scenarioId}|${entry.repetition}|${entry.pipeline}|${entry.backend}`,
    entry
  ]));
  const comparisons = [];
  for (const scenario of minecraftScenarios) {
    const repetitions = [...new Set(quadrants
      .filter((entry) => entry.scenarioId === scenario.id)
      .map((entry) => entry.repetition))];
    for (const repetition of repetitions) {
      for (const definition of diagonalDefinitions) {
        const prefix = `${scenario.id}|${repetition}`;
        const left = byKey.get(`${prefix}|${definition.left[0]}|${definition.left[1]}`);
        const right = byKey.get(`${prefix}|${definition.right[0]}|${definition.right[1]}`);
        if (left && right) comparisons.push(await compareQuadrantPair(left, right, definition.relation));
      }
    }
  }
  return comparisons;
}

/** Validates static no-input equivalence and distinct replay movement per quadrant. */
async function validateScenarioRelations(quadrants) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    for (const backend of requiredBackends) {
      const repetitions = [...new Set(quadrants
        .filter((entry) => entry.pipeline === pipeline && entry.backend === backend)
        .map((entry) => entry.repetition))];
      for (const repetition of repetitions) {
        const byScenario = new Map(quadrants
          .filter((entry) => entry.pipeline === pipeline && entry.backend === backend && entry.repetition === repetition)
          .map((entry) => [entry.scenarioId, entry]));
        const failures = [];
        const initial = byScenario.get('initial');
        const animated = byScenario.get('animated');
        const interactive = byScenario.get('first-person-input');
        if (![initial, animated, interactive].every((entry) => entry?.status === 'pass')) {
          failures.push('All three successful scenario captures are required.');
        } else {
          const [initialImage, animatedImage, initialIdentity, animatedIdentity, interactiveIdentity] = await Promise.all([
            loadRgbaArtifact(initial.artifacts.rgbaPath, initial.artifacts.metadataPath),
            loadRgbaArtifact(animated.artifacts.rgbaPath, animated.artifacts.metadataPath),
            sha256File(initial.artifacts.rgbaPath),
            sha256File(animated.artifacts.rgbaPath),
            sha256File(interactive.artifacts.rgbaPath)
          ]);
          const staticComparison = compareThreeCaptures(initialImage, animatedImage);
          if (staticComparison.failures.length > 0) {
            failures.push('Initial and frame-60 no-input captures must remain visually equivalent.');
          }
          if (interactiveIdentity === initialIdentity || interactiveIdentity === animatedIdentity) {
            failures.push('The canonical FirstPersonControls replay must change the final image.');
          }
        }
        records.push({ pipeline, backend, repetition, status: failures.length === 0 ? 'pass' : 'fail', failures });
      }
    }
  }
  return records;
}

/** Validates RNG, terrain, atlas, controls, and actual four-draw topology per capture. */
async function validateRuntimeScenarios(quadrants) {
  const expectedById = new Map(minecraftScenarios.map((scenario) => [scenario.id, scenario]));
  const records = [];
  for (const quadrant of quadrants) {
    const failures = [];
    try {
      const [metadata, snapshot] = await Promise.all([
        loadJson(quadrant.artifacts.metadataPath),
        loadJson(quadrant.artifacts.sceneSnapshotPath)
      ]);
      const expected = expectedById.get(quadrant.scenarioId);
      validateExpectedFields(metadata, {
        randomState: 124295903,
        randomDrawCount: 92309,
        warmupRenderCount: 2,
        virtualTimeMs: expected.virtualTimeMs,
        timerDeltaSeconds: expected.timerDeltaSeconds,
        cameraPosition: expected.cameraPosition,
        cameraQuaternion: [0, 0, 0, 1],
        controlsVelocity: expected.controlsVelocity,
        atlasSha256,
        atlasWidth: 16,
        atlasHeight: 32,
        atlasColorSpace: 'srgb',
        atlasMagFilter: 'nearest',
        atlasMinFilter: 'linear-mipmap-linear',
        atlasMipCount: 6,
        geometrySha256,
        terrainFaceCount: 23031,
        inputReplay: expected.inputReplay == null ? null : {
          sha256: replaySha256,
          eventCount: 4,
          lastEventFrame: 31,
          target: '#container > canvas'
        }
      }, `${quadrant.scenarioId}.${quadrant.pipeline}.${quadrant.backend}.repeat-${quadrant.repetition}`, failures);
      validateExpectedFields(snapshot, {
        renderSetPolicy: 'not-required',
        sceneRenderSetCount: 0,
        renderableObjectCount: 1,
        instanceCount: 1,
        terrainFaceCount: 23031,
        vertexCount: 92124,
        indexCount: 138186,
        triangleCount: 46062,
        textureCount: 1,
        textureMipCount: 6,
        warmupRenderCount: 2,
        scenePassCount: 1,
        screenPassCount: 0,
        drawCommandCount: 1,
        gpuWorkDslOnly: true
      }, `${quadrant.scenarioId}.snapshot`, failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({
      scenarioId: quadrant.scenarioId,
      pipeline: quadrant.pipeline,
      backend: quadrant.backend,
      repetition: quadrant.repetition,
      status: failures.length === 0 ? 'pass' : 'fail',
      failures
    });
  }
  return records;
}

/** Executes the three-scenario, four-quadrant, three-repeat formal gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    replayPath: requirePathOption(options, 'input-replay'),
    repeatCount: parseRepeatCount(options.repeat),
    timeoutMs: parseTimeout(options['timeout-ms'])
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  const [manifest, statusLock] = await Promise.all([
    loadJson(path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json')),
    loadJson(path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-phase1-status-lock.json'))
  ]);
  const minecraftReview = statusLock.phase1RequiredReviews?.find((review) => review.id === caseId);
  const inputValidation = await validateInputs(context, statusLock);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    validateGeneratedArtifacts(context.generatedRoot),
    validateGpuBoundary()
  ]);
  const assetRoot = await stageAssetPack(context);
  let matrix = {
    status: 'fail',
    selectedCases: [],
    quadrants: [],
    crossComparisons: [],
    stabilityComparisons: [],
    coverage: null
  };
  if (inputValidation.status === 'pass'
      && generatedArtifacts.every((entry) => entry.status === 'pass')
      && gpuBoundaryLint.status === 'pass') {
    matrix = await runThreeMatrix({
      sourceDir: repositoryRoot,
      runDir: context.outputRoot,
      profile: {
        name: 'macos-three-r185-webgl-geometry-minecraft',
        buildDir: path.dirname(path.dirname(context.binaryRoot)),
        hosts: {
          legacy: path.join(context.binaryRoot, `${shardName}-legacy`),
          experimental: path.join(context.binaryRoot, `${shardName}-experimental`)
        }
      },
      manifest: createDiagnosticManifest(manifest, minecraftReview),
      assetRoot,
      oracleRoot: context.oracleRoot,
      status: 'phase1-required',
      group: 'full',
      pipelines: [...requiredPipelines],
      backends: [...requiredBackends],
      repetitions: context.repeatCount,
      timeoutMs: context.timeoutMs,
      onProgress: (result, completed) => {
        console.log(`[${result.status.toUpperCase()}] ${completed} ${result.caseId}/${result.scenarioId} ${result.pipeline}/${result.backend} repeat=${result.repetition}`);
        for (const failure of result.failures) console.log(`  ${failure}`);
      }
    });
  }
  const [diagonalComparisons, scenarioRelations, runtimeScenarioValidation] = await Promise.all([
    buildDiagonalComparisons(matrix.quadrants),
    validateScenarioRelations(matrix.quadrants),
    validateRuntimeScenarios(matrix.quadrants)
  ]);
  const crossComparisons = [...matrix.crossComparisons, ...diagonalComparisons];
  const expectedRunCount = minecraftScenarios.length * requiredPipelines.length
    * requiredBackends.length * context.repeatCount;
  const expectedCrossComparisonCount = minecraftScenarios.length * context.repeatCount * 6;
  const expectedStabilityComparisonCount = minecraftScenarios.length * requiredPipelines.length
    * requiredBackends.length * (context.repeatCount - 1);
  const expectedScenarioRelationCount = requiredPipelines.length * requiredBackends.length
    * context.repeatCount;
  const status = matrix.status === 'pass'
    && matrix.quadrants.length === expectedRunCount
    && matrix.quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.length === expectedCrossComparisonCount
    && crossComparisons.every((entry) => entry.status === 'pass')
    && matrix.stabilityComparisons.length === expectedStabilityComparisonCount
    && matrix.stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioRelations.length === expectedScenarioRelationCount
    && scenarioRelations.every((entry) => entry.status === 'pass')
    && runtimeScenarioValidation.length === expectedRunCount
    && runtimeScenarioValidation.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-geometry-minecraft',
    caseId,
    status,
    comparisonThresholds,
    repeatCount: context.repeatCount,
    runCount: matrix.quadrants.length,
    expectedRunCount,
    inputValidation,
    generatedArtifacts,
    gpuBoundaryLint,
    selectedCases: matrix.selectedCases,
    quadrants: matrix.quadrants,
    crossComparisons,
    stabilityComparisons: matrix.stabilityComparisons,
    scenarioRelations,
    runtimeScenarioValidation,
    coverage: matrix.coverage
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_geometry_minecraft formal report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
