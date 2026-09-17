#!/usr/bin/env node

import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath, pathToFileURL } from 'node:url';

import {
  compareThreeCaptures,
  comparisonThresholds,
  loadRgbaArtifact
} from '../../../tests/runners/three/node/image-comparison.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, '../../..');
const pipelines = Object.freeze(['legacy', 'experimental']);
const backends = Object.freeze(['metal', 'vulkan']);
const caseName = 'WebglMaterialsTexturePartialupdate';
const caseId = 'webgl_materials_texture_partialupdate';
const mainPassName = 'WebglMaterialsTexturePartialupdateMainPass';
const upstreamCommit = '2431a09f46f34c560bc8e44b33be0e567723d5b9';
const randomSeed = 42;
const manifestPath = path.join(
  repositoryRoot, 'GVMRuntime_ThreeSamples', 'Manifest', 'three-r185-manifest.json');
const checkerboardRelativePath = path.join(
  'textures', 'floors', 'FloorsCheckerboard_S_Diffuse.jpg');
const checkerboardSha256 =
  'd7547036c6221a840b80ce11138dc2c1a26df2630419217234c479b00e50a119';
const oracleSha256 = Object.freeze({
  'initial-loader': Object.freeze({
    rgba: 'f72a65d6696fc245a600863fb9bcd09a2f49608e1b15ec8e5fc3552a8801cd68',
    json: '3d405d6314da1ee1a403827b753515d0c1e1eab30f8791a3bd0cfe8ee10b5002'
  }),
  patched: Object.freeze({
    rgba: 'a58e742b26f40211d12af7db9e869e2cff69683a5ff2735f70512b1cd3fb2550',
    json: 'a092f483fc2276d1ab226a6c49a37c49a0258ee2b3eb9fff2d796d36efa5375d'
  })
});

export const partialupdateScenarios = Object.freeze([
  Object.freeze({
    id: 'initial-loader',
    kind: 'initial-frame',
    frame: 0,
    patchCount: 0,
    canonicalState: 'checkerboard-before-patches-seed-42'
  }),
  Object.freeze({
    id: 'patched',
    kind: 'fixed-frame',
    frame: 60,
    patchCount: 9,
    canonicalState: 'seed-42-fixed-step-60hz-nine-color-patches'
  })
]);

export const parityDefinitions = Object.freeze([
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'metal',
    rightPipeline: 'experimental',
    rightBackend: 'metal',
    relation: 'pipeline-parity-metal'
  }),
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'vulkan',
    rightPipeline: 'experimental',
    rightBackend: 'vulkan',
    relation: 'pipeline-parity-vulkan'
  }),
  Object.freeze({
    leftPipeline: 'legacy',
    leftBackend: 'metal',
    rightPipeline: 'legacy',
    rightBackend: 'vulkan',
    relation: 'backend-parity-legacy'
  }),
  Object.freeze({
    leftPipeline: 'experimental',
    leftBackend: 'metal',
    rightPipeline: 'experimental',
    rightBackend: 'vulkan',
    relation: 'backend-parity-experimental'
  })
]);

