import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { lintGeneratedArtifacts, parseArguments } from './lint_generated_artifacts.mjs';

/** Creates the generated host header subset exercised by the production parser. */
function createGeneratedHeader(textureCount, useExplicitDraw, legacyMslNames) {
  const fields = legacyMslNames
    ? [
        ['RenderSetAccessBoundData', 0], ['verticesComponentList', 1], ['vertices', 2],
        ['indicesComponentList', 3], ['indices', 4], ['albedoComponentList', 5],
        ['albedo', 6], ['RenderEntityInfo', 7], ['RenderEntityCMDParams', 8]
      ]
    : [
        ['AccessBounds', 0], ['verticesIndexTable', 1], ['vertices', 2],
        ['indicesIndexTable', 3], ['indices', 4], ['albedoIndexTable', 5],
        ['albedo', 6], ['DrawInfo', 7], ['CommandParams', 8]
      ];
  const mslFields = fields.map(([name, binding]) => `device uint* ${name} [[id(${binding})]];`).join('\n');
  const runArguments = useExplicitDraw ? '3u, 1u, 0u, 0, 0u' : '';
  return `
struct UGL_RenderEntityInfo_
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint globalInstanceBase;
  uint vertexCount;
  uint entityVersion;
  uint cmdParamsOffset;
};
struct FixtureSceneSet: public GVM::Core::RenderSet
{
  void create(GVM::RHI::Device device)
  {
    GVM::Core::RenderSetCreateInfo createInfo = {};
    createInfo.vertexComponentName = "vertices";
    createInfo.indexComponentName = "indices";
    createInfo.componentInfos.emplace(0, GVM::Core::RenderComponentCreateInfo{.componentName = "vertices", .dataElementStorageSize = sizeof(FixtureVertex), .type = GVM::Core::RenderComponentType::BufferComponent, .maxResourceCount = 1});
    createInfo.componentInfos.emplace(1, GVM::Core::RenderComponentCreateInfo{.componentName = "indices", .dataElementStorageSize = sizeof(unsigned int), .type = GVM::Core::RenderComponentType::BufferComponent, .maxResourceCount = 1});
    createInfo.componentInfos.emplace(2, GVM::Core::RenderComponentCreateInfo{.componentName = "albedo", .dataElementStorageSize = sizeof(float4), .type = GVM::Core::RenderComponentType::TextureComponent, .maxResourceCount = ${textureCount}});
  }
};
class FixtureScenePass : public GVM::Core::IRenderClass, public GVM::RHI::RefCountedObject
{
  eastl::intrusive_ptr<FixtureSceneSet> sceneSet;
  static constexpr uint32_t vertexShaderArtifact_SpirvWords[] = {0x07230203};
  static constexpr uint32_t fragmentShaderArtifact_SpirvWords[] = {0x07230203};
  int vertexArtifact = makeArtifact(vertexShaderArtifact_SpirvWords, 1);
  int fragmentArtifact = makeArtifact(fragmentShaderArtifact_SpirvWords, 1);
public:
  void create(eastl::intrusive_ptr<FixtureSceneSet> sceneSet [[Slot0]])
  {
    this->mRenderSet = sceneSet;
    this->sceneSet = sceneSet;
    this->mRenderSetBindGroupIndex = 0;
  }
};
class FixtureRenderer
{
  eastl::intrusive_ptr<FixtureScenePass> scenePass;
  void render() { scenePass->run(${runArguments}); }
};
R"(
struct FixtureSceneSet
{
${mslFields}
};
)"
`;
}

/** Creates the merged DSL artifact with both RenderSet entity builtins. */
function createDslHeader() {
  return `
class FixtureScenePass final : public IRenderClass
{
  Output vertex(Input input [[VertexInput0]], uint entity [[RenderEntityID]], uint instance [[RenderEntityInstanceID]])
  {
    return Output{};
  }
};
`;
}

