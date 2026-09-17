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
  validateSemanticSnapshot,
  validateStructuralSnapshot
} from '../../../tests/runners/three/node/runner.mjs';

const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const lockedRandomSeed = 305419896;
const stabilityRunCount = 3;
const lockedReplaySha256 = '9821bb367ea1d2cf27c9e231662f5c2c6caee7290c11632389fcba211827fa9f';
const lockedReplayIdentity = Object.freeze({
  caseId: 'webgl_materials_texture_canvas',
  scenarioId: 'painted',
  captureFrame: 30,
  eventCount: 6,
  target: '#drawing-canvas'
});
const lockedRotationReplaySha256 =
  '7f2b24034ef6fc429b0ce6503345a14f522671db33d8f169b80b96026bacb209';
const lockedRotationReplayIdentity = Object.freeze({
  caseId: 'webgl_materials_texture_rotation',
  scenarioId: 'uv-transform-orbit',
  captureFrame: 1,
  eventCount: 3,
  target: 'canvas'
});
const lockedUvGridSha256 =
  '909d9a1eb2a5d5de9d221a5e8de4e9119d409decddf522d48896bd51523d354d';
const baseComponentSchema = Object.freeze([
  Object.freeze({ name: 'vertices', kind: 'buffer', role: 'vertex' }),
  Object.freeze({ name: 'indices', kind: 'buffer', role: 'index' }),
  Object.freeze({ name: 'objects', kind: 'buffer', role: 'object' }),
  Object.freeze({ name: 'instances', kind: 'buffer', role: 'instance' }),
  Object.freeze({ name: 'materials', kind: 'buffer', role: 'material' }),
  Object.freeze({ name: 'textures', kind: 'texture', role: 'texture' })
]);
const caseDefinitions = Object.freeze([
  {
    name: 'WebglGeometryCube',
    caseId: 'webgl_geometry_cube',
    scenePassName: 'WebglGeometryCubeScenePass',
    renderSetType: 'WebglGeometryCubeSceneRenderSet',
    computePassNames: Object.freeze([]),
    screenPassNames: Object.freeze([]),
    scenarios: Object.freeze([
      { id: 'initial', frame: 0, rotationX: 0.005, rotationY: 0.01 },
      { id: 'rotated', frame: 120, rotationX: 0.605, rotationY: 1.21 }
    ]),
    expectedSnapshot: Object.freeze({
      sceneRenderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      instanceCount: 1,
      containsHierarchy: false,
      materialCount: 1,
      scenePassCount: 1,
      screenPassCount: 0,
      drawCommandCount: 1,
      directDrawFallback: false,
      indexedVertexCount: 36,
      boxVertexCount: 24,
      texturePath: 'textures/crate.gif',
      textureColorSpace: 'srgb',
      cameraFovDegrees: 70,
      cameraNear: 0.1,
      cameraFar: 100,
      cameraPositionZ: 2,
      gpuWorkDslOnly: true
    })
  },
  {
    name: 'WebglMaterialsTextureCanvas',
    caseId: 'webgl_materials_texture_canvas',
    scenePassName: 'WebglMaterialsTextureCanvasScenePass',
    renderSetType: 'WebglMaterialsTextureCanvasSceneRenderSet',
    computePassNames: Object.freeze([]),
    screenPassNames: Object.freeze([]),
    scenarios: Object.freeze([
      { id: 'initial', frame: 0, rotationX: 0.01, rotationY: 0.01, replay: false },
      { id: 'painted', frame: 30, rotationX: 0.31, rotationY: 0.31, replay: true }
    ]),
    expectedSnapshot: Object.freeze({
      sceneRenderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      instanceCount: 1,
      containsHierarchy: false,
      materialCount: 1,
      scenePassCount: 1,
      screenPassCount: 0,
      computePassCount: 0,
      canvasGenerationPipeline: 'dsl-fragment-analytic',
      drawCommandCount: 1,
      directDrawFallback: false,
      indexedVertexCount: 36,
      boxVertexCount: 24,
      canvasWidth: 128,
      canvasHeight: 128,
      textureColorSpace: 'none-linear',
      canvasGenerationPassCount: 0,
      cameraFovDegrees: 50,
      cameraNear: 1,
      cameraFar: 2000,
      cameraPositionZ: 500,
      gpuWorkDslOnly: true
    })
  },
  {
    name: 'WebglMaterialsTextureRotation',
    caseId: 'webgl_materials_texture_rotation',
    scenePassName: 'WebglMaterialsTextureRotationMainPass',
    renderSetType: 'WebglMaterialsTextureRotationSceneRenderSet',
    computePassNames: Object.freeze([]),
    screenPassNames: Object.freeze([]),
    scenarios: Object.freeze([
      {
        id: 'initial-loader',
        frame: 0,
        replay: false,
        offset: Object.freeze([0, 0]),
        repeat: Object.freeze([0.25, 0.25]),
        rotation: Math.PI / 4,
        center: Object.freeze([0.5, 0.5]),
        cameraPosition: Object.freeze([10, 15, 25])
      },
      {
        id: 'canonical-loader',
        frame: 0,
        replay: false,
        semantic: true,
        offset: Object.freeze([0, 0]),
        repeat: Object.freeze([0.25, 0.25]),
        rotation: Math.PI / 4,
        center: Object.freeze([0.5, 0.5]),
        cameraPosition: Object.freeze([10, 15, 25])
      },
      {
        id: 'uv-transform-orbit',
        frame: 1,
        replay: true,
        replayKind: 'rotation',
        offset: Object.freeze([0.2, 0.1]),
        repeat: Object.freeze([0.75, 0.5]),
        rotation: -0.6,
        center: Object.freeze([0.3, 0.7]),
        cameraPosition: Object.freeze([
          -7.49509626441178,
          4.034590359525475,
          29.62339637209118
        ])
      }
    ]),
    expectedSnapshot: Object.freeze({
      renderSetPolicy: 'required',
      sceneRenderSetCount: 1,
      renderableObjectCount: 1,
      entityCount: 1,
      instanceCount: 1,
      containsHierarchy: false,
      materialCount: 1,
      geometryGroupCount: 6,
      scenePassCount: 1,
      screenPassCount: 0,
      drawCommandCount: 1,
      directDrawFallback: false,
      indexedVertexCount: 36,
      boxVertexCount: 24,
      texturePath: 'textures/uv_grid_opengl.jpg',
      textureAssetSha256: lockedUvGridSha256,
      textureColorSpace: 'srgb',
      textureWidth: 1024,
      textureHeight: 1024,
      mipLevelCount: 11,
      mipmapGeneration: 'explicit-cpu-linear-light',
      samplerAddressMode: 'repeat',
      samplerMaxAnisotropy: 16,
      cameraFovDegrees: 40,
      cameraNear: 1,
      cameraFar: 1000,
      gpuWorkDslOnly: true
    })
  }
]);

