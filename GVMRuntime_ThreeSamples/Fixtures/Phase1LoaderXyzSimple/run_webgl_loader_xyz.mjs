import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  compareThreeCaptures
} from '../../../tests/runners/three/node/image-comparison.mjs';

const fixtureDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(fixtureDirectory, '../../..');
const caseId = 'webgl_loader_xyz';
const shardName = 'Phase1LoaderXyzSimple';
const passName = 'WebglLoaderXyzPointPass';
const defaultRandomSeed = 0x12345678;
const expectedAssetSha256 =
  '489c27c4b619c9a47c15df62ebb7a5474791a7ae85f0c9c3f8323a9504288519';
const scenarios = Object.freeze([
  Object.freeze({
    id: 'initial-loader', frame: 0,
    canonicalState: 'helix-201-white-square-points-time-zero',
    rgbaSha256: 'bd74e2be0067c662f406ef7fed03bf0d072f589bb678de292e742d939205d5fc',
    metadataSha256: 'c565eb5fdfaa82bac4603a0279fdf65347ed0322372774a24edd824b702920cc'
  }),
  Object.freeze({
    id: 'canonical-loader', frame: 0,
    canonicalState: '201-centered-uncolored-xyz-points-one-draw',
    rgbaSha256: 'bd74e2be0067c662f406ef7fed03bf0d072f589bb678de292e742d939205d5fc',
    metadataSha256: '18749579c9dcbfaf07d2f7db93af7c2d9975d2e29d8e8276cbf9d9a1e1869ea8'
  }),
  Object.freeze({
    id: 'animated', frame: 120,
    canonicalState: 'fixed-step-two-seconds-x-y-rotation',
    rgbaSha256: 'd02af5ca63bbf8fb3ad89f8c27d49ec61e36ddbe09b6b829468da52fbd02d7e0',
    metadataSha256: '48a1b1e746b271d7fbd9b65f951c489b3ea3af5031f562d0b8e8738663074668'
  })
]);
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);

/** Parses strict key/value fixture arguments and rejects duplicates or positional values. */
export function parseArguments(argv) {
  const options = {};
  for (let index = 2; index < argv.length; index += 2) {
    const option = argv[index];
    if (!option?.startsWith('--') || index + 1 >= argv.length) {
      throw new Error(`Expected --name value at argument ${index}.`);
    }
    const name = option.slice(2);
    if (Object.hasOwn(options, name)) throw new Error(`Duplicate --${name}.`);
    options[name] = argv[index + 1];
  }
  return options;
}

/** Requires one non-empty path option and resolves it against the current directory. */
function requirePathOption(options, name) {
  const value = options[name];
  if (typeof value !== 'string' || value.length === 0) {
    throw new Error(`Missing required --${name}.`);
  }
  return path.resolve(value);
}

