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
const caseId = 'webgl_gpgpu_birds';
const shardName = 'WebglGpgpuBirds';
const randomSeed = 0x12345678;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = 'd34ee0fbf6a43dea28f0bc0b1f5eb286013356b9d1648fd0492926e6c8779409';
const replaySha256 = 'ddf07fda08630675f161e4a2137d9c60b1af0f221441d11debc799da94822c52';
const stateScriptSha256 = '12f0b5a1e175fbd78fc3f74ff14370b7006a2c60b6d87bd43a52e4d68ff059d6';

export const birdsScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-seeded-flock',
    kind: 'initial-frame',
    frame: 0,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-width-32-initialized-frame0-delta0-one-simulation-step-two-fragment-render-passes-pingpong-index-1-predator-zero-then-sentinel',
    simulationStepCount: 1,
    simulationRenderPassCount: 2,
    currentPingPongIndex: 1,
    separation: 20,
    alignment: 20,
    cohesion: 20
  }),
  Object.freeze({
    id: 'fixed-flock-step',
    kind: 'fixed-frame',
    frame: 1,
    inputReplay: null,
    canonicalState: 'seed-0x12345678-frame0-delta0-plus-frame1-fixed-1-over-60-two-simulation-steps-four-fragment-render-passes-default-20-20-20-pingpong-index-0',
    simulationStepCount: 2,
    simulationRenderPassCount: 4,
    currentPingPongIndex: 0,
    separation: 20,
    alignment: 20,
    cohesion: 20
  }),
  Object.freeze({
    id: 'pointer-and-gui',
    kind: 'input-replay',
    frame: 1,
    inputReplay: 'inputs/webgl_gpgpu_birds_pointer_gui.json',
    canonicalState: 'seed-0x12345678-gui-preframe-28-17-31-pointer-frame1-x200-y250-predator-minus0.25-0-two-simulation-steps-four-fragment-render-passes-pingpong-index-0',
    simulationStepCount: 2,
    simulationRenderPassCount: 4,
    currentPingPongIndex: 0,
    separation: 28,
    alignment: 17,
    cohesion: 31
  })
]);

const lockedOracleSha256 = Object.freeze({
  'initial-seeded-flock': Object.freeze({
    rgba: 'cdacb9893f402a3149b30932d2b04151743bde5a0fecf0aa27dc400be83c7e91',
    json: '6c3af5dbdfa4433875e6dfbe5269230ff1dee11c6fb50abd4304f676e4eea453'
  }),
  'fixed-flock-step': Object.freeze({
    rgba: '86369f7bd81a0f843bf61cdc0070e3423ee1610a7e210c3bd46d3c78811dbc6c',
    json: 'a851564abf4ff53bd992e82e0ec309cdcf490bd04506d63e0bacd191c40e039f'
  }),
  'pointer-and-gui': Object.freeze({
    rgba: '2c37ffae21a6f7b17168e0151643abb1fe67522edf4bb8910c75dd9f26b5b719',
    json: '69903a1321664d5e7b6fd928bcfbfa5cc4b70ff45929e3d629bd38c08e49f492'
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
  if (value == null) return 30_000;
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

/** Builds the runtime scenario while preserving the locked semantic contract. */
function makeRuntimeScenario(scenario) {
  return {
    id: scenario.id,
    kind: scenario.kind,
    frame: scenario.frame,
    inputReplay: scenario.inputReplay,
    canonicalState: scenario.id === 'pointer-and-gui'
      ? 'inputs/webgl_gpgpu_birds_pointer_gui_state.js'
      : null,
    scenePassInvocations: [{
      sceneRoot: 'scene',
      scenePass: 'main-double-sided-flock',
      invocationCount: 1
    }],
    scenePassSequence: [{ sceneRoot: 'scene', scenePass: 'main-double-sided-flock' }]
  };
}

/** Verifies that the status lock contains one complete authoritative birds review. */
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
      name: 'main-double-sided-flock',
      renderClass: 'WebglGpgpuBirdsMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }]
  }, `statusLock.${caseId}`, failures);
  const lockedScenarios = new Map((review?.scenarios ?? []).map((scenario) => [scenario.id, scenario]));
  if (lockedScenarios.size !== birdsScenarios.length) {
    failures.push(`statusLock.${caseId}.scenarios has ${lockedScenarios.size} entries; expected ${birdsScenarios.length}.`);
  }
  for (const scenario of birdsScenarios) {
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

/** Creates a 588-entry diagnostic manifest whose birds contract comes from the status lock. */
export function createDiagnosticManifest(manifest, birdsReview) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error(`Manifest inventory must contain 588 examples; observed ${manifest?.examples?.length ?? 0}.`);
  }
  const examples = manifest.examples.map((example) => {
    if (example.id !== caseId) return { ...example, status: 'excluded_upstream' };
    return {
      ...example,
      ...birdsReview,
      status: 'phase1_required',
      scenarios: birdsScenarios.map(makeRuntimeScenario)
    };
  });
  if (!examples.some((example) => example.id === caseId)) {
    throw new Error(`Manifest inventory does not contain ${caseId}.`);
  }
  return { ...manifest, examples };
}

/** Validates immutable source, replay, state script, and Oracle identities. */
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
  const sourcePath = path.join(context.upstreamRoot, 'examples', 'webgl_gpgpu_birds.html');
  try {
    if (await sha256File(sourcePath) !== upstreamSourceSha256) failures.push('Pinned upstream source SHA-256 differs.');
    if (await sha256File(context.inputReplayPath) !== replaySha256) failures.push('Pointer replay SHA-256 differs.');
    if (await sha256File(context.canonicalStatePath) !== stateScriptSha256) failures.push('GUI state script SHA-256 differs.');
  } catch (error) {
    failures.push(error instanceof Error ? error.message : String(error));
  }
  const oracles = [];
  for (const scenario of birdsScenarios) {
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
        width: 800,
        height: 500
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
    replaySha256,
    stateScriptSha256,
    oracles,
    failures
  };
}

