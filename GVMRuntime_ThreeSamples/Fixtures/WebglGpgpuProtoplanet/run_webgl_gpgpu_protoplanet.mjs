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
import { comparisonThresholds } from '../../../tests/runners/three/node/image-comparison.mjs';
import { scanSampleCppSource } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgl_gpgpu_protoplanet';
const shardName = 'WebglGpgpuProtoplanet';
const randomSeed = 0x12345678;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = 'd6fb9f50fa55f91f0827f8283519481f0a4d7ce675ac72e475275ae7bf1d959b';
const stateScriptSha256 = 'dde37881ef7ebe788ba211d8b084d9347ef7998276fdffd2179b40e9f3c67a45';
const stateScriptAssetPath = 'inputs/webgl_gpgpu_protoplanet_restart_gui_state.js';

export const protoplanetScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-seeded-disc',
    kind: 'initial-frame',
    frame: 0,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-width-64-frame0-one-fixed-fragment-pair-pingpong-index-1-rng-state-492283682',
    simulationStepCount: 1,
    simulationFragmentPassCount: 2,
    currentPingPongIndex: 1,
    randomState: 492283682,
    gravityConstant: 100,
    density: 0.45,
    restartApplied: false,
    initialPositionSha256: '0e417ba691875dbb636dafb9a3688fd09b73aabb3db940b444e61b70491980bf',
    initialVelocitySha256: 'd8881655408692ae94227d0097106e8284e379eb847b4e907c42b3921817bc04',
    currentPositionSha256: 'c8fe2703339ddbab1dc5869455d0f4af82121043bd86de6d1a7a156d3d8dfa15',
    currentVelocitySha256: 'f9acae9b8c71a7f4276075b67cc869f39731f5f869cabca750cbc8e4f8c5eb2a'
  }),
  Object.freeze({
    id: 'fixed-nbody-step',
    kind: 'fixed-frame',
    frame: 60,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-frame60-sixty-one-fixed-fragment-pairs-pingpong-index-1-rng-state-492283682',
    simulationStepCount: 61,
    simulationFragmentPassCount: 122,
    currentPingPongIndex: 1,
    randomState: 492283682,
    gravityConstant: 100,
    density: 0.45,
    restartApplied: false,
    initialPositionSha256: '0e417ba691875dbb636dafb9a3688fd09b73aabb3db940b444e61b70491980bf',
    initialVelocitySha256: 'd8881655408692ae94227d0097106e8284e379eb847b4e907c42b3921817bc04',
    currentPositionSha256: 'ddb42185b99c3176ebbbad06251fec28fc13d59a85d8cc74617f7d15daa20805',
    currentVelocitySha256: '9fed4b201c848da0ca0094458702b9d4be75e15b6efc2b2e1d6a3614e05474a9'
  }),
  Object.freeze({
    id: 'gui-restart',
    kind: 'fixed-frame',
    frame: 60,
    inputReplay: null,
    canonicalState: stateScriptAssetPath,
    simulationStepCount: 61,
    simulationFragmentPassCount: 122,
    currentPingPongIndex: 1,
    randomState: 1864344099,
    gravityConstant: 175,
    density: 0.72,
    restartApplied: true,
    initialPositionSha256: '39cb00f46ee45fedcf5401e8257088e05e8e891ec7119d4bf33467e7a9b106fc',
    initialVelocitySha256: '53a9f10f4906ba95f8c03364e906453a1bc31c66db1f12b12279f979b2f546fb',
    currentPositionSha256: 'fd63fcd6c8ef04c15db1ea61e5d204c92ca380180cb9b11f9c631b81dcc399f1',
    currentVelocitySha256: 'ada7ad2a05e501d138861cac7c336cc003891ced4b8529f25143e1f2cfd832a3'
  })
]);