/** Creates canonical RenderSet resources matching the synthetic generated layout. */
function createReflectionResources(stage) {
  const entries = [
    ['AccessBounds', 0, 'access_bounds', 0, 1, 'storage_buffer'],
    ['verticesIndexTable', 1, 'buffer_index_table', 1, 1, 'storage_buffer'],
    ['vertices', 2, 'buffer_value', 1, 1, 'storage_buffer'],
    ['indicesIndexTable', 3, 'buffer_index_table', 2, 1, 'storage_buffer'],
    ['indices', 4, 'buffer_value', 2, 1, 'storage_buffer'],
    ['albedoIndexTable', 5, 'texture_index_table', 3, 1, 'storage_buffer'],
    ['albedo', 6, 'texture_value', 3, 8, 'texture'],
    ['DrawInfo', 7, 'draw_info', 0, 1, 'storage_buffer'],
    ['CommandParams', 8, 'command_params', 0, 1, 'storage_buffer']
  ];
  return entries.map(([name, bindingIndex, resourceRole, resourceIndex, arrayCount, kind]) => ({
    name: `sceneSet.${name}`,
    kind,
    bindGroupIndex: 0,
    bindingIndex,
    accessMode: 'read',
    elementType: name === 'albedo' ? 'half4' : 'u32',
    arrayCount,
    textureDimension: name === 'albedo' ? '2d' : 'none',
    textureFormat: 'unknown',
    isMultisampled: false,
    inputAttachmentIndex: 0,
    resourceRole,
    resourceIndex,
    visibleStages: [stage]
  }));
}

/** Creates the generated UGLIR reflection JSON for one synthetic render stage. */
function createUglirJson(stage) {
  return JSON.stringify({
    schemaVersion: 1,
    name: `FixtureScenePass.${stage}`,
    reflection: {
      entryName: `FixtureScenePass.${stage}`,
      stage,
      resources: createReflectionResources(stage)
    }
  }, null, 2);
}

/** Creates an MSL argument-buffer artifact with RenderSet entity command decoding. */
function createMsl(stage) {
  const entityDecode = stage === 'vertex'
    ? `uint drawInstance [[instance_id]];
       uint2 command = sceneSet->CommandParams[drawInstance];
       uint renderEntityID = command.x;
       uint renderEntityInstanceID = command.y;`
    : '';
  return `
struct UGL_DrawInfo_
{
  uint indexCount;
  uint instanceCount;
  uint firstIndex;
  int vertexOffset;
  uint globalInstanceBase;
  uint vertexCount;
  uint entityVersion;
  uint cmdParamsOffset;
};
struct FixtureSceneSet
{
  const device uint2* AccessBounds [[id(0)]];
  const device uint* verticesIndexTable [[id(1)]];
  const device FixtureVertex* vertices [[id(2)]];
  const device uint* indicesIndexTable [[id(3)]];
  const device uint* indices [[id(4)]];
  const device uint* albedoIndexTable [[id(5)]];
  device TextureWrapper* albedo [[id(6)]];
  const device DrawInfo* DrawInfo [[id(7)]];
  const device uint2* CommandParams [[id(8)]];
};
${entityDecode}
`;
}