const experimentalStages = Object.freeze([
  Object.freeze({
    baseName: 'WebglMaterialsTexturePartialupdateBaseCopyPass',
    moduleName: 'WebglMaterialsTexturePartialupdateBaseCopyPass',
    entryName: 'WebglMaterialsTexturePartialupdateBaseCopyPass.compute',
    stage: 'compute',
    mslEntry: 'kernel void computeMain',
    spirvEntry: 'OpEntryPoint GLCompute',
    requiredUglirTokens: ['intrinsic texture_read', 'intrinsic texture_write'],
    requiredResources: [
      { name: 'bindGroup.baseTexture', kind: 'texture', bindingIndex: 0 },
      {
        name: 'bindGroup.workingTexture',
        kind: 'storage_texture',
        bindingIndex: 1,
        textureFormat: 'RGBA16Float'
      }
    ]
  }),
  Object.freeze({
    baseName: 'WebglMaterialsTexturePartialupdatePatchPass',
    moduleName: 'WebglMaterialsTexturePartialupdatePatchPass',
    entryName: 'WebglMaterialsTexturePartialupdatePatchPass.compute',
    stage: 'compute',
    mslEntry: 'kernel void computeMain',
    spirvEntry: 'OpEntryPoint GLCompute',
    requiredUglirTokens: ['intrinsic texture_read', 'intrinsic texture_write'],
    requiredResources: [
      { name: 'bindGroup.patchTexture', kind: 'texture', bindingIndex: 0 },
      { name: 'bindGroup.patches', kind: 'storage_buffer', bindingIndex: 1 },
      { name: 'bindGroup.uniforms', kind: 'uniform_buffer', bindingIndex: 2 },
      {
        name: 'bindGroup.workingTexture',
        kind: 'storage_texture',
        bindingIndex: 3,
        textureFormat: 'RGBA16Float'
      }
    ]
  }),
  Object.freeze({
    baseName: 'WebglMaterialsTexturePartialupdateMainPass__vertex',
    moduleName: 'WebglMaterialsTexturePartialupdateMainPass.vertex',
    entryName: 'WebglMaterialsTexturePartialupdateMainPass.vertex',
    stage: 'vertex',
    mslEntry: 'vertexMain',
    spirvEntry: 'OpEntryPoint Vertex',
    requiredUglirTokens: ['modelViewProjection', 'texCoord'],
    requiredResources: [
      { name: 'bindGroup.uniforms', kind: 'uniform_buffer', bindingIndex: 0 }
    ]
  }),
  Object.freeze({
    baseName: 'WebglMaterialsTexturePartialupdateMainPass__fragment',
    moduleName: 'WebglMaterialsTexturePartialupdateMainPass.fragment',
    entryName: 'WebglMaterialsTexturePartialupdateMainPass.fragment',
    stage: 'fragment',
    mslEntry: 'fragmentMain',
    spirvEntry: 'OpEntryPoint Fragment',
    requiredUglirTokens: ['intrinsic texture_sample', 'pow'],
    requiredResources: [
      { name: 'bindGroup.workingTexture', kind: 'texture', bindingIndex: 1 },
      { name: 'bindGroup.textureSampler', kind: 'sampler', bindingIndex: 2 }
    ]
  })
]);

/** Parses strict value-bearing long options without environment-variable fallbacks. */
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

/** Resolves one mandatory explicit path option. */
function requirePathOption(options, name) {
  if (!options[name]) throw new Error(`Missing required --${name} path.`);
  return path.resolve(options[name]);
}

/** Parses one positive integer option or returns its deterministic fallback. */
export function parsePositiveInteger(value, fallback, label) {
  if (value == null) return fallback;
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < 1) {
    throw new Error(`${label} must be a positive integer; received '${value}'.`);
  }
  return parsed;
}

/** Reports whether one generated product or executable exists. */
async function pathExists(targetPath) {
  try {
    await fs.access(targetPath);
    return true;
  } catch {
    return false;
  }
}

