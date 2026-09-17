import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  integerAttributeScenarios,
  parseArguments,
  validateExpectedFields,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_buffergeometry_attributes_integer.mjs';

test('parseArguments requires strict unique option-value pairs', () => {
  assert.deepEqual(parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/assets', '--oracle-root', '/oracles'
  ]), {
    'asset-root': '/assets',
    'oracle-root': '/oracles'
  });
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--asset-root', '/one', '--asset-root', '/two'
  ]), /Duplicate --asset-root/u);
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--asset-root'
  ]), /Expected --option value pair/u);
});

test('buildHostArguments locks seed, extent, frame, assets, and outputs', () => {
  const artifacts = {
    rgbaPath: '/out/capture.rgba',
    metadataPath: '/out/capture.json',
    snapshotPath: '/out/snapshot.json'
  };
  const argumentsList = buildHostArguments(
    integerAttributeScenarios[1], 'experimental', 'vulkan', '/assets', artifacts);
  assert.deepEqual(argumentsList, [
    '--case-id', 'webgl_buffergeometry_attributes_integer',
    '--scenario-id', 'animated',
    '--pipeline', 'experimental',
    '--backend', 'vulkan',
    '--random-seed', '407896065',
    '--asset-root', '/assets',
    '--width', '800',
    '--height', '500',
    '--frame', '60',
    '--capture-rgba', '/out/capture.rgba',
    '--capture-metadata', '/out/capture.json',
    '--scene-snapshot', '/out/snapshot.json'
  ]);
});

