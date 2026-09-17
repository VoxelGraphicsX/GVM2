#!/usr/bin/env node

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
import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';
import { scanSampleCppSource } from '../../Tools/lint_sample_gpu_boundary.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const caseId = 'webgl_gpgpu_water';
const shardName = 'WebglGpgpuWater';
const randomSeed = 0x12345678;
const replayAssetPath = 'inputs/webgl_gpgpu_water_pointer_viscosity_shadow.json';
const stateScriptAssetPath = 'inputs/webgl_gpgpu_water_pointer_viscosity_shadow_state.js';
const replaySha256 = '625efac0d15ec954b80b30de13739cac5bc5b4bef9cc314197208059860f2bb1';
const stateScriptSha256 = '9d7e42b1dde39331e674a4d09e19e0bf69c5a0e72747bf3f694d4264e3cc4a93';
const initialHeightSha256 = '089b8943b76c5d5433b45755c53b20e6202ce4975a8684f510c89c85db1cc676';
const initialWaterVertexSha256 = '9800c4af06360b12c925fdc533544a57fb9cc5f64aaddb92bdba39ca0c537e6b';
const interactiveHeightSha256 = '70cb85532d102baa7b6e2244ba407fb1a0559125a313ea7df633ac00479b67c1';
const interactiveWaterVertexSha256 = 'aac8d29f98f09ecd6efcb158a0ff0bc34d836c45e882b70cc918a9ffa1b9cc30';
const duckMeshPackSha256 = 'c22f69cfe259aab38ec3f5dfa1c3c94f9dd0c4dc2c47bc49bde16df81dc5916a';
const duckSourceGlbSha256 = '76c62e63a0aec09cd66f2e2c9452a6dcee428f2464dd2c6d845958a8f7f7cdd1';
const duckTextureSha256 = 'd9b75fd8dc15a4b46d5a1b1f9cd9a31a1844fce9cdb24a9de6e61f27d5170124';
const environmentSourceSha256 = '1cb809a131ff3cb7df94b639d5967fc6fc08ecd25b6c43930af3b5d99f1d8855';
const decoderWasmSha256 = 'a680d927bed9cb864ddbd63521868891af2bfbe755092761b4837487618df8ac';
const decoderWrapperSha256 = '8bb2952d2ba7d67e1414f8df819410cb0434a666be53f671fff75f68843d76f6';

const componentSchema = Object.freeze([
  Object.freeze({ name: 'vertices', kind: 'buffer', role: 'vertex' }),
  Object.freeze({ name: 'indices', kind: 'buffer', role: 'index' }),
  Object.freeze({ name: 'objects', kind: 'buffer', role: 'object' }),
  Object.freeze({ name: 'instances', kind: 'buffer', role: 'instance' }),
  Object.freeze({ name: 'materials', kind: 'buffer', role: 'material' }),
  Object.freeze({
    name: 'renderFlags',
    kind: 'buffer',
    role: 'phase-visibility-shadow-wireframe-texture'
  }),
  Object.freeze({
    name: 'textures',
    kind: 'texture',
    role: 'fixed-per-entity-static-material-texture'
  })
]);

const scenePasses = Object.freeze([
  Object.freeze({ name: 'directional-shadow-depth', renderClass: 'WebglGpgpuWaterShadowPass' }),
  Object.freeze({ name: 'main-opaque-pbr', renderClass: 'WebglGpgpuWaterOpaquePass' }),
  Object.freeze({ name: 'main-transparent-water-back', renderClass: 'WebglGpgpuWaterBackPass' }),
  Object.freeze({ name: 'main-transparent-water-front', renderClass: 'WebglGpgpuWaterFrontPass' }),
  Object.freeze({ name: 'main-wireframe-phase', renderClass: 'WebglGpgpuWaterWireframePass' })
]);