/** Copies immutable input artifacts into the relative paths consumed by the formal runner. */
async function stageInputPack(context) {
  const inputDirectory = path.join(context.outputRoot, 'locked-input-pack', 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  await Promise.all([
    fs.copyFile(context.inputReplayPath, path.join(inputDirectory, 'webgl_gpgpu_birds_pointer_gui.json')),
    fs.copyFile(context.canonicalStatePath, path.join(inputDirectory, 'webgl_gpgpu_birds_pointer_gui_state.js'))
  ]);
  return path.dirname(inputDirectory);
}

/** Validates the dual generated ABI and all Experimental UGLIR backend products. */
export async function validateGeneratedArtifacts(generatedRoot) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    try {
      const [exportsSource, generatedSource] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8')
      ]);
      if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
        failures.push('ExportedRenderSet must remain empty for the single ordinary Mesh scene.');
      }
      for (const passName of [
        'WebglGpgpuBirdsVelocityPass',
        'WebglGpgpuBirdsPositionPass',
        'WebglGpgpuBirdsMainPass'
      ]) {
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
        const velocityOffset = generatedSource.indexOf(`WebglGpgpuBirdsVelocity${direction}`);
        const positionOffset = generatedSource.indexOf(`WebglGpgpuBirdsPosition${direction}`);
        if (velocityOffset < 0 || positionOffset <= velocityOffset) {
          failures.push(`${direction} ping-pong must encode velocity before position in one submission.`);
        }
      }
      if (!generatedSource.includes('run(WebglGpgpuBirdsVertexCount, 1u, 0u, 0u)')) {
        failures.push('Main pass must remain one explicit non-instanced 9,216-vertex draw.');
      }
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({ pipeline, generatedDirectory, status: failures.length === 0 ? 'pass' : 'fail', failures });
  }
  return records;
}

/** Scans only the birds host boundary for prohibited direct GPU work. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebglGpgpuBirdsRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebglGpgpuBirdsRuntimeAdapter.cpp')
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
  for (const scenario of birdsScenarios) {
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
        if (runs.length !== birdsScenarios.length || identities.length !== birdsScenarios.length) {
          failures.push('All three successful scenario captures are required.');
        } else if (new Set(identities).size !== birdsScenarios.length) {
          failures.push('Initial, fixed, and pointer/GUI scenarios must produce distinct RGBA captures.');
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

/** Executes the complete three-scenario, four-quadrant, three-repeat formal gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    inputReplayPath: requirePathOption(options, 'input-replay'),
    canonicalStatePath: requirePathOption(options, 'canonical-state'),
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
  const birdsReview = statusLock.phase1RequiredReviews?.find((review) => review.id === caseId);
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
        name: 'macos-three-r185-webgl-gpgpu-birds',
        buildDir: path.dirname(path.dirname(context.binaryRoot)),
        hosts: {
          legacy: path.join(context.binaryRoot, `${shardName}-legacy`),
          experimental: path.join(context.binaryRoot, `${shardName}-experimental`)
        }
      },
      manifest: createDiagnosticManifest(manifest, birdsReview),
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
  const diagonalComparisons = await buildDiagonalComparisons(matrix.quadrants);
  const scenarioComparisons = await validateScenarioDistinction(matrix.quadrants);
  const crossComparisons = [...matrix.crossComparisons, ...diagonalComparisons];
  const expectedRunCount = birdsScenarios.length * requiredPipelines.length
    * requiredBackends.length * context.repeatCount;
  const expectedCrossComparisonCount = birdsScenarios.length * context.repeatCount * 6;
  const expectedStabilityComparisonCount = birdsScenarios.length * requiredPipelines.length
    * requiredBackends.length * (context.repeatCount - 1);
  const status = matrix.status === 'pass'
    && matrix.quadrants.length === expectedRunCount
    && matrix.quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.length === expectedCrossComparisonCount
    && crossComparisons.every((entry) => entry.status === 'pass')
    && matrix.stabilityComparisons.length === expectedStabilityComparisonCount
    && matrix.stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-gpgpu-birds',
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
    coverage: matrix.coverage
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_gpgpu_birds formal report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
