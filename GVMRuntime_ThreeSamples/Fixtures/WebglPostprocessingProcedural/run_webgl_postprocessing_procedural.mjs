#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { defaultThreeRandomSeed } from '../../../tests/runners/three/node/determinism.mjs';
import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const shardName = 'Phase1WebglPostprocessingProcedural';
const passName = 'WebglPostprocessingProceduralPass';
const coordinatePassName = 'WebglPostprocessingProceduralCoordinatePass';
const generatedPassNames = Object.freeze([passName, coordinatePassName]);
const caseId = 'webgl_postprocessing_procedural';
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const lockedReplaySha256 = Object.freeze({
  'noise-1d': 'c8e6b41d87dcc55a45b78475e82ba792145089f12ce6f3c577ad8ff3d2ba747f',
  'noise-2d': 'e2d1250651642ec5f3abb815aa313a624a52bca500591cfa207d0f739d2570c5'
});
const lockedOracleSha256 = Object.freeze({
  'initial-3d': Object.freeze({
    rgba: '0bb287a1bc2472d9f5d6ec5ac7c5510e3ee118afb50c4007eb03906e95b20ca0',
    json: '051c9bd1c8b6d43e023c55fe81a977ee664ae0943693763bf60135fe50dd77fc'
  }),
  'noise-1d': Object.freeze({
    rgba: '0cb6f2895e2b1c012d78e7f70a51f1ddcac60a342b9bd40fcfa49c3578753bf8',
    json: '50703e7207c4059a23287fae2240487ad85a0f18f396a3b571d0394a8467239c'
  }),
  'noise-2d': Object.freeze({
    rgba: '17d59c431f71f6f7d007f91b2ef949aa6b86e33b9cff79c0922b29c68b08939a',
    json: 'a2413cae8a66813e39be113a757dfbaac4a8306b7d485fa1e5467e956de699b8'
  })
});

export const proceduralScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-3d',
    frame: 0,
    noiseMode: 'noiseRandom3D',
    noiseDimension: 3,
    canonicalState: 'noise-random-three-channel',
    replayOption: null
  }),
  Object.freeze({
    id: 'noise-1d',
    frame: 1,
    noiseMode: 'noiseRandom1D',
    noiseDimension: 1,
    canonicalState: 'noise-random-one-channel',
    replayOption: 'inputReplay1dPath'
  }),
  Object.freeze({
    id: 'noise-2d',
    frame: 1,
    noiseMode: 'noiseRandom2D',
    noiseDimension: 2,
    canonicalState: 'noise-random-two-channel-blue-black-mix',
    replayOption: 'inputReplay2dPath'
  })
]);

/** Parses strict value-bearing long options accepted by this focused fixture. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith('--') || value == null || value.startsWith('--')) {
      throw new Error(`Expected --option value pair near '${option ?? '<end>'}'.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) {
      throw new Error(`Duplicate --${name} option.`);
    }
    options[name] = value;
  }
  return options;
}

/** Resolves one required explicit fixture path without consulting environment variables. */
function requirePathOption(options, name) {
  if (!options[name]) {
    throw new Error(`Missing required --${name} path.`);
  }
  return path.resolve(options[name]);
}

/** Parses one optional positive integer watchdog setting. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) {
    return fallback;
  }
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Parses the mandatory stability repetition count and rejects fewer than three runs. */
export function parseRepeatCount(value) {
  const repeatCount = parsePositiveInteger(value, 3, '--repeat');
  if (repeatCount < 3) {
    throw new Error(`--repeat must be at least 3; received '${value ?? repeatCount}'.`);
  }
  return repeatCount;
}