/** Calculates the lowercase SHA-256 digest of one exact file. */
async function sha256File(targetPath) {
  return createHash('sha256').update(await fs.readFile(targetPath)).digest('hex');
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

/** Validates the formal required classification and ordinary-RenderClass policy. */
export function validateManifestContract(manifest) {
  const failures = [];
  const example = manifest?.examples?.find((entry) => entry.id === caseId);
  if (!example) return [`Formal manifest is missing ${caseId}.`];
  validateExpectedFields(example, {
    id: caseId,
    upstreamPath: 'examples/webgl_materials_texture_partialupdate.html',
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
      name: 'main',
      renderClass: mainPassName,
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [
      'partial-texture-base-copy',
      'partial-texture-update-compute'
    ],
    renderSetType: null,
    componentSchema: [],
    dslShard: caseName,
    scenarios: partialupdateScenarios.map((scenario) => ({
      id: scenario.id,
      kind: scenario.kind,
      frame: scenario.frame,
      inputReplay: null,
      canonicalState: scenario.canonicalState,
      scenePassInvocations: [{
        sceneRoot: 'scene',
        scenePass: 'main',
        invocationCount: 1
      }],
      scenePassSequence: [{
        sceneRoot: 'scene',
        scenePass: 'main',
        entityOrdinal: 0
      }]
    })),
    deferredEvidence: null
  }, 'manifest.example', failures);
  const available = new Set(example.capabilityAudit?.availableCapabilities ?? []);
  for (const capability of [
    'indexed_triangle_rendering',
    'compute_dispatch',
    'rwtexture2d',
    'texture2d_sampling',
    'storage_to_sampled_role_transition',
    'deterministic_subregion_update'
  ]) {
    if (!available.has(capability)) {
      failures.push(`manifest.example capabilityAudit is missing '${capability}'.`);
    }
  }
  return failures;
}

/** Builds the full structural expectation for one locked partial-update state. */
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
    scenePassCount: 1,
    logicalScenePassCount: 1,
    screenPassCount: 2,
    computePassCount: 2,
    drawCommandCount: 1,
    logicalDrawCommandCount: 1,
    explicitIndexCount: 6,
    planeVertexCount: 4,
    standaloneGeometryBufferCount: 2,
    primitiveTopology: 'triangle-list',
    baseTextureFormat: 'rgba8unorm-srgb',
    workingTextureFormat: 'rgba16float',
    baseTextureWidth: 512,
    baseTextureHeight: 512,
    generateMipmaps: false,
    patchExtent: 32,
    patchCount: scenario.patchCount,
    seed: randomSeed,
    prePatchRandomDrawCount: 156,
    initialRandomState: 78_731_182,
    finalRandomState: 3_665_713_169,
    texturePath: 'textures/floors/FloorsCheckerboard_S_Diffuse.jpg',
    textureAssetSha256: checkerboardSha256,
    scenePassSequence: [{
      sceneRoot: 'scene',
      scenePass: 'main',
      entityOrdinal: 0
    }],
    sceneRoots: [{
      id: 'scene',
      renderSetCount: 0,
      renderSetId: null,
      renderSetType: null,
      renderableObjectCount: 1,
      entityCount: 0,
      entities: [],
      drawCommandCount: 1,
      logicalDrawCommandCount: 1,
      directDrawFallback: false,
      scenePasses: [{
        name: 'main',
        renderClass: mainPassName,
        renderSetId: null,
        renderSetBindingCount: 0,
        drawMode: 'explicit-indexed',
        invocationCount: 1,
        logicalInvocationCount: 1,
        drawCommandCount: 1,
        usesStandaloneGeometry: true,
        usesExplicitDrawCount: true
      }]
    }],
    gpuWorkDslOnly: true
  };
}

