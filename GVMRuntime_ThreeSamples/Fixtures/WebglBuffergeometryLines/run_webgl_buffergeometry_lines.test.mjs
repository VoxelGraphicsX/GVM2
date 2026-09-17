import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  lineScenarios,
  parityDefinitions,
  parseArguments,
  validateExpectedFields,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_buffergeometry_lines.mjs';

const emptyRenderSetExports = `
namespace ExportedRenderSet
{
};
`;

const validGeneratedSource = `
static const uint WebglBuffergeometryLinesVertexCount = 10000u;
class WebglBuffergeometryLinesMainPass : public GVM::Core::IRenderClass {};
layoutEntry[0].buffer.type = GVM::RHI::BufferBindingType::Uniform;
vertexState.buffers[0].arrayStride = 36;
vertexState.buffers[0].attributes[0].format = GVM::RHI::VertexFormat::Float32x3;
vertexState.buffers[0].attributes[0].offset = offsetof(WebglBuffergeometryLinesVertex, position);
vertexState.buffers[0].attributes[1].format = GVM::RHI::VertexFormat::Float32x3;
vertexState.buffers[0].attributes[1].offset = offsetof(WebglBuffergeometryLinesVertex, morphPosition);
vertexState.buffers[0].attributes[2].format = GVM::RHI::VertexFormat::Float32x3;
vertexState.buffers[0].attributes[2].offset = offsetof(WebglBuffergeometryLinesVertex, color);
setPrimitiveTopology(GVM::RHI::PrimitiveTopology::LineStrip);
vertexBufferUsage = GVM::RHI::BufferUsage::Vertex | GVM::RHI::BufferUsage::CopyDst;
graphicsQueue->renderPass("main-line-strip", frameBuffer,
  mainPass->setVertexBuffer(vertexBuffer),
  mainPass->run(WebglBuffergeometryLinesVertexCount, 1u, 0u, 0u));
`;

/** Creates one minimal valid Experimental stage product set for pure logic tests. */
function makeValidExperimentalStageSources(stage) {
  return {
    uglirJson: JSON.stringify({
      schemaVersion: 1,
      name: `WebglBuffergeometryLinesMainPass.${stage}`,
      reflection: {
        entryName: `WebglBuffergeometryLinesMainPass.${stage}`,
        stage
      }
    }),
    uglirText: stage === 'vertex'
      ? `module "WebglBuffergeometryLinesMainPass.vertex" stage vertex morphPosition intrinsic math_lerp`
      : `module "WebglBuffergeometryLinesMainPass.fragment" stage fragment`,
    msl: stage === 'vertex' ? 'vertex void vertexMain() {}' : 'fragment void fragmentMain() {}',
    spirvWords: 'word_count 8 0x07230203',
    spirvAssembly: stage === 'vertex'
      ? 'OpEntryPoint Vertex %vertexMain "vertexMain"'
      : 'OpEntryPoint Fragment %fragmentMain "fragmentMain"'
  };
}

/** Verifies strict explicit-path parsing and duplicate rejection. */
test('parseArguments accepts the four required fixture paths and rejects duplicates', () => {
  const options = parseArguments([
    'node', 'fixture.mjs',
    '--binary-root', '/binaries',
    '--generated-root', '/generated',
    '--output-dir', '/output',
    '--oracle-root', '/oracles'
  ]);
  assert.deepEqual(options, {
    'binary-root': '/binaries',
    'generated-root': '/generated',
    'output-dir': '/output',
    'oracle-root': '/oracles'
  });
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--oracle-root', '/one', '--oracle-root', '/two'
  ]), /Duplicate --oracle-root/u);
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--output-dir'
  ]), /Expected --option value pair/u);
});

/** Verifies scenario identity and output paths in the generated host CLI. */
test('buildHostArguments locks initial and animated morph frames', () => {
  const artifacts = {
    rgbaPath: '/output/capture.rgba',
    metadataPath: '/output/capture.json',
    snapshotPath: '/output/snapshot.json'
  };
  const initial = buildHostArguments(
    lineScenarios[0], 'legacy', 'metal', artifacts);
  assert.equal(initial[initial.indexOf('--scenario-id') + 1], 'initial');
  assert.equal(initial[initial.indexOf('--frame') + 1], '0');
  assert.equal(initial[initial.indexOf('--backend') + 1], 'metal');

  const animated = buildHostArguments(
    lineScenarios[1], 'experimental', 'vulkan', artifacts);
  assert.equal(animated[animated.indexOf('--scenario-id') + 1], 'animated-morph');
  assert.equal(animated[animated.indexOf('--frame') + 1], '60');
  assert.equal(animated[animated.indexOf('--pipeline') + 1], 'experimental');
});

/** Verifies the exact simple-Scene, LineStrip, and morph structural contract. */
test('buildExpectedSnapshot locks RenderSet-free LineStrip morph state', () => {
  const initial = buildExpectedSnapshot(lineScenarios[0]);
  assert.equal(initial.sceneRenderSetCount, 0);
  assert.equal(initial.renderableObjectCount, 1);
  assert.equal(initial.scenePassCount, 1);
  assert.equal(initial.screenPassCount, 0);
  assert.equal(initial.drawCommandCount, 1);
  assert.equal(initial.explicitVertexCount, 10_000);
  assert.equal(initial.primitiveTopology, 'line-strip');
  assert.equal(initial.morphWeight, 0);
  assert.deepEqual(initial.scenePassSequence, [{
    sceneRoot: 'scene',
    scenePass: 'main-line-strip',
    entityOrdinal: 0
  }]);

  const animated = buildExpectedSnapshot(lineScenarios[1]);
  assert.equal(animated.frame, 60);
  assert.equal(animated.timeSeconds, 1);
  assert.equal(animated.rotationX, 0.25);
  assert.equal(animated.rotationY, 0.5);
  assert.ok(Math.abs(animated.morphWeight - 0.479425538604203) < 1e-15);
});

