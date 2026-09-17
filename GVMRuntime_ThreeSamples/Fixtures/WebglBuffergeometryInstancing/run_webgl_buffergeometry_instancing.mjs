#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import {
  validateRenderSetSnapshot,
  validateStructuralSnapshot
} from '../../../tests/runners/three/node/runner.mjs';
import { lintSampleGpuBoundary } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgl_buffergeometry_instancing';
const shardName = 'WebglBuffergeometryInstancing';
const renderSetType = 'WebglBuffergeometryInstancingSceneRenderSet';
const scenePassName = 'WebglBuffergeometryInstancingMainPass';
const randomSeed = 0x18500006;
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const repeatCount = 3;
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const upstreamSourceSha256 = 'e26c4b93f76503b7baffed2a61a9082dfd18b9996e5ca03b578687d250efc095';
const replaySha256 = '86d78787cea1280f621080011ac73972fada9640799c13f350df8c756e2a8f18';
const stateScriptSha256 = 'adb3fc1931e687495ccab28b58894edc503f00dbe47a26d68325cd539106ce82';

const componentSchema = Object.freeze([
  Object.freeze({ name: 'vertices', kind: 'buffer', role: 'vertex' }),
  Object.freeze({ name: 'indices', kind: 'buffer', role: 'index' }),
  Object.freeze({ name: 'objects', kind: 'buffer', role: 'object' }),
  Object.freeze({ name: 'instances', kind: 'buffer', role: 'instance' }),
  Object.freeze({ name: 'materials', kind: 'buffer', role: 'material' })
]);

export const instancingScenarios = Object.freeze([
  Object.freeze({
    id: 'initial',
    frame: 0,
    instanceCount: 50_000,
    canonicalState: 'seed-0x18500006-instance-count-50000-time-zero',
    replay: false,
    activeCountMutation: 'none',
    entityReallocated: false,
    virtualTimeMilliseconds: 0
  }),
  Object.freeze({
    id: 'animated',
    frame: 60,
    instanceCount: 50_000,
    canonicalState: 'seed-0x18500006-fixed-step-60hz-one-second',
    replay: false,
    activeCountMutation: 'none',
    entityReallocated: false,
    virtualTimeMilliseconds: 999.9999999999991
  }),
  Object.freeze({
    id: 'reduced-count',
    frame: 61,
    instanceCount: 12_500,
    canonicalState: 'gui-instance-count-12500',
    replay: true,
    activeCountMutation: 'remove-reallocate-same-render-set',
    entityReallocated: true,
    virtualTimeMilliseconds: 1016.6666666666657
  })
]);

const lockedOracleSha256 = Object.freeze({
  initial: Object.freeze({
    rgba: '184440a84465a49e1eaeaedbcd060efc945d241abec55471eafe064712526d8c',
    json: 'd805addacf95ccdfd64eae6d44b760cd939afa34d1c96d3e01e33ad71f9210b8'
  }),
  animated: Object.freeze({
    rgba: '49c31cc8703230aa04bc47083e8818bcce4da189e0c74bbbba4880b41b2613f1',
    json: 'cc0adcce7a989c628bc7bb98e5fcac5a70091e045044d87d5d66f1096c94a3e3'
  }),
  'reduced-count': Object.freeze({
    rgba: '83b139461e9e1575eff3785a9d3f55bca210b56501620559f11367287b1ade4c',
    json: '0a2464faa40761711573d789424378629b4be061fa7b7230555d36f6d5b044e9'
  })
});

export const parityDefinitions = Object.freeze([
  Object.freeze({ relation: 'pipeline-parity-metal', left: ['legacy', 'metal'], right: ['experimental', 'metal'] }),
  Object.freeze({ relation: 'pipeline-parity-vulkan', left: ['legacy', 'vulkan'], right: ['experimental', 'vulkan'] }),
  Object.freeze({ relation: 'backend-parity-legacy', left: ['legacy', 'metal'], right: ['legacy', 'vulkan'] }),
  Object.freeze({ relation: 'backend-parity-experimental', left: ['experimental', 'metal'], right: ['experimental', 'vulkan'] })
]);

/** Parses strict unique --option value pairs without environment fallbacks. */
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

/** Resolves one required explicit path option. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one optional positive integer watchdog value. */
function parsePositiveInteger(value, fallback, label) {
  if (value == null) return fallback;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) throw new Error(`${label} must be a positive integer.`);
  return parsed;
}

