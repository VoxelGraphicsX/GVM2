import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

const fixtureDirectory = path.dirname(fileURLToPath(import.meta.url));
const sampleRoot = path.resolve(fixtureDirectory, '../..');
const manifestPath = path.join(sampleRoot, 'Manifest/three-r185-manifest.json');
const dslRoot = path.join(sampleRoot, 'Dsl');

const batchShards = new Map();

const renderSetCases = new Set([...batchShards]
  .filter(([shard]) => shard.includes('RenderSet'))
  .flatMap(([, caseIds]) => caseIds));
const batchCaseIds = [...batchShards.values()].flat();

test('active wave-two shared placeholders are fully retired', async () => {
  assert.equal(batchCaseIds.length, 0);
  assert.equal(new Set(batchCaseIds).size, 0);
  assert.equal(renderSetCases.size, 0);

  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  const examplesById = new Map(manifest.examples.map((example) => [example.id, example]));
  for (const caseId of batchCaseIds) {
    const example = examplesById.get(caseId);
    assert.ok(example, `${caseId} must exist in the locked r185 manifest`);
    assert.equal(example.status, 'phase1_required');
    assert.equal(example.renderSetPolicy, renderSetCases.has(caseId) ? 'required' : 'not-required');
  }
});

test('dedicated radial blur owns its exact one-Set semantic Renderer', async () => {
  const shard = 'Phase1WebgpuPostprocessingRadialBlurRenderSet';
  const directory = path.join(dslRoot, shard);
  const inventory = JSON.parse(await fs.readFile(
    path.join(directory, 'implemented-cases.json'), 'utf8'));
  assert.equal(inventory.cases.length, 1);
  assert.match(
    inventory.cases[0].implementationLevel,
    /^(?:semantic-complete|strict-pass)$/u);
  const source = await fs.readFile(
    path.join(directory, `${shard}.hpp`), 'utf8');
  assert.match(source, /class WebgpuPostprocessingRadialBlurRenderer final/u);
  assert.match(source, /struct WebgpuPostprocessingRadialBlurSceneRenderSet : public IRenderSet/u);
  assert.match(source, /\[\[RenderEntityID\]\]/u);
  assert.match(source, /\[\[RenderEntityInstanceID\]\]/u);
  assert.match(source, /sampleIndex < 64u/u);
  assert.doesNotMatch(source, /Phase1BatchRenderSetRenderer/u);
});