export const waterScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-assets-loaded',
    kind: 'initial-frame',
    frame: 0,
    inputReplay: null,
    canonicalState: 'seed-0x185a73r-hdr-duck-draco-loaded-one-set-fourteen-entities-default-heightmap',
    simulationTickCount: 0,
    randomState: 2260508053,
    viscosity: 0.93,
    shadowEnabled: false,
    visualSurfaceFlat: true,
    currentHeightSha256: initialHeightSha256,
    waterVertexSha256: initialWaterVertexSha256,
    screenPassNames: Object.freeze(['equirectangular-background', 'aces-tone-map'])
  }),
  Object.freeze({
    id: 'canonical-loader-snapshot',
    kind: 'loader-snapshot',
    frame: 0,
    inputReplay: null,
    canonicalState: 'duck-glb-one-scene-one-node-one-draco-primitive-one-embedded-texture-hdr-1024x512',
    simulationTickCount: 0,
    randomState: 2260508053,
    viscosity: 0.93,
    shadowEnabled: false,
    visualSurfaceFlat: true,
    currentHeightSha256: initialHeightSha256,
    waterVertexSha256: initialWaterVertexSha256,
    screenPassNames: Object.freeze(['equirectangular-background', 'aces-tone-map'])
  }),
  Object.freeze({
    id: 'pointer-viscosity-shadow',
    kind: 'input-replay',
    frame: 120,
    inputReplay: replayAssetPath,
    canonicalState: stateScriptAssetPath,
    simulationTickCount: 60,
    randomState: 246033143,
    viscosity: 0.97,
    shadowEnabled: true,
    visualSurfaceFlat: false,
    currentHeightSha256: interactiveHeightSha256,
    waterVertexSha256: interactiveWaterVertexSha256,
    screenPassNames: Object.freeze([
      'vsm-shadow-filter-vertical',
      'vsm-shadow-filter-horizontal',
      'equirectangular-background',
      'aces-tone-map'
    ])
  })
]);

const lockedAssetSha256 = Object.freeze({
  [replayAssetPath]: replaySha256,
  [stateScriptAssetPath]: stateScriptSha256,
  'models/gltf/duck.glb': duckSourceGlbSha256,
  'textures/equirectangular/blouberg_sunrise_2_1k.hdr': environmentSourceSha256,
  'derived/webgl_gpgpu_water/duck_mesh.json': '7c84ceed7c4bb87dce9889e8513a7a806dbd20dffe38f3bbf2c6672967bec0a8',
  'derived/webgl_gpgpu_water/duck_mesh.bin': duckMeshPackSha256,
  'derived/webgl_gpgpu_water/duck.png': duckTextureSha256
});

const lockedOracleSha256 = Object.freeze({
  'initial-assets-loaded': Object.freeze({
    rgba: '357981a21fa3cad9800819389d7546d894ceab06c27ed1a21a23a897172605d3',
    json: '5479e8ff7c4f497e78050c28685140cd9c26d84453bca5f6ea173304e4f59c6b'
  }),
  'canonical-loader-snapshot': Object.freeze({
    rgba: '357981a21fa3cad9800819389d7546d894ceab06c27ed1a21a23a897172605d3',
    json: 'fc49f30268787a1aa3efa9192019cd4253efcddfba799e2e297f7aeb6b6f6099',
    semantic: 'd764704cef97eb50461f09c1ca8a499736b16e99f05815d601fccee02bf447bc'
  }),
  'pointer-viscosity-shadow': Object.freeze({
    rgba: 'cbd894c923c9b4661ce57eef7e2526ea5c78065e68fb1114005cc4a8369aebdd',
    json: '52593260f3b8576f2b7ba3fa6c605c2e7f6ee2ac3d07e044986057ab3a7a79e4'
  })
});