/** Creates direct SPIR-V assembly with the complete RenderSet descriptor ABI. */
function createSpirvAssembly() {
  const names = ['AccessBounds', 'verticesIndexTable', 'vertices', 'indicesIndexTable', 'indices', 'albedoIndexTable', 'albedo', 'DrawInfo', 'CommandParams'];
  const nameLines = names.map((name) => `OpName %sceneSet_${name} "sceneSet_${name}"`).join('\n');
  const bindingLines = names.map((name, binding) => `OpDecorate %sceneSet_${name} DescriptorSet 0\nOpDecorate %sceneSet_${name} Binding ${binding}`).join('\n');
  const variableLines = names.map((name) => name === 'albedo'
    ? `%sceneSet_${name} = OpVariable %ptrTexture UniformConstant`
    : `%sceneSet_${name} = OpVariable %ptrStorage StorageBuffer`).join('\n');
  const memberLines = Array.from({ length: 8 }, (_, index) => `OpMemberDecorate %drawInfoType ${index} Offset ${index * 4}`).join('\n');
  return `
${nameLines}
OpName %drawInfoType "UGL_DrawInfo_"
${bindingLines}
${memberLines}
OpDecorate %drawInfoArray ArrayStride 32
OpDecorate %instanceInput BuiltIn InstanceIndex
%storageStruct = OpTypeStruct %uint
%ptrStorage = OpTypePointer StorageBuffer %storageStruct
%drawInfoArray = OpTypeRuntimeArray %drawInfoType
%textureArray = OpTypeRuntimeArray %textureType
%ptrTexture = OpTypePointer UniformConstant %textureArray
${variableLines}
`;
}

/** Writes one complete Legacy/Experimental artifact pair into temporary roots. */
function writeArtifactPair(rootDir, options = {}) {
  const legacyDir = path.join(rootDir, 'legacy', 'fixture');
  const experimentalDir = path.join(rootDir, 'experimental', 'fixture');
  fs.mkdirSync(path.join(experimentalDir, 'msl'), { recursive: true });
  fs.mkdirSync(path.join(experimentalDir, 'uglir'), { recursive: true });
  fs.mkdirSync(path.join(experimentalDir, 'spv'), { recursive: true });
  fs.mkdirSync(legacyDir, { recursive: true });
  fs.writeFileSync(path.join(legacyDir, 'generate_result.hpp'), createGeneratedHeader(8, options.explicitLegacyDraw === true, true));
  fs.writeFileSync(path.join(experimentalDir, 'generate_result.hpp'), createGeneratedHeader(options.experimentalTextureCount ?? 8, false, false));
  fs.writeFileSync(path.join(legacyDir, 'dsl_single_header.hpp'), createDslHeader());
  fs.writeFileSync(path.join(experimentalDir, 'dsl_single_header.hpp'), createDslHeader());
  const spirv = createSpirvAssembly();
  for (const stage of ['vertex', 'fragment']) {
    const stem = `FixtureScenePass__${stage}`;
    fs.writeFileSync(path.join(experimentalDir, 'msl', `${stem}.msl`), createMsl(stage));
    fs.writeFileSync(path.join(experimentalDir, 'uglir', `${stem}.uglir.json`), createUglirJson(stage));
    fs.writeFileSync(path.join(experimentalDir, 'spv', `${stem}.raw.spvasm`), spirv);
    fs.writeFileSync(path.join(experimentalDir, 'spv', `${stem}.spvasm`), spirv);
  }
  return {
    legacyRoot: path.join(rootDir, 'legacy'),
    experimentalRoot: path.join(rootDir, 'experimental')
  };
}

/** Creates lint options for the synthetic instanced scene fixture. */
function createLintOptions(roots) {
  return {
    ...roots,
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: ['FixtureScenePass:instancing'],
    instancingPasses: []
  };
}

/** Creates one generated screen-only RenderClass host artifact. */
function createScreenGeneratedHeader(useExplicitDraw = true) {
  const runArguments = useExplicitDraw ? '3u, 1u, 0u, 0u' : '';
  return `
class FixtureScreenPass : public GVM::Core::IRenderClass, public GVM::RHI::RefCountedObject
{
  static constexpr uint32_t vertexShaderArtifact_SpirvWords[] = {0x07230203};
  static constexpr uint32_t fragmentShaderArtifact_SpirvWords[] = {0x07230203};
  int vertexArtifact = makeArtifact(vertexShaderArtifact_SpirvWords, 1);
  int fragmentArtifact = makeArtifact(fragmentShaderArtifact_SpirvWords, 1);
public:
  void create(eastl::intrusive_ptr<FixtureResources> resources [[Slot0]]) {}
};
class FixtureScreenRenderer
{
  eastl::intrusive_ptr<FixtureScreenPass> screenPass;
  void render() { screenPass->run(${runArguments}); }
};
`;
}