/** Verifies nested field checks tolerate serialized Float32 values but report drift. */
test('validateExpectedFields accepts Float32 morph precision and reports nested drift', () => {
  const failures = [];
  validateExpectedFields({
    morphWeight: 0.47942555,
    scenePassSequence: [{ scenePass: 'wrong-pass' }]
  }, {
    morphWeight: Math.sin(0.5),
    scenePassSequence: [{ scenePass: 'main-line-strip' }]
  }, 'snapshot', failures);
  assert.deepEqual(failures, [
    'snapshot.scenePassSequence[0].scenePass="wrong-pass", expected "main-line-strip".'
  ]);
});

/** Verifies clear-only captures are distinguishable from visible opaque line pixels. */
test('inspectRgbaPixels identifies non-empty opaque line content', () => {
  const clear = inspectRgbaPixels(new Uint8Array([
    0, 0, 0, 255,
    0, 0, 0, 255
  ]));
  assert.deepEqual(clear, {
    nonBlackPixels: 0,
    nonOpaquePixels: 0,
    uniqueRgbColorCount: 1
  });

  const visible = inspectRgbaPixels(new Uint8Array([
    0, 0, 0, 255,
    10, 20, 30, 255,
    30, 20, 10, 128
  ]));
  assert.deepEqual(visible, {
    nonBlackPixels: 2,
    nonOpaquePixels: 1,
    uniqueRgbColorCount: 3
  });
  assert.throws(() => inspectRgbaPixels(new Uint8Array(3)), /tightly packed RGBA8/u);
});

/** Verifies the generated host contract and Legacy/Experimental ABI equality. */
test('validateGeneratedSourceContract locks standalone LineStrip draw ABI', () => {
  const legacy = validateGeneratedSourceContract(
    'legacy', validGeneratedSource, emptyRenderSetExports);
  const experimental = validateGeneratedSourceContract(
    'experimental', validGeneratedSource, emptyRenderSetExports);
  assert.equal(legacy.status, 'pass');
  assert.deepEqual(legacy.abiSignature, {
    renderSetExportCount: 0,
    uniformBinding: 0,
    vertexStrideBytes: 36,
    vertexAttributes: [
      { index: 0, format: 'Float32x3', field: 'position' },
      { index: 1, format: 'Float32x3', field: 'morphPosition' },
      { index: 2, format: 'Float32x3', field: 'color' }
    ],
    primitiveTopology: 'LineStrip',
    standaloneVertexBufferCount: 1,
    indexed: false,
    explicitVertexCount: 10_000,
    explicitInstanceCount: 1
  });
  assert.equal(validateGeneratedAbiParity([legacy, experimental]).status, 'pass');

  const invalid = validateGeneratedSourceContract(
    'legacy', `${validGeneratedSource}\nRenderSet<Unexpected> sceneSet;`, emptyRenderSetExports);
  assert.equal(invalid.status, 'fail');
  assert.match(invalid.failures.join('\n'), /must not declare a RenderSet/u);
});

/** Verifies all Experimental shader product families carry valid stage identities. */
test('validateExperimentalProductSources accepts UGLIR MSL and direct SPIR-V stages', () => {
  assert.deepEqual(validateExperimentalProductSources({
    vertex: makeValidExperimentalStageSources('vertex'),
    fragment: makeValidExperimentalStageSources('fragment')
  }), []);
});

/** Verifies the Oracle identity and the four required parity relation types. */
test('validateOracleMetadata locks r185 identity and exposes four parity relations', () => {
  const metadata = {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId: 'webgl_buffergeometry_lines',
    scenarioId: 'initial',
    frame: 0,
    randomSeed: 305419896,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: null
  };
  assert.deepEqual(validateOracleMetadata(metadata, lineScenarios[0]), []);
  assert.deepEqual(parityDefinitions.map((definition) => definition.relation), [
    'pipeline-parity-metal',
    'pipeline-parity-vulkan',
    'backend-parity-legacy',
    'backend-parity-experimental'
  ]);
});

/** Verifies that changed Oracle bytes cannot retain a trusted r185 identity. */
test('validateOracleSha256 rejects line Oracle digest drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial',
    'rgba',
    'fda2d1a8cb074fe6c744eec2ee5f3314f132cef36643499f0ea0e330e154bdf4'
  ), []);
  assert.deepEqual(validateOracleSha256(
    'animated-morph',
    'json',
    '91ea59ef2d75762d10174463ab83ee3294fd160faa6e8da540b14586bdb22d17'
  ), []);
  assert.match(
    validateOracleSha256('initial', 'rgba', '0'.repeat(64)).join('\n'),
    /oracle\.initial\.rgba\.sha256/u);
  assert.throws(
    () => validateOracleSha256('unknown', 'rgba', '0'.repeat(64)),
    /No locked Oracle SHA-256/u);
});
