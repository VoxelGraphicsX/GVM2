#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import {
  validateDeterministicCaptureMetadata,
  validateRenderSetSnapshot,
  validateStructuralSnapshot
} from '../../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const stabilityRunCount = 3;
const lockedRandomSeed = 305419896;
const shardName = 'WebglInstancingRaycast';
const caseId = 'webgl_instancing_raycast';
const renderSetType = 'WebglInstancingRaycastSceneRenderSet';
const scenePassName = 'WebglInstancingRaycastMainPass';
const hitReplaySha256 = '8c5ebf0a5c4eb4b2dc08dfea7af73321aba540ac7e520f192e5e1ed80c355370';
const reducedReplaySha256 = '9e631bfee387b057ed1d7a46cc61a37a440b3eaeed71c47c9c6387c5ed5b90f5';
const componentSchema = Object.freeze([
  Object.freeze({ name: 'vertices', kind: 'buffer', role: 'vertex' }),
  Object.freeze({ name: 'indices', kind: 'buffer', role: 'index' }),
  Object.freeze({ name: 'objects', kind: 'buffer', role: 'object' }),
  Object.freeze({ name: 'instances', kind: 'buffer', role: 'instance' }),
  Object.freeze({ name: 'materials', kind: 'buffer', role: 'material' })
]);
const scenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    instanceCount: 1000,
    replayKind: null,
    cameraPosition: Object.freeze([10, 10, 10]),
    hitIds: Object.freeze([]),
    hitFrames: Object.freeze([]),
    activeCountMutation: 'none',
    activeEntityId: 0,
    entityReallocated: false
  }),
  Object.freeze({
    id: 'ray-hit',
    frame: 60,
    instanceCount: 1000,
    replayKind: 'hit',
    cameraPosition: Object.freeze([
      9.081609434973593,
      9.130756853442595,
      11.582471642698279
    ]),
    hitIds: Object.freeze([0, 110]),
    hitFrames: Object.freeze([0, 13]),
    activeCountMutation: 'none',
    activeEntityId: 0,
    entityReallocated: false
  }),
  Object.freeze({
    id: 'reduced-count',
    frame: 1,
    instanceCount: 125,
    replayKind: 'reduced',
    cameraPosition: Object.freeze([10, 10, 10]),
    hitIds: Object.freeze([]),
    hitFrames: Object.freeze([]),
    activeCountMutation: 'remove-reallocate-same-render-set',
    activeEntityId: 1,
    entityReallocated: true
  })
]);

/** Parses strict value-bearing long options for this focused fixture. */
function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    options[option.slice(2)] = value;
  }
  return options;
}