/** Parses one optional positive watchdog duration. */
function parseTimeoutMilliseconds(value) {
  if (value == null) return 120_000;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed <= 0) {
    throw new Error(`--timeout-ms must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Parses the deterministic repetition count and enforces the three-run stability gate. */
export function parseRepeatCount(value) {
  if (value == null) return 3;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 3) {
    throw new Error(`--repeat must be an integer greater than or equal to 3; received '${value}'.`);
  }
  return parsed;
}

/** Computes a lowercase SHA-256 digest from immutable bytes. */
function sha256(bytes) {
  return createHash('sha256').update(bytes).digest('hex');
}

/** Proves byte-exact RGBA, metadata, and snapshot stability for one quadrant. */
export function validateRepeatStability(runs, expectedRepeatCount) {
  const failures = [];
  const identity = {
    scenario: runs[0]?.scenario ?? null,
    pipeline: runs[0]?.pipeline ?? null,
    backend: runs[0]?.backend ?? null
  };
  if (runs.length !== expectedRepeatCount) {
    failures.push(`Observed ${runs.length} runs; expected ${expectedRepeatCount}.`);
  }
  for (let index = 0; index < runs.length; index += 1) {
    const run = runs[index];
    if (run.repetition !== index + 1) {
      failures.push(`Run index ${index} has repetition ${run.repetition}; expected ${index + 1}.`);
    }
    if (run.scenario !== identity.scenario || run.pipeline !== identity.pipeline ||
        run.backend !== identity.backend) {
      failures.push(`Repetition ${run.repetition} does not belong to the same quadrant.`);
    }
    if (run.status !== 'pass') failures.push(`Repetition ${run.repetition} did not pass.`);
  }
  const artifacts = {};
  for (const [field, label] of [
    ['rgba', 'RGBA'], ['metadata', 'metadata'], ['snapshot', 'snapshot']
  ]) {
    const sha256ByRepeat = runs.map((run) => run.artifactSha256?.[field] ?? null);
    const canonicalSha256 = sha256ByRepeat[0] ?? null;
    const byteExact = sha256ByRepeat.length === expectedRepeatCount &&
      canonicalSha256 !== null && sha256ByRepeat.every((digest) => digest === canonicalSha256);
    if (!byteExact) failures.push(`${label} SHA-256 is not byte-exact across all repetitions.`);
    artifacts[field] = { canonicalSha256, sha256ByRepeat, byteExact };
  }
  return {
    ...identity,
    repeatCount: expectedRepeatCount,
    status: failures.length === 0 ? 'pass' : 'fail',
    byteExact: failures.length === 0,
    artifacts,
    failures
  };
}

/** Reads one JSON document with path-specific diagnostics. */
async function readJson(filePath) {
  try {
    return JSON.parse(await fs.readFile(filePath, 'utf8'));
  } catch (error) {
    throw new Error(`Could not read JSON '${filePath}': ${error.message}`);
  }
}

/** Reads one locked 800x500 RGBA8 image and rejects malformed byte counts. */
async function readRgbaImage(filePath) {
  const pixels = new Uint8Array(await fs.readFile(filePath));
  if (pixels.byteLength !== 800 * 500 * 4) {
    throw new Error(`${filePath} has ${pixels.byteLength} bytes; expected 1600000.`);
  }
  return { width: 800, height: 500, pixels };
}

/** Recursively validates that an actual JSON value contains one expected contract subset. */
function validateExpectedFields(actual, expected, label, failures) {
  if (expected === null || typeof expected !== 'object') {
    if (actual !== expected) failures.push(`${label}=${JSON.stringify(actual)}; expected ${JSON.stringify(expected)}.`);
    return;
  }
  if (Array.isArray(expected)) {
    if (!Array.isArray(actual) || actual.length !== expected.length) {
      failures.push(`${label} must contain ${expected.length} entries.`);
      return;
    }
    expected.forEach((value, index) => validateExpectedFields(actual[index], value, `${label}[${index}]`, failures));
    return;
  }
  if (actual == null || typeof actual !== 'object' || Array.isArray(actual)) {
    failures.push(`${label} must be an object.`);
    return;
  }
  for (const [key, value] of Object.entries(expected)) {
    validateExpectedFields(actual[key], value, `${label}.${key}`, failures);
  }
}

/** Validates the frozen formal classification and one-object Scene policy. */
export function validateFormalManifestEntry(manifest) {
  const failures = [];
  const example = manifest?.examples?.find((entry) => entry.id === caseId);
  if (!example) return ['Formal Manifest is missing webgl_loader_xyz.'];
  validateExpectedFields(example, {
    id: caseId,
    status: 'phase1_required',
    renderSetPolicy: 'not-required',
    renderSetReasons: [],
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    loaderRenderableObjectCount: 1,
    dslShard: shardName,
    sceneRoots: [{
      name: 'scene', renderSetRuntimeInstanceCount: 0, renderSetType: null
    }],
    scenePasses: [{
      name: 'point-billboards', renderClass: passName, sceneRoot: 'scene',
      renderSetBindingCount: 0, usesStandaloneGeometry: true, usesExplicitDrawCount: true
    }],
    screenPasses: [],
    scenarios: scenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      canonicalState: scenario.canonicalState,
      renderableObjectCount: 1,
      scenePassInvocations: [{
        sceneRoot: 'scene', scenePass: 'point-billboards', invocationCount: 1
      }],
      scenePassSequence: [{
        sceneRoot: 'scene', scenePass: 'point-billboards', entityOrdinal: 0
      }]
    }))
  }, 'manifest.webgl_loader_xyz', failures);
  if (example.capabilityAudit?.state !== 'supported' ||
      example.capabilityAudit?.gpuWorkDslOnly !== true ||
      example.capabilityAudit?.requiresNewPublicCapability !== false) {
    failures.push('Formal capability audit must remain supported, DSL-only, and API-frozen.');
  }
  return failures;
}

/** Validates the immutable XYZ asset and all three Oracle byte identities. */
export async function validateLockedInputs(assetRoot, oracleRoot) {
  const failures = [];
  const assetPath = path.join(assetRoot, 'models/xyz/helix_201.xyz');
  try {
    const digest = sha256(await fs.readFile(assetPath));
    if (digest !== expectedAssetSha256) {
      failures.push(`Asset SHA-256 is ${digest}; expected ${expectedAssetSha256}.`);
    }
  } catch (error) {
    failures.push(`Could not validate asset '${assetPath}': ${error.message}`);
  }
  for (const scenario of scenarios) {
    for (const [extension, expected] of [
      ['rgba', scenario.rgbaSha256], ['json', scenario.metadataSha256]
    ]) {
      const oraclePath = path.join(oracleRoot, caseId, `${scenario.id}.${extension}`);
      try {
        const digest = sha256(await fs.readFile(oraclePath));
        if (digest !== expected) {
          failures.push(`${scenario.id}.${extension} SHA-256 is ${digest}; expected ${expected}.`);
        }
      } catch (error) {
        failures.push(`Could not validate Oracle '${oraclePath}': ${error.message}`);
      }
    }
  }
  return failures;
}

/** Validates one generated host against the ordinary indexed RenderClass ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, exportsSource) {
  const failures = [];
  const label = `${caseId}/${pipeline}`;
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<')) {
    failures.push(`${label}: the single Points Scene must not declare a RenderSet.`);
  }
  for (const token of [
    `class ${passName}`,
    'WebglLoaderXyzPointCount = 201u',
    'GVM::RHI::PrimitiveTopology::TriangleList',
    'GVM::RHI::BufferUsage::Vertex',
    'GVM::RHI::BufferUsage::Index',
    'GVM::RHI::BufferBindingType::Uniform'
  ]) {
    if (!generatedSource.includes(token)) failures.push(`${label}: generated host is missing '${token}'.`);
  }
  const vertexBindings = generatedSource.match(/pointPass\d->setVertexBuffer\(vertexBuffer\)/gu) ?? [];
  const indexBindings = generatedSource.match(/pointPass\d->setIndexBuffer\(indexBuffer\)/gu) ?? [];
  const draws = generatedSource.match(
    /pointPass\d->run\(WebglLoaderXyzIndexCount, 1u, 0u, 0, 0u\)/gu) ?? [];
  if (vertexBindings.length !== 1 || indexBindings.length !== 1 || draws.length !== 1) {
    failures.push(`${label}: expected one indexed single-sample draw; observed vertex=${vertexBindings.length}, index=${indexBindings.length}, draw=${draws.length}.`);
  }
  const strideMatch = generatedSource.match(/vertexState\.buffers\[0\]\.arrayStride = (\d+);/u);
  const vertexStrideBytes = strideMatch ? Number(strideMatch[1]) : null;
  if (vertexStrideBytes !== 20) failures.push(`${label}: vertex stride is ${vertexStrideBytes}; expected 20.`);
  const formats = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.format = GVM::RHI::VertexFormat::(\w+);/gu
  )].map((match) => [Number(match[1]), match[2]]).sort((a, b) => a[0] - b[0]);
  const offsets = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.offset = offsetof\(WebglLoaderXyzVertex, (\w+)\);/gu
  )].map((match) => [Number(match[1]), match[2]]).sort((a, b) => a[0] - b[0]);
  const expectedFormats = [[0, 'Float32x3'], [1, 'Float32x2']];
  const expectedOffsets = [[0, 'position'], [1, 'corner']];
  if (JSON.stringify(formats) !== JSON.stringify(expectedFormats)) {
    failures.push(`${label}: vertex formats are ${JSON.stringify(formats)}.`);
  }
  if (JSON.stringify(offsets) !== JSON.stringify(expectedOffsets)) {
    failures.push(`${label}: vertex offsets are ${JSON.stringify(offsets)}.`);
  }
  return {
    pipeline,
    abiSignature: {
      renderSetExportCount: 0,
      vertexStrideBytes,
      vertexAttributes: formats.map((entry, index) => ({
        location: entry[0], format: entry[1], field: offsets[index]?.[1] ?? null
      })),
      primitiveTopology: 'TriangleList',
      indexed: indexBindings.length === 1,
      expandedIndexCount: draws.length === 1 ? 1206 : null,
      coverageSampleCount: draws.length
    },
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Validates Experimental UGLIR, MSL, and direct-SPIR-V products for all GPU stages. */
export function validateExperimentalProductSources(productSources) {
  const failures = [];
  const stages = [
    { name: `${passName}__vertex`, stage: 'vertex' },
    { name: `${passName}__fragment`, stage: 'fragment' }
  ];
  for (const stage of stages) {
    const sources = productSources[stage.name];
    if (!sources) {
      failures.push(`experimental ${stage.name}: product sources are missing.`);
      continue;
    }
    try {
      const document = JSON.parse(sources.uglirJson);
      validateExpectedFields(document, {
        schemaVersion: 1,
        name: stage.name.replace('__', '.'),
        reflection: { stage: stage.stage }
      }, `experimental.${stage.name}.uglir`, failures);
    } catch (error) {
      failures.push(`experimental ${stage.name}: invalid UGLIR JSON: ${error.message}`);
    }
    if (!sources.uglirText.includes(`stage ${stage.stage}`)) {
      failures.push(`experimental ${stage.name}: UGLIR text is missing stage ${stage.stage}.`);
    }
    if (!sources.msl.includes(stage.stage)) {
      failures.push(`experimental ${stage.name}: MSL is missing ${stage.stage}.`);
    }
    if (!sources.spirvWords.includes('0x07230203')) {
      failures.push(`experimental ${stage.name}: direct-SPIR-V words lack the module header.`);
    }
    const entry = stage.stage === 'vertex' ? 'Vertex' : stage.stage === 'fragment' ? 'Fragment' : 'GLCompute';
    if (!sources.spirvAssembly.includes(`OpEntryPoint ${entry}`)) {
      failures.push(`experimental ${stage.name}: SPIR-V assembly lacks OpEntryPoint ${entry}.`);
    }
  }
  if (productSources[`${passName}__vertex`] &&
      !productSources[`${passName}__vertex`].uglirText.includes('pointSizePixels')) {
    failures.push('experimental vertex UGLIR does not preserve perspective point sizing.');
  }
  return failures;
}

/** Builds the complete explicit host CLI for one pipeline/backend quadrant. */
export function buildHostArguments(scenario, pipeline, backend, artifacts, assetRoot) {
  return [
    '--case-id', caseId,
    '--scenario-id', scenario.id,
    '--pipeline', pipeline,
    '--backend', backend,
    '--random-seed', String(defaultRandomSeed),
    '--width', '800',
    '--height', '500',
    '--frame', String(scenario.frame),
    '--asset-root', assetRoot,
    '--capture-rgba', artifacts.rgbaPath,
    '--capture-metadata', artifacts.metadataPath,
    '--scene-snapshot', artifacts.snapshotPath
  ];
}

/** Runs one generated host with a bounded watchdog and captures diagnostics. */
async function runHost(binaryPath, argumentsList, timeoutMs) {
  return new Promise((resolve) => {
    const child = spawn(binaryPath, argumentsList, {
      cwd: repositoryRoot,
      stdio: ['ignore', 'pipe', 'pipe']
    });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => { if (stdout.length < 64_000) stdout += chunk; });
    child.stderr.on('data', (chunk) => { if (stderr.length < 64_000) stderr += chunk; });
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill('SIGTERM');
      setTimeout(() => child.kill('SIGKILL'), 2_000).unref();
    }, timeoutMs);
    child.on('error', (error) => {
      clearTimeout(timer);
      resolve({ exitCode: null, signal: null, timedOut, stdout, stderr, launchError: error.message });
    });
    child.on('exit', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({ exitCode, signal, timedOut, stdout, stderr, launchError: null });
    });
  });
}

/** Validates one emitted structural snapshot against the frozen loader contract. */
export function validateStructuralSnapshot(snapshot, scenario) {
  const failures = [];
  validateExpectedFields(snapshot, {
    schemaVersion: 1,
    caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    canonicalState: scenario.canonicalState,
    renderSetPolicy: 'not-required',
    sceneRenderSetCount: 0,
    renderableObjectCount: 1,
    instanceCount: 1,
    pointCount: 201,
    expandedVertexCount: 804,
    expandedIndexCount: 1206,
    vertexStrideBytes: 20,
    standaloneGeometryBufferCount: 2,
    sourcePrimitiveTopology: 'points',
    expandedPrimitiveTopology: 'triangle-list',
    logicalScenePassCount: 1,
    logicalDrawCommandCount: 1,
    coverageSampleCount: 1,
    physicalSceneDrawCommandCount: 1,
    screenPassCount: 0,
    assetPath: 'models/xyz/helix_201.xyz',
    assetSha256: expectedAssetSha256,
    geometryHasColor: false,
    pointsMaterialSize: 0.1,
    perspectiveSizeScale: 250,
    scenePassSequence: [{
      sceneRoot: 'scene', scenePass: 'point-billboards', entityOrdinal: 0
    }],
    sceneRoots: [{
      id: 'scene', renderSetCount: 0, renderSetId: null, renderSetType: null,
      renderableObjectCount: 1, entityCount: 0, entities: [], drawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [{
        name: 'point-billboards', renderClass: passName, renderSetId: null,
        renderSetBindingCount: 0, drawMode: 'explicit-indexed', invocationCount: 1,
        drawCommandCount: 1, usesStandaloneGeometry: true, usesExplicitDrawCount: true
      }]
    }],
    screenPasses: [],
    gpuWorkDslOnly: true
  }, 'snapshot', failures);
  const expectedRotationX = scenario.frame === 120 ? 0.4 : 0;
  const expectedRotationY = scenario.frame === 120 ? 1 : 0;
  for (const [name, actual, expected] of [
    ['geometryCenter.x', snapshot?.geometryCenter?.[0], 0.17178547382354736],
    ['timeSeconds', snapshot?.timeSeconds, scenario.frame / 60],
    ['rotationX', snapshot?.rotationX, expectedRotationX],
    ['rotationY', snapshot?.rotationY, expectedRotationY]
  ]) {
    if (!Number.isFinite(actual) || Math.abs(actual - expected) > 1e-7) {
      failures.push(`snapshot.${name}=${actual}; expected ${expected}.`);
    }
  }
  return failures;
}

/** Validates one emitted capture metadata document. */
function validateCaptureMetadata(metadata, scenario, pipeline, backend) {
  const failures = [];
  validateExpectedFields(metadata, {
    schemaVersion: 1, source: 'gvm-three-r185', caseId,
    scenarioId: scenario.id, pipeline, backend,
    randomSeed: defaultRandomSeed, frame: scenario.frame,
    width: 800, height: 500, rowStrideBytes: 3200,
    byteCount: 1_600_000, format: 'rgba8unorm'
  }, 'metadata', failures);
  return failures;
}

/** Reads the complete generated product set for one Experimental stage. */
async function readExperimentalStage(generatedDirectory, stageName) {
  const [uglirJson, uglirText, msl, spirvWords, spirvAssembly] = await Promise.all([
    fs.readFile(path.join(generatedDirectory, 'uglir', `${stageName}.uglir.json`), 'utf8'),
    fs.readFile(path.join(generatedDirectory, 'uglir', `${stageName}.uglir.txt`), 'utf8'),
    fs.readFile(path.join(generatedDirectory, 'msl', `${stageName}.msl`), 'utf8'),
    fs.readFile(path.join(generatedDirectory, 'spv', `${stageName}.spv.txt`), 'utf8'),
    fs.readFile(path.join(generatedDirectory, 'spv', `${stageName}.spvasm`), 'utf8')
  ]);
  return { uglirJson, uglirText, msl, spirvWords, spirvAssembly };
}

/** Executes the complete immutable-input, product, quadrant, Oracle, and cross gate. */
export async function runFixture(configuration) {
  const startedAt = new Date().toISOString();
  const failures = [];
  const manifest = await readJson(path.join(
    repositoryRoot, 'GVMRuntime_ThreeSamples/Manifest/three-r185-manifest.json'));
  failures.push(...validateFormalManifestEntry(manifest));
  failures.push(...await validateLockedInputs(configuration.assetRoot, configuration.oracleRoot));

  const generatedProducts = [];
  for (const pipeline of pipelines) {
    const generatedDirectory = path.join(configuration.generatedRoot, pipeline, shardName, 'UGLBin');
    try {
      const [generatedSource, exportsSource] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8')
      ]);
      const result = validateGeneratedSourceContract(pipeline, generatedSource, exportsSource);
      if (pipeline === 'experimental') {
        const stageNames = [`${passName}__vertex`, `${passName}__fragment`];
        const productSources = {};
        for (const stageName of stageNames) {
          productSources[stageName] = await readExperimentalStage(generatedDirectory, stageName);
        }
        result.failures.push(...validateExperimentalProductSources(productSources));
        result.status = result.failures.length === 0 ? 'pass' : 'fail';
      }
      generatedProducts.push(result);
      failures.push(...result.failures);
    } catch (error) {
      const message = `${pipeline} generated products: ${error.message}`;
      generatedProducts.push({ pipeline, status: 'fail', failures: [message] });
      failures.push(message);
    }
  }
  let abiParity = { status: 'fail', failures: ['Both generated ABI signatures are required.'] };
  if (generatedProducts.every((entry) => entry.abiSignature)) {
    const equal = JSON.stringify(generatedProducts[0].abiSignature) ===
      JSON.stringify(generatedProducts[1].abiSignature);
    abiParity = {
      status: equal ? 'pass' : 'fail',
      failures: equal ? [] : ['Legacy and Experimental generated host ABIs differ.']
    };
    failures.push(...abiParity.failures);
  }

  const oracleImages = new Map();
  for (const scenario of scenarios) {
    oracleImages.set(scenario.id, await readRgbaImage(path.join(
      configuration.oracleRoot, caseId, `${scenario.id}.rgba`)));
    const metadata = await readJson(path.join(
      configuration.oracleRoot, caseId, `${scenario.id}.json`));
    const metadataFailures = [];
    validateExpectedFields(metadata, {
      source: 'three-r185-reference',
      upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
      caseId, scenarioId: scenario.id, frame: scenario.frame,
      randomSeed: defaultRandomSeed, width: 800, height: 500,
      rowStrideBytes: 3200, byteCount: 1_600_000, format: 'rgba8unorm'
    }, `oracle.${scenario.id}`, metadataFailures);
    failures.push(...metadataFailures);
  }

  await fs.rm(configuration.outputDirectory, { recursive: true, force: true });
  await fs.mkdir(configuration.outputDirectory, { recursive: true });

  const runs = [];
  const quadrants = [];
  const stabilityGroups = [];
  const captures = new Map();
  for (const scenario of scenarios) {
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const label = `${scenario.id}/${pipeline}/${backend}`;
        const quadrantRuns = [];
        const quadrantFailures = [];
        for (let repetition = 1; repetition <= configuration.repeatCount; repetition += 1) {
          const directory = path.join(
            configuration.outputDirectory, 'artifacts', scenario.id, pipeline, backend,
            `repeat-${repetition}`);
          await fs.mkdir(directory, { recursive: true });
          const artifacts = {
            rgbaPath: path.join(directory, 'capture.rgba'),
            metadataPath: path.join(directory, 'capture.json'),
            snapshotPath: path.join(directory, 'scene-snapshot.json')
          };
          const binaryPath = path.join(
            configuration.binaryRoot, `${shardName}-${pipeline}`);
          const processResult = await runHost(
            binaryPath,
            buildHostArguments(scenario, pipeline, backend, artifacts, configuration.assetRoot),
            configuration.timeoutMs);
          const runFailures = [];
          if (processResult.launchError) runFailures.push(`launch failed: ${processResult.launchError}`);
          if (processResult.timedOut) runFailures.push(`timed out after ${configuration.timeoutMs} ms.`);
          if (processResult.exitCode !== 0) {
            runFailures.push(`host exit=${processResult.exitCode} signal=${processResult.signal ?? 'none'}.`);
          }
          let comparison = null;
          let artifactSha256 = null;
          let capture = null;
          try {
            const [loadedCapture, metadata, snapshot, rgbaBytes, metadataBytes, snapshotBytes] =
              await Promise.all([
                readRgbaImage(artifacts.rgbaPath),
                readJson(artifacts.metadataPath),
                readJson(artifacts.snapshotPath),
                fs.readFile(artifacts.rgbaPath),
                fs.readFile(artifacts.metadataPath),
                fs.readFile(artifacts.snapshotPath)
              ]);
            capture = loadedCapture;
            artifactSha256 = {
              rgba: sha256(rgbaBytes),
              metadata: sha256(metadataBytes),
              snapshot: sha256(snapshotBytes)
            };
            runFailures.push(...validateCaptureMetadata(metadata, scenario, pipeline, backend));
            runFailures.push(...validateStructuralSnapshot(snapshot, scenario));
            comparison = compareThreeCaptures(oracleImages.get(scenario.id), capture);
            runFailures.push(...comparison.failures.map((failure) => `Three Oracle: ${failure}`));
            const nonBlackPixels = capture.pixels.reduce((count, value, index) =>
              index % 4 !== 3 && value !== 0 ? count + (index % 4 === 0 ? 1 : 0) : count, 0);
            if (nonBlackPixels < 100) runFailures.push(`capture has only ${nonBlackPixels} non-black pixels.`);
          } catch (error) {
            runFailures.push(`artifact validation failed: ${error.message}`);
          }
          const run = {
            id: `${label}/repeat-${repetition}`,
            scenario: scenario.id,
            pipeline,
            backend,
            repetition,
            status: runFailures.length === 0 ? 'pass' : 'fail',
            failures: runFailures,
            artifacts,
            artifactSha256,
            oracleMetrics: comparison?.metrics ?? null,
            process: processResult
          };
          runs.push(run);
          quadrantRuns.push(run);
          quadrantFailures.push(...runFailures.map(
            (failure) => `repeat-${repetition}: ${failure}`));
          if (repetition === 1 && run.status === 'pass') captures.set(label, capture);
        }

        const stability = validateRepeatStability(quadrantRuns, configuration.repeatCount);
        stabilityGroups.push(stability);
        quadrantFailures.push(...stability.failures.map((failure) => `stability: ${failure}`));
        quadrants.push({
          scenario: scenario.id, pipeline, backend,
          status: quadrantFailures.length === 0 ? 'pass' : 'fail',
          failures: quadrantFailures,
          repeatCount: configuration.repeatCount,
          runIds: quadrantRuns.map((run) => run.id),
          oracleMetrics: quadrantRuns[0]?.oracleMetrics ?? null,
          stabilityStatus: stability.status
        });
        failures.push(...quadrantFailures.map((failure) => `${label}: ${failure}`));
      }
    }
  }

  const relations = [
    ['legacy/metal', 'experimental/metal'],
    ['legacy/vulkan', 'experimental/vulkan'],
    ['legacy/metal', 'legacy/vulkan'],
    ['experimental/metal', 'experimental/vulkan']
  ];
  const crossComparisons = [];
  for (const scenario of scenarios) {
    for (const [left, right] of relations) {
      const leftImage = captures.get(`${scenario.id}/${left}`);
      const rightImage = captures.get(`${scenario.id}/${right}`);
      const crossFailures = [];
      let metrics = null;
      if (!leftImage || !rightImage) {
        crossFailures.push('one or both quadrant captures are unavailable.');
      } else {
        const result = compareThreeCaptures(leftImage, rightImage);
        metrics = result.metrics;
        crossFailures.push(...result.failures);
      }
      const relation = `${scenario.id}:${left}->${right}`;
      crossComparisons.push({
        relation, status: crossFailures.length === 0 ? 'pass' : 'fail',
        failures: crossFailures, metrics
      });
      failures.push(...crossFailures.map((failure) => `${relation}: ${failure}`));
    }
  }

  const report = {
    schemaVersion: 1,
    fixture: caseId,
    startedAt,
    finishedAt: new Date().toISOString(),
    status: failures.length === 0 ? 'pass' : 'fail',
    repeatCount: configuration.repeatCount,
    immutableThresholdPolicy: 'tests/runners/three/node/image-comparison.mjs',
    classification: {
      status: 'phase1_required', renderSetPolicy: 'not-required',
      gpuWorkDslOnly: true, publicCapabilityAdded: false
    },
    lockedInputs: {
      assetSha256: expectedAssetSha256,
      oracleSha256: scenarios.map((scenario) => ({
        scenario: scenario.id,
        rgba: scenario.rgbaSha256,
        metadata: scenario.metadataSha256
      }))
    },
    generatedProducts,
    abiParity,
    runs,
    quadrants,
    stabilityGroups,
    crossComparisons,
    counts: {
      scenarioCount: scenarios.length,
      expectedRunCount: scenarios.length * pipelines.length * backends.length *
        configuration.repeatCount,
      runCount: runs.length,
      passedRuns: runs.filter((entry) => entry.status === 'pass').length,
      quadrantCount: quadrants.length,
      passedQuadrants: quadrants.filter((entry) => entry.status === 'pass').length,
      stabilityGroupCount: stabilityGroups.length,
      passedStabilityGroups: stabilityGroups.filter((entry) => entry.status === 'pass').length,
      crossComparisonCount: crossComparisons.length,
      passedCrossComparisons: crossComparisons.filter((entry) => entry.status === 'pass').length
    },
    failures
  };
  await fs.mkdir(configuration.outputDirectory, { recursive: true });
  await fs.writeFile(
    path.join(configuration.outputDirectory, 'summary.json'),
    `${JSON.stringify(report, null, 2)}\n`);
  return report;
}

/** Runs the fixture CLI and reports decision-oriented quadrant progress. */
async function main() {
  const options = parseArguments(process.argv);
  const configuration = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputDirectory: requirePathOption(options, 'output-dir'),
    timeoutMs: parseTimeoutMilliseconds(options['timeout-ms']),
    repeatCount: parseRepeatCount(options.repeat)
  };
  const report = await runFixture(configuration);
  for (const run of report.runs) {
    console.log(
      `[${run.status === 'pass' ? 'PASS' : 'FAIL'}] ` +
      `${run.scenario}/${run.pipeline}/${run.backend}/repeat-${run.repetition}`);
    for (const failure of run.failures) console.log(`  ${failure}`);
  }
  for (const stability of report.stabilityGroups) {
    console.log(
      `[${stability.status === 'pass' ? 'PASS' : 'FAIL'}] ` +
      `${stability.scenario}/${stability.pipeline}/${stability.backend} ` +
      `${stability.repeatCount}x byte-exact stability`);
    for (const failure of stability.failures) console.log(`  ${failure}`);
  }
  for (const comparison of report.crossComparisons) {
    console.log(`[${comparison.status === 'pass' ? 'PASS' : 'FAIL'}] ${comparison.relation}`);
  }
  console.log(`Fixture JSON: ${path.join(configuration.outputDirectory, 'summary.json')}`);
  if (report.status !== 'pass') process.exitCode = 1;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch((error) => {
    console.error(error.stack ?? error.message);
    process.exitCode = 1;
  });
}