/** Builds the complete explicit host command for one mandatory quadrant. */
export function buildHostArguments(scenario, pipeline, backend, assetRoot, artifacts) {
  return [
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
}

/** Counts visible checkerboard pixels, alpha drift, and color diversity. */
export function inspectRgbaPixels(pixels) {
  if (!(pixels instanceof Uint8Array) || pixels.byteLength === 0 ||
      pixels.byteLength % 4 !== 0) {
    throw new Error('Partial-update capture must be tightly packed RGBA8.');
  }
  let nonBlackPixels = 0;
  let nonOpaquePixels = 0;
  const uniqueRgbColors = new Set();
  for (let offset = 0; offset < pixels.length; offset += 4) {
    const red = pixels[offset];
    const green = pixels[offset + 1];
    const blue = pixels[offset + 2];
    if (red !== 0 || green !== 0 || blue !== 0) nonBlackPixels += 1;
    if (pixels[offset + 3] !== 255) nonOpaquePixels += 1;
    uniqueRgbColors.add((red << 16) | (green << 8) | blue);
  }
  return {
    nonBlackPixels,
    nonOpaquePixels,
    uniqueRgbColorCount: uniqueRgbColors.size
  };
}

/** Extracts and validates the RenderSet-free generated host ABI. */
export function validateGeneratedSourceContract(pipeline, generatedSource, exportsSource) {
  const failures = [];
  const label = `${caseName}/${pipeline}`;
  if (!/namespace\s+ExportedRenderSet\s*\{\s*\};/u.test(exportsSource)) {
    failures.push(`${label}: ExportedRenderSet namespace must be empty.`);
  }
  if (generatedSource.includes('RenderSet<') ||
      generatedSource.includes('drawIndexedIndirect')) {
    failures.push(`${label}: the single-object Scene must not use RenderSet drawing.`);
  }
  for (const token of [
    'class WebglMaterialsTexturePartialupdateBaseCopyPass',
    'class WebglMaterialsTexturePartialupdatePatchPass',
    `class ${mainPassName}`,
    'GVM::RHI::PrimitiveTopology::TriangleList',
    'GVM::RHI::VertexFormat::Float32x3',
    'GVM::RHI::VertexFormat::Float32x2',
    'GVM::RHI::BufferUsage::Index|GVM::RHI::BufferUsage::CopyDst',
    'sizeof(unsigned int)',
    'GVM::RHI::TextureFormat::RGBA8UnormSrgb',
    'GVM::RHI::TextureFormat::RGBA16Float',
    'GVM::RHI::TextureUsage::StorageBinding|GVM::RHI::TextureUsage::TextureBinding',
    'computePass("partial-texture-base-copy"',
    'computePass("partial-texture-update-compute"'
  ]) {
    if (!generatedSource.includes(token)) {
      failures.push(`${label}: generated host is missing '${token}'.`);
    }
  }
  const strideMatch = generatedSource.match(
    /vertexState\.buffers\[0\]\.arrayStride = (\d+);/u);
  const vertexStrideBytes = strideMatch ? Number(strideMatch[1]) : null;
  if (vertexStrideBytes !== 20) {
    failures.push(`${label}: vertex stride is ${vertexStrideBytes}, expected 20.`);
  }
  const attributeFormats = [...generatedSource.matchAll(
    /vertexState\.buffers\[0\]\.attributes\[(\d+)\]\.format = GVM::RHI::VertexFormat::(\w+);/gu)]
    .map((match) => ({ index: Number(match[1]), format: match[2] }))
    .sort((left, right) => left.index - right.index);
  const expectedFormats = [
    { index: 0, format: 'Float32x3' },
    { index: 1, format: 'Float32x2' }
  ];
  if (JSON.stringify(attributeFormats) !== JSON.stringify(expectedFormats)) {
    failures.push(
      `${label}: vertex formats are ${JSON.stringify(attributeFormats)}, expected ${JSON.stringify(expectedFormats)}.`);
  }
  const vertexBindings = generatedSource.match(
    /mainPass\d->setVertexBuffer\(vertexBuffer\)/gu) ?? [];
  const indexBindings = generatedSource.match(
    /mainPass\d->setIndexBuffer\(indexBuffer\)/gu) ?? [];
  const drawCalls = generatedSource.match(
    /mainPass\d->run\(6u, 1u, 0u, 0, 0u\)/gu) ?? [];
  if (vertexBindings.length !== 1 || indexBindings.length !== 1 || drawCalls.length !== 1) {
    failures.push(
      `${label}: expected one explicit indexed single-sample draw with six indices and one instance.`);
  }
  const dispatches = {
    baseCopy: (generatedSource.match(/baseCopyPass->run\(/gu) ?? []).length,
    patch: (generatedSource.match(/patchPass->run\(/gu) ?? []).length
  };
  if (Object.values(dispatches).some((count) => count !== 1)) {
    failures.push(`${label}: expected one base-copy and one patch dispatch.`);
  }
  const abiSignature = {
    renderSetExportCount: 0,
    vertexStrideBytes,
    vertexAttributes: attributeFormats,
    primitiveTopology: generatedSource.includes('GVM::RHI::PrimitiveTopology::TriangleList')
      ? 'TriangleList'
      : null,
    standaloneVertexBufferBindings: vertexBindings.length,
    standaloneIndexBufferBindings: indexBindings.length,
    indexElementType: generatedSource.includes('sizeof(unsigned int)') ? 'Uint32' : null,
    explicitIndexCount: drawCalls.length === 1 ? 6 : null,
    explicitInstanceCount: drawCalls.length === 1 ? 1 : null,
    coverageDrawCount: drawCalls.length,
    workingTextureFormat: generatedSource.includes('GVM::RHI::TextureFormat::RGBA16Float')
      ? 'RGBA16Float'
      : null,
    computeDispatches: dispatches
  };
  return {
    pipeline,
    abiSignature,
    status: failures.length === 0 ? 'pass' : 'fail',
    failures
  };
}

/** Confirms that Legacy and Experimental expose the same generated C++ ABI. */
export function validateGeneratedAbiParity(entries) {
  const failures = [];
  const legacy = entries.find((entry) => entry.pipeline === 'legacy');
  const experimental = entries.find((entry) => entry.pipeline === 'experimental');
  if (!legacy || !experimental) {
    failures.push('Both Legacy and Experimental generated entries are required.');
  } else if (JSON.stringify(legacy.abiSignature) !==
             JSON.stringify(experimental.abiSignature)) {
    failures.push(
      `Legacy/Experimental ABI differs: legacy=${JSON.stringify(legacy.abiSignature)}, experimental=${JSON.stringify(experimental.abiSignature)}.`);
  }
  return {
    status: failures.length === 0 ? 'pass' : 'fail',
    failures,
    legacy: legacy?.abiSignature ?? null,
    experimental: experimental?.abiSignature ?? null
  };
}

/** Validates one Experimental UGLIR reflection against required resource bindings. */
export function validateExperimentalReflection(stageDefinition, document) {
  const failures = [];
  validateExpectedFields(document, {
    schemaVersion: 1,
    name: stageDefinition.moduleName,
    reflection: {
      entryName: stageDefinition.entryName,
      stage: stageDefinition.stage,
      entryKind: stageDefinition.stage
    }
  }, `experimental.${stageDefinition.baseName}.uglir`, failures);
  const resources = document?.reflection?.resources ?? [];
  for (const expectedResource of stageDefinition.requiredResources) {
    const actualResource = resources.find(
      (resource) => resource.name === expectedResource.name);
    if (!actualResource) {
      failures.push(
        `experimental.${stageDefinition.baseName}: missing resource ${expectedResource.name}.`);
      continue;
    }
    validateExpectedFields(
      actualResource,
      expectedResource,
      `experimental.${stageDefinition.baseName}.${expectedResource.name}`,
      failures);
  }
  return failures;
}

/** Reads and validates all Experimental UGLIR, MSL, and direct-SPIR-V products. */
async function validateExperimentalProducts(generatedDirectory) {
  const failures = [];
  const productPaths = [];
  const stageReports = [];
  for (const stageDefinition of experimentalStages) {
    const paths = {
      uglirJson: path.join(
        generatedDirectory, 'uglir', `${stageDefinition.baseName}.uglir.json`),
      uglirText: path.join(
        generatedDirectory, 'uglir', `${stageDefinition.baseName}.uglir.txt`),
      msl: path.join(generatedDirectory, 'msl', `${stageDefinition.baseName}.msl`),
      spirvWords: path.join(
        generatedDirectory, 'spv', `${stageDefinition.baseName}.raw.spv.txt`),
      spirvAssembly: path.join(
        generatedDirectory, 'spv', `${stageDefinition.baseName}.raw.spvasm`)
    };
    productPaths.push(...Object.values(paths));
    const stageFailures = [];
    const sources = {};
    for (const [kind, productPath] of Object.entries(paths)) {
      if (!await pathExists(productPath)) {
        stageFailures.push(`Missing ${productPath}.`);
        sources[kind] = '';
      } else {
        sources[kind] = await fs.readFile(productPath, 'utf8');
      }
    }
    if (sources.uglirJson) {
      try {
        stageFailures.push(...validateExperimentalReflection(
          stageDefinition, JSON.parse(sources.uglirJson)));
      } catch (error) {
        stageFailures.push(`Invalid UGLIR JSON: ${error.message}.`);
      }
    }
    if (!sources.uglirText.includes(`module "${stageDefinition.moduleName}"`)) {
      stageFailures.push(`UGLIR text does not identify ${stageDefinition.moduleName}.`);
    }
    for (const token of stageDefinition.requiredUglirTokens) {
      if (!sources.uglirText.includes(token)) {
        stageFailures.push(`UGLIR text is missing '${token}'.`);
      }
    }
    if (!sources.msl.includes(stageDefinition.mslEntry)) {
      stageFailures.push(`MSL is missing '${stageDefinition.mslEntry}'.`);
    }
    if (!sources.spirvWords.includes('word_count') ||
        !sources.spirvWords.includes('0x07230203')) {
      stageFailures.push('Direct-SPIR-V words are missing the module header.');
    }
    if (!sources.spirvAssembly.includes(stageDefinition.spirvEntry)) {
      stageFailures.push(
        `Direct-SPIR-V assembly is missing '${stageDefinition.spirvEntry}'.`);
    }
    failures.push(...stageFailures.map(
      (failure) => `${stageDefinition.baseName}: ${failure}`));
    stageReports.push({
      baseName: stageDefinition.baseName,
      stage: stageDefinition.stage,
      paths,
      status: stageFailures.length === 0 ? 'pass' : 'fail',
      failures: stageFailures
    });
  }
  return { productPaths, stageReports, failures };
}

/** Validates one isolated Legacy or Experimental generated-product directory. */
async function validateGeneratedPipeline(generatedRoot, pipeline) {
  const generatedDirectory = path.join(generatedRoot, pipeline, caseName, 'UGLBin');
  const exportsPath = path.join(generatedDirectory, 'exports.hpp');
  const generatedHeaderPath = path.join(generatedDirectory, 'generate_result.hpp');
  if (!await pathExists(exportsPath) || !await pathExists(generatedHeaderPath)) {
    return {
      pipeline,
      generatedDirectory,
      abiSignature: null,
      status: 'fail',
      failures: [`${caseName}/${pipeline}: generated host products are missing.`],
      products: []
    };
  }
  const [exportsSource, generatedSource] = await Promise.all([
    fs.readFile(exportsPath, 'utf8'),
    fs.readFile(generatedHeaderPath, 'utf8')
  ]);
  const result = {
    ...validateGeneratedSourceContract(pipeline, generatedSource, exportsSource),
    generatedDirectory,
    exportsPath,
    generatedHeaderPath,
    products: []
  };
  if (pipeline === 'experimental') {
    const products = await validateExperimentalProducts(generatedDirectory);
    result.experimentalStages = products.stageReports;
    result.products = products.productPaths;
    result.failures.push(...products.failures);
    result.status = result.failures.length === 0 ? 'pass' : 'fail';
  }
  return result;
}

/** Rejects Oracle files that differ from the immutable r185 capture digests. */
export function validateOracleSha256(scenarioId, extension, actualSha256) {
  const expectedSha256 = oracleSha256[scenarioId]?.[extension];
  if (expectedSha256 === undefined) {
    return [`No locked Oracle SHA-256 for ${scenarioId}.${extension}.`];
  }
  return actualSha256 === expectedSha256
    ? []
    : [`oracle.${scenarioId}.${extension}.sha256=${actualSha256}, expected ${expectedSha256}.`];
}

/** Validates immutable Three r185 Oracle identity and image-layout metadata. */
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
    referenceCaptureMode: 'single-canvas',
    inputReplay: null,
    externalAssetMap: null
  }, `oracle.${scenario.id}`, failures);
  return failures;
}