const quadrantPairs = Object.freeze([
  Object.freeze(['legacy', 'metal', 'experimental', 'metal', 'pipeline-parity-metal']),
  Object.freeze(['legacy', 'vulkan', 'experimental', 'vulkan', 'pipeline-parity-vulkan']),
  Object.freeze(['legacy', 'metal', 'legacy', 'vulkan', 'backend-parity-legacy']),
  Object.freeze(['experimental', 'metal', 'experimental', 'vulkan', 'backend-parity-experimental']),
  Object.freeze(['legacy', 'metal', 'experimental', 'vulkan', 'diagonal-legacy-metal-experimental-vulkan']),
  Object.freeze(['legacy', 'vulkan', 'experimental', 'metal', 'diagonal-legacy-vulkan-experimental-metal'])
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

/** Parses a repeat count that preserves the mandatory three-run byte-stability gate. */
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

/** Builds one runtime scenario with five RenderSet Scene invocations in the locked order. */
function makeRuntimeScenario(scenario) {
  return {
    id: scenario.id,
    kind: scenario.kind,
    frame: scenario.frame,
    inputReplay: scenario.inputReplay,
    canonicalState: scenario.canonicalState,
    scenePassInvocations: scenePasses.map((scenePass) => ({
      sceneRoot: 'scene',
      scenePass: scenePass.name,
      invocationCount: 1
    }))
  };
}

/** Verifies the authoritative status lock describes the implemented one-Set water contract. */
export function validateStatusLockContract(statusLock) {
  const failures = [];
  const reviews = (statusLock?.phase1RequiredReviews ?? []).filter((candidate) => candidate.id === caseId);
  if (reviews.length !== 1) {
    failures.push(`Status lock must contain exactly one ${caseId} required review; observed ${reviews.length}.`);
  }
  const review = reviews[0];
  validateExpectedFields(review, {
    dslShard: shardName,
    renderSetPolicy: 'required',
    renderableObjectCount: 14,
    loaderRenderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: true,
    containsMultipleMaterials: true,
    renderSetType: 'WebglGpgpuWaterSceneRenderSet',
    sceneRoots: [{
      name: 'scene',
      renderSetRuntimeInstanceCount: 1,
      renderSetType: 'WebglGpgpuWaterSceneRenderSet'
    }],
    componentSchema,
    scenePasses: scenePasses.map((scenePass) => ({
      name: scenePass.name,
      renderClass: scenePass.renderClass,
      sceneRoot: 'scene',
      renderSetBindingCount: 1,
      usesStandaloneGeometry: false,
      usesExplicitDrawCount: false
    }))
  }, `statusLock.${caseId}`, failures);
  const lockedScenarios = new Map((review?.scenarios ?? []).map((scenario) => [scenario.id, scenario]));
  if (lockedScenarios.size !== waterScenarios.length) {
    failures.push(`statusLock.${caseId}.scenarios has ${lockedScenarios.size} entries; expected ${waterScenarios.length}.`);
  }
  for (const scenario of waterScenarios) {
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

/** Creates a 588-entry diagnostic manifest containing only the formal water case as required. */
export function createDiagnosticManifest(manifest, waterReview) {
  if (!Array.isArray(manifest?.examples) || manifest.examples.length !== 588) {
    throw new Error(`Manifest inventory must contain 588 examples; observed ${manifest?.examples?.length ?? 0}.`);
  }
  const examples = manifest.examples.map((example) => {
    if (example.id !== caseId) return { ...example, status: 'excluded_upstream' };
    return {
      ...example,
      ...waterReview,
      status: 'phase1_required',
      scenarios: waterScenarios.map(makeRuntimeScenario)
    };
  });
  if (!examples.some((example) => example.id === caseId)) {
    throw new Error(`Manifest inventory does not contain ${caseId}.`);
  }
  return { ...manifest, examples };
}

/** Validates the complete locked asset pack and all immutable Three Oracle artifacts. */
async function validateInputs(context, statusLock) {
  const failures = validateStatusLockContract(statusLock);
  const assets = [];
  for (const [relativePath, expectedSha256] of Object.entries(lockedAssetSha256)) {
    const assetPath = path.join(context.assetRoot, relativePath);
    try {
      const actualSha256 = await sha256File(assetPath);
      if (actualSha256 !== expectedSha256) failures.push(`${relativePath} SHA-256 differs.`);
      assets.push({ relativePath, sha256: actualSha256 });
    } catch (error) {
      failures.push(`${relativePath}: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  const oracles = [];
  for (const scenario of waterScenarios) {
    const basePath = path.join(context.oracleRoot, caseId, scenario.id);
    const rgbaPath = `${basePath}.rgba`;
    const metadataPath = `${basePath}.json`;
    const semanticPath = `${basePath}.semantic.json`;
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
        inputReplay: scenario.inputReplay == null ? null : {
          sha256: replaySha256,
          captureFrame: 120,
          eventCount: 4,
          lastEventFrame: 30,
          target: 'body > div:nth-of-type(2) > canvas'
        }
      }, `oracle.${scenario.id}`, failures);
      const record = { scenarioId: scenario.id, rgbaPath, metadataPath, rgbaIdentity, metadataIdentity };
      if (locked.semantic) {
        const semanticIdentity = await sha256File(semanticPath);
        if (semanticIdentity !== locked.semantic) failures.push(`${scenario.id} Oracle semantic SHA-256 differs.`);
        record.semanticPath = semanticPath;
        record.semanticIdentity = semanticIdentity;
      }
      oracles.push(record);
    } catch (error) {
      failures.push(`${scenario.id} Oracle: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    replaySha256,
    stateScriptSha256,
    assets,
    oracles,
    failures
  };
}

/** Validates the dual generated RenderSet ABI and every Experimental shader product. */
export async function validateGeneratedArtifacts(generatedRoot) {
  const records = [];
  const renderPassNames = [
    'WebglGpgpuWaterHeightPass',
    'WebglGpgpuWaterEnvironmentPass',
    'WebglGpgpuWaterToneMapPass',
    ...scenePasses.map((scenePass) => scenePass.renderClass),
    'WebglGpgpuWaterVsmVerticalPass',
    'WebglGpgpuWaterVsmHorizontalPass'
  ];
  const computePassNames = [
    'WebglGpgpuWaterEnvironmentPrefilterPass',
    'WebglGpgpuWaterPmremFilterPass',
    'WebglGpgpuWaterPmremCopyPass'
  ];
  for (const pipeline of requiredPipelines) {
    const generatedDirectory = path.join(generatedRoot, pipeline, shardName, 'UGLBin');
    const failures = [];
    try {
      const [exportsSource, generatedSource] = await Promise.all([
        fs.readFile(path.join(generatedDirectory, 'exports.hpp'), 'utf8'),
        fs.readFile(path.join(generatedDirectory, 'generate_result.hpp'), 'utf8')
      ]);
      if (!/namespace\s+ExportedRenderSet\s*\{[^}]*sceneSet\s*=\s*1/u.test(exportsSource)) {
        failures.push('Generated ABI must export exactly the Scene RenderSet handle sceneSet.');
      }
      for (const [index, component] of componentSchema.entries()) {
        const pattern = new RegExp(`\\b${component.name}\\s*=\\s*${index + 1}\\s*;`, 'u');
        if (!pattern.test(exportsSource)) failures.push(`Generated ABI is missing component ${component.name}=${index + 1}.`);
      }
      for (const passName of [...renderPassNames, ...computePassNames]) {
        if (!generatedSource.includes(`class ${passName}`)) failures.push(`Generated host is missing ${passName}.`);
      }
      if (!generatedSource.includes('RenderEntityInfo')) {
        failures.push('Generated Scene shaders must expose RenderSet entity metadata.');
      }
      if (pipeline === 'experimental') {
        for (const passName of renderPassNames) {
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
        for (const passName of computePassNames) {
          for (const relativePath of [
            path.join('uglir', `${passName}.uglir.json`),
            path.join('msl', `${passName}.msl`),
            path.join('spv', `${passName}.raw.spv.txt`)
          ]) {
            try {
              await fs.access(path.join(generatedDirectory, relativePath));
            } catch {
              failures.push(`Missing Experimental product ${relativePath}.`);
            }
          }
        }
      }
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
    records.push({ pipeline, generatedDirectory, status: failures.length === 0 ? 'pass' : 'fail', failures });
  }
  return records;
}

/** Scans only the water C++ adapter boundary for prohibited direct GPU work. */
async function validateGpuBoundary() {
  const files = [
    path.join(scriptDirectory, 'WebglGpgpuWaterRuntimeAdapter.hpp'),
    path.join(scriptDirectory, 'WebglGpgpuWaterRuntimeAdapter.cpp')
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

/** Compares one successful quadrant pair using the shared Three image thresholds. */
async function compareQuadrantPair(left, right, relation) {
  const comparison = {
    relation,
    caseId: left.caseId,
    scenarioId: left.scenarioId,
    repetition: left.repetition,
    left: { pipeline: left.pipeline, backend: left.backend },
    right: { pipeline: right.pipeline, backend: right.backend },
    status: 'fail',
    failures: []
  };
  if (left.status !== 'pass' || right.status !== 'pass') {
    comparison.failures.push('Cross-quadrant comparison requires both Oracle comparisons to pass.');
    return comparison;
  }
  try {
    const [leftImage, rightImage] = await Promise.all([
      loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
      loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
    ]);
    const thresholdComparison = compareThreeCaptures(leftImage, rightImage);
    comparison.metrics = thresholdComparison.metrics;
    comparison.failures.push(...thresholdComparison.failures);
  } catch (error) {
    comparison.failures.push(error instanceof Error ? error.message : String(error));
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Builds all six pairwise quadrant relations for every scenario repetition. */
async function buildCrossComparisons(quadrants) {
  const byKey = new Map(quadrants.map((entry) => [
    `${entry.scenarioId}|${entry.repetition}|${entry.pipeline}|${entry.backend}`,
    entry
  ]));
  const comparisons = [];
  for (const scenario of waterScenarios) {
    const repetitions = [...new Set(quadrants
      .filter((entry) => entry.scenarioId === scenario.id)
      .map((entry) => entry.repetition))];
    for (const repetition of repetitions) {
      for (const [leftPipeline, leftBackend, rightPipeline, rightBackend, relation] of quadrantPairs) {
        const prefix = `${scenario.id}|${repetition}`;
        const left = byKey.get(`${prefix}|${leftPipeline}|${leftBackend}`);
        const right = byKey.get(`${prefix}|${rightPipeline}|${rightBackend}`);
        if (left && right) comparisons.push(await compareQuadrantPair(left, right, relation));
      }
    }
  }
  return comparisons;
}

/** Requires initial and loader images to match while the interactive state remains distinct. */
async function validateScenarioRelations(quadrants) {
  const records = [];
  for (const pipeline of requiredPipelines) {
    for (const backend of requiredBackends) {
      const repetitions = [...new Set(quadrants
        .filter((entry) => entry.pipeline === pipeline && entry.backend === backend)
        .map((entry) => entry.repetition))];
      for (const repetition of repetitions) {
        const byScenario = new Map(quadrants
          .filter((entry) => entry.pipeline === pipeline
            && entry.backend === backend
            && entry.repetition === repetition)
          .map((entry) => [entry.scenarioId, entry]));
        const initial = byScenario.get('initial-assets-loaded');
        const loader = byScenario.get('canonical-loader-snapshot');
        const interactive = byScenario.get('pointer-viscosity-shadow');
        const failures = [];
        if (![initial, loader, interactive].every((entry) => entry?.status === 'pass')) {
          failures.push('All three successful water scenario captures are required.');
        } else {
          const [initialIdentity, loaderIdentity, interactiveIdentity] = await Promise.all([
            sha256File(initial.artifacts.rgbaPath),
            sha256File(loader.artifacts.rgbaPath),
            sha256File(interactive.artifacts.rgbaPath)
          ]);
          if (initialIdentity !== loaderIdentity) {
            failures.push('Initial and canonical loader scenarios must have the same flat-water image.');
          }
          if (interactiveIdentity === initialIdentity) {
            failures.push('Pointer, viscosity, and shadow replay must change the final image.');
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

/** Validates deterministic simulation, one-Set topology, ordered passes, and loader semantics. */
async function validateRuntimeScenarios(quadrants) {
  const expectedById = new Map(waterScenarios.map((scenario) => [scenario.id, scenario]));
  const records = [];
  for (const quadrant of quadrants) {
    const failures = [];
    try {
      const [metadata, snapshot] = await Promise.all([
        loadJson(quadrant.artifacts.metadataPath),
        loadJson(quadrant.artifacts.sceneSnapshotPath)
      ]);
      const expected = expectedById.get(quadrant.scenarioId);
      validateExpectedFields(metadata, {
        implementationStage: 'dsl-height-pmrem-vsm-pbr-aces',
        randomState: expected.randomState,
        randomDrawCount: 848,
        finalRandomState: 2260508053,
        simulationFormat: 'rgba32float-fragment-ping-pong',
        simulationTickCount: expected.simulationTickCount,
        heightTextureIndex: 0,
        initialHeightSha256,
        currentHeightSha256: expected.currentHeightSha256,
        waterVertexSha256: expected.waterVertexSha256,
        duckMeshPackSha256,
        duckSourceGlbSha256,
        duckDecoderWasmSha256: decoderWasmSha256,
        duckDecoderWrapperSha256: decoderWrapperSha256,
        duckTextureSha256,
        duckVertexCount: 2277,
        duckIndexCount: 12636,
        duckTextureWidth: 512,
        duckTextureHeight: 512,
        environmentSourceSha256,
        environmentWidth: 1024,
        environmentHeight: 512,
        toneMapping: 'aces-filmic',
        toneMappingExposure: 0.5,
        visualSurfaceFlat: expected.visualSurfaceFlat,
        viscosity: expected.viscosity,
        shadowEnabled: expected.shadowEnabled,
        wireframeEnabled: false,
        canonicalStateSha256: expected.shadowEnabled ? stateScriptSha256 : null,
        inputReplay: expected.inputReplay == null ? null : {
          sha256: replaySha256,
          eventCount: 4,
          lastEventFrame: 30,
          target: 'body > div:nth-of-type(2) > canvas'
        }
      }, `${quadrant.scenarioId}.${quadrant.pipeline}.${quadrant.backend}.repeat-${quadrant.repetition}`, failures);
      validateExpectedFields(snapshot, {
        implementationStage: 'dsl-height-pmrem-vsm-pbr-aces',
        renderSetPolicy: 'required',
        sceneRenderSetCount: 1,
        renderableObjectCount: 14,
        entityCount: 14,
        containsInstancing: false,
        containsHierarchy: false,
        scenePassCount: 5,
        defaultInvokedScenePassCount: 5,
        drawCommandCount: 5,
        screenPassCount: expected.screenPassNames.length,
        directDrawFallback: false,
        gpuWorkDslOnly: true,
        heightTextureRenderSetComponent: false,
        shadowTextureRenderSetComponent: false,
        staticMaterialTextureRenderSetComponent: true,
        staticMaterialTextureEntityCount: 14,
        duckMeshPackSha256,
        duckTextureSha256,
        environmentSourceSha256,
        simulation: {
          format: 'rgba32float',
          implementation: 'dsl-fragment-ping-pong',
          tickCount: expected.simulationTickCount,
          currentIndex: 0,
          initialSha256: initialHeightSha256,
          currentSha256: expected.currentHeightSha256
        }
      }, `${quadrant.scenarioId}.snapshot`, failures);
      const root = snapshot.sceneRoots?.[0];
      if (!root || root.renderSetId !== 'scene' || root.entityCount !== 14) {
        failures.push('The logical scene must expose exactly one fourteen-entity RenderSet named scene.');
      } else {
        if (root.entities.length !== 14 || root.entities.some((entity) => entity.instanceCount !== 1)) {
          failures.push('All fourteen water Scene entities must remain ordinary instanceCount=1 entities.');
        }
        if (JSON.stringify(root.componentSchema) !== JSON.stringify(componentSchema)) {
          failures.push('Runtime RenderSet component schema differs from the locked seven-component ABI.');
        }
        const passIdentities = root.scenePasses?.map((scenePass) => `${scenePass.name}|${scenePass.renderClass}`);
        const expectedPassIdentities = scenePasses.map((scenePass) => `${scenePass.name}|${scenePass.renderClass}`);
        if (JSON.stringify(passIdentities) !== JSON.stringify(expectedPassIdentities)) {
          failures.push('Scene passes must execute shadow, opaque, transparent-back, transparent-front, then wireframe.');
        }
        if (root.scenePasses?.[0]?.phaseEnabled !== expected.shadowEnabled
          || root.scenePasses?.[4]?.phaseEnabled !== false) {
          failures.push('Shadow and disabled wireframe phases do not match the scenario contract.');
        }
      }
      const screenPassNames = snapshot.screenPasses?.map((screenPass) => screenPass.name);
      if (JSON.stringify(screenPassNames) !== JSON.stringify(expected.screenPassNames)) {
        failures.push('Screen-pass sequence differs from the scenario VSM/background/tone-map contract.');
      }
      if (expected.kind === 'loader-snapshot') {
        const semantic = await loadJson(quadrant.artifacts.semanticPath);
        validateExpectedFields(semantic, {
          result: {
            canonicalSceneSha256: duckMeshPackSha256,
            decoderWasmSha256,
            decoderWrapperSha256,
            embeddedTextureCount: 1,
            indexCount: 12636,
            nodeCount: 1,
            primitiveCount: 1,
            renderableObjectCount: 1,
            sceneRootCount: 1,
            sourceAssetSha256: duckSourceGlbSha256,
            textureSha256: duckTextureSha256,
            vertexCount: 2277
          }
        }, `${quadrant.scenarioId}.semantic`, failures);
      }
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

/** Executes the three-scenario, four-quadrant, three-repeat formal water gate. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    repeatCount: parseRepeatCount(options.repeat),
    timeoutMs: parseTimeout(options['timeout-ms'])
  };
  await fs.rm(context.outputRoot, { recursive: true, force: true });
  await fs.mkdir(context.outputRoot, { recursive: true });
  const [manifest, statusLock] = await Promise.all([
    loadJson(path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json')),
    loadJson(path.join(repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-phase1-status-lock.json'))
  ]);
  const waterReview = statusLock.phase1RequiredReviews?.find((review) => review.id === caseId);
  const inputValidation = await validateInputs(context, statusLock);
  const [generatedArtifacts, gpuBoundaryLint] = await Promise.all([
    validateGeneratedArtifacts(context.generatedRoot),
    validateGpuBoundary()
  ]);
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
        name: 'macos-three-r185-webgl-gpgpu-water',
        buildDir: path.dirname(path.dirname(context.binaryRoot)),
        hosts: {
          legacy: path.join(context.binaryRoot, `${shardName}-legacy`),
          experimental: path.join(context.binaryRoot, `${shardName}-experimental`)
        }
      },
      manifest: createDiagnosticManifest(manifest, waterReview),
      assetRoot: context.assetRoot,
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
  const [crossComparisons, scenarioRelations, runtimeScenarioValidation] = await Promise.all([
    buildCrossComparisons(matrix.quadrants),
    validateScenarioRelations(matrix.quadrants),
    validateRuntimeScenarios(matrix.quadrants)
  ]);
  const expectedRunCount = waterScenarios.length * requiredPipelines.length
    * requiredBackends.length * context.repeatCount;
  const expectedCrossComparisonCount = waterScenarios.length * context.repeatCount * quadrantPairs.length;
  const expectedStabilityComparisonCount = waterScenarios.length * requiredPipelines.length
    * requiredBackends.length * (context.repeatCount - 1);
  const expectedScenarioRelationCount = requiredPipelines.length * requiredBackends.length
    * context.repeatCount;
  const status = matrix.status === 'pass'
    && matrix.quadrants.length === expectedRunCount
    && matrix.quadrants.every((entry) => entry.status === 'pass')
    && crossComparisons.length === expectedCrossComparisonCount
    && crossComparisons.every((entry) => entry.status === 'pass')
    && matrix.stabilityComparisons.length === expectedStabilityComparisonCount
    && matrix.stabilityComparisons.every((entry) => entry.status === 'pass')
    && scenarioRelations.length === expectedScenarioRelationCount
    && scenarioRelations.every((entry) => entry.status === 'pass')
    && runtimeScenarioValidation.length === expectedRunCount
    && runtimeScenarioValidation.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-gpgpu-water',
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
    scenarioRelations,
    runtimeScenarioValidation,
    coverage: matrix.coverage
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`webgl_gpgpu_water formal report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