const lockedOracleSha256 = Object.freeze({
  'initial-seeded-disc': Object.freeze({
    rgba: 'ebf229dd2f6d02ad7bb0b0ae67ff44c74fed7e6ee11db380ac3fb6771999e807',
    json: '883c521252ae65a03eb95d4e8da1b3e8bf39ef0ea19ff6017688d4a278e145c8'
  }),
  'fixed-nbody-step': Object.freeze({
    rgba: 'dd63744a53cfd11e5b15a3eef05b9990b2b462d4d544ab7d70196bc5a9b10948',
    json: 'babcb35617510fd444bbc6d0fcb539360387cbb717e7a87affaa72911f9b2c3e'
  }),
  'gui-restart': Object.freeze({
    rgba: '9eb6919be22a9de08b67fff7ea8f1a17104d837e776c27587870412bd73fa847',
    json: '28b006f4b26e8b4315a89609a52c0610aa16f4283b3956f5cbe9077d851adbdf'
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

/** Parses a repeat count that preserves the mandatory three-run stability gate. */
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

/** Builds one runtime scenario using the canonical-state script contract understood by the host. */
function makeRuntimeScenario(scenario) {
  return {
    id: scenario.id,
    kind: scenario.kind,
    frame: scenario.frame,
    inputReplay: null,
    canonicalState: scenario.canonicalState,
    scenePassInvocations: [{
      sceneRoot: 'scene',
      scenePass: 'main-expanded-circular-particles',
      invocationCount: 1
    }],
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main-expanded-circular-particles'
    }]
  };
}

/** Verifies that the status lock contains one complete authoritative protoplanet review. */
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
      name: 'main-expanded-circular-particles',
      renderClass: 'WebglGpgpuProtoplanetMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: []
  }, `statusLock.${caseId}`, failures);
  const lockedScenarios = new Map((review?.scenarios ?? []).map((scenario) => [scenario.id, scenario]));
  if (lockedScenarios.size !== protoplanetScenarios.length) {
    failures.push(`statusLock.${caseId}.scenarios has ${lockedScenarios.size} entries; expected ${protoplanetScenarios.length}.`);
  }
  for (const scenario of protoplanetScenarios) {
    validateExpectedFields(lockedScenarios.get(scenario.id), {
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: null,
      canonicalState: scenario.canonicalState
    }, `statusLock.${caseId}.scenarios.${scenario.id}`, failures);
  }
  return failures;
}

/** Creates a 588-entry diagnostic manifest whose protoplanet contract comes from the status lock. */
export function createDiagnosticManifest(manifest, protoplanetReview) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error(`Manifest inventory must contain 588 examples; observed ${manifest?.examples?.length ?? 0}.`);
  }
  const examples = manifest.examples.map((example) => {
    if (example.id !== caseId) return { ...example, status: 'excluded_upstream' };
    return {
      ...example,
      ...protoplanetReview,
      status: 'phase1_required',
      scenarios: protoplanetScenarios.map(makeRuntimeScenario)
    };
  });
  if (!examples.some((example) => example.id === caseId)) {
    throw new Error(`Manifest inventory does not contain ${caseId}.`);
  }
  return { ...manifest, examples };
}

