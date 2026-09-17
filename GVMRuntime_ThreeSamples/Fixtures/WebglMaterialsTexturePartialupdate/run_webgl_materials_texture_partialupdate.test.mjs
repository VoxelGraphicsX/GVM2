import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  parseArguments,
  parsePositiveInteger,
  partialupdateScenarios,
  validateExperimentalReflection,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_materials_texture_partialupdate.mjs';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const manifestPath = path.resolve(
  scriptDirectory, '../../Manifest/three-r185-manifest.json');

test('parseArguments requires strict unique option-value pairs', () => {
  assert.deepEqual(parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/assets', '--repeat', '3'
  ]), {
    'asset-root': '/assets',
    repeat: '3'
  });
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/one', '--asset-root', '/two'
  ]), /Duplicate --asset-root/u);
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--asset-root'
  ]), /Expected --option value pair/u);
});

test('parsePositiveInteger rejects zero, fractions, and non-numeric values', () => {
  assert.equal(parsePositiveInteger(undefined, 3, '--repeat'), 3);
  assert.equal(parsePositiveInteger('4', 3, '--repeat'), 4);
  assert.throws(() => parsePositiveInteger('0', 3, '--repeat'), /positive integer/u);
  assert.throws(() => parsePositiveInteger('1.5', 3, '--repeat'), /positive integer/u);
  assert.throws(() => parsePositiveInteger('x', 3, '--repeat'), /positive integer/u);
});

test('buildHostArguments locks identity, seed, extent, asset root, and outputs', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/snapshot.json'
  };
  assert.deepEqual(buildHostArguments(
    partialupdateScenarios[1], 'experimental', 'vulkan', '/assets', artifacts), [
    '--case-id', 'webgl_materials_texture_partialupdate',
    '--scenario-id', 'patched',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '42',
    '--asset-root', '/assets',
    '--width', '800',
    '--height', '500',
    '--frame', '60',
    '--capture-rgba', '/out/capture.rgba',
    '--capture-metadata', '/out/capture.json',
    '--scene-snapshot', '/out/snapshot.json'
  ]);
});

test('formal manifest locks the required ordinary-Scene implementation', async () => {
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  assert.deepEqual(validateManifestContract(manifest), []);
  const example = manifest.examples.find(
    (entry) => entry.id === 'webgl_materials_texture_partialupdate');
  assert.equal(validateManifestContract({
    examples: [{ ...example, renderSetPolicy: 'required' }]
  }).length, 1);
});

test('buildExpectedSnapshot distinguishes initial and nine-patch states', () => {
  const initial = buildExpectedSnapshot(partialupdateScenarios[0]);
  const patched = buildExpectedSnapshot(partialupdateScenarios[1]);
  assert.equal(initial.patchCount, 0);
  assert.equal(patched.patchCount, 9);
  assert.equal(initial.sceneRenderSetCount, 0);
  assert.equal(patched.sceneRoots[0].renderSetCount, 0);
  assert.equal(patched.sceneRoots[0].scenePasses[0].drawMode, 'explicit-indexed');
  assert.equal(patched.explicitIndexCount, 6);
  assert.equal(patched.drawCommandCount, 1);
  assert.equal(patched.computePassCount, 2);
  assert.equal(patched.textureAssetSha256,
    'd7547036c6221a840b80ce11138dc2c1a26df2630419217234c479b00e50a119');
});

test('Oracle SHA and metadata locks reject identity drift', () => {
  assert.deepEqual(validateOracleSha256(
    'patched',
    'rgba',
    'a58e742b26f40211d12af7db9e869e2cff69683a5ff2735f70512b1cd3fb2550'), []);
  assert.equal(validateOracleSha256('patched', 'rgba', '0'.repeat(64)).length, 1);
  const scenario = partialupdateScenarios[0];
  const metadata = {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId: 'webgl_materials_texture_partialupdate',
    scenarioId: 'initial-loader',
    frame: 0,
    randomSeed: 42,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    referenceCaptureMode: 'single-canvas',
    inputReplay: null,
    externalAssetMap: null
  };
  assert.deepEqual(validateOracleMetadata(metadata, scenario), []);
  assert.equal(validateOracleMetadata({ ...metadata, randomSeed: 1 }, scenario).length, 1);
});