/** Resolves one required explicit path option. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one optional positive integer watchdog setting. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) return fallback;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Returns true when one explicit artifact path exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Returns one file's lowercase SHA-256 digest. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

/** Compares expected JSON fields recursively while permitting diagnostics extensions. */
function validateExpectedFields(actual, expected, label, failures) {
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
        if (expectedElement !== null && typeof expectedElement === 'object' && !Array.isArray(expectedElement)) {
          if (actualElement === null || typeof actualElement !== 'object' || Array.isArray(actualElement)) {
            failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected an object.`);
          } else {
            validateExpectedFields(actualElement, expectedElement, `${fieldLabel}[${index}]`, failures);
          }
        } else if (typeof expectedElement === 'number' && !Number.isInteger(expectedElement)) {
          if (typeof actualElement !== 'number' || Math.abs(actualElement - expectedElement) > 1e-9) {
            failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${expectedElement}.`);
          }
        } else if (expectedElement !== actualElement) {
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
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-9) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Validates the formal manifest, replays, host binaries, and real r185 Oracles. */
async function validateFixtureInputs(context) {
  const failures = [];
  const manifestPath = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  if (!example || example.status !== 'phase1_required' ||
      example.renderSetPolicy !== 'required' || example.renderSetType !== renderSetType ||
      example.containsInstancing !== true) {
    failures.push('Formal manifest identity differs from the phase1-required instancing RenderSet contract.');
  }
  if (example && JSON.stringify(example.componentSchema) !== JSON.stringify(componentSchema)) {
    failures.push('Formal manifest componentSchema differs from the five-component fixture ABI.');
  }
  const expectedScenePasses = [{
    name: 'main-lit',
    renderClass: scenePassName,
    sceneRoot: 'scene',
    renderSetBindingCount: 1,
    usesStandaloneGeometry: false,
    usesExplicitDrawCount: false
  }];
  if (example && JSON.stringify(example.scenePasses) !== JSON.stringify(expectedScenePasses)) {
    failures.push('Formal manifest must expose exactly the unique main-lit Scene RenderSet pass.');
  }
  if (example && JSON.stringify(example.screenPasses) !== JSON.stringify([])) {
    failures.push('Formal manifest screenPasses must be empty for direct single-sample rendering.');
  }
  for (const scenario of scenarios) {
    const manifestScenario = example?.scenarios?.find((candidate) => candidate.id === scenario.id);
    if (!manifestScenario || manifestScenario.frame !== scenario.frame) {
      failures.push(`${caseId}/${scenario.id}: scenario identity differs from the formal manifest.`);
    }
    for (const extension of ['rgba', 'json']) {
      const oraclePath = path.join(context.oracleRoot, caseId, `${scenario.id}.${extension}`);
      if (!await pathExists(oraclePath)) failures.push(`Missing real r185 Oracle artifact: ${oraclePath}.`);
    }
  }

  const replayExpectations = [
    [context.hitReplayPath, hitReplaySha256, 'ray-hit', 60, 3],
    [context.reducedReplayPath, reducedReplaySha256, 'reduced-count', 1, 2]
  ];
  const replayIdentities = {};
  for (const [replayPath, expectedSha, scenarioId, frame, eventCount] of replayExpectations) {
    if (!await pathExists(replayPath)) {
      failures.push(`Missing canonical replay: ${replayPath}.`);
      continue;
    }
    const replay = JSON.parse(await fs.readFile(replayPath, 'utf8'));
    const identity = {
      sha256: await sha256File(replayPath),
      caseId: replay.caseId,
      scenarioId: replay.scenarioId,
      frame: replay.frame,
      target: replay.target,
      eventCount: Array.isArray(replay.events) ? replay.events.length : null
    };
    replayIdentities[scenarioId] = identity;
    validateExpectedFields(identity, {
      sha256: expectedSha,
      caseId,
      scenarioId,
      frame,
      target: 'body > canvas',
      eventCount
    }, `replay.${scenarioId}`, failures);
  }
  for (const pipeline of pipelines) {
    const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
    if (!await pathExists(executable)) failures.push(`Missing ${pipeline} host binary: ${executable}.`);
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    manifestPath,
    example,
    replayIdentities,
    failures
  };
}

/** Reads one required generated text artifact or records a stable failure. */
async function readGeneratedText(targetPath, failures, label) {
  if (!await pathExists(targetPath)) {
    failures.push(`${label}: missing ${targetPath}.`);
    return '';
  }
  const source = await fs.readFile(targetPath, 'utf8');
  if (source.length === 0) failures.push(`${label}: generated artifact is empty.`);
  return source;
}