/** Returns whether one resolved artifact path currently exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Returns one byte sequence's lowercase SHA-256 digest. */
function sha256Bytes(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Returns one explicit file's lowercase SHA-256 digest. */
async function sha256File(targetPath) {
  return sha256Bytes(await fs.readFile(targetPath));
}

/** Compares expected JSON fields recursively while allowing diagnostic extensions. */
export function validateExpectedFields(actual, expected, label, failures) {
  for (const [name, expectedValue] of Object.entries(expected)) {
    const actualValue = actual?.[name];
    const fieldLabel = `${label}.${name}`;
    if (Array.isArray(expectedValue)) {
      if (!Array.isArray(actualValue) || actualValue.length !== expectedValue.length) {
        failures.push(
          `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
        continue;
      }
      for (let index = 0; index < expectedValue.length; index += 1) {
        const expectedElement = expectedValue[index];
        const actualElement = actualValue[index];
        if (expectedElement !== null && typeof expectedElement === 'object') {
          validateExpectedFields(
            actualElement, expectedElement, `${fieldLabel}[${index}]`, failures);
        } else if (actualElement !== expectedElement) {
          failures.push(
            `${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${JSON.stringify(expectedElement)}.`);
        }
      }
    } else if (expectedValue !== null && typeof expectedValue === 'object') {
      if (actualValue === null || typeof actualValue !== 'object' || Array.isArray(actualValue)) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected an object.`);
      } else {
        validateExpectedFields(actualValue, expectedValue, fieldLabel, failures);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(
        `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Validates canonical replay bytes and returns their capture-metadata identity. */
export function validateCanonicalReplayBytes(bytes, expectation) {
  const digest = sha256Bytes(bytes);
  let document;
  try {
    document = JSON.parse(bytes.toString('utf8'));
  } catch (error) {
    throw new Error(`Replay is not valid JSON: ${error.message}`);
  }
  if (!Array.isArray(document.events)) {
    throw new Error('Replay events must be an array.');
  }
  const identity = {
    schemaVersion: document.schemaVersion,
    sha256: digest,
    caseId: document.caseId,
    scenarioId: document.scenarioId,
    captureFrame: document.frame,
    eventCount: document.events.length,
    target: document.target
  };
  const failures = [];
  validateExpectedFields(identity, {
    schemaVersion: 1,
    sha256: expectation.sha256,
    caseId,
    scenarioId: expectation.scenarioId,
    captureFrame: expectation.frame,
    eventCount: expectation.eventCount,
    target: expectation.target
  }, `replay.${expectation.scenarioId}`, failures);
  validateExpectedFields(document, {
    canonicalState: { procedure: expectation.noiseMode }
  }, `replayDocument.${expectation.scenarioId}`, failures);
  if (failures.length > 0) {
    throw new Error(failures.join('\n'));
  }
  return identity;
}

/** Loads and validates one immutable replay document from an explicit path. */
async function loadCanonicalReplay(replayPath, expectation) {
  const bytes = await fs.readFile(replayPath);
  return validateCanonicalReplayBytes(bytes, expectation);
}

/** Returns the required replay identity for one scenario, or null for the initial frame. */
function replayIdentityForScenario(context, scenario) {
  return scenario.replayOption === null
    ? null
    : context.replayIdentities[scenario.id];
}

/** Creates deterministic output paths for one repeated scenario quadrant capture. */
function makeArtifactPaths(outputRoot, scenario, pipeline, backend, repeatIndex) {
  const artifactDirectory = path.join(
    outputRoot,
    scenario.id,
    pipeline,
    backend,
    `repeat-${repeatIndex + 1}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the complete explicit host CLI for one locked procedural-noise scenario. */
export function buildHostArguments(context, scenario, pipeline, backend, artifacts) {
  const argumentsList = [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(defaultThreeRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
  if (scenario.replayOption !== null) {
    argumentsList.push('--input-replay', context[scenario.replayOption]);
  }
  return argumentsList;
}

/** Executes one sample host under a hard watchdog with bounded diagnostics. */
async function executeHost(executable, argumentsList, timeoutMs) {
  const startedAt = Date.now();
  return new Promise((resolve) => {
    const child = spawn(executable, argumentsList, {
      cwd: repositoryRoot,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    let timedOut = false;
    child.stdout.on('data', (chunk) => {
      stdout = `${stdout}${chunk.toString('utf8')}`.slice(-64 * 1024);
    });
    child.stderr.on('data', (chunk) => {
      stderr = `${stderr}${chunk.toString('utf8')}`.slice(-64 * 1024);
    });
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
        stdout,
        stderr: `${stderr}${stderr ? '\n' : ''}${error.message}`
      });
    });
    child.once('close', (exitCode, signal) => {
      clearTimeout(watchdog);
      resolve({
        exitCode,
        signal,
        timedOut,
        durationMs: Date.now() - startedAt,
        stdout,
        stderr
      });
    });
  });
}

/** Reads one required generated text artifact and records stable diagnostics. */
async function readRequiredText(targetPath, failures, label) {
  if (!await pathExists(targetPath)) {
    failures.push(`${label}: missing ${targetPath}.`);
    return '';
  }
  const source = await fs.readFile(targetPath, 'utf8');
  if (source.length === 0) {
    failures.push(`${label}: ${targetPath} is empty.`);
  }
  return source;
}

/** Returns every required Experimental UGLIR, MSL, and direct-SPIR-V product path. */
function makeExperimentalProductPaths(generatedDirectory) {
  return generatedPassNames.flatMap((generatedPassName) =>
    ['vertex', 'fragment'].flatMap((stage) => {
      const baseName = `${generatedPassName}__${stage}`;
      return [
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
        path.join(generatedDirectory, 'msl', `${baseName}.msl`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
      ];
    }));
}

/** Validates one isolated Legacy or Experimental generated shader product set. */
async function validateGeneratedPipeline(generatedRoot, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
  const label = `${shardName}/${pipeline}`;
  const failures = [];
  const [exportsSource, generatedSource, singleHeader] = await Promise.all([
    readRequiredText(path.join(generatedDirectory, 'exports.hpp'), failures, label),
    readRequiredText(path.join(generatedDirectory, 'generate_result.hpp'), failures, label),
    readRequiredText(path.join(generatedDirectory, 'dsl_single_header.hpp'), failures, label)
  ]);
  if (exportsSource && !/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${label}: fullscreen procedural rendering must not declare a RenderSet.`);
  }
  if (generatedSource.includes('setVertexBuffer') || generatedSource.includes('setIndexBuffer')) {
    failures.push(`${label}: fullscreen procedural rendering must not bind geometry buffers.`);
  }
  for (const forbiddenToken of [
    'WebglPostprocessingProceduralIndexedPass',
    'WebglPostprocessingProceduralReverseCoordinatePass',
    'planeVertexBuffer',
    'planeIndexBuffer',
    'encodedBits',
    'modelViewMatrix',
    'projectionMatrix'
  ]) {
    if (generatedSource.includes(forbiddenToken) || singleHeader.includes(forbiddenToken)) {
      failures.push(`${label}: generated products retain diagnostic token '${forbiddenToken}'.`);
    }
  }
  for (const requiredToken of [
    `class ${passName}`,
    `class ${coordinatePassName}`,
    'WebglPostprocessingProceduralRenderer',
    'run(3u, 1u, 0u, 0u)',
    'run(6u, 1u, 0u, 0u)',
    'renderPass("procedural-x-coordinates"',
    'renderPass("procedural-y-coordinates"',
    'renderPass("procedural-noise"'
  ]) {
    if (!generatedSource.includes(requiredToken)) {
      failures.push(`${label}: generated host is missing '${requiredToken}'.`);
    }
  }
  for (const generatedPassName of generatedPassNames) {
    if (!singleHeader.includes(generatedPassName)) {
      failures.push(`${label}: DSL single-header product is missing ${generatedPassName}.`);
    }
  }

  const products = pipeline === 'experimental'
    ? makeExperimentalProductPaths(generatedDirectory)
    : [];
  for (const product of products) {
    await readRequiredText(product, failures, label);
  }
  if (pipeline === 'experimental' && products.every((product) =>
    !failures.some((failure) => failure.includes(product)))) {
    for (const generatedPassName of generatedPassNames) {
      for (const stage of ['vertex', 'fragment']) {
        const baseName = `${generatedPassName}__${stage}`;
        const [uglir, msl, spirvWords, spirvAssembly] = await Promise.all([
          fs.readFile(path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`), 'utf8'),
          fs.readFile(path.join(generatedDirectory, 'msl', `${baseName}.msl`), 'utf8'),
          fs.readFile(path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`), 'utf8'),
          fs.readFile(path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`), 'utf8')
        ]);
        const entryPoint = stage === 'vertex' ? 'Vertex' : 'Fragment';
        for (const [productLabel, source, tokens] of [
          [`${baseName} UGLIR`, uglir, ['module']],
          [`${baseName} MSL`, msl, [stage]],
          [`${baseName} direct SPIR-V words`, spirvWords, ['word_count', '0x07230203']],
          [`${baseName} direct SPIR-V assembly`, spirvAssembly, [`OpEntryPoint ${entryPoint}`]]
        ]) {
          for (const token of tokens) {
            if (!source.includes(token)) {
              failures.push(`${label}: ${productLabel} is missing '${token}'.`);
            }
          }
        }
      }
    }
    const fragmentBase = `${passName}__fragment`;
    const [fragmentUglir, fragmentMsl, fragmentSpirvAssembly] = await Promise.all([
      fs.readFile(path.join(generatedDirectory, 'uglir', `${fragmentBase}.uglir.txt`), 'utf8'),
      fs.readFile(path.join(generatedDirectory, 'msl', `${fragmentBase}.msl`), 'utf8'),
      fs.readFile(path.join(generatedDirectory, 'spv', `${fragmentBase}.raw.spvasm`), 'utf8')
    ]);
    for (const [productLabel, source, tokens] of [
      ['procedural fragment UGLIR', fragmentUglir, ['intrinsic math_sin', 'intrinsic math_frac']],
      ['procedural fragment MSL', fragmentMsl, ['sin(', 'fract(', 'dot(']],
      ['procedural fragment direct SPIR-V assembly', fragmentSpirvAssembly, ['OpDot']]
    ]) {
      for (const token of tokens) {
        if (!source.includes(token)) {
          failures.push(`${label}: ${productLabel} is missing '${token}'.`);
        }
      }
    }
  }
  return {
    pipeline,
    generatedDirectory,
    products,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Validates canonical replay, Oracle, binary, and generated-root input availability. */
async function validateFixtureInputs(context) {
  const failures = [];
  const replayDefinitions = [
    {
      path: context.inputReplay1dPath,
      sha256: lockedReplaySha256['noise-1d'],
      scenarioId: 'noise-1d',
      frame: 1,
      eventCount: 2,
      target: '.lil-gui .controller.option select',
      noiseMode: 'noiseRandom1D'
    },
    {
      path: context.inputReplay2dPath,
      sha256: lockedReplaySha256['noise-2d'],
      scenarioId: 'noise-2d',
      frame: 1,
      eventCount: 2,
      target: '.lil-gui .controller.option select',
      noiseMode: 'noiseRandom2D'
    }
  ];
  const replayIdentities = {};
  for (const definition of replayDefinitions) {
    if (!await pathExists(definition.path)) {
      failures.push(`Missing canonical replay: ${definition.path}.`);
      continue;
    }
    try {
      replayIdentities[definition.scenarioId] = await loadCanonicalReplay(
        definition.path, definition);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  const oracleIdentities = {};
  for (const scenario of proceduralScenarios) {
    const oracleBase = path.join(context.oracleRoot, caseId, scenario.id);
    for (const extension of ['rgba', 'json']) {
      const oraclePath = `${oracleBase}.${extension}`;
      if (!await pathExists(oraclePath)) {
        failures.push(`Missing Three r185 Oracle artifact: ${oraclePath}.`);
        continue;
      }
      const sha256 = await sha256File(oraclePath);
      oracleIdentities[`${scenario.id}.${extension}`] = { path: oraclePath, sha256 };
      if (sha256 !== lockedOracleSha256[scenario.id][extension]) {
        failures.push(
          `${oraclePath}: SHA-256 ${sha256}, expected ${lockedOracleSha256[scenario.id][extension]}.`);
      }
    }
  }
  for (const pipeline of pipelines) {
    const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
    if (!await pathExists(executable)) {
      failures.push(`Missing ${pipeline} host executable: ${executable}.`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    replayIdentities,
    oracleIdentities,
    failures
  };
}

/** Validates one immutable Three Oracle's scenario and replay identity. */
function validateOracleMetadata(scenario, metadata, replayIdentity) {
  const failures = [];
  validateExpectedFields(metadata, {
    source: 'three-r185-reference',
    upstreamCommit,
    caseId,
    scenarioId: scenario.id,
    randomSeed: defaultThreeRandomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: replayIdentity
  }, 'oracleMetadata', failures);
  return failures;
}

/** Inspects one capture for opaque, non-empty procedural output. */
function inspectCapturedImage(image) {
  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const uniqueRgbColors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    const alpha = image.pixels[offset + 3];
    if (red !== 0 || green !== 0 || blue !== 0) {
      nonBlackPixels += 1;
    }
    if (alpha !== 255) {
      nonOpaquePixels += 1;
    }
    uniqueRgbColors.add((red << 16) | (green << 8) | blue);
  }
  const failures = [];
  if (nonBlackPixels === 0 || uniqueRgbColors.size < 2) {
    failures.push('Capture is black or clear-only.');
  }
  if (nonOpaquePixels !== 0) {
    failures.push(`Capture contains ${nonOpaquePixels} non-opaque pixels.`);
  }
  return {
    failures,
    inspection: {
      nonBlackPixels,
      nonOpaquePixels,
      uniqueRgbColorCount: uniqueRgbColors.size
    }
  };
}

/** Validates one host capture's metadata, snapshot, and procedural image footprint. */
async function validateQuadrantArtifacts(context, scenario, pipeline, backend, artifacts) {
  const [image, snapshot] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8').then(JSON.parse)
  ]);
  const replayIdentity = replayIdentityForScenario(context, scenario);
  const failures = [];
  validateExpectedFields(image.metadata, {
    schemaVersion: 1,
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    randomSeed: defaultThreeRandomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    noiseMode: scenario.noiseMode,
    noiseDimension: scenario.noiseDimension,
    canonicalState: scenario.canonicalState,
    inputReplay: replayIdentity
  }, 'metadata', failures);
  validateExpectedFields(snapshot, {
    schemaVersion: 1,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    upstreamRevision: 'r185',
    upstreamCommit,
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 0,
    instanceCount: 0,
    scenePassCount: 0,
    screenPassCount: 3,
    drawCommandCount: 3,
    explicitVertexCount: 15,
    noiseMode: scenario.noiseMode,
    noiseDimension: scenario.noiseDimension,
    canonicalState: scenario.canonicalState,
    inputReplayEventCount: replayIdentity?.eventCount ?? 0,
    inputReplay: replayIdentity,
    gpuWorkDslOnly: true
  }, 'snapshot', failures);
  const imageInspection = inspectCapturedImage(image);
  failures.push(...imageInspection.failures);
  return { image, snapshot, failures, imageInspection: imageInspection.inspection };
}

/** Runs and validates one repeated Legacy/Experimental and Metal/Vulkan capture. */
async function runCaptureRepeat(context, scenario, pipeline, backend, repeatIndex) {
  const artifacts = makeArtifactPaths(
    context.outputRoot, scenario, pipeline, backend, repeatIndex);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
  const argumentsList = buildHostArguments(context, scenario, pipeline, backend, artifacts);
  const processResult = await executeHost(executable, argumentsList, context.timeoutMs);
  const result = {
    id: `${scenario.id}/${pipeline}/${backend}/repeat-${repeatIndex + 1}`,
    scenarioId: scenario.id,
    pipeline,
    backend,
    repeat: repeatIndex + 1,
    status: 'fail',
    artifacts,
    host: { executable, arguments: argumentsList, ...processResult },
    failures: []
  };
  await fs.writeFile(
    artifacts.hostLogPath, `${JSON.stringify(result.host, null, 2)}\n`, 'utf8');
  if (processResult.timedOut) {
    result.failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  } else if (processResult.exitCode !== 0) {
    result.failures.push(
      `Host exited with code ${processResult.exitCode}${processResult.signal ? ` (${processResult.signal})` : ''}.`);
  } else {
    try {
      const validation = await validateQuadrantArtifacts(
        context, scenario, pipeline, backend, artifacts);
      result.validation = {
        snapshot: validation.snapshot,
        imageInspection: validation.imageInspection
      };
      result.failures.push(...validation.failures);
      result.sha256 = {
        rgba: await sha256File(artifacts.rgbaPath),
        metadata: await sha256File(artifacts.metadataPath),
        snapshot: await sha256File(artifacts.snapshotPath)
      };
    } catch (error) {
      result.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Runs all repetitions for one quadrant and proves byte-exact artifact stability. */
async function runStabilityGroup(context, scenario, pipeline, backend) {
  const runs = [];
  for (let repeatIndex = 0; repeatIndex < context.repeatCount; repeatIndex += 1) {
    runs.push(await runCaptureRepeat(
      context, scenario, pipeline, backend, repeatIndex));
  }
  const failures = runs.flatMap((run) =>
    run.failures.map((failure) => `repeat-${run.repeat}: ${failure}`));
  const sha256 = {
    rgba: runs.map((run) => run.sha256?.rgba ?? null),
    metadata: runs.map((run) => run.sha256?.metadata ?? null),
    snapshot: runs.map((run) => run.sha256?.snapshot ?? null)
  };
  for (const [label, digests] of Object.entries(sha256)) {
    if (digests.some((digest) => digest === null) || new Set(digests).size !== 1) {
      failures.push(`${label} SHA-256 differs across locked repetitions.`);
    }
  }
  return {
    id: `${scenario.id}/${pipeline}/${backend}`,
    scenarioId: scenario.id,
    pipeline,
    backend,
    repeatCount: context.repeatCount,
    runIds: runs.map((run) => run.id),
    artifacts: runs[0]?.artifacts ?? null,
    sha256,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures,
    runs
  };
}

/** Loads one successful quadrant capture from its deterministic artifact paths. */
async function loadQuadrantImage(quadrant) {
  return loadRgbaArtifact(quadrant.artifacts.rgbaPath, quadrant.artifacts.metadataPath);
}

/** Compares one quadrant against the same-scenario immutable Three r185 Oracle. */
async function compareOracle(context, scenario, quadrant) {
  const result = {
    scenarioId: scenario.id,
    pipeline: quadrant.pipeline,
    backend: quadrant.backend,
    repeat: quadrant.repeat,
    status: 'fail',
    failures: []
  };
  if (quadrant.status !== 'pass') {
    result.failures.push('The quadrant must pass before Oracle comparison.');
  } else {
    try {
      const oracleBase = path.join(context.oracleRoot, caseId, scenario.id);
      const [oracle, actual] = await Promise.all([
        loadRgbaArtifact(`${oracleBase}.rgba`, `${oracleBase}.json`),
        loadQuadrantImage(quadrant)
      ]);
      result.failures.push(...validateOracleMetadata(
        scenario, oracle.metadata, replayIdentityForScenario(context, scenario)));
      const comparison = compareThreeCaptures(oracle, actual);
      result.metrics = comparison.metrics;
      result.failures.push(...comparison.failures);
    } catch (error) {
      result.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Compares one mandatory same-scenario pipeline/backend parity pair. */
async function compareParity(scenario, relation, left, right) {
  const result = {
    scenarioId: scenario.id,
    relation,
    status: 'fail',
    failures: []
  };
  if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
    result.failures.push('Both required quadrants must pass before parity comparison.');
  } else {
    const [leftImage, rightImage] = await Promise.all([
      loadQuadrantImage(left),
      loadQuadrantImage(right)
    ]);
    const comparison = compareThreeCaptures(leftImage, rightImage);
    result.metrics = comparison.metrics;
    result.failures.push(...comparison.failures);
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Builds all fixed same-backend and same-pipeline comparisons for one scenario. */
async function compareScenarioParity(scenario, quadrants) {
  const byKey = new Map(
    quadrants.map((quadrant) => [`${quadrant.pipeline}-${quadrant.backend}`, quadrant]));
  const pairs = [
    ['legacy-metal-vs-experimental-metal', 'legacy-metal', 'experimental-metal'],
    ['legacy-vulkan-vs-experimental-vulkan', 'legacy-vulkan', 'experimental-vulkan'],
    ['legacy-metal-vs-legacy-vulkan', 'legacy-metal', 'legacy-vulkan'],
    ['experimental-metal-vs-experimental-vulkan', 'experimental-metal', 'experimental-vulkan']
  ];
  const results = [];
  for (const [relation, leftKey, rightKey] of pairs) {
    results.push(await compareParity(
      scenario, relation, byKey.get(leftKey), byKey.get(rightKey)));
  }
  return results;
}

/** Executes 36 locked captures, 12 stability groups, and the complete focused gate report. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    inputReplay1dPath: requirePathOption(options, 'input-replay-1d'),
    inputReplay2dPath: requirePathOption(options, 'input-replay-2d'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms'),
    repeatCount: parseRepeatCount(options.repeat)
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });

  const inputValidation = await validateFixtureInputs(context);
  context.replayIdentities = inputValidation.replayIdentities;
  const generatedArtifacts = await Promise.all(
    pipelines.map((pipeline) => validateGeneratedPipeline(context.generatedRoot, pipeline)));
  const runs = [];
  const stabilityGroups = [];
  const oracleComparisons = [];
  const parityComparisons = [];
  if (inputValidation.status === 'pass' &&
      generatedArtifacts.every((entry) => entry.status === 'pass')) {
    for (const scenario of proceduralScenarios) {
      const scenarioStabilityGroups = [];
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          const result = await runStabilityGroup(
            context, scenario, pipeline, backend);
          const { runs: groupRuns, ...stabilityGroup } = result;
          runs.push(...groupRuns);
          stabilityGroups.push(stabilityGroup);
          scenarioStabilityGroups.push(stabilityGroup);
          for (const run of groupRuns) {
            console.log(
              `[${run.status.toUpperCase()}] ${caseId}/${scenario.id} ` +
              `${pipeline}/${backend} repeat-${run.repeat}`);
            for (const failure of run.failures) {
              console.log(`  ${failure}`);
            }
            oracleComparisons.push(await compareOracle(context, scenario, run));
          }
          console.log(
            `[${stabilityGroup.status.toUpperCase()}] ${caseId}/${scenario.id} ` +
            `${pipeline}/${backend} ${context.repeatCount}x stability`);
          for (const failure of stabilityGroup.failures) {
            console.log(`  ${failure}`);
          }
        }
      }
      parityComparisons.push(...await compareScenarioParity(
        scenario, scenarioStabilityGroups));
    }
  }

  const expectedStabilityGroupCount =
    proceduralScenarios.length * pipelines.length * backends.length;
  const expectedRunCount = expectedStabilityGroupCount * context.repeatCount;
  const status = inputValidation.status === 'pass' &&
    generatedArtifacts.every((entry) => entry.status === 'pass') &&
    runs.length === expectedRunCount &&
    runs.every((entry) => entry.status === 'pass') &&
    stabilityGroups.length === expectedStabilityGroupCount &&
    stabilityGroups.every((entry) => entry.status === 'pass') &&
    oracleComparisons.length === expectedRunCount &&
    oracleComparisons.every((entry) => entry.status === 'pass') &&
    parityComparisons.length === proceduralScenarios.length * 4 &&
    parityComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-postprocessing-procedural',
    caseId,
    status,
    comparisonThresholds,
    repeatCount: context.repeatCount,
    runCount: runs.length,
    stabilityGroupCount: stabilityGroups.length,
    inputValidation,
    generatedArtifacts,
    runs,
    stabilityGroups,
    quadrants: stabilityGroups,
    oracleComparisons,
    parityComparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  for (const entry of generatedArtifacts) {
    console.log(`[${entry.status.toUpperCase()}] ${caseId} ${entry.pipeline} generated products`);
    for (const failure of entry.failures) {
      console.log(`  ${failure}`);
    }
  }
  for (const comparison of oracleComparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${caseId}/${comparison.scenarioId} Oracle ` +
      `${comparison.pipeline}/${comparison.backend} repeat-${comparison.repeat}`);
  }
  for (const comparison of parityComparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${caseId}/${comparison.scenarioId} ${comparison.relation}`);
  }
  for (const failure of inputValidation.failures) {
    console.log(`[FAIL] ${failure}`);
  }
  console.log(`webgl_postprocessing_procedural report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