/** Returns true when one explicit path is readable. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Returns one file's lowercase SHA-256 identity. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
}

/** Validates nested expected fields while permitting diagnostic extensions. */
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
        } else if (typeof expectedElement === 'number' && !Number.isInteger(expectedElement)) {
          if (typeof actualElement !== 'number' || Math.abs(actualElement - expectedElement) > 1e-6) {
            failures.push(`${fieldLabel}[${index}]=${JSON.stringify(actualElement)}, expected ${expectedElement}.`);
          }
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

/** Builds the exact host CLI for one deterministic scenario quadrant. */
export function buildHostArguments(scenario, pipeline, backend, assetRoot, replayPath, artifacts) {
  const argumentsList = [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(randomSeed),
    '--asset-root', assetRoot,
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
  if (scenario.replay) argumentsList.push('--input-replay', replayPath);
  return argumentsList;
}

/** Builds the runtime fields that must remain invariant in every quadrant. */
export function buildExpectedSnapshot(scenario) {
  return {
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    renderSetPolicy: 'required',
    gpuWorkDslOnly: true,
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
    scenePassSequence: [{ sceneRoot: 'scene', scenePass: 'main-instanced', entityOrdinal: 0 }],
    drawCommandCount: 1,
    directDrawFallback: false,
    vertexCount: 3,
    indexCount: 3,
    triangleCount: 1,
    componentSchema: ['vertices', 'indices', 'objects', 'instances', 'materials'],
    randomDrawsBeforeInstances: 84,
    randomDrawCount: 750_084,
    finalRandomState: 305_162_645,
    virtualTimeMilliseconds: scenario.virtualTimeMilliseconds,
    activeCountMutation: scenario.activeCountMutation,
    entityReallocated: scenario.entityReallocated
  };
}

/** Verifies the formal required single-RenderSet instancing contract. */
export function validateManifestContract(example) {
  const failures = [];
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_buffergeometry_instancing.html',
    status: 'phase1_required',
    renderSetPolicy: 'required',
    renderSetReasons: ['instancing'],
    renderableObjectCount: 1,
    containsInstancing: true,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    screenPasses: [],
    renderSetType,
    componentSchema,
    dslShard: shardName
  }, 'manifest', failures);
  if (example?.sceneRoots?.length !== 1 || example.sceneRoots[0]?.name !== 'scene' ||
      example.sceneRoots[0]?.renderSetRuntimeInstanceCount !== 1 ||
      example.sceneRoots[0]?.renderSetType !== renderSetType) {
    failures.push('manifest.sceneRoots must declare exactly one scene and one RenderSet instance.');
  }
  const pass = example?.scenePasses?.[0];
  if (example?.scenePasses?.length !== 1 || pass?.name !== 'main-instanced' ||
      pass?.renderClass !== scenePassName || pass?.sceneRoot !== 'scene' ||
      pass?.renderSetBindingCount !== 1 || pass?.usesStandaloneGeometry !== false ||
      pass?.usesExplicitDrawCount !== false) {
    failures.push('manifest.scenePasses must declare the sole RenderSet-only main-instanced pass.');
  }
  for (const scenario of instancingScenarios) {
    const manifestScenario = example?.scenarios?.find((candidate) => candidate.id === scenario.id);
    if (!manifestScenario || manifestScenario.frame !== scenario.frame ||
        manifestScenario.canonicalState !== scenario.canonicalState ||
        Boolean(manifestScenario.inputReplay) !== scenario.replay) {
      failures.push(`manifest.scenarios.${scenario.id} differs from the locked fixture state.`);
    }
  }
  return failures;
}

/** Validates one Oracle metadata sidecar against the independent reference capture. */
export function validateOracleMetadata(metadata, scenario) {
  const failures = [];
  validateExpectedFields(metadata, {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    randomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: scenario.replay ? {
      schemaVersion: 1,
      caseId,
      scenarioId: 'reduced-count',
      captureFrame: 61,
      sha256: replaySha256,
      target: '#container > canvas',
      eventCount: 2,
      lastEventFrame: 0
    } : null
  }, `oracle.${scenario.id}`, failures);
  return failures;
}

