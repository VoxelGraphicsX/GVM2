#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

import {
  compareThreeCaptures,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { defaultThreeRandomSeed } from '../../../tests/runners/three/node/determinism.mjs';
import { lintPhase1ShaderCaseArtifacts } from './lint_phase1_shader_case_artifacts.mjs';

const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const caseDefinitions = Object.freeze([
  {
    name: 'WebglShader',
    caseId: 'webgl_shader',
    scenarios: Object.freeze([
      { id: 'initial', frame: 0, timeSeconds: 0 },
      { id: 'animated', frame: 60, timeSeconds: 1 }
    ]),
    expectedSnapshot: {
      sceneRenderSetCount: 0,
      renderableObjectCount: 1,
      instanceCount: 1,
      screenPassCount: 1,
      drawCommandCount: 1,
      explicitVertexCount: 3,
      fixedStepSeconds: 1 / 60,
      gpuWorkDslOnly: true
    },
    minimumNonUniformPixels: 200_000,
    minimumUniqueColors: 512
  },
  {
    name: 'WebglBuffergeometryAttributesNone',
    caseId: 'webgl_buffergeometry_attributes_none',
    scenarios: Object.freeze([
      { id: 'initial', frame: 0, timeSeconds: 0, rotationX: 0, rotationY: 0 },
      { id: 'rotated', frame: 60, timeSeconds: 1, rotationX: 0.25, rotationY: 0.5 }
    ]),
    expectedSnapshot: {
      sceneRenderSetCount: 0,
      renderableObjectCount: 1,
      instanceCount: 1,
      scenePassCount: 1,
      logicalScenePassCount: 1,
      screenPassCount: 0,
      drawCommandCount: 1,
      logicalDrawCommandCount: 1,
      computePassCount: 0,
      rasterSampleCount: 1,
      explicitVertexCount: 30_000,
      submittedVertexCount: 30_000,
      standaloneGeometryBufferCount: 0,
      seed: 42,
      fixedStepSeconds: 1 / 60,
      doubleSided: true,
      scenePassSequence: Object.freeze([
        { sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 0 },
        { sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 0 },
        { sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 0 },
        { sceneRoot: 'scene', scenePass: 'main', entityOrdinal: 0 }
      ]),
      gpuWorkDslOnly: true
    },
    minimumNonUniformPixels: 20_000,
    minimumUniqueColors: 512
  },
  {
    name: 'WebglBuffergeometryRawshader',
    caseId: 'webgl_buffergeometry_rawshader',
    scenarios: Object.freeze([
      { id: 'initial', frame: 0, timeSeconds: 0, rotationY: 0, shaderTime: 0 },
      { id: 'animated', frame: 60, timeSeconds: 1, rotationY: 0.5, shaderTime: 5 }
    ]),
    expectedSnapshot: {
      sceneRenderSetCount: 0,
      renderSetPolicy: 'not-required',
      renderableObjectCount: 1,
      instanceCount: 1,
      scenePassCount: 1,
      screenPassCount: 0,
      drawCommandCount: 1,
      explicitVertexCount: 600,
      vertexStrideBytes: 16,
      positionStorageBytes: 7200,
      normalizedColorStorageBytes: 2400,
      standaloneGeometryBufferCount: 1,
      seed: defaultThreeRandomSeed,
      upstreamModuleRandomDrawCount: 76,
      upstreamObjectUuidRandomDrawCount: 12,
      upstreamPreVertexRandomDrawCount: 88,
      finalRandomState: 507720158,
      fixedStepSeconds: 1 / 60,
      cameraFovDegrees: 50,
      cameraNear: 1,
      cameraFar: 10,
      cameraPositionZ: 2,
      backgroundRgbHex: '101010',
      doubleSided: true,
      transparent: true,
      normalBlending: true,
      depthTest: true,
      depthWrite: true,
      scenePassSequence: Object.freeze([
        { sceneRoot: 'scene', scenePass: 'main-private-shader', entityOrdinal: 0 }
      ]),
      gpuWorkDslOnly: true
    },
    minimumNonUniformPixels: 50_000,
    minimumUniqueColors: 10_000
  }
]);

/** Parses strict value-bearing long options for the shader-case fixture. */
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

/** Resolves one required explicit fixture path. */
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

/** Returns whether one explicitly resolved host path exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Executes one sample host under a hard timeout with bounded diagnostics. */
async function executeHost(executable, argumentsList, sourceRoot, timeoutMs) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, argumentsList, {
      cwd: sourceRoot,
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
    child.on('error', reject);
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.on('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({ exitCode, signal, timedOut, stdout, stderr });
    });
  });
}

