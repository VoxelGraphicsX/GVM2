#!/usr/bin/env node

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  loadJson,
  requiredBackends,
  requiredPipelines,
  runThreeMatrix
} from '../../../tests/runners/three/node/runner.mjs';
import { comparisonThresholds } from '../../../tests/runners/three/node/image-comparison.mjs';
import { scanSampleCppSource } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgl_points_waves';
const shardName = 'WebglPointsWaves';
const randomSeed = 0x12345678;
const randomState = 2488893119;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = '7ec4a045d0f2034e593b10a9449b916e02e44f6996391c8496c8bfe60452e7f5';
const replaySha256 = '8fe5fa752c1f674042813b6d4e82266a1fd93ec1af207d751b8b4d1f7f1837b4';

export const pointsWavesScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    kind: 'initial-frame',
    frame: 0,
    inputReplay: null,
    canonicalState: '50x50-wave-grid-count-zero-single-sample',
    phase: 0,
    cameraUpdateCount: 1,
    cameraX: 0,
    cameraY: 0,
    virtualTimeMilliseconds: 0,
    nextFrameTimeMilliseconds: 1000 / 60
  }),
  Object.freeze({
    id: 'animated',
    kind: 'fixed-frame',
    frame: 60,
    inputReplay: null,
    canonicalState: '50x50-wave-grid-count-six-single-sample',
    phase: 6,
    cameraUpdateCount: 61,
    cameraX: 0,
    cameraY: 0,
    virtualTimeMilliseconds: 999.9999999999991,
    nextFrameTimeMilliseconds: 1016.6666666666657
  }),
  Object.freeze({
    id: 'camera-input',
    kind: 'input-replay',
    frame: 61,
    inputReplay: 'inputs/webgl_points_waves_camera.json',
    canonicalState: 'pointer-x-100-y-zero-camera-eased-62-frames-count-6.1',
    phase: 6.1,
    cameraUpdateCount: 62,
    cameraX: 95.84220064142759,
    cameraY: 0,
    virtualTimeMilliseconds: 1016.6666666666657,
    nextFrameTimeMilliseconds: 1033.3333333333323
  })
]);

const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: '28c559669feb07cc713df52b63e30c1d7707c45289c04e7ebfec45d38f3d5e2b',
    json: 'a4a715d0f5c1e58d7803b358f6affe8a14074dcd78ec940272a2f46c8973e7ce'
  }),
  animated: Object.freeze({
    rgba: '41bda73e2ea6b428a51ff7f1c6e21c98a6c8fba7b7fcef090a07b50aa24fa401',
    json: '03578425cd5230ecceb45762b46d69f6138088073b205bcc7ad367c831345906'
  }),
  'camera-input': Object.freeze({
    rgba: 'fb3a64a0fc8e4ea0147e5ce6effcd1229e3a4b75697563077cce15f3f516dfe8',
    json: '89c0678985f7dd6a6e81971d980953820b056abad42cc321cc0ff41c810f0e85'
  })
});

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
  if (!Number.isInteger(parsed) || parsed < 3) {
    throw new Error(`--repeat must be an integer of at least 3; received '${value}'.`);
  }
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
    if (Array.isArray(expectedValue)) {
      if (!Array.isArray(actualValue) || actualValue.length !== expectedValue.length) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
        continue;
      }
      for (let index = 0; index < expectedValue.length; index += 1) {
        const expectedElement = expectedValue[index];
        const actualElement = actualValue[index];
        if (expectedElement !== null && typeof expectedElement === 'object') {
          validateExpectedFields(actualElement, expectedElement, `${fieldLabel}[${index}]`, failures);
        } else if (actualElement !== expectedElement) {
          failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${JSON.stringify(expectedElement)}.`);
        }
      }
    } else if (expectedValue !== null && typeof expectedValue === 'object') {
      if (actualValue === null || typeof actualValue !== 'object' || Array.isArray(actualValue)) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected an object.`);
      } else {
        validateExpectedFields(actualValue, expectedValue, fieldLabel, failures);
      }
    } else if (typeof expectedValue === 'number' && !Number.isInteger(expectedValue)) {
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-6) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Verifies the ordinary one-object Scene and single-sample manifest contract. */
export function validateManifestContract(example) {
  const failures = [];
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_points_waves.html',
    status: 'phase1_required',
    capabilityAudit: {
      state: 'supported',
      gpuWorkDslOnly: true,
      requiresNewPublicCapability: false,
      missingCapabilities: []
    },
    renderSetPolicy: 'not-required',
    renderSetReasons: [],
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 0,
      renderSetType: null
    }],
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    scenePasses: [{
      name: 'main-sample',
      renderClass: 'WebglPointsWavesMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: shardName,
    scenarios: pointsWavesScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: scenario.inputReplay,
      canonicalState: scenario.canonicalState,
      renderableObjectCount: 1,
      scenePassInvocations: [{
        sceneRoot: 'scene',
        scenePass: 'main-sample',
        invocationCount: 1
      }]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const capabilities = new Set(example?.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'sample_private_dsl_shader_rewrite',
    'ordinary_indexed_triangle_rendering',
    'noninstanced_triangle_billboard_point_expansion',
    'single_sample_triangle_billboard_rendering',
    'deterministic_input_replay'
  ]) {
    if (!capabilities.has(capability)) {
      failures.push(`manifest.example capabilityAudit is missing '${capability}'.`);
    }
  }
  return failures;
}