/** Validates one locked Oracle SHA-256 value. */
export function validateOracleSha256(scenarioId, extension, actualSha256) {
  const expected = lockedOracleSha256[scenarioId]?.[extension];
  if (!expected) throw new Error(`No locked Oracle SHA-256 for ${scenarioId}.${extension}.`);
  return actualSha256 === expected
    ? []
    : [`oracle.${scenarioId}.${extension}.sha256=${actualSha256}, expected ${expected}.`];
}

/** Extracts RenderSet component handles from one generated exports header. */
function extractComponentHandles(exportsSource, failures, label) {
  const handles = {};
  for (const component of componentSchema) {
    const match = new RegExp(`RenderComponentHandle\\s+${component.name}\\s*=\\s*(\\d+)`, 'u').exec(exportsSource);
    if (!match) failures.push(`${label}: exports omit component '${component.name}'.`);
    else handles[component.name] = Number(match[1]);
  }
  return handles;
}

/** Lints one generated pipeline for the frozen single-RenderSet indexed-indirect ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, exportsSource, singleHeader) {
  const failures = [];
  const label = `${shardName}/${pipeline}`;
  const renderSetExports = exportsSource.match(/static constexpr uint64_t\s+sceneSet\s*=\s*\d+/gu) ?? [];
  if (renderSetExports.length !== 1) failures.push(`${label}: expected exactly one exported Scene RenderSet.`);
  if (!exportsSource.includes(`namespace ${renderSetType}Components`)) {
    failures.push(`${label}: component export namespace is missing.`);
  }
  const handles = extractComponentHandles(exportsSource, failures, label);
  const requiredGeneratedTokens = [
    `createRenderSet<${renderSetType}>()`,
    `class ${scenePassName}`,
    'this->mRenderSet = sceneSet',
    'this->mRenderSetBindGroupIndex = 0',
    'scenePass->run()',
    'renderPass("main-instanced"',
    'GVM::RHI::BlendFactor::SrcAlpha',
    'GVM::RHI::BlendFactor::OneMinusSrcAlpha'
  ];
  for (const token of requiredGeneratedTokens) {
    if (!generatedSource.includes(token)) failures.push(`${label}: generated source omits '${token}'.`);
  }
  if (/scenePass->run\([^)]*[0-9][^)]*\)/u.test(generatedSource)) {
    failures.push(`${label}: Scene pass uses an explicit draw count.`);
  }
  if (generatedSource.includes('scenePass->setVertexBuffer(')) {
    failures.push(`${label}: Scene pass binds standalone geometry.`);
  }
  for (const name of ['objects', 'instances', 'materials']) {
    const legacyRead = generatedSource.includes(`sceneSet_${name}_UGLGetSafe`);
    const experimentalRead = generatedSource.includes(`sceneSet->${name}[`) &&
      generatedSource.includes(`${name}IndexTable`);
    if (!legacyRead && !experimentalRead) failures.push(`${label}: ${name} BufferComponent::get lowering is missing.`);
  }
  if (pipeline === 'legacy' &&
      (!generatedSource.includes('RenderEntityCMDParams') ||
       !generatedSource.includes('UGLLoadRenderEntityCMDParamsSafe'))) {
    failures.push(`${label}: Legacy RenderEntity indexed-indirect metadata is missing.`);
  }
  if (pipeline === 'experimental' &&
      (!generatedSource.includes('CommandParams') || !generatedSource.includes('DrawInfo') ||
       !generatedSource.includes('__uglc_draw_command_params') ||
       !generatedSource.includes('vertexShaderArtifact_SpirvWords') ||
       !generatedSource.includes('fragmentShaderArtifact_SpirvWords'))) {
    failures.push(`${label}: Experimental direct-SPIR-V RenderSet metadata is missing.`);
  }
  for (const token of [
    'uint renderEntityID [[RenderEntityID]]',
    'uint renderEntityInstanceID [[RenderEntityInstanceID]]',
    'sceneSet->instances->get(renderEntityID, renderEntityInstanceID)',
    'scenePass()'
  ]) {
    if (!singleHeader.includes(token)) failures.push(`${label}: merged DSL omits '${token}'.`);
  }
  return {
    pipeline,
    status: failures.length === 0 ? 'pass' : 'fail',
    abiSignature: { renderSetExportCount: renderSetExports.length, componentHandles: handles },
    failures
  };
}

/** Verifies Legacy and Experimental expose identical RenderSet handles. */
export function validateGeneratedAbiParity(entries) {
  const failures = [];
  if (entries.length !== 2 || entries.some((entry) => entry.status !== 'pass')) {
    failures.push('Both generated pipelines must pass before ABI comparison.');
  } else if (JSON.stringify(entries[0].abiSignature) !== JSON.stringify(entries[1].abiSignature)) {
    failures.push('Legacy and Experimental RenderSet ABI signatures differ.');
  }
  return { status: failures.length === 0 ? 'pass' : 'fail', failures };
}