/** Loads and validates both immutable Oracle images from one explicit root. */
async function loadOracleImages(oracleRoot) {
  const images = new Map();
  for (const scenario of partialupdateScenarios) {
    const oracleBase = path.join(oracleRoot, caseId, scenario.id);
    const rgbaPath = `${oracleBase}.rgba`;
    const metadataPath = `${oracleBase}.json`;
    const [rgbaDigest, metadataDigest, image] = await Promise.all([
      sha256File(rgbaPath),
      sha256File(metadataPath),
      loadRgbaArtifact(rgbaPath, metadataPath)
    ]);
    const failures = validateOracleMetadata(image.metadata, scenario);
    failures.push(...validateOracleSha256(scenario.id, 'rgba', rgbaDigest));
    failures.push(...validateOracleSha256(scenario.id, 'json', metadataDigest));
    if (failures.length > 0) throw new Error(failures.join('\n'));
    images.set(scenario.id, {
      ...image,
      sha256: Object.freeze({ rgba: rgbaDigest, json: metadataDigest })
    });
  }
  return images;
}

/** Returns deterministic output paths for one quadrant repetition. */
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

/** Executes one generated host with a hard watchdog and bounded logs. */
async function executeHost(executable, argumentsList, timeoutMs) {
  return new Promise((resolve, reject) => {
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

/** Validates one host capture's metadata, structure, and non-empty image content. */
async function validateQuadrantArtifacts(scenario, pipeline, backend, artifacts) {
  const [image, snapshotText] = await Promise.all([
    loadRgbaArtifact(artifacts.rgbaPath, artifacts.metadataPath),
    fs.readFile(artifacts.snapshotPath, 'utf8')
  ]);
  const snapshot = JSON.parse(snapshotText);
  const failures = [];
  validateExpectedFields(image.metadata, {
    schemaVersion: 1,
    source: 'gvm-three-r185',
    caseId,
    scenarioId: scenario.id,
    pipeline,
    backend,
    randomSeed,
    frame: scenario.frame,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm'
  }, 'metadata', failures);
  validateExpectedFields(snapshot, buildExpectedSnapshot(scenario), 'snapshot', failures);
  const imageInspection = inspectRgbaPixels(image.pixels);
  if (imageInspection.nonBlackPixels < 100_000 ||
      imageInspection.uniqueRgbColorCount < 1_000) {
    failures.push('capture is empty or lacks the authored checkerboard detail.');
  }
  if (imageInspection.nonOpaquePixels !== 0) {
    failures.push(`capture contains ${imageInspection.nonOpaquePixels} non-opaque pixels.`);
  }
  return { image, snapshot, imageInspection, failures };
}

/** Executes and validates one repetition of one matrix quadrant. */
async function runQuadrantRepeat(
  context,
  scenario,
  pipeline,
  backend,
  repeatIndex,
  oracleImage) {
  const artifacts = makeArtifactPaths(
    context.outputRoot, scenario, pipeline, backend, repeatIndex);
  await fs.mkdir(artifacts.artifactDirectory, { recursive: true });
  const executable = path.join(context.binaryRoot, `${caseName}-${pipeline}`);
  const argumentsList = buildHostArguments(
    scenario, pipeline, backend, context.assetRoot, artifacts);
  const result = {
    repeat: repeatIndex + 1,
    status: 'fail',
    artifacts,
    failures: []
  };
  if (!await pathExists(executable)) {
    result.failures.push(`Missing host executable: ${executable}.`);
    return result;
  }
  const execution = await executeHost(executable, argumentsList, context.timeoutMs);
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
        scenario, pipeline, backend, artifacts);
      result.validation = {
        snapshot: validation.snapshot,
        imageInspection: validation.imageInspection
      };
      result.failures.push(...validation.failures);
      const oracleComparison = compareThreeCaptures(oracleImage, validation.image);
      result.oracleComparison = oracleComparison;
      result.failures.push(...oracleComparison.failures);
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

/** Executes all locked repetitions and proves byte-exact stability for one quadrant. */
async function runQuadrant(context, scenario, pipeline, backend, oracleImage) {
  const repetitions = [];
  for (let repeatIndex = 0; repeatIndex < context.repeatCount; repeatIndex += 1) {
    repetitions.push(await runQuadrantRepeat(
      context, scenario, pipeline, backend, repeatIndex, oracleImage));
  }
  const failures = [];
  for (const repetition of repetitions) {
    failures.push(...repetition.failures.map(
      (failure) => `repeat-${repetition.repeat}: ${failure}`));
  }
  const stability = {
    repeatCount: context.repeatCount,
    rgbaSha256: repetitions.map((repetition) => repetition.sha256?.rgba ?? null),
    metadataSha256: repetitions.map((repetition) => repetition.sha256?.metadata ?? null),
    snapshotSha256: repetitions.map((repetition) => repetition.sha256?.snapshot ?? null),
    status: 'fail',
    failures: []
  };
  for (const [label, digests] of [
    ['RGBA', stability.rgbaSha256],
    ['metadata', stability.metadataSha256],
    ['snapshot', stability.snapshotSha256]
  ]) {
    if (digests.some((digest) => digest === null) || new Set(digests).size !== 1) {
      stability.failures.push(`${label} SHA-256 differs across locked repetitions.`);
    }
  }
  stability.status = stability.failures.length === 0 ? 'pass' : 'fail';
  failures.push(...stability.failures);
  return {
    scenarioId: scenario.id,
    frame: scenario.frame,
    pipeline,
    backend,
    status: failures.length === 0 ? 'pass' : 'fail',
    captureStatus: repetitions[0]?.status ?? 'fail',
    artifacts: repetitions[0]?.artifacts ?? null,
    oracleComparison: repetitions[0]?.oracleComparison ?? null,
    repetitions,
    stability,
    failures
  };
}

/** Executes the four mandatory cross-quadrant image comparisons for one scenario. */
async function compareScenarioQuadrants(scenario, quadrants) {
  const byKey = new Map(
    quadrants.map((quadrant) => [`${quadrant.pipeline}|${quadrant.backend}`, quadrant]));
  const comparisons = [];
  for (const definition of parityDefinitions) {
    const left = byKey.get(`${definition.leftPipeline}|${definition.leftBackend}`);
    const right = byKey.get(`${definition.rightPipeline}|${definition.rightBackend}`);
    const comparison = {
      scenarioId: scenario.id,
      relation: definition.relation,
      status: 'fail',
      failures: []
    };
    if (!left || !right || left.captureStatus !== 'pass' || right.captureStatus !== 'pass') {
      comparison.failures.push(
        'Both quadrants must pass capture validation before parity comparison.');
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

/** Runs the complete partial-update Phase 1 fixture and writes its machine report. */
async function main() {
  const options = parseArguments(process.argv);
  const context = {
    binaryRoot: requirePathOption(options, 'binary-root'),
    generatedRoot: requirePathOption(options, 'generated-root'),
    outputRoot: requirePathOption(options, 'output-dir'),
    oracleRoot: requirePathOption(options, 'oracle-root'),
    assetRoot: requirePathOption(options, 'asset-root'),
    timeoutMs: parsePositiveInteger(options['timeout-ms'], 60_000, '--timeout-ms'),
    repeatCount: parsePositiveInteger(options.repeat, 3, '--repeat')
  };
  await fs.mkdir(context.outputRoot, { recursive: true });

  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const manifestFailures = validateManifestContract(manifest);
  const checkerboardPath = path.join(context.assetRoot, checkerboardRelativePath);
  const actualCheckerboardSha256 = await sha256File(checkerboardPath);
  const assetFailures = actualCheckerboardSha256 === checkerboardSha256
    ? []
    : [
      `checkerboard SHA-256=${actualCheckerboardSha256}, expected ${checkerboardSha256}.`
    ];
  if (manifestFailures.length > 0 || assetFailures.length > 0) {
    throw new Error([...manifestFailures, ...assetFailures].join('\n'));
  }

  const oracleImages = await loadOracleImages(context.oracleRoot);
  const generatedEntries = [];
  for (const pipeline of pipelines) {
    generatedEntries.push(await validateGeneratedPipeline(context.generatedRoot, pipeline));
  }
  const generatedAbiParity = validateGeneratedAbiParity(generatedEntries);
  const generatedArtifacts = {
    status: generatedEntries.every((entry) => entry.status === 'pass') &&
      generatedAbiParity.status === 'pass'
      ? 'pass'
      : 'fail',
    entries: generatedEntries,
    abiParity: generatedAbiParity
  };

  const quadrants = [];
  const comparisons = [];
  for (const scenario of partialupdateScenarios) {
    const scenarioQuadrants = [];
    for (const pipeline of pipelines) {
      for (const backend of backends) {
        const result = await runQuadrant(
          context, scenario, pipeline, backend, oracleImages.get(scenario.id));
        quadrants.push(result);
        scenarioQuadrants.push(result);
        console.log(
          `[${result.status.toUpperCase()}] ${caseId}/${scenario.id} ${pipeline}/${backend} ` +
          `${context.repeatCount}x`);
        for (const failure of result.failures) console.log(`  ${failure}`);
      }
    }
    comparisons.push(...await compareScenarioQuadrants(scenario, scenarioQuadrants));
  }

  const status = generatedArtifacts.status === 'pass' &&
    quadrants.length === partialupdateScenarios.length * pipelines.length * backends.length &&
    quadrants.every((entry) => entry.status === 'pass') &&
    comparisons.length === partialupdateScenarios.length * parityDefinitions.length &&
    comparisons.every((entry) => entry.status === 'pass')
    ? 'pass'
    : 'fail';
  const report = {
    schemaVersion: 1,
    gate: 'three-r185-webgl-materials-texture-partialupdate',
    caseId,
    status,
    comparisonThresholds,
    repeatCount: context.repeatCount,
    manifest: {
      path: manifestPath,
      status: 'pass',
      failures: manifestFailures
    },
    asset: {
      path: checkerboardPath,
      sha256: actualCheckerboardSha256,
      expectedSha256: checkerboardSha256,
      status: 'pass',
      failures: assetFailures
    },
    oracles: partialupdateScenarios.map((scenario) => ({
      scenarioId: scenario.id,
      rgbaPath: path.join(context.oracleRoot, caseId, `${scenario.id}.rgba`),
      metadataPath: path.join(context.oracleRoot, caseId, `${scenario.id}.json`),
      sha256: oracleImages.get(scenario.id).sha256,
      metadata: oracleImages.get(scenario.id).metadata
    })),
    generatedArtifacts,
    quadrants,
    comparisons
  };
  const reportPath = path.join(context.outputRoot, 'summary.json');
  await fs.writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`, 'utf8');

  for (const entry of generatedEntries) {
    console.log(`[${entry.status.toUpperCase()}] ${caseId} ${entry.pipeline} generated products`);
    for (const failure of entry.failures) console.log(`  ${failure}`);
  }
  console.log(`[${generatedAbiParity.status.toUpperCase()}] ${caseId} Legacy/Experimental ABI`);
  for (const comparison of comparisons) {
    console.log(
      `[${comparison.status.toUpperCase()}] ${caseId}/${comparison.scenarioId} ` +
      comparison.relation);
    for (const failure of comparison.failures) console.log(`  ${failure}`);
  }
  console.log(`webgl_materials_texture_partialupdate report: ${reportPath}`);
  if (status !== 'pass') process.exitCode = 1;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.stack ?? error.message : String(error));
    process.exitCode = 1;
  });
}