/** Validates immutable source, state script, and Oracle identities. */
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
  const sourcePath = path.join(context.upstreamRoot, 'examples', 'webgl_gpgpu_protoplanet.html');
  try {
    if (await sha256File(sourcePath) !== upstreamSourceSha256) failures.push('Pinned upstream source SHA-256 differs.');
    if (await sha256File(context.stateScriptPath) !== stateScriptSha256) failures.push('Canonical-state script SHA-256 differs.');
  } catch (error) {
    failures.push(error instanceof Error ? error.message : String(error));
  }
  const oracles = [];
  for (const scenario of protoplanetScenarios) {
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
        randomState: scenario.randomState,
        width: 800,
        height: 500,
        inputReplay: null
      }, `oracle.${scenario.id}`, failures);
      oracles.push({ scenarioId: scenario.id, rgbaPath, metadataPath, rgbaIdentity, metadataIdentity });
    } catch (error) {
      failures.push(`${scenario.id} Oracle: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    upstreamCommit,
    upstreamSourceSha256,
    stateScriptSha256,
    oracles,
    failures
  };
}

/** Copies the immutable canonical-state script into the path consumed by the formal runner. */
async function stageInputPack(context) {
  const inputDirectory = path.join(context.outputRoot, 'locked-input-pack', 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  await fs.copyFile(
    context.stateScriptPath,
    path.join(inputDirectory, 'webgl_gpgpu_protoplanet_restart_gui_state.js'));
  return path.dirname(inputDirectory);
}

/** Validates the dual generated ABI and all Experimental UGLIR backend products. */
export async function validateGeneratedArtifacts(generatedRoot) {
  const records = [];
  const passNames = [
    'WebglGpgpuProtoplanetVelocityPass',
    'WebglGpgpuProtoplanetPositionPass',
    'WebglGpgpuProtoplanetMainPass'
  ];
  for (const pipeline of requiredPipelines) {
    const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    try {
      const [exportsSource, generatedSource] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8')
      ]);
      if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
        failures.push('ExportedRenderSet must remain empty for the single ordinary Points scene.');
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
      for (const direction of ['0To1', '1To0']) {
        const velocityLabel = `WebglGpgpuProtoplanetVelocity${direction}`;
        const positionLabel = `WebglGpgpuProtoplanetPosition${direction}`;
        const velocityOffset = generatedSource.indexOf(velocityLabel);
        const positionOffset = generatedSource.indexOf(positionLabel, velocityOffset + velocityLabel.length);
        if (velocityOffset < 0 || positionOffset < 0) {
          failures.push(`${direction} ping-pong must encode velocity before position in one submission.`);
        }
      }
      if (!generatedSource.includes('run(WebglGpgpuProtoplanetVertexCount, 1u, 0u, 0u)')) {
        failures.push('Main pass must remain one explicit non-instanced 24,576-vertex draw.');
      }
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({ pipeline, generatedDirectory, status: failures.length === 0 ? 'pass' : 'fail', failures });
  }
  return records;
}

/** Scans only the protoplanet host boundary for prohibited direct GPU work. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebglGpgpuProtoplanetRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebglGpgpuProtoplanetRuntimeAdapter.cpp')
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

/** Adds the two diagonal relations so every pair among four quadrants is checked. */
async function buildDiagonalComparisons(quadrants) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.scenarioId}|${entry.repetition}|${entry.pipeline}|${entry.backend}`,
    entry
  ]));
  const comparisons = [];
  for (const scenario of protoplanetScenarios) {
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

/** Requires all three semantic scenarios to produce distinct captures in every repeated quadrant. */
async function validateScenarioDistinction(quadrants) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    for (const backend of requiredBackends) {
      const repetitions = [...new Set(quadrants
        .filter((entry) => entry.pipeline === pipeline && entry.backend === backend)
        .map((entry) => entry.repetition))];
      for (const repetition of repetitions) {
        const runs = quadrants.filter((entry) => (
          entry.pipeline === pipeline && entry.backend === backend && entry.repetition === repetition));
        const identities = [];
        for (const run of runs) {
          if (run.status === 'pass') identities.push(await sha256File(run.artifacts.rgbaPath));
        }
        const failures = [];
        if (runs.length !== protoplanetScenarios.length || identities.length !== protoplanetScenarios.length) {
          failures.push('All three successful scenario captures are required.');
        } else if (new Set(identities).size !== protoplanetScenarios.length) {
          failures.push('Initial, fixed, and GUI restart scenarios must produce distinct RGBA captures.');
        }
        records.push({
          pipeline,
          backend,
          repetition,
          status: failures.length === 0 ? 'pass' : 'fail',
          rgbaSha256: identities,
          failures
        });
      }
    }
  }
  return records;
}