/** Creates deterministic artifact paths for one case/scenario quadrant. */
function makeArtifactPaths(outputRoot, definition, scenario, pipeline, backend) {
  const artifactDirectory = path.join(
    outputRoot, definition.name, scenario.id, pipeline, backend);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'final.rgba'),
    metadataPath: path.join(artifactDirectory, 'final.json'),
    snapshotPath: path.join(artifactDirectory, 'snapshot.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the complete deterministic CLI for one locked r185 shader scenario. */
function buildHostArguments(definition, scenario, pipeline, backend, artifacts) {
  return [
    '--case-id', definition.caseId,
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
}

/** Validates metadata, structural state, and non-clear output for one quadrant. */
async function validateQuadrantArtifacts(definition, scenario, pipeline, backend, artifacts) {
  const [image, snapshotText] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8')
  ]);
  const snapshot = JSON.parse(snapshotText);
  const failures = [];
  const expectedMetadata = {
    caseId: definition.caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    randomSeed: defaultThreeRandomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  };
  for (const [name, expected] of Object.entries(expectedMetadata)) {
    if (image.metadata?.[name] !== expected) {
      failures.push(`metadata ${name}=${JSON.stringify(image.metadata?.[name])}, expected ${JSON.stringify(expected)}.`);
    }
  }

  const expectedSnapshot = {
    caseId: definition.caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    ...definition.expectedSnapshot,
    timeSeconds: scenario.timeSeconds
  };
  if (scenario.rotationX != null) {
    expectedSnapshot.rotationX = scenario.rotationX;
    expectedSnapshot.rotationY = scenario.rotationY;
  }
  if (scenario.rotationY != null && scenario.rotationX == null) {
    expectedSnapshot.rotationY = scenario.rotationY;
  }
  if (scenario.shaderTime != null) {
    expectedSnapshot.shaderTime = scenario.shaderTime;
  }
  for (const [name, expected] of Object.entries(expectedSnapshot)) {
    const actual = snapshot?.[name];
    if (typeof expected === 'number' && !Number.isInteger(expected)) {
      if (typeof actual !== 'number' || Math.abs(actual - expected) > 1e-7) {
        failures.push(`snapshot ${name}=${JSON.stringify(actual)}, expected ${expected}.`);
      }
    } else if (expected !== null && typeof expected === 'object'
      ? JSON.stringify(actual) !== JSON.stringify(expected)
      : actual !== expected) {
      failures.push(`snapshot ${name}=${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}.`);
    }
  }

  const firstPixel = image.pixels.slice(0, 4);
  let nonUniformPixels = 0;
  let maximumRgb = 0;
  const uniqueColors = new Set();
  for (let offset = 0; offset < image.pixels.length; offset += 4) {
    const red = image.pixels[offset];
    const green = image.pixels[offset + 1];
    const blue = image.pixels[offset + 2];
    if (red !== firstPixel[0] || green !== firstPixel[1] || blue !== firstPixel[2]) {
      nonUniformPixels += 1;
    }
    uniqueColors.add((red << 16) | (green << 8) | blue);
    maximumRgb = Math.max(maximumRgb, red, green, blue);
  }
  if (nonUniformPixels < definition.minimumNonUniformPixels ||
      uniqueColors.size < definition.minimumUniqueColors || maximumRgb < 128) {
    failures.push('capture is clear-only or lacks the expected shader-generated content.');
  }
  return {
    failures,
    image,
    snapshot,
    imageValidation: {
      firstPixelRgba: [...firstPixel],
      nonUniformPixels,
      uniqueRgbColorCount: uniqueColors.size,
      maximumRgb
    }
  };
}