/** Builds the runtime structural fields fixed for one canonical scenario. */
export function buildExpectedSnapshot(scenario) {
  return {
    schemaVersion: 1,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    upstreamRevision: 'r185',
    canonicalState: scenario.canonicalState,
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    logicalPointCount: 2500,
    expandedVertexCount: 7500,
    explicitIndexCount: 7500,
    expandedTriangleCount: 2500,
    scenePassCount: 1,
    logicalScenePassCount: 1,
    screenPassCount: 0,
    drawCommandCount: 1,
    logicalDrawCommandCount: 1,
    physicalCoverageDrawCount: 1,
    computePassCount: 0,
    standaloneGeometryBufferCount: 2,
    primitiveTopology: 'triangle-list',
    pointExpansion: 'oversized-triangle-three-indices-noninstanced',
    pointRadius: 0.475,
    wavePhase: scenario.phase,
    cameraUpdateCount: scenario.cameraUpdateCount,
    cameraPosition: [scenario.cameraX, scenario.cameraY, 1000],
    pointer: scenario.id === 'camera-input' ? [100, 0] : [0, 0],
    antialias: 'disabled-single-sample',
    samplePattern: [[0, 0]],
    screenPassSequence: [],
    gpuWorkDslOnly: true
  };
}

/** Creates a 588-entry diagnostic manifest containing only this formal case. */
export function createDiagnosticManifest(manifest, example) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error(`Manifest inventory must contain 588 examples; observed ${manifest?.examples?.length ?? 0}.`);
  }
  return {
    ...manifest,
    examples: manifest.examples.map((candidate) => candidate.id === caseId
      ? { ...example, status: 'phase1_required' }
      : { ...candidate, status: 'excluded_upstream' })
  };
}