/** Lints one generated Legacy or Experimental RenderSet and shader artifact ABI. */
async function lintGeneratedPipeline(context, pipeline) {
  const generatedDirectory = path.join(context.generatedRoot, pipeline, shardName, 'UGLBin');
  const label = `${shardName}/${pipeline}`;
  const failures = [];
  const exportsSource = await readGeneratedText(path.join(generatedDirectory, 'exports.hpp'), failures, label);
  const generatedSource = await readGeneratedText(path.join(generatedDirectory, 'generate_result.hpp'), failures, label);
  const singleHeader = await readGeneratedText(path.join(generatedDirectory, 'dsl_single_header.hpp'), failures, label);

  if (exportsSource && !/namespace\s+ExportedRenderSet\s*\{[\s\S]*sceneSet\s*=\s*\d+/u.test(exportsSource)) {
    failures.push(`${label}: exports omit the unique Scene RenderSet handle.`);
  }
  if (exportsSource && !exportsSource.includes(`namespace ${renderSetType}Components`)) {
    failures.push(`${label}: exports omit ${renderSetType} component handles.`);
  }
  const componentHandles = {};
  for (const component of componentSchema) {
    const match = new RegExp(`RenderComponentHandle\\s+${component.name}\\s*=\\s*(\\d+)`, 'u').exec(exportsSource);
    if (!match) failures.push(`${label}: exports omit component '${component.name}'.`);
    else componentHandles[component.name] = Number(match[1]);
    if (generatedSource && !generatedSource.includes(`.componentName = "${component.name}"`)) {
      failures.push(`${label}: generated RenderSet layout omits '${component.name}'.`);
    }
  }
  if (!generatedSource.includes(`createRenderSet<${renderSetType}>()`)) {
    failures.push(`${label}: generated host does not create the unique Scene RenderSet.`);
  }
  for (const className of [scenePassName]) {
    if (!generatedSource.includes(`class ${className}`)) {
      failures.push(`${label}: generated host omits ${className}.`);
    }
  }
  if (!generatedSource.includes('this->mRenderSet = sceneSet') ||
      !generatedSource.includes('this->mRenderSetBindGroupIndex = 0')) {
    failures.push(`${label}: Scene pass does not bind exactly the unique Scene RenderSet.`);
  }
  for (const componentName of ['objects', 'instances', 'materials']) {
    const legacyRead = generatedSource.includes(`${componentName}_UGLGetSafe`);
    const experimentalRead = generatedSource.includes(`sceneSet->${componentName}[`) &&
      generatedSource.includes(`${componentName}IndexTable`);
    if (!legacyRead && !experimentalRead) {
      failures.push(`${label}: shader does not lower ${componentName} BufferComponent::get.`);
    }
  }
  if (!generatedSource.includes('renderEntityID') || !generatedSource.includes('renderEntityInstanceID')) {
    failures.push(`${label}: generated vertex shader omits one RenderEntity builtin.`);
  }
  const sceneDraws = generatedSource.match(/scenePass->run\([^)]*\)/gu) ?? [];
  if (sceneDraws.length !== 1 || sceneDraws[0] !== 'scenePass->run()') {
    failures.push(`${label}: Scene geometry must issue exactly one parameterless RenderSet draw.`);
  }
  const hasEntityMetadata = pipeline === 'legacy'
    ? generatedSource.includes('RenderEntityCMDParams') && generatedSource.includes('UGLLoadRenderEntityCMDParamsSafe')
    : generatedSource.includes('CommandParams') && generatedSource.includes('DrawInfo') &&
      generatedSource.includes('__uglc_draw_command_params');
  if (!hasEntityMetadata) failures.push(`${label}: generated indexed-indirect entity metadata ABI is missing.`);
  if (pipeline === 'experimental' &&
      (!generatedSource.includes('vertexShaderArtifact_SpirvWords') ||
       !generatedSource.includes('fragmentShaderArtifact_SpirvWords') ||
       !generatedSource.includes('UGLC::Generated::MakeShaderArtifact') ||
       !generatedSource.includes('vertex WebglInstancingRaycastVertexOutput vertexMain'))) {
    failures.push(`${label}: Experimental output omits direct-SPIR-V or MSL shader artifacts.`);
  }
  if (!singleHeader.includes('uint renderEntityID [[RenderEntityID]]') ||
      !singleHeader.includes('uint renderEntityInstanceID [[RenderEntityInstanceID]]') ||
      !singleHeader.includes('scenePass()')) {
    failures.push(`${label}: generated single-header source lost the frozen RenderSet-only DSL contract.`);
  }
  return {
    pipeline,
    status: failures.length === 0 ? 'pass' : 'fail',
    generatedDirectory,
    componentHandles,
    failures
  };
}

/** Lints both generated pipelines and checks their component handle layouts for identity. */
async function lintGeneratedArtifacts(context) {
  const results = [];
  for (const pipeline of pipelines) results.push(await lintGeneratedPipeline(context, pipeline));
  const failures = results.flatMap((result) => result.failures);
  if (results.length === 2 &&
      JSON.stringify(results[0].componentHandles) !== JSON.stringify(results[1].componentHandles)) {
    failures.push('Legacy and Experimental component handle layouts differ.');
  }
  return { status: failures.length === 0 ? 'pass' : 'fail', pipelines: results, failures };
}