test('dedicated multiple rendertargets owns a true two-attachment semantic Renderer', async () => {
  const directory = path.join(dslRoot, 'WebgpuMultipleRendertargets');
  const inventory = JSON.parse(await fs.readFile(
    path.join(directory, 'implemented-cases.json'), 'utf8'));
  assert.equal(inventory.cases.length, 1);
  assert.match(
    inventory.cases[0].implementationLevel,
    /^(?:semantic-complete|strict-pass)$/u);
  const source = await fs.readFile(
    path.join(directory, 'WebgpuMultipleRendertargets.hpp'), 'utf8');
  assert.match(source, /class WebgpuMultipleRendertargetsRenderer final/u);
  assert.match(source, /ColorAttachment<TextureFormat::RGBA16Float> output;/u);
  assert.match(source, /ColorAttachment<TextureFormat::RGBA16Float> normal;/u);
  assert.equal(
    (source.match(/renderPass\(\s*"WebgpuMultipleRendertargetsMrt"/gu) ?? []).length,
    1);
  assert.doesNotMatch(source, /Phase1BatchSimpleRenderer/u);
});

test('dedicated instance and compute-point examples own semantic Renderers and RenderSet types', async () => {
  const dedicatedShards = new Map([
    ['WebglInstancingPerformance', {
      caseId: 'webgl_instancing_performance',
      renderer: 'WebglInstancingPerformanceRenderer',
      renderSet: 'WebglInstancingPerformanceSceneRenderSet'
    }],
    ['WebgpuInstanceMesh', {
      caseId: 'webgpu_instance_mesh',
      renderer: 'WebgpuInstanceMeshRenderer',
      renderSet: 'WebgpuInstanceMeshSceneRenderSet'
    }],
    ['WebgpuInstancePoints', {
      caseId: 'webgpu_instance_points',
      renderer: 'WebgpuInstancePointsRenderer',
      renderSet: 'WebgpuInstancePointsSceneRenderSet'
    }],
    ['WebgpuComputePoints', {
      caseId: 'webgpu_compute_points',
      renderer: 'WebgpuComputePointsRenderer',
      renderSet: 'WebgpuComputePointsSceneRenderSet'
    }],
    ['WebgpuComputeParticles', {
      caseId: 'webgpu_compute_particles',
      renderer: 'WebgpuComputeParticlesRenderer',
      renderSet: 'WebgpuComputeParticlesSceneRenderSet'
    }]
  ]);
  for (const [shard, expected] of dedicatedShards) {
    const directory = path.join(dslRoot, shard);
    const inventory = JSON.parse(await fs.readFile(
      path.join(directory, 'implemented-cases.json'), 'utf8'));
    assert.equal(inventory.cases.length, 1);
    assert.equal(inventory.cases[0].caseId, expected.caseId);
    assert.match(
      inventory.cases[0].implementationLevel,
      /^(?:semantic-complete|strict-pass)$/u);
    const source = await fs.readFile(path.join(directory, `${shard}.hpp`), 'utf8');
    assert.match(source, new RegExp(`class ${expected.renderer} final`, 'u'));
    assert.match(source, new RegExp(`struct ${expected.renderSet} : public IRenderSet`, 'u'));
    assert.match(source, /\[\[RenderEntityID\]\]/u);
    assert.match(source, /\[\[RenderEntityInstanceID\]\]/u);
    assert.doesNotMatch(source, /Phase1BatchRenderSetRenderer/u);
  }
});

test('dedicated compute geometry owns its ordinary semantic Renderer without a RenderSet', async () => {
  const directory = path.join(dslRoot, 'WebgpuComputeGeometry');
  const inventory = JSON.parse(await fs.readFile(
    path.join(directory, 'implemented-cases.json'), 'utf8'));
  assert.equal(inventory.cases.length, 1);
  assert.equal(inventory.cases[0].caseId, 'webgpu_compute_geometry');
  assert.match(
    inventory.cases[0].implementationLevel,
    /^(?:semantic-complete|strict-pass)$/u);
  assert.equal(inventory.cases[0].renderSetType, null);
  const source = await fs.readFile(
    path.join(directory, 'WebgpuComputeGeometry.hpp'), 'utf8');
  assert.match(source, /class WebgpuComputeGeometryRenderer final/u);
  assert.match(source, /class \[\[LocalWorkGroupSize\(64, 1, 1\)\]\] WebgpuComputeGeometryUpdatePass final/u);
  assert.match(source, /RWStructuredBuffer<float4> currentPositions/u);
  assert.doesNotMatch(source, /RenderSet</u);
  assert.doesNotMatch(source, /Phase1BatchSimpleRenderer/u);
});

test('dedicated terrain owns its Compute texture and ordinary semantic Renderer', async () => {
  const directory = path.join(dslRoot, 'WebglGeometryTerrain');
  const inventory = JSON.parse(await fs.readFile(
    path.join(directory, 'implemented-cases.json'), 'utf8'));
  assert.equal(inventory.cases.length, 1);
  assert.equal(inventory.cases[0].caseId, 'webgl_geometry_terrain');
  assert.match(
    inventory.cases[0].implementationLevel,
    /^(?:semantic-complete|strict-pass)$/u);
  assert.equal(inventory.cases[0].renderSetType, null);
  const source = await fs.readFile(
    path.join(directory, 'WebglGeometryTerrain.hpp'), 'utf8');
  assert.match(source, /class WebglGeometryTerrainRenderer final/u);
  assert.match(source, /class \[\[LocalWorkGroupSize\(8, 8, 1\)\]\]\s+WebglGeometryTerrainTexturePass final/u);
  assert.match(source, /RWTexture2D<TextureFormat::RGBA8Unorm>/u);
  assert.match(source, /texturePass\(\s*WebglGeometryTerrainTextureExtent,\s*WebglGeometryTerrainTextureExtent,\s*1u\)/u);
  assert.doesNotMatch(source, /RenderSet</u);
  assert.doesNotMatch(source, /Phase1BatchSimpleRenderer/u);
});

test('dedicated convex geometry owns one three-entity Scene RenderSet', async () => {
  const directory = path.join(dslRoot, 'WebglGeometryConvex');
  const inventory = JSON.parse(await fs.readFile(
    path.join(directory, 'implemented-cases.json'), 'utf8'));
  assert.equal(inventory.cases.length, 1);
  assert.equal(inventory.cases[0].caseId, 'webgl_geometry_convex');
  assert.match(
    inventory.cases[0].implementationLevel,
    /^(?:semantic-complete|strict-pass)$/u);
  assert.equal(
    inventory.cases[0].renderSetType,
    'WebglGeometryConvexSceneRenderSet');
  const source = await fs.readFile(
    path.join(directory, 'WebglGeometryConvex.hpp'), 'utf8');
  assert.match(source, /class WebglGeometryConvexRenderer final/u);
  assert.match(
    source,
    /struct WebglGeometryConvexSceneRenderSet : public IRenderSet/u);
  assert.match(source, /\[\[RenderEntityID\]\]/u);
  assert.match(source, /\[\[RenderEntityInstanceID\]\]/u);
  assert.match(source, /TextureComponent<half4, WebglGeometryConvexMaxTextures>/u);
  assert.doesNotMatch(source, /Phase1BatchRenderSetRenderer/u);
});

test('every batch shard truthfully inventories its current scaffold level', async () => {
  for (const [shard, expectedCaseIds] of batchShards) {
    const shardDirectory = path.join(dslRoot, shard);
    const inventory = JSON.parse(await fs.readFile(path.join(shardDirectory, 'implemented-cases.json'), 'utf8'));
    assert.equal(inventory.schemaVersion, 2);
    assert.deepEqual(inventory.cases.map((entry) => entry.caseId), expectedCaseIds);
    assert.equal(inventory.cases.every((entry) => (
      entry.implementationLevel === 'scaffolded'
      && entry.dslEntry === shard
      && entry.hostTarget === shard
      && Array.isArray(entry.scenePasses)
      && Object.hasOwn(entry, 'renderSetType')
    )), true);
    const source = await fs.readFile(path.join(shardDirectory, `${shard}.hpp`), 'utf8');
    assert.match(source, /Phase1Batch(?:RenderSet|Simple)\.hpp/u);
  }
});

test('dedicated FXAA DSL enforces one Scene RenderSet and entity-aware indirect drawing', async () => {
  const source = await fs.readFile(path.join(
    dslRoot,
    'Phase1WebglPostprocessingFxaaRenderSet/WebglPostprocessingFxaa.hpp'
  ), 'utf8');
  assert.match(source, /class WebglPostprocessingFxaaRenderer final/u);
  assert.equal((source.match(/createRenderSet<WebglPostprocessingFxaaSceneRenderSet>/gu) ?? []).length, 1);
  assert.equal((source.match(/\[\[Export\]\]\s+RenderSet<WebglPostprocessingFxaaSceneRenderSet>/gu) ?? []).length, 1);
  assert.match(source, /\[\[RenderEntityID\]\]/u);
  assert.match(source, /\[\[RenderEntityInstanceID\]\]/u);
  assert.match(source, /renderPass\([^\n]+sceneFrameBuffer, scenePass\(\)\)/u);
  assert.doesNotMatch(source, /scenePass\s*\([^)]*indexCount/u);
});

test('shared simple DSL has no RenderSet or entity builtins', async () => {
  const source = await fs.readFile(path.join(dslRoot, 'Phase1BatchShared/Phase1BatchSimple.hpp'), 'utf8');
  assert.doesNotMatch(source, /RenderSet</u);
  assert.doesNotMatch(source, /RenderEntity(?:ID|InstanceID)/u);
  assert.match(source, /RenderClass<Phase1BatchSimpleScenePass>/u);
});

test('remaining simple compute scaffold uses total thread counts rather than workgroup counts', async () => {
  const simpleSource = await fs.readFile(
    path.join(dslRoot, 'Phase1BatchShared/Phase1BatchSimple.hpp'),
    'utf8');
  assert.match(simpleSource, /LocalWorkGroupSize\(8,\s*8,\s*1\)/u);
  assert.match(simpleSource, /coordinate\.x >= 512u \|\| coordinate\.y >= 512u/u);
  assert.match(simpleSource, /computePass\(512u,\s*512u,\s*1u\)/u);
  assert.doesNotMatch(simpleSource, /computePass\(64u,\s*64u,\s*1u\)/u);
});