/** Validates Experimental UGLIR, MSL, and direct-SPIR-V stage products. */
export function validateExperimentalProductSources(products) {
  const failures = [];
  for (const stage of ['vertex', 'fragment']) {
    const product = products[stage];
    let json = null;
    try {
      json = JSON.parse(product.uglirJson);
    } catch {
      failures.push(`${stage}: UGLIR JSON is invalid.`);
    }
    if (json?.reflection?.stage !== stage) failures.push(`${stage}: UGLIR reflection stage differs.`);
    if (!product.uglirText.includes(`stage ${stage}`)) failures.push(`${stage}: UGLIR text stage is missing.`);
    if (!product.msl.includes(stage === 'vertex' ? 'vertex ' : 'fragment ')) failures.push(`${stage}: MSL entry is missing.`);
    if (!product.spirvWords.includes('0x07230203')) failures.push(`${stage}: SPIR-V word magic is missing.`);
    if (!product.spirvAssembly.includes(stage === 'vertex' ? 'OpEntryPoint Vertex' : 'OpEntryPoint Fragment')) {
      failures.push(`${stage}: SPIR-V entry point is missing.`);
    }
  }
  return failures;
}

/** Loads and lints both generated output families. */
async function lintGeneratedArtifacts(context) {
  const entries = [];
  for (const pipeline of pipelines) {
    const root = path.join(context.generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    const paths = {
      generated: path.join(root, 'generate_result.hpp'),
      exports: path.join(root, 'exports.hpp'),
      single: path.join(root, 'dsl_single_header.hpp')
    };
    for (const [name, targetPath] of Object.entries(paths)) {
      if (!await pathExists(targetPath)) failures.push(`${pipeline}: missing ${name} artifact ${targetPath}.`);
    }
    let entry;
    if (failures.length === 0) {
      entry = validateGeneratedSourceContract(
        pipeline,
        await fs.readFile(paths.generated, 'utf8'),
        await fs.readFile(paths.exports, 'utf8'),
        await fs.readFile(paths.single, 'utf8'));
    } else {
      entry = { pipeline, status: 'fail', abiSignature: {}, failures };
    }
    entry.generatedRoot = root;
    if (pipeline === 'experimental') {
      const base = `${scenePassName}__`;
      const products = {};
      for (const stage of ['vertex', 'fragment']) {
        const productPaths = {
          uglirJson: path.join(root, 'uglir', `${base}${stage}.uglir.json`),
          uglirText: path.join(root, 'uglir', `${base}${stage}.uglir.txt`),
          msl: path.join(root, 'msl', `${base}${stage}.msl`),
          spirvWords: path.join(root, 'spv', `${base}${stage}.spv.txt`),
          spirvAssembly: path.join(root, 'spv', `${base}${stage}.spvasm`)
        };
        products[stage] = {};
        for (const [name, targetPath] of Object.entries(productPaths)) {
          products[stage][name] = await pathExists(targetPath) ? await fs.readFile(targetPath, 'utf8') : '';
        }
      }
      entry.failures.push(...validateExperimentalProductSources(products));
      entry.status = entry.failures.length === 0 ? 'pass' : 'fail';
    }
    entries.push(entry);
  }
  const abiParity = validateGeneratedAbiParity(entries);
  return {
    status: entries.every((entry) => entry.status === 'pass') && abiParity.status === 'pass' ? 'pass' : 'fail',
    pipelines: entries,
    abiParity,
    failures: [...entries.flatMap((entry) => entry.failures), ...abiParity.failures]
  };
}

/** Inspects one RGBA8 image for visible opaque output. */
export function inspectRgbaPixels(pixels) {
  if (!(pixels instanceof Uint8Array) || pixels.length % 4 !== 0) {
    throw new Error('Capture is not tightly packed RGBA8.');
  }
  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const colors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const red = pixels[offset];
    const green = pixels[offset + 1];
    const blue = pixels[offset + 2];
    if (red !== 0 || green !== 0 || blue !== 0) nonBlackPixels += 1;
    if (pixels[offset + 3] !== 255) nonOpaquePixels += 1;
    colors.add((red << 16) | (green << 8) | blue);
  }
  return { nonBlackPixels, nonOpaquePixels, uniqueRgbColorCount: colors.size };
}