/** Validates immutable source, replay, and Oracle identities. */
async function validateInputs(context) {
  const failures = [];
  let revision = '';
  try {
    revision = execFileSync('git', ['-C', context.upstreamRoot, 'rev-parse', 'HEAD'], {
      encoding: 'utf8'
    }).trim();
  } catch (error) {
    failures.push(`Could not read the pinned upstream revision: ${error.message}.`);
  }
  if (revision !== upstreamCommit) failures.push(`Upstream revision ${revision} differs from ${upstreamCommit}.`);
  try {
    const sourceIdentity = await sha256File(path.join(
      context.upstreamRoot,
      'examples',
      'webgl_points_waves.html'));
    if (sourceIdentity !== upstreamSourceSha256) failures.push('Pinned upstream source SHA-256 differs.');
    if (await sha256File(context.inputReplayPath) !== replaySha256) failures.push('Pointer replay SHA-256 differs.');
  } catch (error) {
    failures.push(error instanceof Error ? error.message : String(error));
  }
  const oracles = [];
  for (const scenario of pointsWavesScenarios) {
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
        schemaVersion: 1,
        source: 'three-r185-reference',
        upstreamCommit,
        caseId,
        scenarioId: scenario.id,
        frame: scenario.frame,
        randomSeed,
        randomState,
        virtualTimeMs: scenario.virtualTimeMilliseconds,
        nextFrameTimeMs: scenario.nextFrameTimeMilliseconds,
        width: 800,
        height: 500,
        rowStrideBytes: 3200,
        byteCount: 1600000,
        format: 'rgba8unorm'
      }, `oracle.${scenario.id}`, failures);
      if (scenario.inputReplay == null && metadata.inputReplay != null) {
        failures.push(`${scenario.id} Oracle unexpectedly declares inputReplay.`);
      }
      if (scenario.inputReplay != null) {
        validateExpectedFields(metadata.inputReplay, {
          caseId,
          scenarioId: scenario.id,
          captureFrame: scenario.frame,
          sha256: replaySha256,
          eventCount: 1,
          target: 'body > div:nth-of-type(2) > canvas'
        }, `oracle.${scenario.id}.inputReplay`, failures);
      }
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
    oracles,
    failures
  };
}

/** Copies the immutable replay into the relative path consumed by the formal runner. */
async function stageInputPack(context) {
  const inputDirectory = path.join(context.outputRoot, 'locked-input-pack', 'inputs');
  await fs.mkdir(inputDirectory, { recursive: true });
  await fs.copyFile(
    context.inputReplayPath,
    path.join(inputDirectory, 'webgl_points_waves_camera.json'));
  return path.dirname(inputDirectory);
}

/** Validates one generated pipeline's ordinary geometry and four-pass ABI. */
export async function validateGeneratedArtifacts(generatedRoot) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    try {
      const [exportsSource, generatedSource, singleHeader] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'dsl_single_header.hpp'), 'utf8')
      ]);
      if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
        failures.push('ExportedRenderSet must remain empty for the ordinary one-object Scene.');
      }
      for (const token of [
        'class WebglPointsWavesMainPass',
        'vertexState.buffers[0].arrayStride = 32',
        'offsetof(WebglPointsWavesVertex, positionAndGridX)',
        'offsetof(WebglPointsWavesVertex, cornerAndGridY)',
        'createBindGroup<WebglPointsWavesResolveResources>',
        'run(WebglPointsWavesIndexCount, 1u, 0u, 0u)',
        'run(3u, 1u, 0u, 0u)'
      ]) {
        if (!generatedSource.includes(token)) failures.push(`Generated host is missing '${token}'.`);
      }
      for (let sample = 0; sample < 4; sample += 1) {
        for (const token of [
          `uniformBuffer${sample}`,
          `WebglPointsWavesSceneSample${sample}`,
          `mainPass${sample}->setVertexBuffer(vertexBuffer)`,
          `mainPass${sample}->setIndexBuffer(indexBuffer)`
        ]) {
          if (!generatedSource.includes(token)) failures.push(`Generated sample ABI is missing '${token}'.`);
        }
      }
      if (!singleHeader.includes('WebglPointsWavesLogicalPointCount =')
        || !singleHeader.includes('WebglPointsWavesLogicalPointCount * 3u')) {
        failures.push('Merged DSL header is missing the 2,500-point oversized-triangle count contract.');
      }
      if (pipeline === 'experimental') {
        for (const passName of ['WebglPointsWavesMainPass']) {
          for (const stage of ['vertex', 'fragment']) {
            for (const relativePath of [
              path.join('uglir', `${passName}__${stage}.uglir.json`),
              path.join('uglir', `${passName}__${stage}.uglir.txt`),
              path.join('msl', `${passName}__${stage}.msl`),
              path.join('spv', `${passName}__${stage}.raw.spv.txt`)
            ]) {
              try {
                const product = await fs.readFile(path.join(generatedDirectory, relativePath));
                if (product.byteLength === 0) failures.push(`Experimental product ${relativePath} is empty.`);
              } catch {
                failures.push(`Missing Experimental product ${relativePath}.`);
              }
            }
          }
        }
      }
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({
      pipeline,
      generatedDirectory,
      status: failures.length === 0 ? 'pass' : 'fail',
      failures
    });
  }
  return records;
}

