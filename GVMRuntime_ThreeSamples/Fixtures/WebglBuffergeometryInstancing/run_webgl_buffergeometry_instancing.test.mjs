import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildExpectedSnapshot,
  buildHostArguments,
  inspectRgbaPixels,
  instancingScenarios,
  parityDefinitions,
  parseArguments,
  validateExperimentalProductSources,
  validateGeneratedAbiParity,
  validateGeneratedSourceContract,
  validateManifestContract,
  validateOracleMetadata,
  validateOracleSha256
} from './run_webgl_buffergeometry_instancing.mjs';

const validExports = `
namespace ExportedRenderSet { static constexpr uint64_t sceneSet = 1; }
namespace WebglBuffergeometryInstancingSceneRenderSetComponents {
static constexpr GVM::Core::RenderComponentHandle vertices = 1;
static constexpr GVM::Core::RenderComponentHandle indices = 2;
static constexpr GVM::Core::RenderComponentHandle objects = 3;
static constexpr GVM::Core::RenderComponentHandle instances = 4;
static constexpr GVM::Core::RenderComponentHandle materials = 5;
}`;

const validGenerated = `
class WebglBuffergeometryInstancingMainPass {};
sceneSet = device->createRenderSet<WebglBuffergeometryInstancingSceneRenderSet>();
this->mRenderSet = sceneSet; this->mRenderSetBindGroupIndex = 0;
sceneSet_objects_UGLGetSafe(); sceneSet_instances_UGLGetSafe(); sceneSet_materials_UGLGetSafe();
RenderEntityCMDParams; UGLLoadRenderEntityCMDParamsSafe(); CommandParams; DrawInfo;
__uglc_draw_command_params; vertexShaderArtifact_SpirvWords; fragmentShaderArtifact_SpirvWords;
GVM::RHI::BlendFactor::SrcAlpha; GVM::RHI::BlendFactor::OneMinusSrcAlpha;
graphicsQueue->renderPass("main-instanced", frameBuffer, scenePass->run());`;

const validSingleHeader = `
uint renderEntityID [[RenderEntityID]];
uint renderEntityInstanceID [[RenderEntityInstanceID]];
sceneSet->instances->get(renderEntityID, renderEntityInstanceID);
scenePass();`;

/** Creates one minimal valid Experimental product family for pure logic tests. */
function makeExperimentalProducts() {
  const products = {};
  for (const stage of ['vertex', 'fragment']) {
    products[stage] = {
      uglirJson: JSON.stringify({ reflection: { stage } }),
      uglirText: `module stage ${stage}`,
      msl: stage === 'vertex' ? 'vertex void vertexMain() {}' : 'fragment void fragmentMain() {}',
      spirvWords: '0x07230203',
      spirvAssembly: stage === 'vertex' ? 'OpEntryPoint Vertex' : 'OpEntryPoint Fragment'
    };
  }
  return products;
}

test('parseArguments accepts explicit unique pairs and rejects duplicates', () => {
  assert.deepEqual(parseArguments([
    'node', 'fixture.mjs', '--binary-root', '/bin', '--oracle-root', '/oracle'
  ]), { 'binary-root': '/bin', 'oracle-root': '/oracle' });
  assert.throws(() => parseArguments([
    'node', 'fixture.mjs', '--oracle-root', '/one', '--oracle-root', '/two'
  ]), /Duplicate --oracle-root/u);
  assert.throws(() => parseArguments(['node', 'fixture.mjs', '--output-dir']), /Expected --option value pair/u);
});

test('buildHostArguments locks the seed, extent, frame, and GUI replay', () => {
  const artifacts = { rgbaPath: '/o/a.rgba', metadataPath: '/o/a.json', snapshotPath: '/o/s.json' };
  const args = buildHostArguments(instancingScenarios[2], 'experimental', 'vulkan', '/assets', '/input.json', artifacts);
  assert.equal(args[args.indexOf('--random-seed') + 1], '407896070');
  assert.equal(args[args.indexOf('--frame') + 1], '61');
  assert.equal(args[args.indexOf('--backend') + 1], 'vulkan');
  assert.equal(args[args.indexOf('--input-replay') + 1], '/input.json');
});

test('expected snapshots lock one RenderSet, one entity, and count mutation', () => {
  const full = buildExpectedSnapshot(instancingScenarios[0]);
  assert.equal(full.sceneRenderSetCount, 1);
  assert.equal(full.entityCount, 1);
  assert.equal(full.instanceCount, 50_000);
  assert.equal(full.randomDrawCount, 750_084);
  assert.equal(full.finalRandomState, 305_162_645);
  const reduced = buildExpectedSnapshot(instancingScenarios[2]);
  assert.equal(reduced.instanceCount, 12_500);
  assert.equal(reduced.activeCountMutation, 'remove-reallocate-same-render-set');
  assert.equal(reduced.entityReallocated, true);
});