/** Creates deterministic artifact paths for one quadrant repetition. */
function makeArtifactPaths(context, scenario, pipeline, backend, repetition) {
  const artifactDirectory = path.join(context.outputRoot, 'artifacts', scenario.id, pipeline, backend, `run-${repetition}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'capture.rgba'),
    metadataPath: path.join(artifactDirectory, 'capture.json'),
    snapshotPath: path.join(artifactDirectory, 'scene.snapshot.json'),
    stdoutPath: path.join(artifactDirectory, 'stdout.log'),
    stderrPath: path.join(artifactDirectory, 'stderr.log')
  };
}

/** Runs one host process under a hard watchdog. */
async function runHostProcess(executable, argumentsList, timeoutMs) {
  const startedAt = Date.now();
  return new Promise((resolve) => {
    const child = spawn(executable, argumentsList, { cwd: repositoryRoot, shell: false, stdio: ['ignore', 'pipe', 'pipe'] });
    const stdout = [];
    const stderr = [];
    let timedOut = false;
    const watchdog = setTimeout(() => {
      timedOut = true;
      child.kill('SIGKILL');
    }, timeoutMs);
    child.stdout.on('data', (chunk) => stdout.push(chunk));
    child.stderr.on('data', (chunk) => stderr.push(chunk));
    child.once('error', (error) => {
      clearTimeout(watchdog);
      resolve({ exitCode: null, signal: null, timedOut, durationMs: Date.now() - startedAt,
        stdout: Buffer.concat(stdout).toString('utf8'), stderr: `${Buffer.concat(stderr).toString('utf8')}\n${error.message}` });
    });
    child.once('close', (exitCode, signal) => {
      clearTimeout(watchdog);
      resolve({ exitCode, signal, timedOut, durationMs: Date.now() - startedAt,
        stdout: Buffer.concat(stdout).toString('utf8'), stderr: Buffer.concat(stderr).toString('utf8') });
    });
  });
}

/** Validates metadata, structure, and visible output from one host repetition. */
async function validateRunArtifacts(context, scenario, pipeline, backend, artifacts) {
  const failures = [];
  const [actual, snapshot, oracle] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8').then(JSON.parse),
    loadRgbaArtifact(
      path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      path.join(context.oracleRoot, caseId, `${scenario.id}.json`))
  ]);
  validateExpectedFields(actual.metadata, {
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    frame: scenario.frame,
    randomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: scenario.replay ? {
      schemaVersion: 1,
      sha256: replaySha256,
      caseId,
      scenarioId: 'reduced-count',
      captureFrame: 61,
      eventCount: 2,
      lastEventFrame: 0,
      target: '#container > canvas'
    } : null
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);
  failures.push(...validateStructuralSnapshot(context.example, context.manifestScenarios.get(scenario.id), snapshot));
  failures.push(...validateRenderSetSnapshot(context.example, snapshot, context.manifestScenarios.get(scenario.id)));
  const inspection = inspectRgbaPixels(actual.pixels);
  if (inspection.nonBlackPixels === 0 || inspection.uniqueRgbColorCount < 2) failures.push('Capture is black or clear-only.');
  if (inspection.nonOpaquePixels !== 0) failures.push(`Capture contains ${inspection.nonOpaquePixels} non-opaque pixels.`);
  const comparison = compareThreeCaptures(oracle, actual);
  failures.push(...comparison.failures.map((failure) => `Oracle: ${failure}`));
  return { actual, snapshot, inspection, oracleComparison: comparison, failures };
}

/** Runs and validates one scenario/pipeline/backend repetition. */
async function runQuadrantRepetition(context, scenario, pipeline, backend, repetition) {
  const artifacts = makeArtifactPaths(context, scenario, pipeline, backend, repetition);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
  const argumentsList = buildHostArguments(scenario, pipeline, backend, context.assetRoot, context.replayPath, artifacts);
  const processResult = await runHostProcess(executable, argumentsList, context.timeoutMs);
  await Promise.all([
    fs.writeFile(artifacts.stdoutPath, processResult.stdout),
    fs.writeFile(artifacts.stderrPath, processResult.stderr)
  ]);
  const failures = [];
  if (processResult.timedOut) failures.push(`Host exceeded ${context.timeoutMs} ms watchdog.`);
  if (processResult.exitCode !== 0) failures.push(`Host exited code=${processResult.exitCode} signal=${processResult.signal}.`);
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
    repetition,
    status: failures.length === 0 ? 'pass' : 'fail',
    executable,
    arguments: argumentsList,
    process: processResult,
    artifacts,
    inspection: validation?.inspection ?? null,
    oracleMetrics: validation?.oracleComparison.metrics ?? null,
    rgbaSha256: failures.length === 0 ? await sha256File(artifacts.rgbaPath) : null,
    failures
  };
}

/** Loads one successful repetition image. */
async function loadRunImage(run) {
  return loadRgbaArtifact(run.artifacts.rgbaPath, run.artifacts.metadataPath);
}

/** Compares all three repetitions byte-exactly and under fixed image thresholds. */
async function compareStability(runs) {
  const failures = [];
  const comparisons = [];
  if (runs.length !== repeatCount || runs.some((run) => run.status !== 'pass')) {
    failures.push('Three successful repetitions are required for stability.');
  } else {
    const baseline = await loadRunImage(runs[0]);
    for (let index = 1; index < runs.length; index += 1) {
      const candidate = await loadRunImage(runs[index]);
      const comparison = compareThreeCaptures(baseline, candidate);
      const digestEqual = runs[index].rgbaSha256 === runs[0].rgbaSha256;
      if (!digestEqual) failures.push(`Run ${index + 1} RGBA SHA differs from run 1.`);
      failures.push(...comparison.failures.map((failure) => `run1-vs-run${index + 1}: ${failure}`));
      comparisons.push({ relation: `run-1-vs-run-${index + 1}`,
        status: digestEqual && comparison.failures.length === 0 ? 'pass' : 'fail', metrics: comparison.metrics });
    }
  }
  return { scenarioId: runs[0]?.scenarioId, pipeline: runs[0]?.pipeline, backend: runs[0]?.backend,
    status: failures.length === 0 ? 'pass' : 'fail', comparisons, failures };
}

/** Compares the four mandatory cross-pipeline and cross-backend relations. */
async function compareScenarioParity(scenario, canonicalRuns) {
  const comparisons = [];
  for (const definition of parityDefinitions) {
    const left = canonicalRuns.get(definition.left.join('/'));
    const right = canonicalRuns.get(definition.right.join('/'));
    const failures = [];
    let metrics = null;
    if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
      failures.push('Both canonical quadrants must pass before parity comparison.');
    } else {
      const comparison = compareThreeCaptures(await loadRunImage(left), await loadRunImage(right));
      metrics = comparison.metrics;
      failures.push(...comparison.failures);
    }
    comparisons.push({ scenarioId: scenario.id, relation: definition.relation,
      status: failures.length === 0 ? 'pass' : 'fail', metrics, failures });
  }
  return comparisons;
}

/** Locks upstream source, no-asset status, replay, state script, and Oracle artifacts. */
async function validateInputs(context, manifest) {
  const failures = [];
  const example = manifest.examples.find((candidate) => candidate.id === caseId);
  failures.push(...validateManifestContract(example));
  const upstreamSourcePath = path.join(context.upstreamRoot, 'examples', 'webgl_buffergeometry_instancing.html');
  if (!await pathExists(upstreamSourcePath)) failures.push(`Missing upstream source: ${upstreamSourcePath}.`);
  else if (await sha256File(upstreamSourcePath) !== upstreamSourceSha256) failures.push('Pinned upstream source SHA-256 differs.');
  if (!await pathExists(context.assetRoot)) failures.push(`Explicit empty asset root is missing: ${context.assetRoot}.`);
  if (!await pathExists(context.replayPath)) failures.push(`Canonical replay is missing: ${context.replayPath}.`);
  else if (await sha256File(context.replayPath) !== replaySha256) failures.push('Canonical replay SHA-256 differs.');
  const stateScriptPath = path.join(path.dirname(context.replayPath), 'webgl_buffergeometry_instancing_count_12500_state.js');
  if (!await pathExists(stateScriptPath)) failures.push(`Canonical GUI state script is missing: ${stateScriptPath}.`);
  else if (await sha256File(stateScriptPath) !== stateScriptSha256) failures.push('Canonical GUI state script SHA-256 differs.');
  const oracles = [];
  for (const scenario of instancingScenarios) {
    const rgbaPath = path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`);
    const metadataPath = path.join(context.oracleRoot, caseId, `${scenario.id}.json`);
    if (!await pathExists(rgbaPath) || !await pathExists(metadataPath)) {
      failures.push(`Missing locked Oracle for ${scenario.id}.`);
      continue;
    }
    const rgbaSha = await sha256File(rgbaPath);
    const jsonSha = await sha256File(metadataPath);
    failures.push(...validateOracleSha256(scenario.id, 'rgba', rgbaSha));
    failures.push(...validateOracleSha256(scenario.id, 'json', jsonSha));
    const metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    failures.push(...validateOracleMetadata(metadata, scenario));
    oracles.push({ scenarioId: scenario.id, rgbaPath, metadataPath, rgbaSha256: rgbaSha, metadataSha256: jsonSha });
  }
  for (const pipeline of pipelines) {
    const executable = path.join(context.binaryRoot, `${shardName}-${pipeline}`);
    if (!await pathExists(executable)) failures.push(`Missing ${pipeline} host: ${executable}.`);
  }
  return { status: failures.length === 0 ? 'pass' : 'fail', example, upstreamSourcePath,
    upstreamCommit, upstreamSourceSha256, assets: { requiredFiles: [], requiredFileCount: 0 },
    replay: { path: context.replayPath, sha256: replaySha256, stateScriptPath, stateScriptSha256 }, oracles, failures };
}