/** Scans only the wave-points host boundary for prohibited direct GPU work. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebglPointsWavesRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebglPointsWavesRuntimeAdapter.cpp')
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

/** Requires all three semantic scenarios to produce distinct captures per repetition. */
async function validateScenarioDistinction(quadrants) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    for (const backend of requiredBackends) {
      const repetitions = [...new Set(quadrants
        .filter((entry) => entry.pipeline === pipeline && entry.backend === backend)
        .map((entry) => entry.repetition))];
      for (const repetition of repetitions) {
        const runs = quadrants.filter((entry) => entry.pipeline === pipeline
          && entry.backend === backend
          && entry.repetition === repetition);
        const identities = [];
        for (const run of runs) {
          if (run.status === 'pass') identities.push(await sha256File(run.artifacts.rgbaPath));
        }
        const failures = [];
        if (runs.length !== pointsWavesScenarios.length || identities.length !== pointsWavesScenarios.length) {
          failures.push('All three successful semantic scenario captures are required.');
        } else if (new Set(identities).size !== pointsWavesScenarios.length) {
          failures.push('Initial, animated, and camera-input captures must be distinct.');
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
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  const manifestFailures = validateManifestContract(example);
  const [inputValidation, generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    validateInputs(context),
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
  if (manifestFailures.length === 0
      && inputValidation.status === 'pass'
      && generatedArtifacts.every((entry) => entry.status === 'pass')
      && gpuBoundaryLint.status === 'pass') {
    matrix = await runThreeMatrix({
      sourceDir: repositoryRoot,
      runDir: context.outputRoot,
      profile: {
        name: 'macos-three-r185-webgl-points-waves',
        buildDir: path.dirname(path.dirname(context.binaryRoot)),
        hosts: {
          legacy: path.join(context.binaryRoot, `${shardName}-legacy`),
          experimental: path.join(context.binaryRoot, `${shardName}-experimental`)
        }
      },
      manifest: createDiagnosticManifest(manifest, example),
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
  const scenarioComparisons = await validateScenarioDistinction(matrix.quadrants);
  const expectedRunCount = pointsWavesScenarios.length * requiredPipelines.length
    * requiredBackends.length * context.repeatCount;
  const expectedCrossComparisonCount = pointsWavesScenarios.length * 4 * context.repeatCount;
  const expectedStabilityComparisonCount = pointsWavesScenarios.length
    * requiredPipelines.length * requiredBackends.length * (context.repeatCount - 1);
  const status = matrix.status === 'pass'
    && matrix.quadrants.length === expectedRunCount
    && matrix.quadrants.every((entry) => entry.status === 'pass')
    && matrix.crossComparisons.length === expectedCrossComparisonCount
    && matrix.crossComparisons.every((entry) => entry.status === 'pass')
    && matrix.stabilityComparisons.length === expectedStabilityComparisonCount
    && matrix.stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioComparisons.length === requiredPipelines.length * requiredBackends.length * context.repeatCount
    && scenarioComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const authoritativeInventory = Object.fromEntries([
    'excluded_upstream',
    'deferred_missing_capability',
    'phase1_required',
    'audit_pending'
  ].map((inventoryStatus) => [
    inventoryStatus,
    manifest.examples.filter((candidate) => candidate.status === inventoryStatus).length
  ]));
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-points-waves',
    caseId,
    status,
    comparisonThresholds,
    repeatCount: context.repeatCount,
    runCount: matrix.quadrants.length,
    expectedRunCount,
    expectedCrossComparisonCount,
    expectedStabilityComparisonCount,
    authoritativeInventory,
    manifestValidation: {
      status: manifestFailures.length === 0 ? 'pass' : 'fail',
      failures: manifestFailures
    },
    inputValidation,
    generatedArtifacts,
    gpuBoundaryLint,
    selectedCases: matrix.selectedCases,
    quadrants: matrix.quadrants,
    crossComparisons: matrix.crossComparisons,
    stabilityComparisons: matrix.stabilityComparisons,
    scenarioComparisons,
    coverage: matrix.coverage
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_points_waves formal report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