test('generated contract locks RenderEntity builtins and parameterless Scene draw', () => {
  const legacy = validateGeneratedSourceContract('legacy', validGenerated, validExports, validSingleHeader);
  const experimental = validateGeneratedSourceContract('experimental', validGenerated, validExports, validSingleHeader);
  assert.equal(legacy.status, 'pass');
  assert.equal(experimental.status, 'pass');
  assert.equal(validateGeneratedAbiParity([legacy, experimental]).status, 'pass');
  const invalid = validateGeneratedSourceContract(
    'legacy', `${validGenerated}\nscenePass->run(3u, 50000u);`, validExports, validSingleHeader);
  assert.equal(invalid.status, 'fail');
  assert.match(invalid.failures.join('\n'), /explicit draw count/u);
});

test('Experimental product validation requires UGLIR, MSL, and direct SPIR-V', () => {
  assert.deepEqual(validateExperimentalProductSources(makeExperimentalProducts()), []);
  const invalid = makeExperimentalProducts();
  invalid.vertex.spirvWords = '';
  assert.match(validateExperimentalProductSources(invalid).join('\n'), /SPIR-V word magic/u);
});

test('Oracle SHA and metadata locks reject drift', () => {
  assert.deepEqual(validateOracleSha256(
    'initial', 'rgba', '184440a84465a49e1eaeaedbcd060efc945d241abec55471eafe064712526d8c'), []);
  assert.equal(validateOracleSha256('initial', 'rgba', '0'.repeat(64)).length, 1);
  const metadata = {
    schemaVersion: 1,
    source: 'three-r185-reference',
    upstreamCommit: '2431a09f46f34c560bc8e44b33be0e567723d5b9',
    caseId: 'webgl_buffergeometry_instancing',
    scenarioId: 'initial',
    frame: 0,
    randomSeed: 407896070,
    width: 800,
    height: 500,
    rowStrideBytes: 3200,
    byteCount: 1_600_000,
    format: 'rgba8unorm',
    inputReplay: null
  };
  assert.deepEqual(validateOracleMetadata(metadata, instancingScenarios[0]), []);
});

test('manifest contract accepts the frozen single-RenderSet scene', () => {
  const example = {
    id: 'webgl_buffergeometry_instancing',
    upstreamPath: 'examples/webgl_buffergeometry_instancing.html',
    status: 'phase1_required',
    renderSetPolicy: 'required',
    renderSetReasons: ['instancing'],
    sceneRoots: [{ name: 'scene', renderSetRuntimeInstanceCount: 1, renderSetType: 'WebglBuffergeometryInstancingSceneRenderSet' }],
    renderableObjectCount: 1,
    containsInstancing: true,
    containsHierarchy: false,
    containsLod: false,
    containsDynamicObjects: false,
    containsMultipleMaterials: false,
    scenePasses: [{ name: 'main-instanced', renderClass: 'WebglBuffergeometryInstancingMainPass', sceneRoot: 'scene', renderSetBindingCount: 1, usesStandaloneGeometry: false, usesExplicitDrawCount: false }],
    screenPasses: [],
    renderSetType: 'WebglBuffergeometryInstancingSceneRenderSet',
    componentSchema: [
      { name: 'vertices', kind: 'buffer', role: 'vertex' },
      { name: 'indices', kind: 'buffer', role: 'index' },
      { name: 'objects', kind: 'buffer', role: 'object' },
      { name: 'instances', kind: 'buffer', role: 'instance' },
      { name: 'materials', kind: 'buffer', role: 'material' }
    ],
    dslShard: 'WebglBuffergeometryInstancing',
    scenarios: instancingScenarios.map((scenario) => ({
      id: scenario.id,
      frame: scenario.frame,
      canonicalState: scenario.canonicalState,
      inputReplay: scenario.replay ? 'inputs/webgl_buffergeometry_instancing_count_12500.json' : null
    }))
  };
  assert.deepEqual(validateManifestContract(example), []);
});

test('RGBA inspection and parity list cover all mandatory relations', () => {
  assert.deepEqual(inspectRgbaPixels(new Uint8Array([
    0, 0, 0, 255,
    10, 20, 30, 255
  ])), { nonBlackPixels: 1, nonOpaquePixels: 0, uniqueRgbColorCount: 2 });
  assert.deepEqual(parityDefinitions.map((definition) => definition.relation), [
    'pipeline-parity-metal',
    'pipeline-parity-vulkan',
    'backend-parity-legacy',
    'backend-parity-experimental'
  ]);
});