/** Executes the complete strict four-quadrant, Oracle, ABI, and stability gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    upstreamRoot: requirePathOption(options, 'upstream-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    replayPath: requirePathOption(options, 'input-replay'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  const manifestPath = path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const inputs = await validateInputs(context, manifest);
  context.example = inputs.example;
  context.manifestScenarios = new Map(inputs.example?.scenarios?.map((scenario) => [scenario.id, scenario]) ?? []);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    lintGeneratedArtifacts(context),
    lintSampleGpuBoundary(repositoryRoot)
  ]);
  const runs = [];
  const stability = [];
  const parity = [];
  if (inputs.status === 'pass' && generatedArtifacts.status === 'pass' && gpuBoundaryLint.status === 'pass') {
    for (const scenario of instancingScenarios) {
      const canonicalRuns = new Map();
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          const quadrantRuns = [];
          for (let repetition = 1; repetition <= repeatCount; repetition += 1) {
            const run = await runQuadrantRepetition(context, scenario, pipeline, backend, repetition);
            runs.push(run);
            quadrantRuns.push(run);
            console.log(`[${run.status.toUpperCase()}] ${caseId}/${scenario.id} ${pipeline}/${backend} run=${repetition}`);
            for (const failure of run.failures) console.log(`  ${failure}`);
          }
          stability.push(await compareStability(quadrantRuns));
          canonicalRuns.set(`${pipeline}/${backend}`, quadrantRuns[0]);
        }
      }
      parity.push(...await compareScenarioParity(scenario, canonicalRuns));
    }
  }
  const expectedRunCount = instancingScenarios.length * pipelines.length * backends.length * repeatCount;
  const status = inputs.status === 'pass' && generatedArtifacts.status === 'pass' && gpuBoundaryLint.status === 'pass' &&
    runs.length === expectedRunCount && runs.every((entry) => entry.status === 'pass') &&
    stability.length === instancingScenarios.length * pipelines.length * backends.length &&
    stability.every((entry) => entry.status === 'pass') &&
    parity.length === instancingScenarios.length * parityDefinitions.length &&
    parity.every((entry) => entry.status === 'pass') ? 'pass' : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-buffergeometry-instancing',
    caseId,
    status,
    comparisonThresholds,
    matrix: { pipelines, backends, scenarios: instancingScenarios.map((scenario) => scenario.id), repeatCount,
      expectedRunCount, passingRunCount: runs.filter((entry) => entry.status === 'pass').length },
    inputs,
    generatedArtifacts,
    gpuBoundaryLint,
    runs,
    stability,
    parity
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_buffergeometry_instancing report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
