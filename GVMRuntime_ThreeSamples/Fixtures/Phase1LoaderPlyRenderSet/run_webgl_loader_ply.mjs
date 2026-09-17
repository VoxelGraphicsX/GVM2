#!/usr/bin/env node

import { createHash } from 'node:crypto';
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
  executeWithWatchdog,
  validateDeterministicCaptureMetadata,
  validateRenderSetSnapshot,
  validateStructuralSnapshot
} from '../../../tests/runners/three/node/runner.mjs';
import { lintGeneratedArtifacts } from '../../Tools/lint_generated_artifacts.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const manifestPath = path.join(
  repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
const caseId = 'webgl_loader_ply';
const shardName = 'Phase1LoaderPlyRenderSet';
const renderSetType = 'WebglLoaderPlySceneRenderSet';
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const stabilityRunCount = 3;
const lockedRandomSeed = 305419896;
const asciiSha256 = '1246b1050ebc1e2e6b9de796a004bd3914e525546d402a5e286fbe6f13940c8e';
const binarySha256 = '837f769e67155dc4a6e9a90683a61d6f6b4571a2a97d79586831011f450e66e2';
const canonicalSceneSha256 = '5da37adfea8f4e84e2bebc347cf43534e23b55ca6c5ae7019e6006dc1765467f';
const componentSchema = Object.freeze([
  Object.freeze({ name: 'vertices', kind: 'buffer', role: 'vertex' }),
  Object.freeze({ name: 'indices', kind: 'buffer', role: 'index' }),
  Object.freeze({ name: 'objects', kind: 'buffer', role: 'object' }),
  Object.freeze({ name: 'instances', kind: 'buffer', role: 'instance' }),
  Object.freeze({ name: 'materials', kind: 'buffer', role: 'material' }),
  Object.freeze({
    name: 'renderFlags',
    kind: 'buffer',
    role: 'ground-mesh-material-shadow-and-flat-phase'
  })
]);
const scenePasses = Object.freeze([
  Object.freeze({
    name: 'directional-shadow-depth',
    renderClass: 'WebglLoaderPlyShadowDepthPass',
    sceneRoot: 'scene',
    renderSetBindingCount: 1,
    usesStandaloneGeometry: false,
    usesExplicitDrawCount: false
  }),
  Object.freeze({
    name: 'main-lit-fog',
    renderClass: 'WebglLoaderPlyLitPass',
    sceneRoot: 'scene',
    renderSetBindingCount: 1,
    usesStandaloneGeometry: false,
    usesExplicitDrawCount: false
  })
]);
const scenarios = Object.freeze([
  Object.freeze({
    id: 'initial-loader',
    frame: 0,
    canonicalState: 'ground-dolphins-lucy-two-shadow-lights-camera-time-zero',
    cameraPosition: Object.freeze([
      -1.8019819523869463, 0.15, -1.7328765228000897
    ])
  }),
  Object.freeze({
    id: 'canonical-loader',
    frame: 0,
    canonicalState: 'ascii-855-vertices-1689-faces-binary-50002-vertices-100000-faces',
    cameraPosition: Object.freeze([
      -1.8019819523869463, 0.15, -1.7328765228000897
    ])
  }),
  Object.freeze({
    id: 'animated-camera',
    frame: 120,
    canonicalState: 'fixed-step-two-seconds-camera-orbit',
    cameraPosition: Object.freeze([
      -2.4317803181985145, 0.15, 0.5800383470274472
    ])
  })
]);

/** Parses strict value-bearing long options for the focused PLY gate. */
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

/** Resolves one required explicit path option without environment fallback. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one optional positive integer watchdog value. */
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

/** Serializes JSON with recursively sorted object keys for semantic equality. */
function serializeCanonicalJson(value) {
  if (Array.isArray(value)) {
    return `[${value.map(serializeCanonicalJson).join(',')}]`;
  }
  if (value && typeof value === 'object') {
    return `{${Object.keys(value).sort().map((key) => (
      `${JSON.stringify(key)}:${serializeCanonicalJson(value[key])}`
    )).join(',')}}`;
  }
  return JSON.stringify(value);
}