test('inspectRgbaPixels rejects empty or malformed captures and counts content', () => {
  assert.deepEqual(inspectRgbaPixels(new Uint8Array([
    0, 0, 0, 255,
    1, 2, 3, 255,
    1, 2, 3, 128
  ])), {
    nonBlackPixels: 2,
    nonOpaquePixels: 1,
    uniqueRgbColorCount: 2
  });
  assert.throws(() => inspectRgbaPixels(new Uint8Array(3)), /tightly packed RGBA8/u);
});

test('generated source contract locks indexed drawing and compute texture flow', () => {
  const generatedSource = `
class WebglMaterialsTexturePartialupdateBaseCopyPass {};
class WebglMaterialsTexturePartialupdatePatchPass {};
class WebglMaterialsTexturePartialupdateMainPass {};
GVM::RHI::PrimitiveTopology::TriangleList;
GVM::RHI::VertexFormat::Float32x3;
GVM::RHI::VertexFormat::Float32x2;
GVM::RHI::BufferUsage::Index|GVM::RHI::BufferUsage::CopyDst;
sizeof(unsigned int);
GVM::RHI::TextureFormat::RGBA8UnormSrgb;
GVM::RHI::TextureFormat::RGBA16Float;
GVM::RHI::TextureUsage::StorageBinding|GVM::RHI::TextureUsage::TextureBinding;
vertexState.buffers[0].arrayStride = 20;
vertexState.buffers[0].attributes[0].format = GVM::RHI::VertexFormat::Float32x3;
vertexState.buffers[0].attributes[1].format = GVM::RHI::VertexFormat::Float32x2;
mainPass0->setVertexBuffer(vertexBuffer); mainPass0->setIndexBuffer(indexBuffer);
mainPass0->run(6u, 1u, 0u, 0, 0u);
computePass("partial-texture-base-copy", baseCopyPass->run(512u, 512u, 1u));
computePass("partial-texture-update-compute", patchPass->run(32u, 32u, 9u));
`;
  const result = validateGeneratedSourceContract(
    'legacy', generatedSource, 'namespace ExportedRenderSet {};');
  assert.equal(result.status, 'pass', result.failures.join('\n'));
  assert.equal(result.abiSignature.vertexStrideBytes, 20);
  assert.equal(result.abiSignature.indexElementType, 'Uint32');
  assert.equal(result.abiSignature.explicitIndexCount, 6);
  assert.equal(result.abiSignature.coverageDrawCount, 1);
});

test('generated ABI parity rejects a pipeline layout drift', () => {
  const signature = {
    renderSetExportCount: 0,
    vertexStrideBytes: 20,
    vertexAttributes: [],
    explicitIndexCount: 6
  };
  assert.equal(validateGeneratedAbiParity([
    { pipeline: 'legacy', abiSignature: signature },
    { pipeline: 'experimental', abiSignature: signature }
  ]).status, 'pass');
  assert.equal(validateGeneratedAbiParity([
    { pipeline: 'legacy', abiSignature: signature },
    {
      pipeline: 'experimental',
      abiSignature: { ...signature, explicitIndexCount: 3 }
    }
  ]).status, 'fail');
});

test('Experimental reflection locks stage identity and resource binding', () => {
  const definition = {
    baseName: 'PatchPass',
    moduleName: 'PatchPass',
    entryName: 'PatchPass.compute',
    stage: 'compute',
    requiredResources: [{
      name: 'bindGroup.output',
      kind: 'storage_texture',
      bindingIndex: 3,
      textureFormat: 'RGBA16Float'
    }]
  };
  const document = {
    schemaVersion: 1,
    name: 'PatchPass',
    reflection: {
      entryName: 'PatchPass.compute',
      stage: 'compute',
      entryKind: 'compute',
      resources: [{
        name: 'bindGroup.output',
        kind: 'storage_texture',
        bindingIndex: 3,
        textureFormat: 'RGBA16Float'
      }]
    }
  };
  assert.deepEqual(validateExperimentalReflection(definition, document), []);
  document.reflection.resources[0].bindingIndex = 2;
  assert.equal(validateExperimentalReflection(definition, document).length, 1);
});