/** Creates the merged DSL declaration for one screen-only RenderClass. */
function createScreenDslHeader() {
  return `
class FixtureScreenPass final : public IRenderClass
{
  Output vertex(uint vertexID [[VertexID]]) { return Output{}; }
  FrameBuffer fragment(Output input) { return FrameBuffer{}; }
};
`;
}

/** Writes one complete Legacy/Experimental screen-pass artifact pair. */
function writeScreenArtifactPair(rootDir, useExplicitDraw = true) {
  const legacyDir = path.join(rootDir, 'legacy', 'screen');
  const experimentalDir = path.join(rootDir, 'experimental', 'screen');
  fs.mkdirSync(path.join(experimentalDir, 'msl'), { recursive: true });
  fs.mkdirSync(path.join(experimentalDir, 'uglir'), { recursive: true });
  fs.mkdirSync(path.join(experimentalDir, 'spv'), { recursive: true });
  fs.mkdirSync(legacyDir, { recursive: true });
  fs.writeFileSync(
    path.join(legacyDir, 'generate_result.hpp'),
    createScreenGeneratedHeader(useExplicitDraw)
  );
  fs.writeFileSync(
    path.join(experimentalDir, 'generate_result.hpp'),
    createScreenGeneratedHeader(useExplicitDraw)
  );
  fs.writeFileSync(
    path.join(legacyDir, 'dsl_single_header.hpp'),
    createScreenDslHeader()
  );
  fs.writeFileSync(
    path.join(experimentalDir, 'dsl_single_header.hpp'),
    createScreenDslHeader()
  );
  for (const stage of ['vertex', 'fragment']) {
    const stem = `FixtureScreenPass__${stage}`;
    for (const [directory, extension, contents] of [
      ['msl', 'msl', 'kernel void fixture() {}'],
      ['uglir', 'uglir.json', '{}'],
      ['spv', 'raw.spvasm', 'OpCapability Shader'],
      ['spv', 'spvasm', 'OpCapability Shader']
    ]) {
      fs.writeFileSync(
        path.join(experimentalDir, directory, `${stem}.${extension}`),
        contents
      );
    }
  }
  return {
    legacyRoot: path.join(rootDir, 'legacy'),
    experimentalRoot: path.join(rootDir, 'experimental')
  };
}

test('parseArguments keeps all configuration explicit and repeatable', () => {
  const options = parseArguments([
    '--legacy-root', 'legacy',
    '--experimental-root', 'experimental',
    '--manifest', 'manifest.json',
    '--case', 'case-a',
    '--case', 'case-b',
    '--fixture', 'FixtureScenePass:instancing'
  ]);
  assert.equal(options.legacyRoot, 'legacy');
  assert.equal(options.experimentalRoot, 'experimental');
  assert.deepEqual(options.caseIds, ['case-a', 'case-b']);
  assert.deepEqual(options.fixtureSelectors, ['FixtureScenePass:instancing']);
});

test('accepts matching Legacy and Experimental RenderSet artifacts', (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-generated-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const result = lintGeneratedArtifacts(createLintOptions(writeArtifactPair(rootDir)));
  assert.deepEqual(result.errors, []);
});