/** Parses strict value-bearing long options for the Phase 1 texture-case fixture. */
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

/** Returns whether one explicitly resolved artifact path exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Reads one required generated text artifact or records a deterministic failure. */
async function readRequiredText(targetPath, failures, label) {
  if (!await pathExists(targetPath)) {
    failures.push(`${label}: missing ${targetPath}.`);
    return '';
  }
  const source = await fs.readFile(targetPath, 'utf8');
  if (source.length === 0) {
    failures.push(`${label}: generated artifact is empty: ${targetPath}.`);
  }
  return source;
}

/** Compares expected JSON fields recursively while permitting diagnostic extension fields. */
function validateExpectedFields(actual, expected, label, failures) {
  for (const [name, expectedValue] of Object.entries(expected)) {
    const fieldLabel = `${label}.${name}`;
    const actualValue = actual?.[name];
    if (Array.isArray(expectedValue)) {
      if (!Array.isArray(actualValue) || actualValue.length !== expectedValue.length) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
      } else {
        for (let index = 0; index < expectedValue.length; index += 1) {
          const candidate = actualValue[index];
          const expectedCandidate = expectedValue[index];
          if (typeof expectedCandidate === 'number' && !Number.isInteger(expectedCandidate)) {
            if (typeof candidate !== 'number' || Math.abs(candidate - expectedCandidate) > 1e-7) {
              failures.push(`${fieldLabel}[${index}]=${JSON.stringify(candidate)}, expected ${expectedCandidate}.`);
            }
          } else if (candidate !== expectedCandidate) {
            failures.push(`${fieldLabel}[${index}]=${JSON.stringify(candidate)}, expected ${JSON.stringify(expectedCandidate)}.`);
          }
        }
      }
    } else if (expectedValue !== null && typeof expectedValue === 'object') {
      if (actualValue === null || typeof actualValue !== 'object' || Array.isArray(actualValue)) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected an object.`);
      } else {
        validateExpectedFields(actualValue, expectedValue, fieldLabel, failures);
      }
    } else if (typeof expectedValue === 'number' && !Number.isInteger(expectedValue)) {
      if (typeof actualValue !== 'number' || Math.abs(actualValue - expectedValue) > 1e-7) {
        failures.push(`${fieldLabel}=${JSON.stringify(actualValue)}, expected ${expectedValue}.`);
      }
    } else if (actualValue !== expectedValue) {
      failures.push(
        `${fieldLabel}=${JSON.stringify(actualValue)}, expected ${JSON.stringify(expectedValue)}.`);
    }
  }
}

/** Validates immutable assets, reference captures, and the semantic pointer replay. */
async function validateFixtureInputs(context) {
  const failures = [];
  const cratePath = path.join(context.assetRoot, 'textures', 'crate.gif');
  if (!await pathExists(cratePath)) {
    failures.push(`Missing locked Three r185 texture asset: ${cratePath}.`);
  }
  const uvGridPath = path.join(context.assetRoot, 'textures', 'uv_grid_opengl.jpg');
  if (!await pathExists(uvGridPath)) {
    failures.push(`Missing locked Three r185 texture asset: ${uvGridPath}.`);
  } else {
    const uvGridSha256 = createHash('sha256').update(await fs.readFile(uvGridPath)).digest('hex');
    if (uvGridSha256 !== lockedUvGridSha256) {
      failures.push(
        `UV-grid asset SHA-256 is ${uvGridSha256}; expected ${lockedUvGridSha256}.`);
    }
  }

  for (const definition of caseDefinitions) {
    const manifestExample = context.manifestExamples.get(definition.caseId);
    if (!manifestExample) {
      failures.push(`Formal manifest is missing phase1-required case '${definition.caseId}'.`);
      continue;
    }
    if (manifestExample.status !== 'phase1_required' ||
        manifestExample.renderSetPolicy !== 'required' ||
        manifestExample.renderSetType !== definition.renderSetType) {
      failures.push(
        `${definition.caseId}: fixture identity differs from the formal phase1-required RenderSet contract.`);
    }
    for (const scenario of definition.scenarios) {
      const manifestScenario = manifestExample.scenarios?.find(
        (candidate) => candidate.id === scenario.id);
      if (!manifestScenario || manifestScenario.frame !== scenario.frame) {
        failures.push(
          `${definition.caseId}/${scenario.id}: fixture scenario differs from the formal manifest.`);
      }
      const oracleBase = path.join(context.oracleRoot, definition.caseId, scenario.id);
      for (const extension of ['rgba', 'json']) {
        const oraclePath = `${oracleBase}.${extension}`;
        if (!await pathExists(oraclePath)) {
          failures.push(`Missing locked Three r185 Oracle artifact: ${oraclePath}.`);
        }
      }
      if (scenario.semantic === true && !await pathExists(`${oracleBase}.semantic.json`)) {
        failures.push(`Missing locked Three r185 semantic Oracle: ${oracleBase}.semantic.json.`);
      }
    }
  }

  let replayIdentity = null;
  if (!await pathExists(context.inputReplayPath)) {
    failures.push(`Missing canonical CanvasTexture input replay: ${context.inputReplayPath}.`);
  } else {
    try {
      const replayBytes = await fs.readFile(context.inputReplayPath);
      const sha256 = createHash('sha256').update(replayBytes).digest('hex');
      const replay = JSON.parse(replayBytes.toString('utf8'));
      replayIdentity = {
        sha256,
        caseId: replay.caseId,
        scenarioId: replay.scenarioId,
        captureFrame: replay.frame,
        eventCount: Array.isArray(replay.events) ? replay.events.length : null,
        target: replay.target
      };
      validateExpectedFields(
        replayIdentity,
        { sha256: lockedReplaySha256, ...lockedReplayIdentity },
        'inputReplay',
        failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }

  let rotationReplayIdentity = null;
  if (!await pathExists(context.rotationInputReplayPath)) {
    failures.push(
      `Missing canonical texture-rotation input replay: ${context.rotationInputReplayPath}.`);
  } else {
    try {
      const replayBytes = await fs.readFile(context.rotationInputReplayPath);
      const sha256 = createHash('sha256').update(replayBytes).digest('hex');
      const replay = JSON.parse(replayBytes.toString('utf8'));
      rotationReplayIdentity = {
        sha256,
        caseId: replay.caseId,
        scenarioId: replay.scenarioId,
        captureFrame: replay.frame,
        eventCount: Array.isArray(replay.events) ? replay.events.length : null,
        target: replay.target
      };
      validateExpectedFields(
        rotationReplayIdentity,
        { sha256: lockedRotationReplaySha256, ...lockedRotationReplayIdentity },
        'rotationInputReplay',
        failures);
    } catch (error) {
      failures.push(error instanceof Error ? error.message : String(error));
    }
  }

  const checkedSemanticPath = path.join(
    scriptDirectory,
    'Expected',
    'webgl_materials_texture_rotation_canonical_loader.semantic.json');
  const oracleSemanticPath = path.join(
    context.oracleRoot,
    'webgl_materials_texture_rotation',
    'canonical-loader.semantic.json');
  if (await pathExists(checkedSemanticPath) && await pathExists(oracleSemanticPath)) {
    const [checkedSemantic, oracleSemantic] = await Promise.all([
      fs.readFile(checkedSemanticPath, 'utf8'),
      fs.readFile(oracleSemanticPath, 'utf8')
    ]);
    if (checkedSemantic !== oracleSemantic) {
      failures.push('Texture-rotation loader semantic Oracle differs from the checked canonical sidecar.');
    }
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    assetRoot: context.assetRoot,
    oracleRoot: context.oracleRoot,
    inputReplayPath: context.inputReplayPath,
    replayIdentity,
    rotationInputReplayPath: context.rotationInputReplayPath,
    rotationReplayIdentity,
    failures
  };
}

/** Validates one RenderSet-backed texture sample's generated Legacy or Experimental ABI. */
async function lintGeneratedCasePipeline(generatedRoot, definition, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, definition.name, 'UGLBin');
  const label = `${definition.name}/${pipeline}`;
  const failures = [];
  const exportsSource = await readRequiredText(
    path.join(generatedDirectory, 'exports.hpp'), failures, label);
  const generatedSource = await readRequiredText(
    path.join(generatedDirectory, 'generate_result.hpp'), failures, label);

  if (exportsSource && !/namespace\s+ExportedRenderSet\s*\{[\s\S]*sceneSet\s*=\s*\d+/u.test(
    exportsSource)) {
    failures.push(`${label}: generated exports are missing the unique Scene RenderSet handle.`);
  }
  if (exportsSource && !exportsSource.includes(`namespace ${definition.renderSetType}Components`)) {
    failures.push(`${label}: generated exports are missing ${definition.renderSetType} component handles.`);
  }
  for (const component of baseComponentSchema) {
    const componentPattern = new RegExp(
      `RenderComponentHandle\\s+${component.name}\\s*=\\s*\\d+`, 'u');
    if (exportsSource && !componentPattern.test(exportsSource)) {
      failures.push(`${label}: generated exports are missing component '${component.name}'.`);
    }
  }
  if (!new RegExp(`(?:struct|class)\\s+${definition.renderSetType}`, 'u').test(generatedSource) ||
      !generatedSource.includes(`createRenderSet<${definition.renderSetType}>()`)) {
    failures.push(`${label}: generated host is missing the unique Scene RenderSet implementation.`);
  }
  if (!generatedSource.includes(`class ${definition.scenePassName}`)) {
    failures.push(`${label}: generated host is missing ${definition.scenePassName}.`);
  }
  for (const computePassName of definition.computePassNames) {
    if (!generatedSource.includes(`class ${computePassName}`)) {
      failures.push(`${label}: generated host is missing ${computePassName}.`);
    }
  }
  const hasLegacyEntityMetadata = generatedSource.includes('RenderEntityCMDParams') &&
    generatedSource.includes('UGLLoadRenderEntityCMDParamsSafe');
  const hasExperimentalEntityMetadata = generatedSource.includes('CommandParams') &&
    generatedSource.includes('DrawInfo') &&
    generatedSource.includes('__uglc_draw_command_params');
  if (!generatedSource.includes('this->mRenderSet = sceneSet') ||
      !generatedSource.includes('this->mRenderSetBindGroupIndex = 0') ||
      (!hasLegacyEntityMetadata && !hasExperimentalEntityMetadata)) {
    failures.push(
      `${label}: Scene pass is missing the RenderSet binding or entity/instance metadata ABI.`);
  }
  for (const component of baseComponentSchema) {
    if (!generatedSource.includes(`.componentName = "${component.name}"`)) {
      failures.push(`${label}: generated RenderSet layout is missing '${component.name}'.`);
    }
  }
  for (const componentName of ['objects', 'instances', 'materials']) {
    const hasLegacyComponentRead = generatedSource.includes(
      `${componentName}_UGLGetSafe`);
    const hasExperimentalComponentRead = generatedSource.includes(
      `sceneSet->${componentName}[`) && generatedSource.includes(
      `${componentName}IndexTable`);
    if (!hasLegacyComponentRead && !hasExperimentalComponentRead) {
      failures.push(
        `${label}: generated shader does not read '${componentName}' through BufferComponent::get.`);
    }
  }
  if (!generatedSource.includes('renderEntityID') ||
      !generatedSource.includes('renderEntityInstanceID')) {
    failures.push(`${label}: generated vertex shader is missing RenderEntity ID builtins.`);
  }
  if (!generatedSource.includes('textures_UGLResolveTextureIndexSafe') &&
      !(generatedSource.includes('texturesIndexTable') &&
        generatedSource.includes('sceneSet->textures['))) {
    failures.push(`${label}: generated shader does not resolve textures through TextureComponent::get.`);
  }
  const parameterlessSceneDrawCount = (
    generatedSource.match(/scenePass->run\(\)/gu) ?? []).length;
  const allSceneDrawCalls = generatedSource.match(/scenePass->run\([^)]*\)/gu) ?? [];
  if (parameterlessSceneDrawCount !== 1 || allSceneDrawCalls.length !== 1) {
    failures.push(
      `${label}: Scene pass must use exactly one parameterless RenderSet indexed-indirect draw.`);
  }
  if (generatedSource.includes('scenePass->setVertexBuffer') ||
      generatedSource.includes('scenePass->setIndexBuffer') ||
      generatedSource.includes('scenePass->run(36u')) {
    failures.push(`${label}: Scene pass contains a standalone-geometry or explicit-count fallback.`);
  }
  const sceneSubmissionCount = (
    generatedSource.match(new RegExp(`renderPass\\("${definition.name}Scene"`, 'gu')) ?? []).length;
  if (sceneSubmissionCount !== 1) {
    failures.push(`${label}: expected one scene render pass, observed ${sceneSubmissionCount}.`);
  }

  if (definition.screenPassNames.length === 0) {
    if (generatedSource.includes('screenPass->run(')) {
      failures.push(`${label}: screenless case unexpectedly generated a fullscreen screen pass.`);
    }
  } else {
    for (const screenPassName of definition.screenPassNames) {
      if (!generatedSource.includes(`class ${screenPassName}`)) {
        failures.push(`${label}: generated host is missing ${screenPassName}.`);
      }
    }
    const screenDrawCount = (
      generatedSource.match(/screenPass->run\(3u, 1u, 0u, 0u\)/gu) ?? []).length;
    const resolveSubmissionCount = (
      generatedSource.match(/renderPass\("WebglMaterialsTextureCanvasResolve"/gu) ?? []).length;
    if (screenDrawCount !== 1 || resolveSubmissionCount !== 1) {
      failures.push(
        `${label}: CanvasTexture must generate one scene pass and one fullscreen resolve draw.`);
    }
    if (generatedSource.includes('WebglMaterialsTextureCanvasGenerationPass') ||
        generatedSource.includes('generationPass->run(') ||
        generatedSource.includes('TextureUsage::StorageBinding') ||
        !generatedSource.includes('webglMaterialsTextureCanvasTexelIntensity') ||
        !generatedSource.includes('webglMaterialsTextureCanvasSample')) {
      failures.push(
        `${label}: Canvas semantics must use the DSL fragment analytic path without compute/RWTexture2D.`);
    }
  }

  if (definition.caseId === 'webgl_materials_texture_rotation') {
    for (const requiredToken of [
      'AddressMode::Repeat',
      'maxAnisotropy = 16',
      'webglMaterialsTextureRotationUv',
      'cos(',
      'sin('
    ]) {
      if (!generatedSource.includes(requiredToken)) {
        failures.push(
          `${label}: texture-rotation generated product is missing '${requiredToken}'.`);
      }
    }
    if (generatedSource.includes('generateMipmap') ||
        generatedSource.includes('generateMipmaps') ||
        generatedSource.includes('TextureUsage::StorageBinding')) {
      failures.push(
        `${label}: texture rotation must upload explicit mips and must not generate them on GPU.`);
    }
  }

  if (pipeline === 'experimental') {
    const passNames = [definition.scenePassName, ...definition.screenPassNames];
    for (const passName of passNames) {
      for (const stage of ['vertex', 'fragment']) {
        const baseName = `${passName}__${stage}`;
        const products = [
          path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
          path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
          path.join(generatedDirectory, 'msl', `${baseName}.msl`),
          path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
          path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
        ];
        for (const product of products) {
          await readRequiredText(product, failures, label);
        }
      }
    }
    for (const computePassName of definition.computePassNames) {
      const baseName = computePassName;
      const products = [
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.json`),
        path.join(generatedDirectory, 'uglir', `${baseName}.uglir.txt`),
        path.join(generatedDirectory, 'msl', `${baseName}.msl`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spv.txt`),
        path.join(generatedDirectory, 'spv', `${baseName}.raw.spvasm`)
      ];
      for (const product of products) {
        await readRequiredText(product, failures, label);
      }
    }
  }

  return {
    case: definition.name,
    pipeline,
    generatedDirectory,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Lints all texture samples across the isolated Legacy and Experimental products. */
async function lintGeneratedArtifacts(generatedRoot) {
  const entries = [];
  for (const definition of caseDefinitions) {
    for (const pipeline of pipelines) {
      entries.push(await lintGeneratedCasePipeline(generatedRoot, definition, pipeline));
    }
  }
  return {
    schemaVersion: 1,
    gate: 'three-r185-phase1-texture-generated-artifacts',
    status: entries.every((entry) => entry.status === 'pass') ? 'pass' : 'fail',
    entries
  };
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
function makeArtifactPaths(
  outputRoot,
  definition,
  scenario,
  pipeline,
  backend,
  repetitionIndex
) {
  const artifactDirectory = path.join(
    outputRoot,
    definition.name,
    scenario.id,
    pipeline,
    backend,
    `run-${repetitionIndex + 1}`);
  return {
    artifactDirectory,
    rgbaPath: path.join(artifactDirectory, 'final.rgba'),
    metadataPath: path.join(artifactDirectory, 'final.json'),
    snapshotPath: path.join(artifactDirectory, 'snapshot.json'),
    semanticPath: path.join(artifactDirectory, 'semantic.json'),
    hostLogPath: path.join(artifactDirectory, 'host.json')
  };
}

/** Builds the complete deterministic Host CLI for one locked r185 texture scenario. */
function buildHostArguments(context, definition, scenario, pipeline, backend, artifacts) {
  const argumentsList = [
    '--case-id', definition.caseId,
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
    '--scene-snapshot', artifacts.snapshotPath
  ];
  if (scenario.replay === true) {
    const replayPath = scenario.replayKind === 'rotation'
      ? context.rotationInputReplayPath
      : context.inputReplayPath;
    argumentsList.push('--input-replay', replayPath);
  }
  if (scenario.semantic === true) {
    argumentsList.push('--semantic-snapshot', artifacts.semanticPath);
  }
  return argumentsList;
}

/** Validates the strict one-Scene, one-RenderSet runtime structure for one texture case. */
function validateSceneRenderSetSnapshot(snapshot, definition, failures) {
  if (snapshot.sceneRenderSetCount !== 1 || snapshot.entityCount !== 1 ||
      snapshot.instanceCount !== 1 || snapshot.drawCommandCount !== 1 ||
      snapshot.directDrawFallback !== false) {
    failures.push(
      'snapshot must report one RenderSet, one entity, one instance, one automatic draw, and no fallback.');
  }
  if (!Array.isArray(snapshot.sceneRoots) || snapshot.sceneRoots.length !== 1) {
    failures.push('snapshot must contain exactly one logical Scene root.');
    return;
  }
  const root = snapshot.sceneRoots[0];
  if (root.id !== 'scene' || root.renderSetCount !== 1 || root.renderSetId !== 'scene' ||
      root.renderSetType !== definition.renderSetType || root.renderableObjectCount !== 1 ||
      root.entityCount !== 1 || root.drawCommandCount !== 1 ||
      root.directDrawFallback !== false) {
    failures.push('Scene root does not expose the locked unique RenderSet identity and draw topology.');
  }
  if (!Array.isArray(root.entities) || root.entities.length !== 1 ||
      !Number.isInteger(root.entities[0]?.entityId) || root.entities[0].entityId < 0 ||
      root.entities[0].logicalRenderableId !== 'box' ||
      root.entities[0].instanceCount !== 1) {
    failures.push('Scene RenderSet must contain exactly one stable single-instance box entity.');
  }
  if (JSON.stringify(root.componentSchema) !== JSON.stringify(baseComponentSchema)) {
    failures.push('Scene RenderSet component schema differs from the locked base ABI.');
  }
  if (!Array.isArray(root.scenePasses) || root.scenePasses.length !== 1) {
    failures.push('Scene root must expose exactly one Scene pass record.');
    return;
  }
  const scenePass = root.scenePasses[0];
  if (scenePass.name !== 'main' || scenePass.renderClass !== definition.scenePassName ||
      scenePass.renderSetId !== 'scene' || scenePass.renderSetBindingCount !== 1 ||
      scenePass.drawMode !== 'render-set-indexed-indirect' ||
      scenePass.invocationCount !== 1 || scenePass.drawCommandCount !== 1 ||
      scenePass.usesStandaloneGeometry !== false || scenePass.usesExplicitDrawCount !== false) {
    failures.push('Scene pass record is not the locked parameterless RenderSet indexed-indirect draw.');
  }
}

/** Validates capture metadata and the locked single-object structural snapshot. */
async function validateQuadrantArtifacts(
  context,
  definition,
  scenario,
  pipeline,
  backend,
  artifacts
) {
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
    frame: scenario.frame,
    randomSeed: lockedRandomSeed,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  };
  if (definition.caseId === 'webgl_materials_texture_canvas') {
    expectedMetadata.inputReplay = scenario.replay === true
      ? { sha256: lockedReplaySha256, ...lockedReplayIdentity }
      : null;
  } else if (definition.caseId === 'webgl_materials_texture_rotation') {
    expectedMetadata.inputReplay = scenario.replay === true
      ? { sha256: lockedRotationReplaySha256, ...lockedRotationReplayIdentity }
      : null;
  }
  validateExpectedFields(image.metadata, expectedMetadata, 'metadata', failures);

  const replayIdentity = scenario.replayKind === 'rotation'
    ? lockedRotationReplayIdentity
    : lockedReplayIdentity;
  const expectedSnapshot = {
    caseId: definition.caseId,
    scenarioId: scenario.id,
    frame: scenario.frame,
    ...definition.expectedSnapshot,
    inputReplayEventCount: scenario.replay === true ? replayIdentity.eventCount : 0
  };
  if (definition.caseId === 'webgl_geometry_cube') {
    delete expectedSnapshot.inputReplayEventCount;
  }
  if (definition.caseId === 'webgl_geometry_cube' ||
      definition.caseId === 'webgl_materials_texture_canvas') {
    expectedSnapshot.rotationX = scenario.rotationX;
    expectedSnapshot.rotationY = scenario.rotationY;
  }
  if (definition.caseId === 'webgl_materials_texture_rotation') {
    expectedSnapshot.offset = scenario.offset;
    expectedSnapshot.repeat = scenario.repeat;
    expectedSnapshot.rotation = scenario.rotation;
    expectedSnapshot.center = scenario.center;
    expectedSnapshot.cameraPosition = scenario.cameraPosition;
  }
  validateExpectedFields(snapshot, expectedSnapshot, 'snapshot', failures);
  validateSceneRenderSetSnapshot(snapshot, definition, failures);

  const manifestExample = context.manifestExamples.get(definition.caseId);
  const manifestScenario = manifestExample?.scenarios?.find(
    (candidate) => candidate.id === scenario.id);
  if (!manifestExample || !manifestScenario) {
    failures.push('Formal manifest case/scenario identity is unavailable during validation.');
  } else {
    const oracleBase = path.join(context.oracleRoot, definition.caseId, scenario.id);
    const oracleImage = await loadRgbaArtifact(`${oracleBase}.rgba`, `${oracleBase}.json`);
    failures.push(...validateDeterministicCaptureMetadata(
      manifestExample,
      manifestScenario,
      image.metadata,
      oracleImage.metadata,
      pipeline,
      backend));
    failures.push(...validateStructuralSnapshot(manifestExample, manifestScenario, snapshot));
    failures.push(...validateRenderSetSnapshot(manifestExample, snapshot, manifestScenario));
    if (scenario.semantic === true) {
      const [actualSemantic, oracleSemantic] = await Promise.all([
        fs.readFile(artifacts.semanticPath, 'utf8').then(JSON.parse),
        fs.readFile(`${oracleBase}.semantic.json`, 'utf8').then(JSON.parse)
      ]);
      failures.push(...validateSemanticSnapshot(
        manifestExample,
        manifestScenario,
        actualSemantic,
        oracleSemantic));
    }
  }

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
  if (nonBlackPixels === 0 || uniqueRgbColors.size < 2) {
    failures.push('capture is black/clear-only and does not contain the textured object.');
  }
  if (nonOpaquePixels !== 0) {
    failures.push(`capture contains ${nonOpaquePixels} non-opaque pixels; expected opaque RGBA8 output.`);
  }
  return {
    failures,
    snapshot,
    imageValidation: {
      nonBlackPixels,
      nonOpaquePixels,
      uniqueRgbColorCount: uniqueRgbColors.size
    }
  };
}

/** Runs one Legacy/Experimental and Metal/Vulkan texture-case quadrant. */
async function runQuadrant(
  context,
  definition,
  scenario,
  pipeline,
  backend,
  repetitionIndex
) {
  const artifacts = makeArtifactPaths(
    context.outputRoot,
    definition,
    scenario,
    pipeline,
    backend,
    repetitionIndex);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${definition.name}-${pipeline}`);
  const argumentsList = buildHostArguments(
    context, definition, scenario, pipeline, backend, artifacts);
  const result = {
    case: definition.name,
    caseId: definition.caseId,
    scenario: scenario.id,
    frame: scenario.frame,
    pipeline,
    backend,
    repetition: repetitionIndex + 1,
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
        context, definition, scenario, pipeline, backend, artifacts);
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

/** Requires three repeated runs of one quadrant to produce byte-identical RGBA8 captures. */
async function compareStabilityRuns(definition, scenario, pipeline, backend, runs) {
  const comparison = {
    case: definition.name,
    scenario: scenario.id,
    pipeline,
    backend,
    requiredRunCount: stabilityRunCount,
    status: 'fail',
    failures: [],
    runs: runs.map((run) => ({
      repetition: run.repetition,
      status: run.status,
      rgbaPath: run.artifacts.rgbaPath
    }))
  };
  if (runs.length !== stabilityRunCount) {
    comparison.failures.push(
      `Observed ${runs.length} stability runs; expected ${stabilityRunCount}.`);
  }
  const failedRuns = runs.filter((run) => run.status !== 'pass');
  if (failedRuns.length > 0) {
    comparison.failures.push(
      `${failedRuns.length} stability run(s) failed before byte comparison.`);
  } else if (runs.length === stabilityRunCount) {
    try {
      const rgbaPayloads = await Promise.all(
        runs.map((run) => fs.readFile(run.artifacts.rgbaPath)));
      const referencePayload = rgbaPayloads[0];
      comparison.byteCount = referencePayload.byteLength;
      comparison.sha256 = createHash('sha256').update(referencePayload).digest('hex');
      for (let runIndex = 1; runIndex < rgbaPayloads.length; runIndex += 1) {
        const candidate = rgbaPayloads[runIndex];
        if (!referencePayload.equals(candidate)) {
          comparison.failures.push(
            `Run 1 and run ${runIndex + 1} RGBA payloads are not byte-identical.`);
        }
      }
    } catch (error) {
      comparison.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Compares one pair of mandatory same-scenario quadrants with the global gate. */
async function compareQuadrantPair(definition, scenario, left, right, relation) {
  const comparison = {
    case: definition.name,
    scenario: scenario.id,
    relation,
    left: left ? { pipeline: left.pipeline, backend: left.backend } : null,
    right: right ? { pipeline: right.pipeline, backend: right.backend } : null,
    status: 'fail',
    failures: []
  };
  if (!left || !right || left.status !== 'pass' || right.status !== 'pass') {
    comparison.failures.push('Both required quadrants must pass before image comparison.');
  } else {
    try {
      const [leftImage, rightImage] = await Promise.all([
        loadRgbaArtifact(left.artifacts.rgbaPath, left.artifacts.metadataPath),
        loadRgbaArtifact(right.artifacts.rgbaPath, right.artifacts.metadataPath)
      ]);
      const imageComparison = compareThreeCaptures(leftImage, rightImage);
      comparison.metrics = imageComparison.metrics;
      comparison.failures.push(...imageComparison.failures);
    } catch (error) {
      comparison.failures.push(error instanceof Error ? error.message : String(error));
    }
  }
  comparison.status = comparison.failures.length === 0 ? 'pass' : 'fail';
  return comparison;
}

/** Compares all four mandatory pipeline/backend parity pairs for one scenario. */
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
    comparisons.push(await compareQuadrantPair(
      definition,
      scenario,
      byKey.get(`${leftPipeline}|${leftBackend}`),
      byKey.get(`${rightPipeline}|${rightBackend}`),
      relation));
  }
  return comparisons;
}

/** Compares one GVM quadrant against its immutable Three r185 Oracle capture. */
async function compareOracleQuadrant(context, definition, scenario, quadrant) {
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
      const oracleBase = path.join(context.oracleRoot, definition.caseId, scenario.id);
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

/** Prints one gate entry and its bounded failure diagnostics. */
function printGateEntry(label, entry) {
  console.log(`[${entry.status.toUpperCase()}] ${label}`);
  for (const failure of entry.failures) {
    console.log(`  ${failure}`);
  }
}

/** Executes all locked r185 texture samples through the complete four-quadrant gate. */
async function main() {
  const options = parseArguments(process.argv);
  const sourceRoot = path.resolve(scriptDirectory, '..', '..', '..');
  const manifestPath = path.join(
    sourceRoot,
    'GVMRuntime_ThreeSamples',
    'Manifest',
    'three-r185-manifest.json');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const context = {
    sourceRoot,
    manifestPath,
    manifestExamples: new Map(manifest.examples.map((example) => [example.id, example])),
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    assetRoot: requirePathOption(options, 'asset-root'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    inputReplayPath: requirePathOption(options, 'input-replay'),
    rotationInputReplayPath: requirePathOption(options, 'rotation-input-replay'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 30_000, '--timeout-ms')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

  const inputValidation = await validateFixtureInputs(context);
  printGateEntry('fixture inputs', inputValidation);
  const generatedArtifacts = await lintGeneratedArtifacts(context.generatedRoot);
  for (const entry of generatedArtifacts.entries) {
    printGateEntry(`generated ${entry.case}/${entry.pipeline}`, entry);
  }

  const quadrants = [];
  const comparisons = [];
  const oracleComparisons = [];
  const stabilityComparisons = [];
  for (const definition of caseDefinitions) {
    for (const scenario of definition.scenarios) {
      const scenarioQuadrants = [];
      for (const pipeline of pipelines) {
        for (const backend of backends) {
          const repeatedRuns = [];
          for (let repetitionIndex = 0;
            repetitionIndex < stabilityRunCount;
            repetitionIndex += 1) {
            const result = await runQuadrant(
              context,
              definition,
              scenario,
              pipeline,
              backend,
              repetitionIndex);
            repeatedRuns.push(result);
            printGateEntry(
              `${definition.name}/${scenario.id} ${pipeline}/${backend} ` +
                `run ${repetitionIndex + 1}/${stabilityRunCount}`,
              result);
          }
          const primaryResult = repeatedRuns[0];
          scenarioQuadrants.push(primaryResult);
          quadrants.push(primaryResult);
          const stability = await compareStabilityRuns(
            definition,
            scenario,
            pipeline,
            backend,
            repeatedRuns);
          stabilityComparisons.push(stability);
          printGateEntry(
            `${definition.name}/${scenario.id} ${pipeline}/${backend} stability`,
            stability);
        }
      }
      comparisons.push(...await compareScenarioQuadrants(
        definition, scenario, scenarioQuadrants));
      for (const quadrant of scenarioQuadrants) {
        oracleComparisons.push(await compareOracleQuadrant(
          context, definition, scenario, quadrant));
      }
    }
  }

  for (const comparison of comparisons) {
    printGateEntry(
      `${comparison.case}/${comparison.scenario} ${comparison.relation}`,
      comparison);
  }
  for (const comparison of oracleComparisons) {
    printGateEntry(
      `${comparison.case}/${comparison.scenario} Oracle ${comparison.pipeline}/${comparison.backend}`,
      comparison);
  }

  const status = inputValidation.status === 'pass' &&
    generatedArtifacts.status === 'pass' &&
    quadrants.every((entry) => entry.status === 'pass') &&
    stabilityComparisons.every((entry) => entry.status === 'pass') &&
    comparisons.every((entry) => entry.status === 'pass') &&
    oracleComparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-phase1-texture-cases',
    status,
    randomSeed: lockedRandomSeed,
    stabilityRunCount,
    comparisonThresholds,
    inputValidation,
    generatedArtifacts,
    quadrants,
    stabilityComparisons,
    comparisons,
    oracleComparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`Phase 1 texture-case report: ${reportPath}`);
  if (status !== 'pass') {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.stack ?? error.message : String(error));
  process.exitCode = 1;
});