/** Runs one Legacy/Experimental and Metal/Vulkan shader-case quadrant. */
async function runQuadrant(context, definition, scenario, pipeline, backend) {
  const artifacts = makeArtifactPaths(
    context.outputRoot, definition, scenario, pipeline, backend);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${definition.name}-${pipeline}`);
  const argumentsList = buildHostArguments(definition, scenario, pipeline, backend, artifacts);
  const result = {
    case: definition.name,
    scenario: scenario.id,
    frame: scenario.frame,
    pipeline,
    backend,
    status: 'fail',
    artifacts,
    failures: []
  };
  if (!await pathExists(executable)) {
    result.failures.push(`Missing host executable: ${executable}.`);
    return result;
  }
  const execution = await executeHost(
    executable, argumentsList, context.sourceRoot, context.timeoutMs);
  result.host = { executable, arguments: argumentsList, ...execution };
  await fs.writeFile(
    artifacts.hostLogPath, `${JSON.stringify(result.host, null, 2)}\n`, 'utf8');
  if (execution.timedOut) {
    result.failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  } else if (execution.exitCode !== 0) {
    result.failures.push(
      `Host exited with code ${execution.exitCode}${execution.signal ? ` (${execution.signal})` : ''}.`);
  } else {
    try {
      const validation = await validateQuadrantArtifacts(
        definition, scenario, pipeline, backend, artifacts);
      result.validation = {
        snapshot: validation.snapshot,
        imageValidation: validation.imageValidation
      };
      result.failures.push(...validation.failures);
    } catch (error) {
      result.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  result.status = result.failures.length === 0 ? 'pass' : 'fail';
  return result;
}

/** Compares all mandatory same-scenario pipeline/backend quadrant pairs. */
async function compareScenarioQuadrants(definition, scenario, quadrants) {
  const byKey = new Map(
    quadrants.map((quadrant) => [`${quadrant.pipeline}|${quadrant.backend}`, quadrant]));
  const definitions = [
    ['legacy', 'metal', 'experimental', 'metal', 'pipeline-parity-metal'],
    ['legacy', 'vulkan', 'experimental', 'vulkan', 'pipeline-parity-vulkan'],
    ['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy'],
    ['experimental', 'metal', 'experimental', 'vulkan', 'backend-parity-experimental']
  ];
  const comparisons = [];
  for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of definitions) {
    const left = byKey.get(`${leftPipeline}|${leftBackend}`);
    const right = byKey.get(`${rightPipeline}|${rightBackend}`);
    const comparison = {
      case: definition.name,
      scenario: scenario.id,
      relation,
      status: 'fail',
      failures: []
    };
    if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
      comparison.failures.push('Both required quadrants must pass before image comparison.');
    } else {
      const [leftImage, rightImage] = await Promise.all([
        loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
        loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
      ]);
      const imageComparison = compareThreeCaptures(leftImage, rightImage);
      comparison.metrics = imageComparison.metrics;
      comparison.failures.push(...imageComparison.failures);
    }
    comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
    comparisons.push(comparison);
  }
  return comparisons;
}

/** Compares one GVM quadrant against the matching immutable Three r185 Oracle. */
async function compareOracleQuadrant(oracleRoot, definition, scenario, quadrant) {
  const comparison = {
    case: definition.name,
    scenario: scenario.id,
    pipeline: quadrant.pipeline,
    backend: quadrant.backend,
    status: 'fail',
    failures: []
  };
  if (quadrant.status !== 'pass') {
    comparison.failures.push('The GVM quadrant must pass before Oracle comparison.');
  } else {
    try {
      const oracleBase = path.join(oracleRoot, definition.caseId, scenario.id);
      const [oracleImage, actualImage] = await Promise.all([
        loadRgbaArtifact(`${oracleBase}.rgba`, `${oracleBase}.json`),
        loadRgbaArtifact(quadrant.artifacts.rgbaPath, quadrant.artifacts.metadataPath)
      ]);
      const imageComparison = compareThreeCaptures(oracleImage, actualImage);
      comparison.metrics = imageComparison.metrics;
      comparison.failures.push(...imageComparison.failures);
    } catch (error) {
      comparison.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Verifies that frame 60 produces a materially different image from frame zero. */
async function validateAnimationProgression(definition, caseQuadrants) {
  const results = [];
  for (const pipeline of pipelines) {
    for (const backend of backends) {
      const initial = caseQuadrants.find((entry) =>
        entry.pipeline === pipeline && entry.backend === backend && entry.frame === 0);
      const animated = caseQuadrants.find((entry) =>
        entry.pipeline === pipeline && entry.backend === backend && entry.frame === 60);
      const result = {
        case: definition.name,
        pipeline,
        backend,
        status: 'fail',
        failures: []
      };
      if (!initial || !animated || initial.status !== 'pass' || animated.status !== 'pass') {
        result.failures.push('Both locked frames must pass before progression validation.');
      } else {
        const [initialImage, animatedImage] = await Promise.all([
          loadRgbaArtifact(initial.artifacts.rgbaPath, initial.artifacts.metadataPath),
          loadRgbaArtifact(animated.artifacts.rgbaPath, animated.artifacts.metadataPath)
        ]);
        const progression = compareThreeCaptures(initialImage, animatedImage);
        result.metrics = progression.metrics;
        if (progression.metrics.meanAbsoluteRgb < 0.25 ||
            progression.metrics.normalizedDistancePixelRatio < 0.001) {
          result.failures.push('frame 60 does not materially differ from frame zero.');
        }
      }
      result.status = result.failures.length === 0 ? 'pass' : 'fail';
      results.push(result);
    }
  }
  return results;
}

/** Executes the locked r185 shader cases through the complete runtime gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    sourceRoot: path.resolve(scriptDirectory, '..', '..', '..'),
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRoot: options['oracle-root'] ? path.resolve(options['oracle-root']) : '',
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

  const generatedArtifacts = await lintPhase1ShaderCaseArtifacts(context.generatedRoot);
  const quadrants = [];
  const comparisons = [];
  const oracleComparisons = [];
  const progression = [];
  for (const definition of caseDefinitions) {
    const caseQuadrants = [];
    for (const scenario of definition.scenarios) {
      const scenarioQuadrants = [];
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          const result = await runQuadrant(
            context, definition, scenario, pipeline, backend);
          scenarioQuadrants.push(result);
          caseQuadrants.push(result);
          quadrants.push(result);
          console.log(
            `[${result.status.toUpperCase()}] ${definition.name}/${scenario.id} ${pipeline}/${backend}`);
          for (const failure of result.failures) {
            console.log(`  ${failure}`);
          }
        }
      }
      comparisons.push(...await compareScenarioQuadrants(
        definition, scenario, scenarioQuadrants));
      if (context.oracleRoot) {
        for (const quadrant of scenarioQuadrants) {
          oracleComparisons.push(await compareOracleQuadrant(
            context.oracleRoot, definition, scenario, quadrant));
        }
      }
    }
    progression.push(...await validateAnimationProgression(definition, caseQuadrants));
  }

  const status = generatedArtifacts.status === 'pass' &&
    quadrants.every((entry) => entry.status === 'pass') &&
    comparisons.every((entry) => entry.status === 'pass') &&
    oracleComparisons.every((entry) => entry.status === 'pass') &&
    progression.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-phase1-shader-cases',
    status,
    generatedArtifacts,
    quadrants,
    comparisons,
    oracleComparisons,
    progression
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  for (const comparison of comparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${comparison.case}/${comparison.scenario} ${comparison.relation}`);
  }
  for (const comparison of oracleComparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${comparison.case}/${comparison.scenario} Oracle ${comparison.pipeline}/${comparison.backend}`);
  }
  for (const result of progression) {
    console.log(`[${result.status.toUpperCase()}] ${result.case} progression ${result.pipeline}/${result.backend}`);
  }
  console.log(`Phase 1 shader-case report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
