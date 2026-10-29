#!/usr/bin/env node

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
    restartApplied: false
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
    restartApplied: false
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
    restartApplied: true
  })
]);


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

/** Validates the pinned revision, Oracle metadata, and image payload sizes. */
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

  const oracles = [];
  for (const scenario of protoplanetScenarios) {
    const basePath = path.join(context.oracleRoot, caseId, scenario.id);
    const rgbaPath = `${basePath}.rgba`;
    const metadataPath = `${basePath}.json`;
    try {
      const [metadata, rgbaStat] = await Promise.all([loadJson(metadataPath), fs.stat(rgbaPath)]);
      if (rgbaStat.size !== metadata.width * metadata.height * 4) {
        failures.push(`${scenario.id} Oracle RGBA size differs from its declared extent.`);
      }
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
      oracles.push({ scenarioId: scenario.id, rgbaPath, metadataPath });
    } catch (error) {
      failures.push(`${scenario.id} Oracle: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    upstreamCommit,

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
export async function validateScenarioDistinction(quadrants) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    for (const backend of requiredBackends) {
      const repetitions = [...new Set(quadrants
        .filter((entry) => entry.pipeline === pipeline && entry.backend === backend)
        .map((entry) => entry.repetition))];
      for (const repetition of repetitions) {
        const runs = quadrants.filter((entry) => (
          entry.pipeline === pipeline && entry.backend === backend && entry.repetition === repetition));
        const successfulRuns = runs.filter((run) => run.status === 'pass');
        const failures = [];
        if (runs.length !== protoplanetScenarios.length || successfulRuns.length !== protoplanetScenarios.length) {
          failures.push('All three successful scenario captures are required.');
        } else {
          const captures = await Promise.all(successfulRuns.map((run) => fs.readFile(run.artifacts.rgbaPath)));
          if (captures.some((capture, index) => captures.slice(0, index).some((previous) => capture.equals(previous)))) {
            failures.push('Initial, fixed, and GUI restart scenarios must produce distinct RGBA captures.');
          }
        }
        records.push({
          pipeline,
          backend,
          repetition,
          status: failures.length === 0 ? 'pass' : 'fail',

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
        restartApplied: expected.restartApplied
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