/** Creates deterministic artifact paths for one quadrant repetition. */
function makeArtifactPaths(context, scenario, pipeline, backend, repetitionIndex) {
  const artifactDirectory = path.join(
    context.outputRoot,
    'artifacts',
    scenario.id,
    pipeline,
    backend,
    `run-${repetitionIndex + 1}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    stdoutPath: path.join(artifactDirectory, 'stdout.log'),
    stderrPath: path.join(artifactDirectory, 'stderr.log')
  };
}

/** Builds explicit host arguments without environment-based runtime configuration. */
function buildHostArguments(context, scenario, pipeline, backend, artifacts) {
  const argumentsList = [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(lockedRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
  if (scenario.replayKind === 'hit') {
    argumentsList.push('--input-replay', context.hitReplayPath);
  } else if (scenario.replayKind === 'reduced') {
    argumentsList.push('--input-replay', context.reducedReplayPath);
  }
  return argumentsList;
}

/** Runs one host process under a hard watchdog and captures exact diagnostics. */
async function runHostProcess(executable, argumentsList, timeoutMs) {
  const startedAt = Date.now();
  return new Promise((resolve) => {
    const child = spawn(executable, argumentsList, {
      cwd: repositoryRoot,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    const stdout = [];
    const stderr = [];
    let timedOut = false;
    child.stdout.on('data', (chunk) => stdout.push(chunk));
    child.stderr.on('data', (chunk) => stderr.push(chunk));
    const watchdog = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.once('error', (error) => {
      clearTimeout(watchdog);
      resolve({
        exitCode: null,
        signal: null,
        timedOut,
        durationMs: Date.now() - startedAt,
        stdout: Buffer.concat(stdout).toString('utf8'),
        stderr: `${Buffer.concat(stderr).toString('utf8')}\n${error.message}`
      });
    });
    child.once('close', (exitCode, signal) => {
      clearTimeout(watchdog);
      resolve({
        exitCode,
        signal,
        timedOut,
        durationMs: Date.now() - startedAt,
        stdout: Buffer.concat(stdout).toString('utf8'),
        stderr: Buffer.concat(stderr).toString('utf8')
      });
    });
  });
}

/** Validates one capture's metadata, structure, RenderSet topology, and image diversity. */
async function validateRunArtifacts(context, scenario, pipeline, backend, artifacts) {
  const failures = [];
  const [image, snapshot] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8').then(JSON.parse)
  ]);
  const replayExpectation = scenario.replayKind === 'hit'
    ? {
        schemaVersion: 1,
        sha256: hitReplaySha256,
        caseId,
        scenarioId: 'ray-hit',
        captureFrame: 60,
        eventCount: 3,
        lastEventFrame: 0,
        target: 'body > canvas'
      }
    : scenario.replayKind === 'reduced'
      ? {
          schemaVersion: 1,
          sha256: reducedReplaySha256,
          caseId,
          scenarioId: 'reduced-count',
          captureFrame: 1,
          eventCount: 2,
          lastEventFrame: 0,
          target: 'body > canvas'
        }
      : null;
  validateExpectedFields(image.metadata, {
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    frame: scenario.frame,
    randomSeed: lockedRandomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: replayExpectation
  }, 'metadata', failures);
  validateExpectedFields(snapshot, {
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    renderSetPolicy: 'required',
    sceneRenderSetCount: 1,
    renderableObjectCount: 1,
    entityCount: 1,
    instanceCount: scenario.instanceCount,
    containsInstancing: true,
    containsHierarchy: false,
    materialCount: 1,
    scenePassCount: 1,
    screenPassCount: 0,
    screenPasses: [],
    scenePassSequence: [{ sceneRoot: 'scene', scenePass: 'main-lit', entityOrdinal: 0 }],
    drawCommandCount: 1,
    directDrawFallback: false,
    antialiasResolve: 'disabled-single-sample',
    icosahedronRadius: 0.5,
    icosahedronDetail: 3,
    vertexCount: 960,
    indexCount: 960,
    triangleCount: 320,
    cameraFovDegrees: 60,
    cameraNear: 0.1,
    cameraFar: 100,
    cameraPosition: scenario.cameraPosition,
    activeCountMutation: scenario.activeCountMutation,
    initialEntityId: 0,
    activeEntityId: scenario.activeEntityId,
    entityReallocated: scenario.entityReallocated,
    instanceComponentUpdateCount: scenario.hitIds.length,
    raycastHitInstanceIds: scenario.hitIds,
    raycastHitFrames: scenario.hitFrames,
    inputReplayEventCount: replayExpectation?.eventCount ?? 0,
    gpuWorkDslOnly: true
  }, 'snapshot', failures);
  const example = context.inputValidation.example;
  const manifestScenario = example.scenarios.find((candidate) => candidate.id === scenario.id);
  const oracleImage = await loadRgbaArtifact(
    path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
    path.join(context.oracleRoot, caseId, `${scenario.id}.json`));
  failures.push(...validateDeterministicCaptureMetadata(
    example, manifestScenario, image.metadata, oracleImage.metadata, pipeline, backend));
  failures.push(...validateStructuralSnapshot(example, manifestScenario, snapshot));
  failures.push(...validateRenderSetSnapshot(example, snapshot, manifestScenario));

  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const colors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    const alpha = image.pixels[offset + 3];
    if (red !== 0 || green !== 0 || blue !== 0) nonBlackPixels += 1;
    if (alpha !== 255) nonOpaquePixels += 1;
    colors.add((red << 16) | (green << 8) | blue);
  }
  if (nonBlackPixels === 0 || colors.size < 2) failures.push('Capture is black or clear-only.');
  if (nonOpaquePixels !== 0) failures.push(`Capture contains ${nonOpaquePixels} non-opaque pixels.`);
  return {
    image,
    snapshot,
    failures,
    imageValidation: { nonBlackPixels, nonOpaquePixels, uniqueRgbColorCount: colors.size }
  };
}

/** Executes and validates one quadrant repetition. */
async function runQuadrantRepetition(context, scenario, pipeline, backend, repetitionIndex) {
  const artifacts = makeArtifactPaths(context, scenario, pipeline, backend, repetitionIndex);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
  const argumentsList = buildHostArguments(context, scenario, pipeline, backend, artifacts);
  const processResult = await runHostProcess(executable, argumentsList, context.timeoutMs);
  await Promise.all([
    fs.writeFile(artifacts.stdoutPath, processResult.stdout),
    fs.writeFile(artifacts.stderrPath, processResult.stderr)
  ]);
  const failures = [];
  if (processResult.timedOut) failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  if (processResult.exitCode !== 0) {
    failures.push(`Host exited code=${processResult.exitCode} signal=${processResult.signal}.`);
  }
  let validation = null;
  if (failures.length === 0) {
    try {
      validation = await validateRunArtifacts(context, scenario, pipeline, backend, artifacts);
      failures.push(...validation.failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  return {
    scenarioId: scenario.id,
    pipeline,
    backend,
    repetition: repetitionIndex + 1,
    status: failures.length === 0 ? 'pass' : 'fail',
    executable,
    arguments: argumentsList,
    process: processResult,
    artifacts,
    imageValidation: validation?.imageValidation ?? null,
    failures
  };
}

/** Loads one completed run's capture image. */
async function loadRunImage(run) {
  return loadRgbaArtifact(run.artifacts.rgbaPath, run.artifacts.metadataPath);
}

/** Builds one three-run stability result for a scenario quadrant. */
async function buildStabilityComparison(runs) {
  const failures = [];
  const comparisons = [];
  if (runs.length !== stabilityRunCount || runs.some((run) => run.status !== 'pass')) {
    failures.push('Three successful repetitions are required before stability comparison.');
  } else {
    const baseline = await loadRunImage(runs[0]);
    const baselineDigest = await sha256File(runs[0].artifacts.rgbaPath);
    for (let index = 1; index < runs.length; index += 1) {
      const candidate = await loadRunImage(runs[index]);
      const comparison = compareThreeCaptures(baseline, candidate);
      const digest = await sha256File(runs[index].artifacts.rgbaPath);
      if (digest !== baselineDigest) {
        failures.push(`Run ${index + 1} RGBA SHA-256 differs from run 1.`);
      }
      failures.push(...comparison.failures.map((failure) => `run1-vs-run${index + 1}: ${failure}`));
      comparisons.push({
        relation: `run-1-vs-run-${index + 1}`,
        status: comparison.failures.length === 0 && digest === baselineDigest ? 'pass' : 'fail',
        rgbaSha256: digest,
        metrics: comparison.metrics,
        failures: comparison.failures
      });
    }
    return {
      scenarioId: runs[0].scenarioId,
      pipeline: runs[0].pipeline,
      backend: runs[0].backend,
      status: failures.length === 0 ? 'pass' : 'fail',
      baselineRgbaSha256: baselineDigest,
      comparisons,
      failures
    };
  }
  return {
    scenarioId: runs[0]?.scenarioId,
    pipeline: runs[0]?.pipeline,
    backend: runs[0]?.backend,
    status: 'fail',
    comparisons,
    failures
  };
}

/** Compares one canonical quadrant capture to its real r185 Oracle. */
async function compareOracle(context, scenario, run) {
  const [oracle, actual] = await Promise.all([
    loadRgbaArtifact(
      path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      path.join(context.oracleRoot, caseId, `${scenario.id}.json`)),
    loadRunImage(run)
  ]);
  const comparison = compareThreeCaptures(oracle, actual);
  return {
    scenarioId: scenario.id,
    pipeline: run.pipeline,
    backend: run.backend,
    status: comparison.failures.length === 0 ? 'pass' : 'fail',
    metrics: comparison.metrics,
    failures: comparison.failures
  };
}

/** Compares one pair of canonical quadrant captures under fixed global thresholds. */
async function compareParity(relation, leftRun, rightRun) {
  const [left, right] = await Promise.all([loadRunImage(leftRun), loadRunImage(rightRun)]);
  const comparison = compareThreeCaptures(left, right);
  return {
    scenarioId: leftRun.scenarioId,
    relation,
    status: comparison.failures.length === 0 ? 'pass' : 'fail',
    metrics: comparison.metrics,
    failures: comparison.failures
  };
}

/** Writes the complete focused gate report and sets a failing process status when needed. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    hitReplayPath: requirePathOption(options, 'hit-input-replay'),
    reducedReplayPath: requirePathOption(options, 'reduced-input-replay'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });

  context.inputValidation = await validateFixtureInputs(context);
  const [generatedArtifactLint, gpuBoundaryLint] = await Promise.all([
    lintGeneratedArtifacts(context),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const runs = [];
  if (context.inputValidation.status === 'pass' &&
      generatedArtifactLint.status === 'pass' && gpuBoundaryLint.status === 'pass') {
    for (const scenario of scenarios) {
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          for (let repetition = 0; repetition < stabilityRunCount; repetition += 1) {
            const run = await runQuadrantRepetition(
              context, scenario, pipeline, backend, repetition);
            runs.push(run);
            console.log(
              `[${run.status.toUpperCase()}] ${scenario.id} ${pipeline}/${backend} run ${repetition + 1}/${stabilityRunCount}`);
            for (const failure of run.failures) console.log(`  ${failure}`);
          }
        }
      }
    }
  }

  const quadrants = [];
  const stabilityComparisons = [];
  const oracleComparisons = [];
  const parityComparisons = [];
  for (const scenario of scenarios) {
    const canonicalRuns = new Map();
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const quadrantRuns = runs.filter((run) =>
          run.scenarioId === scenario.id && run.pipeline === pipeline && run.backend === backend);
        const stability = await buildStabilityComparison(quadrantRuns);
        stabilityComparisons.push(stability);
        const canonical = quadrantRuns[0];
        if (canonical) {
          canonicalRuns.set(`${pipeline}-${backend}`, canonical);
          oracleComparisons.push(await compareOracle(context, scenario, canonical));
        }
        const failures = [
          ...quadrantRuns.flatMap((run) => run.failures),
          ...stability.failures
        ];
        quadrants.push({
          scenarioId: scenario.id,
          pipeline,
          backend,
          status: failures.length === 0 ? 'pass' : 'fail',
          runs: quadrantRuns,
          stability,
          failures
        });
      }
    }
    const parityPairs = [
      ['legacy-metal-vs-experimental-metal', 'legacy-metal', 'experimental-metal'],
      ['legacy-vulkan-vs-experimental-vulkan', 'legacy-vulkan', 'experimental-vulkan'],
      ['legacy-metal-vs-legacy-vulkan', 'legacy-metal', 'legacy-vulkan'],
      ['experimental-metal-vs-experimental-vulkan', 'experimental-metal', 'experimental-vulkan']
    ];
    for (const [relation, left, right] of parityPairs) {
      if (canonicalRuns.has(left) && canonicalRuns.has(right)) {
        parityComparisons.push(await compareParity(
          relation, canonicalRuns.get(left), canonicalRuns.get(right)));
      }
    }
  }

  const status = context.inputValidation.status === 'pass' &&
    generatedArtifactLint.status === 'pass' && gpuBoundaryLint.status === 'pass' &&
    quadrants.length === scenarios.length * pipelines.length * backends.length &&
    quadrants.every((entry) => entry.status === 'pass') &&
    stabilityComparisons.every((entry) => entry.status === 'pass') &&
    oracleComparisons.every((entry) => entry.status === 'pass') &&
    parityComparisons.every((entry) => entry.status === 'pass')
      ? 'pass'
      : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'webgl-instancing-raycast-four-quadrant-oracle',
    status,
    caseId,
    thresholds: comparisonThresholds,
    stabilityRunCount,
    inputValidation: {
      status: context.inputValidation.status,
      manifestPath: context.inputValidation.manifestPath,
      replayIdentities: context.inputValidation.replayIdentities,
      failures: context.inputValidation.failures
    },
    generatedArtifactLint,
    gpuBoundaryLint,
    quadrants,
    stabilityComparisons,
    oracleComparisons,
    parityComparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`);
  console.log(`Fixture JSON: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