test('Oracle SHA and metadata locks reject drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial', 'rgba',
    '5fa9764503bce8eef6f4e0346cfef0ab5b36ab99134ebda5c4b99ea99c2db937'
  ), []);
  assert.equal(validateOracleSha256('initial', 'rgba', '0'.repeat(64)).length, 1);
  const scenario = integerAttributeScenarios[0];
  const metadata = {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId: 'webgl_buffergeometry_attributes_integer',
    scenarioId: 'initial',
    frame: 0,
    randomSeed: 407896065,
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

test('buildExpectedSnapshot records ordinary Scene and integer transport semantics', () => {
  const snapshot = buildExpectedSnapshot(integerAttributeScenarios[1]);
  assert.equal(snapshot.sceneRenderSetCount, 0);
  assert.equal(snapshot.renderableObjectCount, 1);
  assert.equal(snapshot.sourceIntegerAttributeType, 'int16');
  assert.equal(snapshot.gpuIntegerAttributeType, 'sint32');
  assert.equal(snapshot.integerVaryingInterpolation, 'flat');
  assert.equal(snapshot.textureCount, 3);
  assert.deepEqual(snapshot.textureMipCounts, [9, 10, 12]);
  assert.equal(snapshot.drawCommandCount, 1);
  assert.equal(snapshot.computePassCount, 0);
  assert.equal(snapshot.sceneRoots[0].renderSetCount, 0);
});

test('validateManifestContract locks the required ordinary-Scene policy', () => {
  const scenarios = integerAttributeScenarios.map((scenario) => ({
    id: scenario.id,
    frame: scenario.frame,
    inputReplay: null,
    canonicalState: scenario.canonicalState,
    scenePassInvocations: [{
      sceneRoot: 'scene',
      scenePass: 'main-textured',
      invocationCount: 1
    }]
  }));
  const example = {
    id: 'webgl_buffergeometry_attributes_integer',
    upstreamPath: 'examples/webgl_buffergeometry_attributes_integer.html',
    status: 'phase1_required',
    capabilityAudit: {
      state: 'supported',
      gpuWorkDslOnly: true,
      requiresNewPublicCapability: false,
      missingCapabilities: [],
      availableCapabilities: [
        'signed_integer_vertex_attribute',
        'flat_integer_varying',
        'texture2d_sampling',
        'three_texture_material_selection'
      ]
    },
    renderSetPolicy: 'not-required',
    renderSetReasons: [],
    sceneRoots: [{
      name: 'scene', renderSetRuntimeInstanceCount: 0, renderSetType: null
    }],
    renderableObjectCount: 1,
    containsInstancing: false,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    scenePasses: [{
      name: 'main-textured',
      renderClass: 'WebglBuffergeometryAttributesIntegerMainPass',
      sceneRoot: 'scene',
      renderSetBindingCount: 0,
      usesStandaloneGeometry: true,
      usesExplicitDrawCount: true
    }],
    screenPasses: [],
    renderSetType: null,
    componentSchema: [],
    dslShard: 'WebglBuffergeometryAttributesInteger',
    scenarios,
    deferredEvidence: null
  };
  assert.deepEqual(validateManifestContract({ examples: [example] }), []);
  assert.equal(validateManifestContract({
    examples: [{ ...example, renderSetPolicy: 'required' }]
  }).length, 1);
});

test('validateExpectedFields permits diagnostics but rejects semantic drift', () => {
  const failures = [];
  validateExpectedFields({ a: 1, b: { c: 2 }, diagnostics: true }, {
    a: 1,
    b: { c: 2 }
  }, 'root', failures);
  assert.deepEqual(failures, []);
  validateExpectedFields({ a: 2 }, { a: 1 }, 'root', failures);
  assert.equal(failures.length, 1);
});

test('inspectRgbaPixels counts authored color, background, and alpha properties', () => {
  const pixels = new Uint8Array([
    5, 5, 5, 255,
    100, 50, 25, 255,
    100, 50, 25, 128
  ]);
  assert.deepEqual(inspectRgbaPixels(pixels), {
    nonBackgroundPixels: 2,
    nonOpaquePixels: 1,
    uniqueRgbColorCount: 2
  });
  assert.throws(() => inspectRgbaPixels(new Uint8Array(3)), /tightly packed RGBA8/u);
});

test('generated source contract locks ordinary integer ABI and single-sample drawing', () => {
  const generatedSource = `
class WebglBuffergeometryAttributesIntegerMainPass {};
static const uint WebglBuffergeometryAttributesIntegerVertexCount = 30000u;
GVM::RHI::PrimitiveTopology::TriangleList;
GVM::RHI::TextureUsage::CopyDst;
GVM::RHI::TextureSampleType::Float;
vertexState.buffers[0].arrayStride = 24;
vertexState.buffers[0].attributes[0].format = GVM::RHI::VertexFormat::Float32x3;
vertexState.buffers[0].attributes[1].format = GVM::RHI::VertexFormat::Float32x2;
vertexState.buffers[0].attributes[2].format = GVM::RHI::VertexFormat::Sint32;
mainPass0->setVertexBuffer(vertexBuffer);
mainPass0->run(WebglBuffergeometryAttributesIntegerVertexCount, 1u, 0u, 0u);
renderPass("WebglBuffergeometryAttributesIntegerSample0");
writeTexture(crateTexture);
writeTexture(floorTexture);
writeTexture(grassTexture);
`;
  const result = validateGeneratedSourceContract(
    'legacy', generatedSource, 'namespace ExportedRenderSet {};');
  assert.equal(result.status, 'pass', result.failures.join('\n'));
  assert.equal(result.abiSignature.vertexStrideBytes, 24);
  assert.equal(result.abiSignature.coverageDrawCount, 1);
});

test('generated ABI parity rejects pipeline layout drift', () => {
  const signature = {
    renderSetExportCount: 0,
    vertexStrideBytes: 24,
    vertexAttributes: [],
    primitiveTopology: 'TriangleList'
  };
  assert.equal(validateGeneratedAbiParity([
    { pipeline: 'legacy', abiSignature: signature },
    { pipeline: 'experimental', abiSignature: signature }
  ]).status, 'pass');
  assert.equal(validateGeneratedAbiParity([
    { pipeline: 'legacy', abiSignature: signature },
    { pipeline: 'experimental', abiSignature: { ...signature, vertexStrideBytes: 28 } }
  ]).status, 'fail');
});