test('accepts Experimental lowered entityID local names for the same ABI', (t) => {
  const rootDir = fs.mkdtempSync(
    path.join(os.tmpdir(), 'gvm-generated-lowered-entity-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const roots = writeArtifactPair(rootDir);
  const mslPath = path.join(
    roots.experimentalRoot,
    'fixture',
    'msl',
    'FixtureScenePass__vertex.msl');
  const lowered = fs.readFileSync(mslPath, 'utf8')
    .replaceAll('renderEntityID', 'entityID')
    .replaceAll('renderEntityInstanceID', 'instanceID');
  fs.writeFileSync(mslPath, lowered);
  const result = lintGeneratedArtifacts(createLintOptions(roots));
  assert.deepEqual(result.errors, []);
});

test('accepts matching Legacy and Experimental screen-only artifacts', (t) => {
  const rootDir = fs.mkdtempSync(
    path.join(os.tmpdir(), 'gvm-generated-screen-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const roots = writeScreenArtifactPair(rootDir);
  const result = lintGeneratedArtifacts({
    ...roots,
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: ['FixtureScreenPass:screen'],
    instancingPasses: []
  });
  assert.deepEqual(result.errors, []);
});

test('rejects a screen-only pass without an explicit fullscreen draw', (t) => {
  const rootDir = fs.mkdtempSync(
    path.join(os.tmpdir(), 'gvm-generated-screen-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const roots = writeScreenArtifactPair(rootDir, false);
  const result = lintGeneratedArtifacts({
    ...roots,
    manifestPath: null,
    caseIds: [],
    fixtureSelectors: ['FixtureScreenPass:screen'],
    instancingPasses: []
  });
  assert(result.errors.some(
    (error) => error.code === 'screen_draw_entry_missing'));
});

test('maps a logical manifest pass name to its generated RenderClass identifier', (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-generated-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const roots = writeArtifactPair(rootDir);
  const manifestPath = path.join(rootDir, 'manifest.json');
  fs.writeFileSync(manifestPath, JSON.stringify({
    examples: [{
      id: 'fixture_case',
      status: 'phase1_required',
      renderSetPolicy: 'required',
      containsInstancing: true,
      renderSetType: 'FixtureSceneSet',
      componentSchema: [
        { name: 'vertices', kind: 'buffer', role: 'vertex' },
        { name: 'indices', kind: 'buffer', role: 'index' },
        { name: 'albedo', kind: 'texture', role: 'texture' }
      ],
      sceneRoots: [{ name: 'scene', renderSetType: 'FixtureSceneSet' }],
      scenePasses: [{
        name: 'shadow-depth',
        renderClass: 'FixtureScenePass',
        sceneRoot: 'scene'
      }]
    }]
  }));
  const result = lintGeneratedArtifacts({
    ...roots,
    manifestPath,
    caseIds: [],
    fixtureSelectors: [],
    instancingPasses: []
  });
  assert.deepEqual(result.errors, []);
});

test('rejects explicit scene draw counts in generated host code', (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-generated-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const result = lintGeneratedArtifacts(createLintOptions(writeArtifactPair(rootDir, { explicitLegacyDraw: true })));
  assert(result.errors.some((error) => error.code === 'explicit_scene_draw_count'));
  assert(result.errors.some((error) => error.code === 'render_set_indirect_entry_missing'));
});

test('rejects Legacy and Experimental component layout drift', (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-generated-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const result = lintGeneratedArtifacts(createLintOptions(writeArtifactPair(rootDir, { experimentalTextureCount: 4 })));
  assert(result.errors.some((error) => error.code === 'pipeline_component_layout_mismatch'));
  assert(result.errors.some((error) => error.code === 'uglir_resource_shape_mismatch'));
});

test('rejects a missing direct-SPIR-V product instead of trusting reflection alone', (t) => {
  const rootDir = fs.mkdtempSync(path.join(os.tmpdir(), 'gvm-generated-lint-'));
  t.after(() => fs.rmSync(rootDir, { recursive: true, force: true }));
  const roots = writeArtifactPair(rootDir);
  fs.rmSync(path.join(roots.experimentalRoot, 'fixture', 'spv', 'FixtureScenePass__fragment.raw.spvasm'));
  const result = lintGeneratedArtifacts(createLintOptions(roots));
  assert(result.errors.some((error) => error.code === 'missing_artifact'
    && error.message.includes('FixtureScenePass__fragment.raw.spvasm')));
});