/** Compares expected JSON fields recursively while allowing diagnostic extensions. */
function validateExpectedFields(actual, expected, label, failures) {
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
        } else if (typeof expectedElement === 'number' && !Number.isInteger(expectedElement)) {
          if (typeof actualElement !== 'number' ||
              Math.abs(actualElement - expectedElement) > 1e-9) {
            failures.push(
              `${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${expectedElement}.`);
          }
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
    } else if (typeof expectedValue === 'number' && !Number.isInteger(expectedValue)) {
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-9) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(
        `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Validates the latest formal Manifest, locked assets, binaries, and real r185 Oracles. */
async function validateFixtureInputs(context) {
  const failures = [];
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  if (!example || example.status !== 'phase1_required' ||
      example.renderSetPolicy !== 'required' || example.renderSetType !== renderSetType ||
      example.dslShard !== shardName || example.renderableObjectCount !== 3 ||
      example.loaderRenderableObjectCount !== 2 || example.containsInstancing !== false ||
      example.containsMultipleMaterials !== true) {
    failures.push('Formal Manifest identity differs from the required PLY single-RenderSet contract.');
  }
  if (example && serializeCanonicalJson(example.componentSchema) !==
      serializeCanonicalJson(componentSchema)) {
    failures.push('Formal Manifest componentSchema differs from the six-component PLY ABI.');
  }
  if (example && serializeCanonicalJson(example.scenePasses) !==
      serializeCanonicalJson(scenePasses)) {
    failures.push('Formal Manifest Scene pass declarations differ from the two-pass PLY contract.');
  }
  if (example && serializeCanonicalJson(example.screenPasses) !==
      serializeCanonicalJson([])) {
    failures.push('Formal Manifest must declare no screen pass for direct single-sample rendering.');
  }
  for (const scenario of scenarios) {
    const manifestScenario = example?.scenarios?.find((candidate) => candidate.id === scenario.id);
    if (!manifestScenario || manifestScenario.frame !== scenario.frame ||
        manifestScenario.canonicalState !== scenario.canonicalState) {
      failures.push(`${caseId}/${scenario.id}: scenario differs from the latest formal Manifest.`);
    }
    for (const extension of ['rgba', 'json']) {
      const oraclePath = path.join(context.oracleRoot, caseId, `${scenario.id}.${extension}`);
      if (!await pathExists(oraclePath)) {
        failures.push(`Missing real Three r185 Oracle artifact: ${oraclePath}.`);
      }
    }
  }
  const assetExpectations = [
    [path.join(context.assetRoot, 'models', 'ply', 'ascii', 'dolphins.ply'), asciiSha256],
    [path.join(context.assetRoot, 'models', 'ply', 'binary', 'Lucy100k.ply'), binarySha256]
  ];
  const assetIdentities = [];
  for (const [assetPath, expectedSha256] of assetExpectations) {
    if (!await pathExists(assetPath)) {
      failures.push(`Missing locked r185 PLY asset: ${assetPath}.`);
      continue;
    }
    const sha256 = await sha256File(assetPath);
    assetIdentities.push({ path: assetPath, sha256 });
    if (sha256 !== expectedSha256) {
      failures.push(`${assetPath}: SHA-256 ${sha256}, expected ${expectedSha256}.`);
    }
  }
  for (const pipeline of pipelines) {
    const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
    if (!await pathExists(executable)) {
      failures.push(`Missing ${pipeline} PLY host binary: ${executable}.`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    manifestPath,
    example,
    assetIdentities,
    failures
  };
}

/** Creates deterministic artifact paths for one scenario quadrant repetition. */
function makeArtifactPaths(context, scenario, pipeline, backend, repetition) {
  const artifactDirectory = path.join(
    context.outputRoot, 'artifacts', scenario.id, pipeline, backend, `run-${repetition}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    semanticPath: path.join(artifactDirectory, 'semantic.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the explicit host CLI without runtime environment configuration. */
function buildHostArguments(context, scenario, pipeline, backend, artifacts) {
  return [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(lockedRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--asset-root', context.assetRoot,
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath,
    '--semantic-snapshot', artifacts.semanticPath,
    '--canonical-state', scenario.canonicalState
  ];
}

/** Returns the exact canonical loader semantic document required from the C++ parser. */
function expectedLoaderSemantic() {
  return {
    schemaVersion: 1,
    caseId,
    scenarioId: 'canonical-loader',
    frame: 0,
    kind: 'loader-snapshot',
    canonicalState: 'ascii-855-vertices-1689-faces-binary-50002-vertices-100000-faces',
    result: {
      renderableObjectCount: 2,
      sceneRootCount: 1,
      canonicalSceneSha256,
      ascii: {
        encoding: 'ascii',
        vertexCount: 855,
        faceCount: 1689,
        sha256: asciiSha256
      },
      binary: {
        encoding: 'binary_little_endian',
        vertexCount: 50002,
        faceCount: 100000,
        sha256: binarySha256
      }
    }
  };
}

/** Validates one run's Oracle image, metadata, topology, parser evidence, and semantic sidecar. */
async function validateRunArtifacts(context, scenario, pipeline, backend, artifacts) {
  const manifestScenario = context.inputValidation.example.scenarios.find(
    (candidate) => candidate.id === scenario.id);
  const [actual, oracle, snapshot] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    loadRgbaArtifact(
      path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      path.join(context.oracleRoot, caseId, `${scenario.id}.json`)),
    fs.readFile(artifacts.snapshotPath, 'utf8').then(JSON.parse)
  ]);
  const comparison = compareThreeCaptures(oracle, actual);
  const failures = [
    ...validateDeterministicCaptureMetadata(
      context.inputValidation.example,
      manifestScenario,
      actual.metadata,
      oracle.metadata,
      pipeline,
      backend),
    ...validateStructuralSnapshot(context.inputValidation.example, manifestScenario, snapshot),
    ...validateRenderSetSnapshot(context.inputValidation.example, snapshot, manifestScenario),
    ...comparison.failures
  ];
  validateExpectedFields(snapshot, {
    renderSetPolicy: 'required',
    sceneRenderSetCount: 1,
    renderableObjectCount: 3,
    entityCount: 3,
    instanceCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    materialCount: 2,
    scenePassCount: 2,
    screenPassCount: 0,
    screenPasses: [],
    drawCommandCount: 3,
    directDrawFallback: false,
    antialiasResolve: 'disabled-single-sample',
    shadowMapCount: 2,
    shadowMapSize: [1024, 1024],
    shadowBias: -0.001,
    cameraFovDegrees: 35,
    cameraNear: 1,
    cameraFar: 15,
    cameraPosition: scenario.cameraPosition,
    asciiPly: {
      format: 'ascii', vertexCount: 855, faceCount: 1689, sha256: asciiSha256
    },
    binaryPly: {
      format: 'binary_little_endian',
      vertexCount: 50002,
      faceCount: 100000,
      sha256: binarySha256
    },
    normalGeneration: 'three-r185-computeVertexNormals',
    gpuWorkDslOnly: true
  }, 'snapshot', failures);
  let semantic = null;
  if (scenario.id === 'canonical-loader') {
    semantic = JSON.parse(await fs.readFile(artifacts.semanticPath, 'utf8'));
    if (serializeCanonicalJson(semantic) !== serializeCanonicalJson(expectedLoaderSemantic())) {
      failures.push('Canonical loader semantic sidecar differs from the exact locked parser result.');
    }
  } else if (await pathExists(artifacts.semanticPath)) {
    failures.push(`${scenario.id}: non-loader scenario unexpectedly emitted a semantic sidecar.`);
  }
  let nonOpaquePixels = 0;
  const colors = new Set();
  for (let offset = 0; offset < actual.pixels.length; offset += 4) {
    colors.add((actual.pixels[offset] << 16) |
      (actual.pixels[offset + 1] << 8) | actual.pixels[offset + 2]);
    if (actual.pixels[offset + 3] !== 255) nonOpaquePixels += 1;
  }
  if (colors.size < 16) failures.push('Capture is black, clear-only, or lacks expected shaded diversity.');
  if (nonOpaquePixels !== 0) failures.push(`Capture contains ${nonOpaquePixels} non-opaque pixels.`);
  return {
    oracleMetrics: comparison.metrics,
    snapshot,
    semantic,
    imageValidation: { uniqueRgbColorCount: colors.size, nonOpaquePixels },
    failures
  };
}

/** Executes and validates one quadrant repetition under a hard watchdog. */
async function runQuadrantRepetition(context, scenario, pipeline, backend, repetition) {
  const artifacts = makeArtifactPaths(context, scenario, pipeline, backend, repetition);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
  const argumentsList = buildHostArguments(
    context, scenario, pipeline, backend, artifacts);
  const processResult = await executeWithWatchdog(executable, argumentsList, {
    cwd: repositoryRoot,
    timeoutMs: context.timeoutMs
  });
  await fs.writeFile(artifacts.hostLogPath, `${JSON.stringify({
    executable,
    arguments: argumentsList,
    ...processResult
  }, null, 2)}\n`);
  const failures = [];
  if (processResult.timedOut) failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  if (processResult.exitCode !== 0) {
    failures.push(`Host exited code=${processResult.exitCode} signal=${processResult.signal}.`);
  }
  let validation = null;
  if (failures.length === 0) {
    try {
      validation = await validateRunArtifacts(
        context, scenario, pipeline, backend, artifacts);
      failures.push(...validation.failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  return {
    scenarioId: scenario.id,
    pipeline,
    backend,
    repetition,
    status: failures.length === 0 ? 'pass' : 'fail',
    executable,
    arguments: argumentsList,
    process: processResult,
    artifacts,
    oracleMetrics: validation?.oracleMetrics ?? null,
    imageValidation: validation?.imageValidation ?? null,
    failures
  };
}

/** Loads one completed run's capture image. */
async function loadRunImage(run) {
  return loadRgbaArtifact(run.artifacts.rgbaPath, run.artifacts.metadataPath);
}

/** Builds byte-exact three-run stability evidence for one scenario quadrant. */
async function buildStabilityComparison(runs) {
  const failures = [];
  const comparisons = [];
  if (runs.length !== stabilityRunCount || runs.some((run) => run.status !== 'pass')) {
    failures.push('Three successful repetitions are required before exact stability comparison.');
  } else {
    const baselineRgbaSha256 = await sha256File(runs[0].artifacts.rgbaPath);
    const baselineSnapshotSha256 = await sha256File(runs[0].artifacts.snapshotPath);
    for (let index = 1; index < runs.length; index += 1) {
      const rgbaSha256 = await sha256File(runs[index].artifacts.rgbaPath);
      const snapshotSha256 = await sha256File(runs[index].artifacts.snapshotPath);
      const comparisonFailures = [];
      if (rgbaSha256 !== baselineRgbaSha256) {
        comparisonFailures.push(`run ${index + 1} RGBA SHA-256 differs from run 1.`);
      }
      if (snapshotSha256 !== baselineSnapshotSha256) {
        comparisonFailures.push(`run ${index + 1} structural snapshot differs from run 1.`);
      }
      if (runs[0].scenarioId === 'canonical-loader') {
        const baselineSemanticSha256 = await sha256File(runs[0].artifacts.semanticPath);
        const semanticSha256 = await sha256File(runs[index].artifacts.semanticPath);
        if (semanticSha256 !== baselineSemanticSha256) {
          comparisonFailures.push(`run ${index + 1} semantic snapshot differs from run 1.`);
        }
      }
      failures.push(...comparisonFailures);
      comparisons.push({
        relation: `run-1-vs-run-${index + 1}`,
        status: comparisonFailures.length === 0 ? 'pass' : 'fail',
        rgbaSha256,
        snapshotSha256,
        failures: comparisonFailures
      });
    }
    return {
      scenarioId: runs[0].scenarioId,
      pipeline: runs[0].pipeline,
      backend: runs[0].backend,
      status: failures.length === 0 ? 'pass' : 'fail',
      baselineRgbaSha256,
      baselineSnapshotSha256,
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

/** Compares one same-repetition quadrant pair under the immutable image thresholds. */
async function compareParity(relation, leftRun, rightRun) {
  const [left, right] = await Promise.all([loadRunImage(leftRun), loadRunImage(rightRun)]);
  const comparison = compareThreeCaptures(left, right);
  return {
    scenarioId: leftRun.scenarioId,
    repetition: leftRun.repetition,
    relation,
    status: comparison.failures.length === 0 ? 'pass' : 'fail',
    metrics: comparison.metrics,
    failures: comparison.failures
  };
}

/** Runs all 36 captures, generic lints, stability checks, parity checks, and report output. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 120_000, '--timeout-ms')
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  context.inputValidation = await validateFixtureInputs(context);
  const [generatedLintResult, gpuBoundaryLint] = await Promise.all([
    Promise.resolve(lintGeneratedArtifacts({
      legacyRoot: path.join(context.generatedRoot, 'legacy'),
      experimentalRoot: path.join(context.generatedRoot, 'experimental'),
      manifestPath,
      caseIds: [caseId],
      fixtureSelectors: [],
      instancingPasses: []
    })),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const generatedArtifactLint = {
    ...generatedLintResult,
    status: generatedLintResult.errors.length === 0 ? 'pass' : 'fail'
  };
  const runs = [];
  if (context.inputValidation.status === 'pass' &&
      generatedArtifactLint.status === 'pass' && gpuBoundaryLint.status === 'pass') {
    for (const scenario of scenarios) {
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          for (let repetition = 1; repetition <= stabilityRunCount; repetition += 1) {
            const run = await runQuadrantRepetition(
              context, scenario, pipeline, backend, repetition);
            runs.push(run);
            console.log(
              `[${run.status.toUpperCase()}] ${scenario.id} ${pipeline}/${backend} ` +
              `run ${repetition}/${stabilityRunCount}`);
            for (const failure of run.failures) console.log(`  ${failure}`);
          }
        }
      }
    }
  }
  const stabilityComparisons = [];
  for (const scenario of scenarios) {
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        stabilityComparisons.push(await buildStabilityComparison(runs.filter((run) =>
          run.scenarioId === scenario.id && run.pipeline === pipeline && run.backend === backend)));
      }
    }
  }
  const parityDefinitions = [
    ['pipeline-parity-metal', 'legacy', 'metal', 'experimental', 'metal'],
    ['pipeline-parity-vulkan', 'legacy', 'vulkan', 'experimental', 'vulkan'],
    ['backend-parity-legacy', 'legacy', 'metal', 'legacy', 'vulkan'],
    ['backend-parity-experimental', 'experimental', 'metal', 'experimental', 'vulkan']
  ];
  const parityComparisons = [];
  for (const scenario of scenarios) {
    for (let repetition = 1; repetition <= stabilityRunCount; repetition += 1) {
      for (const [relation, leftPipeline, leftBackend, rightPipeline, rightBackend] of
        parityDefinitions) {
        const left = runs.find((run) => run.scenarioId === scenario.id &&
          run.repetition === repetition && run.pipeline === leftPipeline &&
          run.backend === leftBackend);
        const right = runs.find((run) => run.scenarioId === scenario.id &&
          run.repetition === repetition && run.pipeline === rightPipeline &&
          run.backend === rightBackend);
        if (left && right && left.status === 'pass' && right.status === 'pass') {
          parityComparisons.push(await compareParity(relation, left, right));
        }
      }
    }
  }
  const status = context.inputValidation.status === 'pass' &&
    generatedArtifactLint.status === 'pass' && gpuBoundaryLint.status === 'pass' &&
    runs.length === scenarios.length * pipelines.length * backends.length * stabilityRunCount &&
    runs.every((run) => run.status === 'pass') &&
    stabilityComparisons.length === scenarios.length * pipelines.length * backends.length &&
    stabilityComparisons.every((entry) => entry.status === 'pass') &&
    parityComparisons.length === scenarios.length * stabilityRunCount * parityDefinitions.length &&
    parityComparisons.every((entry) => entry.status === 'pass')
      ? 'pass'
      : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'webgl-loader-ply-four-quadrant-oracle',
    status,
    caseId,
    thresholds: comparisonThresholds,
    stabilityRunCount,
    expectedCaptureCount: 36,
    inputValidation: {
      status: context.inputValidation.status,
      manifestPath: context.inputValidation.manifestPath,
      assetIdentities: context.inputValidation.assetIdentities,
      failures: context.inputValidation.failures
    },
    generatedArtifactLint,
    gpuBoundaryLint,
    runs,
    stabilityComparisons,
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