/** Validates the locked fragment-ping-pong and deterministic random state of every capture. */
async function validateRuntimeScenarios(quadrants) {
  const expectedById = new Map(protoplanetScenarios.map((scenario) => [scenario.id, scenario]));
  const records = [];
  for (const quadrant of quadrants) {
    const failures = [];
    try {
      const metadata = await loadJson(quadrant.artifacts.metadataPath);
      const expected = expectedById.get(quadrant.scenarioId);
      validateExpectedFields(metadata, {
        simulationStepCount: expected.simulationStepCount,
        simulationFragmentPassCount: expected.simulationFragmentPassCount,
        currentPingPongIndex: expected.currentPingPongIndex,
        randomState: expected.randomState,
        gravityConstant: expected.gravityConstant,
        density: expected.density,
        restartApplied: expected.restartApplied,
        initialPositionSha256: expected.initialPositionSha256,
        initialVelocitySha256: expected.initialVelocitySha256,
        currentPositionSha256: expected.currentPositionSha256,
        currentVelocitySha256: expected.currentVelocitySha256
      }, `${quadrant.scenarioId}.${quadrant.pipeline}.${quadrant.backend}.repeat-${quadrant.repetition}`, failures);
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

/** Executes the complete three-scenario, four-quadrant, three-repeat formal gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    stateScriptPath: requirePathOption(options, 'state-script'),
    repeatCount: parseRepeatCount(options.repeat),
    timeoutMs: parseTimeout(options['timeout-ms'])
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  const manifest = await loadJson(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json'));
  const statusLock = await loadJson(path.join(
    repositoryRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-phase1-status-lock.json'));
  const protoplanetReview = statusLock.phase1RequiredReviews?.find((review) => review.id === caseId);
  const inputValidation = await validateInputs(context, statusLock);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    validateGeneratedArtifacts(context.generatedRoot),
    validateGpuBoundary()
  ]);
  const assetRoot = await stageInputPack(context);
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
        name: 'macos-three-r185-webgl-gpgpu-protoplanet',
        buildDir: path.dirname(path.dirname(context.binaryRoot)),
        hosts: {
          legacy: path.join(context.binaryRoot, `${shardName}-legacy`),
          experimental: path.join(context.binaryRoot, `${shardName}-experimental`)
        }
      },
      manifest: createDiagnosticManifest(manifest, protoplanetReview),
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
  const [diagonalComparisons, scenarioComparisons, runtimeScenarioValidation] = await Promise.all([
    buildDiagonalComparisons(matrix.quadrants),
    validateScenarioDistinction(matrix.quadrants),
    validateRuntimeScenarios(matrix.quadrants)
  ]);
  const crossComparisons = [...matrix.crossComparisons, ...diagonalComparisons];
  const expectedRunCount = protoplanetScenarios.length * requiredPipelines.length
    * requiredBackends.length * context.repeatCount;
  const expectedCrossComparisonCount = protoplanetScenarios.length * context.repeatCount * 6;
  const expectedStabilityComparisonCount = protoplanetScenarios.length * requiredPipelines.length
    * requiredBackends.length * (context.repeatCount - 1);
  const expectedScenarioComparisonCount = requiredPipelines.length * requiredBackends.length
    * context.repeatCount;
  const status = matrix.status === 'pass'
    && matrix.quadrants.length === expectedRunCount
    && matrix.quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.length === expectedCrossComparisonCount
    && crossComparisons.every((entry) => entry.status === 'pass')
    && matrix.stabilityComparisons.length === expectedStabilityComparisonCount
    && matrix.stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioComparisons.length === expectedScenarioComparisonCount
    && scenarioComparisons.every((entry) => entry.status === 'pass')
    && runtimeScenarioValidation.length === expectedRunCount
    && runtimeScenarioValidation.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-gpgpu-protoplanet',
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
    scenarioComparisons,
    runtimeScenarioValidation,
    coverage: matrix.coverage
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_gpgpu_protoplanet formal report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
