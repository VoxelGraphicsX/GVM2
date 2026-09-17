import path from 'node:path';
import { createHash } from 'node:crypto';

function buildFixturePath(sourceDir, fixtureDir, fileName) {
  return path.join(sourceDir, 'tests', 'uglc', 'fixtures', fixtureDir, fileName);
}

function buildHarnessPath(sourceDir, fileName) {
  return path.join(sourceDir, 'tests', 'uglc', 'harnesses', fileName);
}

function createFixtureCase({
  id,
  title,
  group,
  labels,
  fixtureDir,
  sourceFile,
  extraIncludeDirs = [],
  description,
  validates = [],
  watchouts = [],
  timeoutMs = 90_000,
  expectedExitCode = 0,
  uglcArgs = ['--shader-pipeline=legacy'],
  uglcEnv = {},
  initialArtifacts = {},
  hostCompileSteps = [],
  verify
}) {
  return {
    id,
    title,
    group,
    labels,
    kind: 'uglc-fixture',
    fixtureDir,
    sourceFile,
    extraIncludeDirs,
    description,
    validates,
    watchouts,
    timeoutMs,
    expectedExitCode,
    uglcArgs,
    uglcEnv,
    initialArtifacts,
    hostCompileSteps,
    verify
  };
}

function createHarnessCase({
  id,
  title,
  group,
  labels,
  sourceFile,
  extraSources = [],
  runArgs = [],
  description,
  validates = [],
  watchouts = [],
  timeoutMs = 60_000,
  verify
}) {
  return {
    id,
    title,
    group,
    labels,
    kind: 'host-harness',
    sourceFile,
    extraSources,
    runArgs,
    description,
    validates,
    watchouts,
    timeoutMs,
    verify
  };
}

/** Checks layout rejection on both paths and stale-artifact invalidation on the experimental path. */
function createBufferLayoutDiagnosticCase({ id, sourceFile, diagnostic, experimental = false }) {
  return createFixtureCase({
    id, title: `Buffer layout rejection: ${sourceFile} (${experimental ? 'UGLIR' : 'Legacy'})`,
    group: experimental ? 'uglir' : 'diagnostics', labels: ['diagnostic', 'layout', 'regression'],
    fixtureDir: 'invalid-buffer-layout', sourceFile,
    uglcArgs: experimental ? ['--shader-pipeline=uglir'] : ['--shader-pipeline=legacy'], expectedExitCode: 1,
    initialArtifacts: experimental ? { 'generate_result.hpp': 'stale success', 'exports.hpp': 'stale exports' } : {},
    description: 'Rejects a Host/shader layout mismatch before publishing usable shader artifacts.',
    verify: async (t) => {
      await t.expectStepOutputContains('uglc', diagnostic, 'The diagnostic must identify the actual layout failure.');
      await t.expectArtifactNotExists('generate_result.hpp', 'A layout failure must invalidate the generated header.');
      await t.expectArtifactNotExists('exports.hpp', 'A layout failure must invalidate stale exports.');
    }
  });
}

/** Creates an UGLIR verifier fixture that must fail with a specific source-bound diagnostic feature. */
function createExperimentalUglirDiagnosticCase({
  id,
  title,
  fixtureDir,
  sourceFile,
  feature
}) {
  return createFixtureCase({
    id,
    title,
    group: 'uglir',
    labels: ['uglir', 'uglir', 'verifier', 'diagnostic'],
    fixtureDir,
    sourceFile,
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 1,
    description: 'Runs the UGLIR verifier against a shader fixture that intentionally crosses one unsupported source-language boundary.',
    validates: [
      `The verifier reports the unsupported feature category: ${feature}.`,
      'The diagnostic is emitted by the UGLIR verifier path instead of the legacy shader emitters.'
    ],
    verify: async (t) => {
      await t.expectStepOutputContains('uglc', 'UGLIR verifier:', 'Diagnostic output should come from the UGLIR verifier.');
      await t.expectStepOutputContains('uglc', feature, `Diagnostic output should name the unsupported feature "${feature}".`);
      await t.expectArtifactNotExists('generate_result.hpp', 'Verifier failures should not write the generated host artifact.');
      await t.expectArtifactNotExists('msl', 'Verifier failures should not write experimental MSL debug artifacts.');
      await t.expectArtifactNotExists('hlsl', 'Verifier failures should not write experimental HLSL debug artifacts.');
      await t.expectArtifactNotExists('spv', 'Verifier failures should not write experimental direct SPIR-V debug artifacts.');
    }
  });
}

/** Creates an UGLIR fixture that must reach the direct SPIR-V writer and fail there without using DXC fallback. */
function createExperimentalUglirSPIRVEmitterDiagnosticCase() {
  return createFixtureCase({
    id: 'experimental-uglir-invalid-spirv-binary-operator',
    title: 'Experimental UGLIR SPIR-V emitter: unsupported binary operator is rejected',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'spirv', 'diagnostic'],
    fixtureDir: 'experimental-uglir-invalid-spirv-float-literal',
    sourceFile: 'ExperimentalUGLIRInvalidSPIRVFloatLiteral.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 1,
    description: 'Runs a compute shader that passes the experimental verifier and lowering stages, then fails at the Phase-6 direct SPIR-V writer boundary.',
    validates: [
      'The diagnostic is emitted by the direct UGLIR SPIR-V emitter rather than by verifier or lowering.',
      'The experimental path does not fall back to the legacy HLSL-to-DXC SPIR-V path.'
    ],
    verify: async (t) => {
      await t.expectStepOutputContains('uglc', 'UGLIR SPIR-V emitter:', 'Diagnostic output should come from the direct SPIR-V writer.');
      await t.expectStepOutputContains('uglc', 'unsupported binary operator', 'Diagnostic output should name the unsupported SPIR-V expression lowering.');
      await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'Experimental direct SPIR-V failures should not be reported as DXC failures.');
      await t.expectArtifactNotExists('generate_result.hpp', 'SPIR-V emitter failures should not write the generated host artifact.');
      await t.expectArtifactNotExists('spv', 'SPIR-V emitter failures should not write direct SPIR-V debug artifacts.');
    }
  });
}

/** Creates an UGLIR fixture that reads a UniformBuffer through experimental MSL and direct SPIR-V. */
function createExperimentalUglirUniformBufferComputeCase() {
  return createFixtureCase({
    id: 'experimental-uglir-uniform-buffer-compute',
    title: 'Experimental UGLIR shader pipeline: uniform buffer compute artifact',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'uniform-buffer', 'msl', 'spirv', 'positive'],
    fixtureDir: 'experimental-uglir-uniform-buffer-compute',
    sourceFile: 'ExperimentalUGLIRUniformBufferCompute.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a compute shader that reads UniformBuffer<T> data and writes a storage output through the UGLIR MSL and direct SPIR-V path.',
    validates: [
      'UniformBuffer<T> is no longer rejected by the Phase-10 preflight boundary.',
      'The direct SPIR-V writer emits a Uniform storage-class Block with descriptor decorations.',
      'Uniform field reads lower to OpAccessChain plus OpLoad rather than falling back to DXC.'
    ],
    verify: async (t) => {
      await t.expectStepOutputNotContains('uglc', 'UniformBuffer resources are not supported', 'UniformBuffer should no longer be rejected by the direct SPIR-V preflight pass.');
      await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'Experimental direct SPIR-V path should not report DXC failures.');
      await t.expectArtifactExists('generate_result.hpp', 'UniformBuffer fixture should write the generated host artifact.');
      await t.expectArtifactExists('uglir/ExperimentalUGLIRUniformBufferComputePass.uglir.txt', 'UniformBuffer fixture should write the UGLIR text dump.');
      await t.expectArtifactExists('msl/ExperimentalUGLIRUniformBufferComputePass.msl', 'UniformBuffer fixture should write the MSL debug source.');
      await t.expectArtifactNotExists('hlsl', 'Experimental UGLIR no longer writes HLSL debug source.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRUniformBufferComputePass.spvasm', 'UniformBuffer fixture should write direct SPIR-V disassembly.');
      await t.expectFileContains('uglir/ExperimentalUGLIRUniformBufferComputePass.uglir.txt', 'resource "bindGroup.params" kind uniform_buffer set 0 binding 0', 'UGLIR reflection should classify the params resource as a uniform buffer.');
      await t.expectFileContains('uglir/ExperimentalUGLIRUniformBufferComputePass.uglir.txt', 'field "addend" type "u32" semantic "" location 0 offset 0', 'UGLIR type dump should preserve the host-layout field offset.');
      await t.expectFileContains('msl/ExperimentalUGLIRUniformBufferComputePass.msl', 'constant ExperimentalUGLIRUniformBufferComputeParams* params [[id(0)]]', 'MSL should bind the uniform payload inside the bind-group argument buffer.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.raw.spvasm', 'OpDecorate %UniformBlock_ExperimentalUGLIRUniformBufferComputeParams Block', 'SPIR-V should decorate the resource wrapper as a Block.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.raw.spvasm', 'OpVariable %_ptr_Uniform_UniformBlock_ExperimentalUGLIRUniformBufferComputeParams Uniform', 'SPIR-V should declare the uniform resource in Uniform storage class.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.raw.spvasm', 'OpDecorate %bindGroup_params DescriptorSet 0', 'SPIR-V should decorate the uniform descriptor set.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.raw.spvasm', 'OpDecorate %bindGroup_params Binding 0', 'SPIR-V should decorate the uniform binding.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.raw.spvasm', 'OpAccessChain %_ptr_Uniform_uint %bindGroup_params %uint_0', 'SPIR-V should access the uniform field through the Uniform block.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.spvasm', 'OpLoad %uint', 'SPIR-V should load the uniform field before arithmetic.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.spvasm', 'OpIAdd %uint', 'SPIR-V should add the uniform value to the literal.');
      await t.expectFileContains('spv/ExperimentalUGLIRUniformBufferComputePass.spvasm', 'OpStore', 'SPIR-V should store the computed value to the writable storage buffer.');
    }
  });
}

/** Creates an UGLIR fixture that validates scalar/vector UniformBuffer payload support. */
function createExperimentalUglirInvalidUniformScalarCase() {
  return createFixtureCase({
    id: 'experimental-uglir-invalid-uniform-scalar',
    title: 'Experimental UGLIR SPIR-V emitter: scalar uniform payload is block-wrapped',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'uniform-buffer', 'spirv', 'positive'],
    fixtureDir: 'experimental-uglir-invalid-uniform-scalar',
    sourceFile: 'ExperimentalUGLIRInvalidUniformScalar.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Confirms the direct SPIR-V backend wraps a scalar/vector UniformBuffer<T> payload in a valid Uniform block.',
    validates: [
      'The scalar/vector uniform payload is reflected as a uniform buffer.',
      'The generated SPIR-V uses Uniform storage, a Block decoration, and an access-chain load.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'Scalar/vector uniform fixture should generate successfully.');
      await t.expectArtifactNotExists('hlsl', 'Experimental UGLIR should not write HLSL debug artifacts.');
      await t.expectFileContains('uglir/ExperimentalUGLIRInvalidUniformScalarPass.uglir.txt', 'resource "bindGroup.params" kind uniform_buffer set 0 binding 0', 'UGLIR reflection should classify the vector payload as a uniform buffer.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.spvasm', 'Block', 'SPIR-V should decorate the wrapped scalar/vector uniform payload as a Block.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.spvasm', 'Uniform', 'SPIR-V should use Uniform storage for the scalar/vector payload.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.raw.spvasm', 'OpDecorate %bindGroup_params DescriptorSet 0', 'SPIR-V should decorate the scalar/vector uniform descriptor set.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.raw.spvasm', 'OpDecorate %bindGroup_params Binding 0', 'SPIR-V should decorate the scalar/vector uniform binding.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.spvasm', 'OpAccessChain', 'SPIR-V should access the wrapped uniform payload through an access chain.');
      await t.expectFileContains('spv/ExperimentalUGLIRInvalidUniformScalarPass.spvasm', 'OpLoad', 'SPIR-V should load the wrapped uniform payload.');
    }
  });
}

/** Creates the positive UGLIR fixture that should generate host artifacts from UGLIR-derived MSL and direct SPIR-V. */
function createExperimentalUglirComputeBasicLoweringCase() {
  return createFixtureCase({
    id: 'experimental-uglir-compute-basic-msl-direct-spirv',
    title: 'Experimental UGLIR shader pipeline: compute-basic generates MSL and direct SPIR-V',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'msl', 'spirv', 'artifact', 'positive'],
    fixtureDir: 'compute-basic',
    sourceFile: 'ComputeBasic.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the UGLIR verifier, lowering, MSL emitter, and direct SPIR-V writer on a valid minimal compute shader.',
    validates: [
      'The verifier accepts the shader-reachable code in the compute-basic fixture.',
      'The experimental path writes deterministic UGLIR text and JSON dumps.',
      'The generated host artifact embeds UGLIR-derived MSL and an empty HLSL shadow payload.',
      'The embedded SPIR-V words are emitted directly from UGLIR and validated by SPIRV-Tools.'
    ],
    verify: async (t) => {
      await t.expectStepOutputNotContains('uglc', 'no emitter is wired in Phase 3', 'Phase-4 positive case should no longer stop at the Phase-3 missing emitter boundary.');
      await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'Experimental direct SPIR-V path should not report DXC compilation failures.');
      await t.expectStepOutputNotContains('uglc', 'UGLIR verifier:', 'Positive lowering case should not produce unsupported-feature diagnostics.');
      await t.expectArtifactExists('generate_result.hpp', 'Experimental Phase-4 path should write the generated host header.');
      await t.expectArtifactExists('exports.hpp', 'Experimental Phase-4 path should write generated exports.');
      await t.expectArtifactExists('uglir/ComputeBasicPass.uglir.txt', 'Experimental path should write the compute-basic UGLIR text dump.');
      await t.expectArtifactExists('uglir/ComputeBasicPass.uglir.json', 'Experimental path should write the compute-basic UGLIR JSON dump.');
      await t.expectArtifactExists('msl/ComputeBasicPass.msl', 'Experimental path should write a prelude-plus-body MSL debug file.');
      await t.expectArtifactNotExists('hlsl', 'Experimental path should not write HLSL debug artifacts.');
      await t.expectArtifactExists('spv/ComputeBasicPass.spv.txt', 'Experimental path should write a direct SPIR-V word dump.');
      await t.expectArtifactExists('spv/ComputeBasicPass.spvasm', 'Experimental path should write SPIRV-Tools disassembly for the direct module.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'module "ComputeBasicPass"', 'Text dump should name the compute class module.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'stage compute entry_kind compute entry true', 'Text dump should contain a compute entry function.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'workgroup_size 8 1 1', 'Text dump should preserve the compute workgroup size.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'semantic "DispatchThreadID"', 'Text dump should preserve the compute builtin parameter semantic.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'resource "bindGroup.values"', 'Text dump should include the lowered bind-group storage buffer resource.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'expr store type "u32"', 'Buffer assignment must retain the type of the value it stores.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'expr binary type "u32"', 'Text dump should contain the arithmetic binary expression.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'op "+"', 'Text dump should preserve the addition operator.');
      await t.expectFileContains('uglir/ComputeBasicPass.uglir.txt', 'expr subscript type "u32"', 'Text dump should contain the buffer subscript expression.');

      const parsed = JSON.parse(await t.readArtifact('uglir/ComputeBasicPass.uglir.json'));
      t.recordCheck('UGLIR JSON dump should preserve schema, entry, semantic, workgroup, and resource reflection.', (
        parsed.schemaVersion === 1 &&
        parsed.name === 'ComputeBasicPass' &&
        parsed.functions?.[0]?.stage === 'compute' &&
        parsed.functions?.[0]?.parameters?.[0]?.semantic === 'DispatchThreadID' &&
        parsed.reflection?.workgroupSize?.join(',') === '8,1,1' &&
        parsed.reflection?.resources?.[0]?.name === 'bindGroup.values' &&
        parsed.reflection?.resources?.[0]?.kind === 'storage_buffer'
      ), JSON.stringify(parsed.reflection ?? {}));

      const generatedSources = await readGeneratedShaderSources(t, 'generate_result.hpp');
      t.recordCheck('Generated MSL source should come from the UGLIR MSL emitter.', (
        generatedSources.mslSource.includes('kernel void computeMain') &&
        generatedSources.mslSource.includes('[[thread_position_in_grid]]') &&
        generatedSources.mslSource.includes('[[buffer(0)]]') &&
        generatedSources.mslSource.includes('bindGroup->values[index]')
      ), generatedSources.mslSource);
      t.recordCheck('Generated HLSL source should be empty for the experimental SPIR-V-only path.', generatedSources.hlslSource.length === 0, generatedSources.hlslSource);
      await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {', 'Experimental direct writer should embed SPIR-V words.');
      const spirvWords = await readSpirvWordArray(t, 'generate_result.hpp', 'computeShaderArtifact_SpirvWords', 'Experimental UGLIR direct writer should produce a valid SPIR-V module.');
      t.recordCheck('Experimental UGLIR SPIR-V payload should be non-empty.', spirvWords !== null && spirvWords.length > 0, spirvWords ? `words=${spirvWords.length}` : 'Missing SPIR-V words.');
      await t.expectFileContains('msl/ComputeBasicPass.msl', '#include <metal_stdlib>', 'MSL debug file should include the shared Metal prelude.');
      await t.expectFileContains('msl/ComputeBasicPass.msl', 'kernel void computeMain', 'MSL debug file should include the UGLIR-derived compute entry.');
      await t.expectFileContains('spv/ComputeBasicPass.raw.spvasm', 'OpEntryPoint GLCompute %computeMain "computeMain"', 'Direct SPIR-V should expose the compute entry point.');
      await t.expectFileContains('spv/ComputeBasicPass.raw.spvasm', 'OpExecutionMode %computeMain LocalSize 8 1 1', 'Direct SPIR-V should preserve the compute workgroup size.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'BuiltIn GlobalInvocationId', 'Direct SPIR-V should map DispatchThreadID to GlobalInvocationId.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'DescriptorSet 0', 'Direct SPIR-V should decorate the storage buffer descriptor set.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'Binding 0', 'Direct SPIR-V should decorate the storage buffer binding.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'StorageBuffer', 'Direct SPIR-V should use a storage-buffer resource variable.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'OpLoad', 'Direct SPIR-V should load the storage-buffer element.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'OpIAdd', 'Direct SPIR-V should lower the addition.');
      await t.expectFileContains('spv/ComputeBasicPass.spvasm', 'OpStore', 'Direct SPIR-V should store the result.');
    }
  });
}

/** Creates the positive UGLIR fixture that covers helper and control-flow MSL plus direct SPIR-V emission. */
function createExperimentalUglirControlFlowLoweringCase() {
  return createFixtureCase({
    id: 'experimental-uglir-compute-control-flow-direct-spirv-artifact',
    title: 'Experimental UGLIR shader pipeline: compute control flow generates artifact',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'msl', 'spirv', 'control-flow'],
    fixtureDir: 'experimental-uglir-compute-control-flow',
    sourceFile: 'ExperimentalUGLIRComputeControlFlow.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs UGLIR lowering, MSL emission, and direct SPIR-V emission on a compute shader that uses a user helper plus if/for/while/return control flow.',
    validates: [
      'A direct user helper function is materialized as an internal UGLIR function.',
      'If, for, while, and return statements are represented in UGLIR, UGLIR-derived MSL, and direct SPIR-V.'
    ],
    verify: async (t) => {
      await t.expectStepOutputNotContains('uglc', 'no emitter is wired in Phase 3', 'Control-flow case should no longer stop at the Phase-3 missing emitter boundary.');
      await t.expectArtifactExists('generate_result.hpp', 'Control-flow fixture should write the generated host header.');
      await t.expectArtifactExists('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'Control-flow fixture should write a UGLIR text dump.');
      await t.expectArtifactExists('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'Control-flow fixture should write a UGLIR-derived MSL debug file.');
      await t.expectArtifactNotExists('hlsl', 'Control-flow fixture should not write HLSL debug artifacts.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRComputeControlFlowPass.spvasm', 'Control-flow fixture should write direct SPIR-V disassembly.');
      await t.expectFileContains('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'function "experimentalUGLIRControlFlowHelper" linkage internal', 'User helper should be lowered as an internal function.');
      await t.expectFileContains('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'stmt if', 'Control-flow dump should contain an if statement.');
      await t.expectFileContains('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'stmt for', 'Control-flow dump should contain a for statement.');
      await t.expectFileContains('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'stmt while', 'Control-flow dump should contain a while statement.');
      await t.expectFileContains('uglir/ExperimentalUGLIRComputeControlFlowPass.uglir.txt', 'stmt return', 'Control-flow dump should contain a return statement.');
      await t.expectFileContains('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'uint experimentalUGLIRControlFlowHelper(uint value', 'UGLIR-derived MSL should emit the user helper function.');
      await t.expectFileContains('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'if (', 'UGLIR-derived MSL should emit an if statement.');
      await t.expectFileContains('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'for (', 'UGLIR-derived MSL should emit a for loop.');
      await t.expectFileContains('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'while (', 'UGLIR-derived MSL should emit a while loop.');
      await t.expectFileContains('msl/ExperimentalUGLIRComputeControlFlowPass.msl', 'bindGroup->values[threadID.x]', 'UGLIR-derived MSL should emit the resource store.');
      const spirvWords = await readSpirvWordArray(t, 'generate_result.hpp', 'computeShaderArtifact_SpirvWords', 'Control-flow UGLIR direct writer should produce a valid SPIR-V module.');
      t.recordCheck('Control-flow UGLIR SPIR-V payload should be non-empty.', spirvWords !== null && spirvWords.length > 0, spirvWords ? `words=${spirvWords.length}` : 'Missing SPIR-V words.');
      await t.expectFileContains('spv/ExperimentalUGLIRComputeControlFlowPass.raw.spvasm', 'experimentalUGLIRControlFlowHelper', 'Raw SPIR-V diagnostics should retain the helper function debug name.');
      await t.expectFileContains('spv/ExperimentalUGLIRComputeControlFlowPass.spvasm', 'OpSelectionMerge', 'Direct SPIR-V should emit structured selection for if statements.');
      await t.expectFileContains('spv/ExperimentalUGLIRComputeControlFlowPass.spvasm', 'OpLoopMerge', 'Direct SPIR-V should emit structured loops for for/while statements.');
      await t.expectFileContains('spv/ExperimentalUGLIRComputeControlFlowPass.spvasm', 'OpStore', 'Direct SPIR-V should emit the resource store.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates render vertex/fragment artifact generation. */
function createExperimentalUglirRenderBasicCase() {
  return createFixtureCase({
    id: 'experimental-uglir-render-basic-msl-direct-spirv',
    title: 'Experimental UGLIR Phase 8: render-basic generates vertex/fragment artifacts',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'render', 'msl', 'spirv'],
    fixtureDir: 'render-basic',
    sourceFile: 'RenderBasic.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a minimal render class through UGLIR-derived MSL and direct SPIR-V for both vertex and fragment shader artifacts.',
    validates: [
      'Render vertex and fragment entries lower into separate UGLIR modules.',
      'Texture and sampler bindings are reflected through UGLIR and emitted by all experimental backends.',
      'The generated host artifact remains compatible with the existing render shader artifact seam.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'Experimental render-basic should write the generated host header.');
      await t.expectArtifactExists('exports.hpp', 'Experimental render-basic should write generated exports.');
      for (const stem of ['RenderBasicPass__vertex', 'RenderBasicPass__fragment']) {
        await t.expectArtifactExists(`uglir/${stem}.uglir.txt`, `${stem} UGLIR text dump should exist.`);
        await t.expectArtifactExists(`msl/${stem}.msl`, `${stem} MSL debug output should exist.`);
        await t.expectArtifactNotExists(`hlsl/${stem}.hlsl`, `${stem} HLSL debug output should not exist.`);
        await t.expectArtifactExists(`spv/${stem}.spvasm`, `${stem} SPIR-V disassembly should exist.`);
      }
      await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact vertexShaderArtifact', 'Host wrapper should contain the vertex shader artifact.');
      await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact fragmentShaderArtifact', 'Host wrapper should contain the fragment shader artifact.');
      await t.expectFileContains('uglir/RenderBasicPass__vertex.uglir.txt', 'entry_kind vertex', 'UGLIR vertex module should preserve vertex entry kind.');
      await t.expectFileContains('uglir/RenderBasicPass__fragment.uglir.txt', 'entry_kind fragment', 'UGLIR fragment module should preserve fragment entry kind.');
      await t.expectFileContains('msl/RenderBasicPass__vertex.msl', 'vertex RenderBasicVertexOutput vertexMain', 'MSL should emit the vertex entry point.');
      await t.expectFileContains('msl/RenderBasicPass__fragment.msl', 'fragment RenderBasicFrameBuffer fragmentMain', 'MSL should emit the fragment entry point.');
      await t.expectFileContains('msl/RenderBasicPass__fragment.msl', 'bindGroup->texture0.sample(bindGroup->sampler0, inputValue.uv)', 'MSL should emit sampled texture access from UGLIR.');
      await t.expectFileContains('spv/RenderBasicPass__vertex.raw.spvasm', 'OpEntryPoint Vertex %vertexMain "vertexMain"', 'Direct SPIR-V should expose the vertex entry point.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.raw.spvasm', 'OpEntryPoint Fragment %fragmentMain "fragmentMain"', 'Direct SPIR-V should expose the fragment entry point.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.spvasm', 'DescriptorSet 0', 'Direct SPIR-V should decorate texture/sampler descriptor sets.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.spvasm', 'Binding 1', 'Direct SPIR-V should decorate the sampled texture binding.');
      await t.expectFileContains('spv/RenderBasicPass__vertex.raw.spvasm', 'OpStore %pos_0', 'Direct SPIR-V vertex body should store the position output.');
      await t.expectFileContains('spv/RenderBasicPass__vertex.raw.spvasm', 'OpStore %uv_0', 'Direct SPIR-V vertex body should store the varying output.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.spvasm', 'OpSampledImage', 'Direct SPIR-V fragment body should combine the texture and sampler.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.spvasm', 'OpImageSampleImplicitLod', 'Direct SPIR-V fragment body should emit the texture sample.');
      await t.expectFileContains('spv/RenderBasicPass__fragment.raw.spvasm', 'OpStore %color', 'Direct SPIR-V fragment body should store the color output.');
    }
  });
}

/** Creates the positive UGLIR fixture that guards Vulkan row-major matrix-vector lowering. */
function createExperimentalUglirMatrixVectorMulVulkanCase() {
  return createFixtureCase({
    id: 'experimental-uglir-matrix-vector-mul-vulkan',
    title: 'Experimental UGLIR: matrix-vector mul matches Vulkan row-major SPIR-V ABI',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'render', 'spirv', 'matrix'],
    fixtureDir: 'experimental-uglir-matrix-vector-mul-vulkan',
    sourceFile: 'ExperimentalUGLIRMatrixVectorMulVulkan.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a non-symmetric float4x4 * float4 vertex transform through the direct SPIR-V writer so row-major matrix ABI regressions are visible in disassembly.',
    validates: [
      'The experimental path emits direct SPIR-V without HLSL or DXC.',
      'The direct writer lowers matrix-vector multiplication to explicit dot products instead of relying on native SPIR-V matrix op orientation.',
      'The generated vertex module keeps the non-symmetric transform observable for direction-sensitive checks.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('spv/ExperimentalUGLIRMatrixVectorMulVulkanPass__vertex.raw.spvasm', 'Raw direct SPIR-V disassembly should exist for matrix diagnostics.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRMatrixVectorMulVulkanPass__vertex.spvasm', 'Optimized direct SPIR-V disassembly should exist for matrix diagnostics.');
      await t.expectArtifactNotExists('hlsl', 'Experimental matrix-vector fixture should not write HLSL debug artifacts.');
      const rawSpv = await t.readArtifact('spv/ExperimentalUGLIRMatrixVectorMulVulkanPass__vertex.raw.spvasm');
      t.recordCheck('Raw direct SPIR-V should lower matrix-vector multiplication with explicit dot products.', rawSpv.includes('OpDot %float'), rawSpv);
      t.recordCheck('Raw direct SPIR-V should not depend on native matrix-vector op orientation.', !rawSpv.includes('OpMatrixTimesVector') && !rawSpv.includes('OpVectorTimesMatrix'), rawSpv);
      t.recordCheck('Raw direct SPIR-V should keep the asymmetric matrix constants visible for direction-sensitive diagnostics.', rawSpv.includes('OpConstant %float 13') && rawSpv.includes('OpConstant %float 16'), rawSpv);
      const extractedMatrixVectors = new Set();
      for (const line of rawSpv.split('\n')) {
        const match = line.match(/^\s*(%\w+) = OpCompositeExtract %v4float %\w+ [0-3]$/);
        if (match) {
          extractedMatrixVectors.add(match[1]);
        }
      }
      const directExtractDotCount = rawSpv.split('\n').filter((line) => {
        const match = line.match(/^\s*%\w+ = OpDot %float %\w+ (%\w+)$/);
        return match && extractedMatrixVectors.has(match[1]);
      }).length;
      t.recordCheck('Raw direct SPIR-V should dot the input vector directly with extracted matrix vectors, matching DXC row-major Vulkan ABI.', directExtractDotCount >= 4, rawSpv);
    }
  });
}

/** Creates the positive UGLIR fixture that validates Texture2DArray gather lowering on render stages. */
function createExperimentalUglirTexture2DArrayGatherCase() {
  return createFixtureCase({
    id: 'experimental-uglir-texture2darray-gather',
    title: 'Experimental UGLIR Phase 8: Texture2DArray gather render artifact',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'render', 'texture', 'gather'],
    fixtureDir: 'texture2darray-gather-lowering',
    sourceFile: 'Texture2DArrayGatherLowering.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the Texture2DArray gather fixture through the experimental render pipeline.',
    validates: [
      'Texture2DArray resources retain their array dimension in UGLIR reflection.',
      'Gather calls lower as texture intrinsics in UGLIR-derived MSL and direct SPIR-V.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('uglir/Texture2DArrayGatherPass__fragment.uglir.txt', 'Texture2DArray fragment UGLIR dump should exist.');
      await t.expectArtifactExists('spv/Texture2DArrayGatherPass__fragment.spvasm', 'Texture2DArray fragment SPIR-V disassembly should exist.');
      await t.expectFileContains('uglir/Texture2DArrayGatherPass__fragment.uglir.txt', 'texture_dimension "2d_array"', 'UGLIR reflection should preserve the texture array dimension.');
      await t.expectFileContains('uglir/Texture2DArrayGatherPass__fragment.uglir.txt', 'name "gatherGreen"', 'UGLIR should lower gatherGreen as a texture intrinsic call.');
      await t.expectFileContains('msl/Texture2DArrayGatherPass__fragment.msl', 'component::y', 'MSL should emit the green gather component.');
      await t.expectArtifactNotExists('hlsl/Texture2DArrayGatherPass__fragment.hlsl', 'Experimental texture gather should not write HLSL debug source.');
      await t.expectFileContains('spv/Texture2DArrayGatherPass__fragment.spvasm', 'OpEntryPoint Fragment', 'Direct SPIR-V should emit a fragment entry for the gather fixture.');
      await t.expectFileContains('spv/Texture2DArrayGatherPass__fragment.spvasm', 'OpCapability ImageGatherExtended', 'Direct SPIR-V should declare the gather-offset capability.');
      await t.expectFileContains('spv/Texture2DArrayGatherPass__fragment.spvasm', ' 2D 2 1 0 1 Unknown', 'Direct SPIR-V should declare an arrayed sampled image type without depending on the generated result id.');
      await t.expectFileContains('spv/Texture2DArrayGatherPass__fragment.spvasm', 'OpImageGather', 'Direct SPIR-V should emit the gather operation.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates storage texture compute emission. */
function createExperimentalUglirStorageTextureComputeCase() {
  return createFixtureCase({
    id: 'experimental-uglir-storage-texture-compute',
    title: 'Experimental UGLIR Phase 8: storage texture compute artifact',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'compute', 'storage-texture', 'spirv'],
    fixtureDir: 'storage-texture-vulkan-format-lowering',
    sourceFile: 'StorageTextureVulkanFormatLowering.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a compute shader with two RWTexture2D resources through UGLIR-derived MSL and direct SPIR-V.',
    validates: [
      'Storage texture formats are preserved in UGLIR reflection and direct SPIR-V image types.',
      'The direct SPIR-V writer emits storage images and OpImageWrite.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('spv/StorageTextureVulkanFormatPass.spvasm', 'Storage texture SPIR-V disassembly should exist.');
      await t.expectFileContains('uglir/StorageTextureVulkanFormatPass.uglir.txt', 'kind storage_texture', 'UGLIR should reflect storage texture resources.');
      await t.expectArtifactNotExists('hlsl/StorageTextureVulkanFormatPass.hlsl', 'Experimental storage texture compute should not write HLSL debug source.');
      await t.expectFileContains('spv/StorageTextureVulkanFormatPass.spvasm', ' 2D 2 0 0 2 Rgba8', 'SPIR-V should declare RGBA8 storage image semantics without depending on the generated result id.');
      await t.expectFileContains('spv/StorageTextureVulkanFormatPass.spvasm', ' 2D 2 0 0 2 R32f', 'SPIR-V should declare R32F storage image semantics without depending on the generated result id.');
      await t.expectFileContains('spv/StorageTextureVulkanFormatPass.spvasm', 'OpImageWrite', 'SPIR-V should emit storage image writes.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates DSL render-set erasure into ordinary resources. */
function createExperimentalUglirRenderSetShaderABICase() {
  return createFixtureCase({
    id: 'experimental-uglir-render-set-shader-abi',
    title: 'Experimental UGLIR: render-set DSL erases to ordinary resources',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'render', 'component-table', 'spirv'],
    fixtureDir: 'render-set-shader-abi',
    sourceFile: 'RenderSetShaderABI.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the render-set shader ABI fixture through AST-side erasure and ordinary resource backend paths.',
    validates: [
      'DSL render-set resources are reflected as ordinary component tables, texture arrays, and draw metadata buffers.',
      'UGLIR expressions use ordinary subscript, member, binary, and conditional operations rather than backend pseudo-calls.',
      'Direct SPIR-V consumes the generic indexed-resource ABI without HLSL or DXC.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('spv/RenderSetShaderABIPass__vertex.spvasm', 'Component-table vertex SPIR-V disassembly should exist.');
      await t.expectArtifactExists('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'Component-table raw writer SPIR-V disassembly should exist.');
      await t.expectArtifactNotExists('hlsl/RenderSetShaderABIPass__vertex.hlsl', 'Experimental component-table path should not write HLSL debug source.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource "renderSet.AccessBounds" kind storage_buffer set 0 binding 0', 'UGLIR should reflect the generic access-bounds table at binding 0.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource "renderSet.albedoIndexTable" kind storage_buffer set 0 binding 1', 'UGLIR should reflect the generic texture index table.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource "renderSet.albedo" kind texture set 0 binding 2 access read element "half4" array_count 4', 'UGLIR should reflect the texture descriptor array after its index table.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource "renderSet.DrawInfo" kind storage_buffer set 0 binding 7', 'UGLIR should reflect draw metadata as an ordinary storage buffer.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource "renderSet.CommandParams" kind storage_buffer set 0 binding 8', 'UGLIR should reflect command parameters as an ordinary storage buffer.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource_role "access_bounds"', 'UGLIR should classify the generic access-bounds role.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource_role "texture_index_table"', 'UGLIR should classify the generic texture-index role.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource_role "texture_value"', 'UGLIR should classify the generic texture-value role.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource_role "draw_info"', 'UGLIR should classify the generic draw-info role.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'resource_role "command_params"', 'UGLIR should classify the generic command-params role.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'op "!="', 'UGLIR checkValid lowering should reject sentinel draw-info counts.');
      await t.expectFileContains('uglir/RenderSetShaderABIPass__vertex.uglir.txt', 'literal type "u32" intrinsic none construct_kind none value "4294967295"', 'UGLIR checkValid lowering should materialize the uint sentinel literal.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_albedo DescriptorSet 0', 'SPIR-V should decorate the indexed texture descriptor set.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_AccessBounds Binding 0', 'SPIR-V should decorate the generic access-bounds binding.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_albedoIndexTable Binding 1', 'SPIR-V should decorate the texture index-table binding.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_albedo Binding 2', 'SPIR-V should decorate the texture descriptor-array binding.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_DrawInfo Binding 7', 'SPIR-V should decorate the draw-info binding.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %renderSet_CommandParams Binding 8', 'SPIR-V should decorate the command-params binding.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'ArrayStride 16', 'Raw direct SPIR-V should layout struct buffer arrays before optimization.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpDecorate %gl_InstanceIndex BuiltIn InstanceIndex', 'SPIR-V should use the vertex InstanceIndex builtin for draw-command decode.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.spvasm', 'OpTypeRuntimeArray %', 'SPIR-V should represent texture components as a runtime descriptor array.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.spvasm', 'OpImageFetch', 'SPIR-V should emit texture descriptor-array fetches.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpAccessChain %_ptr_StorageBuffer_v2uint %renderSet_CommandParams', 'SPIR-V should decode draw IDs from command parameters.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpAccessChain %_ptr_StorageBuffer_v2uint %renderSet_AccessBounds', 'SPIR-V should read the generic access-bounds table.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpAccessChain %_ptr_StorageBuffer_uint %renderSet_albedoIndexTable', 'SPIR-V should read the texture index table.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.raw.spvasm', 'OpAccessChain %_ptr_StorageBuffer_UGL_DrawInfo_ %renderSet_DrawInfo', 'SPIR-V should load generic draw-info records.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.spvasm', 'OpINotEqual %bool', 'SPIR-V should reject sentinel draw-info counts in checkValid.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.spvasm', 'OpSelect %uint', 'SPIR-V should use explicit safe-index selects for bounds hardening.');
      await t.expectFileContains('spv/RenderSetShaderABIPass__vertex.spvasm', 'OpLoad %uint', 'SPIR-V should load decoded scalar data from storage buffers.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates render template specialization names. */
function createExperimentalUglirStaticVariantRenderCase() {
  return createFixtureCase({
    id: 'experimental-uglir-static-variant-render',
    title: 'Experimental UGLIR Phase 8: static variant render specializations',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'render', 'template', 'specialization'],
    fixtureDir: 'shader-static-variant-render',
    sourceFile: 'ShaderStaticVariantRender.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs two render class template specializations through the experimental render pipeline.',
    validates: [
      'Stable UGLIR symbol names include render template specialization parameters.',
      'Each specialization emits independent vertex and fragment artifacts.'
    ],
    verify: async (t) => {
      const stems = [
        'StaticVariantRenderPass__TOpaqueMaterialPolicy_TStaticVariantBindGroup_OpaqueMaterialPolicy_TStaticVariantFrameBuffer_OpaqueMaterialPolicy_TStaticVariantVertexInput_OpaqueMaterialPolicy_I16',
        'StaticVariantRenderPass__TCutoutMaterialPolicy_TStaticVariantBindGroup_CutoutMaterialPolicy_TStaticVariantFrameBuffer_CutoutMaterialPolicy_TStaticVariantVertexInput_CutoutMaterialPolicy_I32'
      ];
      for (const stem of stems) {
        await t.expectArtifactExists(`uglir/${stem}__vertex.uglir.txt`, `${stem} vertex UGLIR dump should exist.`);
        await t.expectArtifactExists(`uglir/${stem}__fragment.uglir.txt`, `${stem} fragment UGLIR dump should exist.`);
        await t.expectArtifactExists(`spv/${stem}__vertex.spvasm`, `${stem} vertex SPIR-V disassembly should exist.`);
        await t.expectArtifactExists(`spv/${stem}__fragment.spvasm`, `${stem} fragment SPIR-V disassembly should exist.`);
        await t.expectFileContains(`spv/${stem}__vertex.spvasm`, 'OpStore', `${stem} vertex SPIR-V body should store stage outputs.`);
        await t.expectFileContains(`spv/${stem}__fragment.spvasm`, 'OpStore', `${stem} fragment SPIR-V body should store framebuffer outputs.`);
      }
    }
  });
}

/** Creates the positive UGLIR fixture that validates pixel-local render artifacts. */
function createExperimentalUglirPixelLocalDeferredScreenCase() {
  return createFixtureCase({
    id: 'experimental-uglir-pixel-local-deferred-screen',
    title: 'Experimental UGLIR Phase 8: pixel-local deferred screen artifacts',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'pixel-local', 'render', 'spirv'],
    fixtureDir: 'pixel-local-deferred-screen',
    sourceFile: 'PixelLocalDeferredScreen.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a deferred pixel-local render flow through the experimental render and pixel-local pipeline.',
    validates: [
      'IRenderClass producer artifacts and IPixelLocalRenderClass pixel artifacts are both generated.',
      'Pixel-local entries use fragment backend stage with PixelLocal UGLIR entry kind.',
      'Direct SPIR-V validates all generated pixel-local modules.'
    ],
    verify: async (t) => {
      for (const stem of ['PixelLocalGBufferPass__vertex', 'PixelLocalGBufferPass__fragment', 'PixelLocalLightingPass__pixel', 'PixelLocalTonemapPass__pixel']) {
        await t.expectArtifactExists(`uglir/${stem}.uglir.txt`, `${stem} UGLIR dump should exist.`);
        await t.expectArtifactExists(`msl/${stem}.msl`, `${stem} MSL debug output should exist.`);
        await t.expectArtifactNotExists(`hlsl/${stem}.hlsl`, `${stem} HLSL debug output should not exist.`);
        await t.expectArtifactExists(`spv/${stem}.spvasm`, `${stem} SPIR-V disassembly should exist.`);
      }
      await t.expectFileContains('uglir/PixelLocalLightingPass__pixel.uglir.txt', 'entry_kind pixel_local', 'Lighting pass UGLIR should preserve PixelLocal entry kind.');
      await t.expectFileContains('uglir/PixelLocalTonemapPass__pixel.uglir.txt', 'entry_kind pixel_local', 'Tonemap pass UGLIR should preserve PixelLocal entry kind.');
      await t.expectFileContains('uglir/PixelLocalLightingPass__pixel.uglir.txt', 'resource "inputValue.albedo" kind input_attachment', 'Lighting pass UGLIR should reflect pixel-local albedo as an input attachment resource.');
      await t.expectFileContains('uglir/PixelLocalLightingPass__pixel.uglir.txt', 'resource "inputValue.gbufferDepth" kind input_attachment', 'Lighting pass UGLIR should reflect pixel-local gbuffer depth as an input attachment resource.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.raw.spvasm', 'OpEntryPoint Fragment %fragmentMain "fragmentMain"', 'Pixel-local lighting direct SPIR-V should use fragment execution model.');
      await t.expectFileContains('spv/PixelLocalTonemapPass__pixel.raw.spvasm', 'OpEntryPoint Fragment %fragmentMain "fragmentMain"', 'Pixel-local tonemap direct SPIR-V should use fragment execution model.');
      await t.expectFileNotContains('spv/PixelLocalGBufferPass__fragment.spvasm', 'DepthReplacing', 'Pixel-local producer fragment should not declare native depth replacement for pixel-local depth attachments.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpCapability InputAttachment', 'Pixel-local lighting SPIR-V should declare the input-attachment capability.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpCapability Float16', 'Pixel-local lighting SPIR-V should declare native Float16 support for half attachments.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpTypeFloat 16', 'Pixel-local lighting SPIR-V should define a native half type.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', ' SubpassData 2 0 0 2 Unknown', 'Pixel-local lighting SPIR-V should declare a Vulkan subpass-data image type without depending on the generated result id.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'InputAttachmentIndex 0', 'Pixel-local lighting SPIR-V should decorate the first input attachment index.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'InputAttachmentIndex 1', 'Pixel-local lighting SPIR-V should decorate the second input attachment index.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpImageRead %v4float', 'Pixel-local lighting SPIR-V should read pixel-local inputs through subpass image reads.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpFConvert %v4half', 'Pixel-local lighting SPIR-V should convert color input reads into native half4 values.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.spvasm', 'OpCompositeExtract %float', 'Pixel-local lighting SPIR-V should extract scalar R32 input attachments from the four-component subpass load.');
      await t.expectFileContains('spv/PixelLocalLightingPass__pixel.raw.spvasm', 'OpStore %lighting', 'Pixel-local lighting SPIR-V should store the lighting output.');
      await t.expectFileContains('spv/PixelLocalTonemapPass__pixel.spvasm', 'OpCapability InputAttachment', 'Pixel-local tonemap SPIR-V should declare the input-attachment capability.');
      await t.expectFileContains('spv/PixelLocalTonemapPass__pixel.spvasm', 'OpImageRead %v4float', 'Pixel-local tonemap SPIR-V should read lighting through a subpass image read.');
      await t.expectFileContains('spv/PixelLocalTonemapPass__pixel.raw.spvasm', 'OpStore %present', 'Pixel-local tonemap SPIR-V should store the final present output.');
      await t.expectFileNotContains('spv/PixelLocalTonemapPass__pixel.raw.spvasm', 'OpStore %depth', 'Pixel-local tonemap SPIR-V should not store pixel-local depth as native fragment depth.');
    }
  });
}

/** Creates the positive UGLIR fixture that requires native 16-bit half SPIR-V emission. */
function createExperimentalUglirNativeHalfSPIRVCase() {
  return createFixtureCase({
    id: 'experimental-uglir-native-half-spirv',
    title: 'Experimental UGLIR Phase 9: native half direct SPIR-V',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'compute', 'half', 'spirv'],
    fixtureDir: 'hlsl-native-half-vulkan',
    sourceFile: 'HLSLNativeHalfVulkan.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs a half-heavy compute shader through UGLIR-derived MSL/HLSL/direct SPIR-V and requires real 16-bit float types.',
    validates: [
      'Direct SPIR-V declares Float16 and emits OpTypeFloat 16.',
      'Half values retain 16-bit representation while arithmetic evaluates in float and rounds back to half.',
      'The storage image write path accepts half values without falling back to DXC.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('spv/HLSLNativeHalfVulkanPass.spvasm', 'Native half direct SPIR-V disassembly should exist.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpCapability Float16', 'Direct SPIR-V should declare native Float16 support.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpTypeFloat 16', 'Direct SPIR-V should define a native 16-bit float type.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpTypeVector %half 4', 'Direct SPIR-V should define half4 as a vector of native half components.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpFConvert %v4half', 'Direct SPIR-V should convert sampled values into native half4 values.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpVectorTimesScalar %v4float', 'UGL half vector-scalar arithmetic should evaluate in float before rounding.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpFAdd %v4float', 'UGL half addition should evaluate in float before rounding.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpFConvert %v4float', 'Half operands should explicitly widen for arithmetic.');
      await t.expectFileNotContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'NoContraction', 'Half arithmetic must not impose costly contraction barriers on Vulkan targets.');
      await t.expectFileContains('spv/HLSLNativeHalfVulkanPass.spvasm', 'OpImageWrite', 'Direct SPIR-V should write the native-half result to the storage image.');
    }
  });
}

/** Creates the positive UGLIR fixture that locks WVM-inspired direct SPIR-V regression coverage. */
function createExperimentalUglirWVMRegressionCase() {
  return createFixtureCase({
    id: 'experimental-uglir-wvm-regressions',
    title: 'Experimental UGLIR: WVM-inspired intrinsic and resource alias regressions',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'spirv', 'msl', 'wvm-regression'],
    fixtureDir: '../../rhi_cases',
    sourceFile: 'ExperimentalUGLIRWVMRegressionSuite.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs minimized shaders covering the WVM diagnostics that previously exposed direct SPIR-V intrinsic, derivative, and resource alias bugs.',
    validates: [
      'sincos writes both out parameters in raw direct SPIR-V.',
      'all(boolN), sign(halfN), local StructuredBuffer aliases, fixed array parameters, and member methods lower without source rewrites.',
      'sampleLevel, sampleGrad, ddx, and ddy lower to explicit SPIR-V and MSL operations.',
      'Experimental HLSL remains bypassed.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'WVM regression fixture should write the generated host artifact.');
      await t.expectFileContains('generate_result.hpp', 'eastl::string{},', 'Experimental UGLIR artifact should keep hlslSource empty.');
      await t.expectArtifactNotExists('hlsl', 'Experimental UGLIR should not write HLSL debug artifacts.');

      await t.expectArtifactExists('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'Compute raw SPIR-V disassembly should exist for writer diagnostics.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRWVMRegressionComputePass.spvasm', 'Compute optimized SPIR-V disassembly should exist for runtime artifact diagnostics.');
      await t.expectFileNotContains('spv/ExperimentalUGLIRWVMRegressionComputePass.spvasm', 'OpName ', 'Optimized runtime SPIR-V must omit debug names.');
      await t.expectFileNotContains('spv/ExperimentalUGLIRWVMRegressionComputePass.spvasm', 'OpMemberName ', 'Optimized runtime SPIR-V must omit member debug names.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpName ', 'Raw writer diagnostics must retain debug names.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', ' Sin ', 'Raw direct SPIR-V should emit the sine half of sincos.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', ' Cos ', 'Raw direct SPIR-V should emit the cosine half of sincos.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpStore', 'Raw direct SPIR-V should store intrinsic out-parameter results.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpAll', 'Raw direct SPIR-V should lower all(boolN) to OpAll.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpBitwiseAnd', 'Binary vector operator& must not be rejected as unary address-of.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'FSign', 'Raw direct SPIR-V should lower sign(halfN) through GLSL.std.450 FSign.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpCapability Float16', 'Raw direct SPIR-V should keep native half support enabled.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionComputePass.raw.spvasm', 'OpTypeFloat 16', 'Raw direct SPIR-V should define native half type.');

      await t.expectArtifactExists('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'Texture fragment raw SPIR-V disassembly should exist.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.spvasm', 'Texture fragment optimized SPIR-V disassembly should exist.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'OpDPdx', 'Raw direct SPIR-V should lower ddx to OpDPdx.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'OpDPdy', 'Raw direct SPIR-V should lower ddy to OpDPdy.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'OpImageSampleExplicitLod', 'Raw direct SPIR-V should use explicit image sampling for sampleLevel/sampleGrad.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'Lod', 'Raw direct SPIR-V should include a Lod image operand.');
      await t.expectFileContains('spv/ExperimentalUGLIRWVMRegressionTexturePass__fragment.raw.spvasm', 'Grad', 'Raw direct SPIR-V should include a Grad image operand.');
      const computeMsl = await t.readArtifact('msl/ExperimentalUGLIRWVMRegressionComputePass.msl');
      for (const name of ['roundedCancellation', 'vectorCancellation']) {
        const declaration = computeMsl.split('\n').find(line => line.includes(` ${name} =`)) ?? '';
        t.recordCheck(`${name} should retain half rounding boundaries without duplicate float conversions.`,
          declaration.includes('__uglc_half_multiply<') && declaration.includes('__uglc_half_subtract<') &&
          !/float([234])?\(float\1\(/.test(declaration), declaration);
      }
      for (const name of ['ReadFirstResource', 'ForwardFirstResource']) {
        const signatures = computeMsl.split('\n').filter(line => line.startsWith('inline __attribute__((always_inline)) uint experimentalUGLIRWVMRegression' + name));
        t.recordCheck(`${name} should pass only its directly or transitively required resource.`,
          signatures.length === 2 && signatures.every(line => line.includes('valuesA') && !line.includes('valuesB') && !line.includes('bindGroup_output')),
          signatures.join('\n'));
      }
      const pureSignatures = computeMsl.split('\n').filter(line => line.startsWith('inline __attribute__((always_inline)) uint experimentalUGLIRWVMRegressionReadFixedArray'));
      t.recordCheck('Pure array helpers should be inline and have no resource parameters.',
        pureSignatures.length === 2 && pureSignatures.every(line => !line.includes('bindGroup_')),
        pureSignatures.join('\n'));
      await t.expectFileContains('msl/ExperimentalUGLIRWVMRegressionComputePass.msl', '[[id(2)]]', 'Helper pruning must retain the output resource in the class argument buffer.');

      await t.expectFileContains('msl/ExperimentalUGLIRWVMRegressionTexturePass__fragment.msl', 'dfdx', 'MSL should lower ddx to dfdx.');
      await t.expectFileContains('msl/ExperimentalUGLIRWVMRegressionTexturePass__fragment.msl', 'dfdy', 'MSL should lower ddy to dfdy.');
      await t.expectFileContains('msl/ExperimentalUGLIRWVMRegressionTexturePass__fragment.msl', 'level(', 'MSL should lower sampleLevel through explicit level sampling.');
      await t.expectFileContains('msl/ExperimentalUGLIRWVMRegressionTexturePass__fragment.msl', 'gradient2d', 'MSL should lower sampleGrad through explicit gradients.');
    }
  });
}

/** Creates a shadow case that compares legacy and UGLIR-derived compute artifact semantics. */
function createExperimentalUglirComputeBasicShadowCase() {
  return {
    id: 'experimental-uglir-compute-basic-shadow',
    title: 'Experimental UGLIR shader pipeline: compute-basic shadows legacy artifact semantics',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'msl', 'spirv', 'shadow'],
    kind: 'uglc-shadow-fixture',
    fixtureDir: 'compute-basic',
    sourceFile: 'ComputeBasic.hpp',
    timeoutMs: 90_000,
    description: 'Runs compute-basic through both legacy and experimental shader source pipelines and compares host reflection semantics.',
    validates: [
      'Legacy and experimental paths both generate host artifacts for the same compute fixture.',
      'The host-visible bind group layout and workgroup metadata remain equivalent.',
      'The experimental path keeps HLSL empty and compares SPIR-V through direct-writer artifacts.'
    ],
    watchouts: [],
    verify: async (t) => {
      await t.expectArtifactExists('legacy/generate_result.hpp', 'Legacy shadow output should write the generated host header.');
      await t.expectArtifactExists('uglir/generate_result.hpp', 'Experimental shadow output should write the generated host header.');
      const sharedSnippets = [
        'layoutEntry[0].binding = 0',
        'layoutEntry[0].visibility = GVM::RHI::ShaderStage::Compute',
        'bindgroupEntry[0].binding = 0',
        'computeDesp.entryPoint = "computeMain"',
        'computeDesp.workgroupX = 8',
        'computeDesp.workgroupY = 1',
        'computeDesp.workgroupZ = 1'
      ];
      for (const snippet of sharedSnippets) {
        await t.expectFileContains('legacy/generate_result.hpp', snippet, `Legacy artifact should contain reflection snippet: ${snippet}`);
        await t.expectFileContains('uglir/generate_result.hpp', snippet, `Experimental artifact should preserve reflection snippet: ${snippet}`);
      }

      const legacySources = await readGeneratedShaderSources(t, 'legacy/generate_result.hpp');
      const experimentalSources = await readGeneratedShaderSources(t, 'uglir/generate_result.hpp');
      t.recordCheck('Shadow comparison should compare non-empty legacy and experimental MSL payloads.', (
        legacySources.mslSource.length > 0 && experimentalSources.mslSource.length > 0
      ), `legacy=${legacySources.mslSource.length}, experimental=${experimentalSources.mslSource.length}`);
      t.recordCheck('Experimental shadow output should keep HLSL empty while legacy HLSL remains available.', (
        legacySources.hlslSource.length > 0 && experimentalSources.hlslSource.length === 0
      ), `legacy=${legacySources.hlslSource.length}, experimental=${experimentalSources.hlslSource.length}`);
      t.recordCheck('Experimental shadow output should use the UGLIR MSL emitter entry shape.', (
        experimentalSources.mslSource.includes('kernel void computeMain') &&
        experimentalSources.mslSource.includes('bindGroup->values[index]')
      ), experimentalSources.mslSource);
      await t.expectArtifactExists('uglir/spv/ComputeBasicPass.spvasm', 'Experimental shadow output should write direct SPIR-V disassembly.');
      await t.expectFileContains('uglir/spv/ComputeBasicPass.raw.spvasm', 'OpExecutionMode %computeMain LocalSize 8 1 1', 'Experimental direct SPIR-V should preserve workgroup size in shadow output.');
      await t.expectFileContains('uglir/spv/ComputeBasicPass.spvasm', 'DescriptorSet 0', 'Experimental direct SPIR-V should preserve descriptor set in shadow output.');
      await t.expectFileContains('uglir/spv/ComputeBasicPass.spvasm', 'Binding 0', 'Experimental direct SPIR-V should preserve binding index in shadow output.');
      const experimentalSpirvWords = await readSpirvWordArray(t, 'uglir/generate_result.hpp', 'computeShaderArtifact_SpirvWords', 'Experimental shadow output should embed SPIR-V emitted directly from UGLIR.');
      t.recordCheck('Experimental shadow SPIR-V payload should be non-empty.', experimentalSpirvWords !== null && experimentalSpirvWords.length > 0, experimentalSpirvWords ? `words=${experimentalSpirvWords.length}` : 'Missing SPIR-V words.');
    }
  };
}

/** Creates the positive UGLIR fixture that validates class-template compute specialization names and if constexpr lowering. */
function createExperimentalUglirStaticVariantComputeTemplateCase() {
  return createFixtureCase({
    id: 'experimental-uglir-static-variant-compute-template',
    title: 'Experimental UGLIR C++20: compute class template specializations generate distinct artifacts',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'cpp20', 'template', 'constexpr', 'spirv'],
    fixtureDir: 'shader-static-variant-compute',
    sourceFile: 'ShaderStaticVariantCompute.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the experimental compute pipeline on two class-template specializations with NTTP workgroup sizes and an if constexpr body.',
    validates: [
      'The UGLIR pipeline lowers only concrete class-template specializations.',
      'Stable UGLIR symbols keep the two NTTP variants separate.',
      'if constexpr contributes only the selected branch to UGLIR and generated backends.'
    ],
    verify: async (t) => {
      for (const stem of ['StaticVariantComputePass__I8', 'StaticVariantComputePass__I16']) {
        await t.expectArtifactExists(`uglir/${stem}.uglir.txt`, `UGLIR text dump should exist for ${stem}.`);
        await t.expectArtifactExists(`uglir/${stem}.uglir.json`, `UGLIR JSON dump should exist for ${stem}.`);
        await t.expectArtifactExists(`msl/${stem}.msl`, `UGLIR-derived MSL debug output should exist for ${stem}.`);
        await t.expectArtifactNotExists(`hlsl/${stem}.hlsl`, `UGLIR-derived HLSL debug output should not exist for ${stem}.`);
        await t.expectArtifactExists(`spv/${stem}.spvasm`, `Direct SPIR-V disassembly should exist for ${stem}.`);
        await t.expectFileContains(`uglir/${stem}.uglir.txt`, `module "${stem}"`, `UGLIR module should use the stable specialization symbol for ${stem}.`);
        await t.expectFileNotContains(`uglir/${stem}.uglir.txt`, 'if constexpr', `UGLIR dump should not preserve source if constexpr syntax for ${stem}.`);
      }
      await t.expectFileContains('uglir/StaticVariantComputePass__I8.uglir.txt', 'workgroup_size 8 1 1', 'Workgroup-8 specialization should preserve its NTTP workgroup size.');
      await t.expectFileContains('uglir/StaticVariantComputePass__I16.uglir.txt', 'workgroup_size 16 1 1', 'Workgroup-16 specialization should preserve its NTTP workgroup size.');
      await t.expectFileContains('spv/StaticVariantComputePass__I8.spvasm', 'LocalSize 8 1 1', 'Direct SPIR-V should preserve the workgroup-8 execution mode.');
      await t.expectFileContains('spv/StaticVariantComputePass__I16.spvasm', 'LocalSize 16 1 1', 'Direct SPIR-V should preserve the workgroup-16 execution mode.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates function-template policy callbacks and struct support. */
function createExperimentalUglirTemplatePolicyClassCallbackCase() {
  return createFixtureCase({
    id: 'experimental-uglir-template-policy-class-callback',
    title: 'Experimental UGLIR C++20: function-template policy callbacks generate MSL/direct SPIR-V',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'cpp20', 'template', 'policy', 'struct', 'spirv'],
    fixtureDir: 'template-function-class-callback',
    sourceFile: 'TemplateFunctionClassCallback.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the experimental compute pipeline on a function template instantiated with two stateless policy classes.',
    validates: [
      'Function-template specializations receive stable UGLIR symbols.',
      'Policy methods lower as ordinary helper functions.',
      'The direct SPIR-V writer supports user struct parameters, returns, and member load/store.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'Policy callback fixture should generate host artifacts.');
      await t.expectArtifactExists('uglir/TemplateFunctionClassCallbackPass.uglir.txt', 'Policy callback fixture should write a UGLIR text dump.');
      await t.expectArtifactExists('msl/TemplateFunctionClassCallbackPass.msl', 'Policy callback fixture should write UGLIR-derived MSL.');
      await t.expectArtifactNotExists('hlsl/TemplateFunctionClassCallbackPass.hlsl', 'Policy callback fixture should not write UGLIR-derived HLSL.');
      await t.expectArtifactExists('spv/TemplateFunctionClassCallbackPass.raw.spvasm', 'Policy callback fixture should write raw direct SPIR-V disassembly for writer diagnostics.');
      await t.expectArtifactExists('spv/TemplateFunctionClassCallbackPass.spvasm', 'Policy callback fixture should write direct SPIR-V disassembly.');
      await t.expectFileContains('uglir/TemplateFunctionClassCallbackPass.uglir.txt', 'runCallback__T', 'UGLIR should contain concrete runCallback function-template specializations.');
      await t.expectFileContains('uglir/TemplateFunctionClassCallbackPass.uglir.txt', 'EvenCallback.init', 'UGLIR should contain the even policy init helper.');
      await t.expectFileContains('uglir/TemplateFunctionClassCallbackPass.uglir.txt', 'AlwaysCallback.init', 'UGLIR should contain the always policy init helper.');
      await t.expectFileContains('uglir/TemplateFunctionClassCallbackPass.uglir.txt', 'type "TemplateFunctionClassCallbackHelpers::Context"', 'UGLIR should contain the policy Context struct type.');
      await t.expectFileContains('msl/TemplateFunctionClassCallbackPass.msl', 'struct TemplateFunctionClassCallbackHelpers__Context', 'MSL should emit the Context struct definition.');
      await t.expectFileContains('spv/TemplateFunctionClassCallbackPass.raw.spvasm', 'OpFunctionCall', 'Raw direct SPIR-V should lower policy calls as helper function calls before optimization.');
      await t.expectFileContains('spv/TemplateFunctionClassCallbackPass.spvasm', 'OpStore', 'Direct SPIR-V should store the transformed policy result.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates concepts/requires success through Clang-instantiated helpers. */
function createExperimentalUglirConceptsRequiresPolicyCase() {
  return createFixtureCase({
    id: 'experimental-uglir-concepts-requires-policy',
    title: 'Experimental UGLIR C++20: concepts/requires accepted helpers lower after Clang instantiation',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'cpp20', 'concepts', 'requires'],
    fixtureDir: 'experimental-uglir-concepts-requires-policy',
    sourceFile: 'ExperimentalUGLIRConceptsRequiresPolicy.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the experimental compute pipeline on a constrained helper template whose requires clause has already passed Clang semantic analysis.',
    validates: [
      'Concept and requires syntax does not leak into UGLIR.',
      'Only the instantiated helper and policy method are lowered.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'Concepts/requires fixture should generate host artifacts.');
      await t.expectArtifactExists('uglir/ExperimentalUGLIRConceptsRequiresPolicyPass.uglir.txt', 'Concepts/requires fixture should write a UGLIR text dump.');
      await t.expectFileContains('uglir/ExperimentalUGLIRConceptsRequiresPolicyPass.uglir.txt', 'applyPolicy__T', 'UGLIR should contain the instantiated constrained helper.');
      await t.expectFileContains('uglir/ExperimentalUGLIRConceptsRequiresPolicyPass.uglir.txt', 'AddFivePolicy.transform', 'UGLIR should contain the resolved policy method helper.');
      await t.expectFileNotContains('uglir/ExperimentalUGLIRConceptsRequiresPolicyPass.uglir.txt', 'TransformPolicy', 'UGLIR should not preserve concept-only AST symbols.');
      await t.expectFileNotContains('uglir/ExperimentalUGLIRConceptsRequiresPolicyPass.uglir.txt', 'requires TransformPolicy', 'UGLIR should not preserve requires-clause AST syntax.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRConceptsRequiresPolicyPass.spvasm', 'Concepts/requires fixture should write direct SPIR-V disassembly.');
    }
  });
}

/** Creates the positive UGLIR fixture that validates non-escaping lambda lowering. */
function createExperimentalUglirLambdaComputeCase() {
  return createFixtureCase({
    id: 'experimental-uglir-lambda-compute',
    title: 'Experimental UGLIR C++20: non-escaping lambdas lower into internal helper calls',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'cpp20', 'lambda', 'spirv'],
    fixtureDir: 'experimental-uglir-lambda-compute',
    sourceFile: 'ExperimentalUGLIRLambdaCompute.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Runs the experimental compute pipeline on immediate and local non-escaping lambdas with value captures and a resource write.',
    validates: [
      'Each supported lambda lowers into an internal UGLIR helper.',
      'Capture values become explicit helper parameters.',
      'MSL and direct SPIR-V emit helper calls instead of preserving C++ lambda syntax.'
    ],
    verify: async (t) => {
      await t.expectArtifactExists('generate_result.hpp', 'Lambda fixture should generate host artifacts.');
      await t.expectArtifactExists('uglir/ExperimentalUGLIRLambdaComputePass.uglir.txt', 'Lambda fixture should write a UGLIR text dump.');
      await t.expectArtifactExists('msl/ExperimentalUGLIRLambdaComputePass.msl', 'Lambda fixture should write UGLIR-derived MSL.');
      await t.expectArtifactNotExists('hlsl/ExperimentalUGLIRLambdaComputePass.hlsl', 'Lambda fixture should not write UGLIR-derived HLSL.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRLambdaComputePass.raw.spvasm', 'Lambda fixture should write raw direct SPIR-V disassembly for writer diagnostics.');
      await t.expectArtifactExists('spv/ExperimentalUGLIRLambdaComputePass.spvasm', 'Lambda fixture should write direct SPIR-V disassembly.');
      await t.expectFileContains('uglir/ExperimentalUGLIRLambdaComputePass.uglir.txt', '.lambda0', 'UGLIR should contain the first lowered lambda helper.');
      await t.expectFileContains('uglir/ExperimentalUGLIRLambdaComputePass.uglir.txt', 'capture_base', 'UGLIR lambda helper should expose value capture parameters.');
      await t.expectFileContains('uglir/ExperimentalUGLIRLambdaComputePass.uglir.txt', 'capture_result', 'UGLIR lambda helper should expose the captured result parameter.');
      await t.expectFileContains('msl/ExperimentalUGLIRLambdaComputePass.msl', 'lambda', 'MSL should contain sanitized lambda helper calls.');
      await t.expectFileContains('spv/ExperimentalUGLIRLambdaComputePass.raw.spvasm', 'OpFunctionCall', 'Raw direct SPIR-V should lower lambda calls as helper function calls before optimization.');
    }
  });
}

/** Verifies the common experimental direct-SPIR-V artifact policy for one fixture output. */
async function expectExperimentalSpirvOnlyPolicy(t, stems) {
  await t.expectArtifactExists('generate_result.hpp', 'Experimental fixture should write the generated host artifact.');
  await t.expectFileContains('generate_result.hpp', 'eastl::string{},', 'Experimental UGLIR artifact should keep hlslSource empty.');
  await t.expectArtifactNotExists('hlsl', 'Experimental UGLIR should not write HLSL debug artifacts.');

  for (const stem of stems) {
    await t.expectArtifactExists(`spv/${stem}.raw.spvasm`, `${stem} should write raw direct SPIR-V disassembly for writer diagnostics.`);
    await t.expectArtifactExists(`spv/${stem}.spvasm`, `${stem} should write optimized direct SPIR-V disassembly for runtime artifact diagnostics.`);
    await t.expectArtifactExists(`spv/${stem}.raw.spv.txt`, `${stem} should write a raw direct SPIR-V word dump.`);
    await t.expectArtifactExists(`spv/${stem}.spv.txt`, `${stem} should write an optimized direct SPIR-V word dump.`);
  }
}

/** Creates the UGLIR fixture that locks structured scalar, vector, matrix, swizzle, and construct metadata. */
function createExperimentalUglirSymbolicValueTypesCase() {
  const stem = 'ExperimentalUGLIRSymbolicValueTypesPass';
  return createFixtureCase({
    id: 'experimental-uglir-symbolic-value-types',
    title: 'Experimental UGLIR: symbolic value type metadata',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'value-types', 'spirv', 'msl'],
    fixtureDir: 'experimental-uglir-symbolic-value-types',
    sourceFile: 'ExperimentalUGLIRSymbolicValueTypes.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Compiles a minimal compute shader that exercises structured value-type metadata instead of backend string parsing.',
    validates: [
      'UGLIR text and JSON expose scalar kind, vector width, matrix shape, and construct metadata.',
      'MSL and direct SPIR-V are generated from the structured value-type metadata.',
      'Experimental HLSL remains bypassed.'
    ],
    verify: async (t) => {
      await expectExperimentalSpirvOnlyPolicy(t, [stem]);
      await t.expectArtifactExists(`uglir/${stem}.uglir.txt`, 'Value-type fixture should write a UGLIR text dump.');
      await t.expectArtifactExists(`uglir/${stem}.uglir.json`, 'Value-type fixture should write a UGLIR JSON dump.');
      await t.expectArtifactExists(`msl/${stem}.msl`, 'Value-type fixture should write UGLIR-derived MSL.');

      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'scalar_kind "half"', 'UGLIR text should describe half values with scalar_kind metadata.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'vector_width 4', 'UGLIR text should describe vector width metadata.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'matrix 3x3', 'UGLIR text should describe matrix dimensions.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'construct_kind vector_from_components', 'UGLIR text should expose normalized vector construct metadata.');

      const parsed = JSON.parse(await t.readArtifact(`uglir/${stem}.uglir.json`));
      const text = JSON.stringify(parsed);
      t.recordCheck('UGLIR JSON should expose structured scalar, vector, matrix, and construct metadata.', (
        text.includes('"scalarKind":"half"') &&
        text.includes('"scalarKind":"float"') &&
        text.includes('"vectorWidth":4') &&
        text.includes('"matrixRows":3') &&
        text.includes('"matrixColumns":3') &&
        text.includes('"constructKind":"vector_from_components"')
      ), text);

      await t.expectFileContains(`msl/${stem}.msl`, 'half4', 'MSL should emit half vectors from structured type metadata.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpTypeFloat 16', 'Raw direct SPIR-V should declare native half from structured type metadata.');
    }
  });
}

/** Creates the UGLIR fixture that locks structured resource metadata and backend declarations. */
function createExperimentalUglirSymbolicResourcesCase() {
  const stem = 'ExperimentalUGLIRSymbolicResourcesPass';
  return createFixtureCase({
    id: 'experimental-uglir-symbolic-resources',
    title: 'Experimental UGLIR: symbolic resource metadata',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'resources', 'spirv', 'msl'],
    fixtureDir: 'experimental-uglir-symbolic-resources',
    sourceFile: 'ExperimentalUGLIRSymbolicResources.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Compiles a compute shader that declares buffers, uniforms, sampled textures, storage textures, and a sampler through structured UGLIR resource metadata.',
    validates: [
      'UGLIR reflection exposes resource kind, access mode, texture dimension, texture format, and set/binding.',
      'MSL uses the argument-buffer ABI and direct SPIR-V emits descriptor/image/storage markers.',
      'Experimental HLSL remains bypassed.'
    ],
    verify: async (t) => {
      await expectExperimentalSpirvOnlyPolicy(t, [stem]);
      await t.expectArtifactExists(`uglir/${stem}.uglir.json`, 'Resource fixture should write a UGLIR JSON dump.');
      await t.expectArtifactExists(`msl/${stem}.msl`, 'Resource fixture should write UGLIR-derived MSL.');

      const parsed = JSON.parse(await t.readArtifact(`uglir/${stem}.uglir.json`));
      const text = JSON.stringify(parsed);
      t.recordCheck('UGLIR JSON should expose structured resource kinds, dimensions, formats, and bindings.', (
        text.includes('"kind":"storage_buffer"') &&
        text.includes('"kind":"uniform_buffer"') &&
        text.includes('"kind":"texture"') &&
        text.includes('"kind":"storage_texture"') &&
        text.includes('"kind":"sampler"') &&
        text.includes('"accessMode":"read_write"') &&
        text.includes('"textureDimension":"2d_array"') &&
        text.includes('"textureDimension":"3d"') &&
        text.includes('"textureFormat":"RGBA8Unorm"') &&
        text.includes('"bindingIndex":9')
      ), text);

      await t.expectFileContains(`msl/${stem}.msl`, '[[buffer(0)]]', 'MSL should bind the single bind group through the Metal argument-buffer slot.');
      await t.expectFileContains(`msl/${stem}.msl`, '[[id(9)]]', 'MSL argument buffer should preserve the highest resource binding id.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'DescriptorSet 0', 'Raw direct SPIR-V should decorate resources with descriptor set metadata.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'Binding 9', 'Raw direct SPIR-V should decorate storageVolume with binding metadata.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpImageWrite', 'Raw direct SPIR-V should emit storage image writes.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpImageSampleExplicitLod', 'Raw direct SPIR-V should emit explicit sampled texture operations that are valid in compute.');
    }
  });
}

/** Checks stage semantics on an uninstantiated member record of a shader class template. */
function createExperimentalUglirNestedStageOutputCase() {
  return createFixtureCase({
    id: 'experimental-uglir-nested-stage-output',
    title: 'Experimental UGLIR: nested template stage output',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'stage-io', 'render', 'spirv', 'msl'],
    fixtureDir: 'experimental-uglir-nested-stage-output',
    sourceFile: 'NestedStageOutput.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Instantiates factory-referenced shader entries and nested stage records, retaining only the selected if constexpr branch.',
    validates: ['Both direct backends retain nested template stage fields and the Metal position correction.'],
    verify: async (t) => {
      const vertexStem = 'NestedStagePass__I0__vertex';
      const fragmentStem = 'NestedStagePass__I0__fragment';
      await expectExperimentalSpirvOnlyPolicy(t, [vertexStem, fragmentStem]);
      await t.expectFileContains(`msl/${vertexStem}.msl`, '[[position]]', 'Nested vertex output must retain its position semantic.');
      await t.expectFileContains(`msl/${vertexStem}.msl`, '.position.y = -', 'Nested vertex output must receive the Metal Y correction.');
      await t.expectFileContains(`spv/${vertexStem}.raw.spvasm`, 'BuiltIn Position', 'Nested vertex output must retain the SPIR-V position decoration.');
      await t.expectFileContains(`spv/${vertexStem}.raw.spvasm`, 'Location 1', 'Nested indexed varyings must retain their locations.');
    }
  });
}

/** Creates the UGLIR fixture that locks symbolic stage IO and builtin semantic metadata. */
function createExperimentalUglirStageIOSemanticsCase() {
  const vertexStem = 'ExperimentalUGLIRStageIOSemanticsPass__vertex';
  const fragmentStem = 'ExperimentalUGLIRStageIOSemanticsPass__fragment';
  return createFixtureCase({
    id: 'experimental-uglir-stage-io-semantics',
    title: 'Experimental UGLIR: symbolic stage IO semantics',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'stage-io', 'render', 'spirv', 'msl'],
    fixtureDir: 'experimental-uglir-stage-io-semantics',
    sourceFile: 'ExperimentalUGLIRStageIOSemantics.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Compiles a minimal render shader that exercises VertexID, InstanceID, VertexInput0, Position, AttributeN, color, and depth metadata.',
    validates: [
      'UGLIR dumps include semanticKind and semanticIndex for stage inputs and outputs.',
      'MSL and SPIR-V map the symbolic semantics to native stage ABI markers.',
      'Experimental HLSL remains bypassed.'
    ],
    verify: async (t) => {
      await expectExperimentalSpirvOnlyPolicy(t, [vertexStem, fragmentStem]);
      await t.expectArtifactExists(`uglir/${vertexStem}.uglir.json`, 'Stage IO vertex module should write a UGLIR JSON dump.');
      await t.expectArtifactExists(`uglir/${fragmentStem}.uglir.json`, 'Stage IO fragment module should write a UGLIR JSON dump.');

      const vertexJson = JSON.stringify(JSON.parse(await t.readArtifact(`uglir/${vertexStem}.uglir.json`)));
      const fragmentJson = JSON.stringify(JSON.parse(await t.readArtifact(`uglir/${fragmentStem}.uglir.json`)));
      t.recordCheck('Vertex UGLIR JSON should expose builtin and indexed semantic metadata.', (
        vertexJson.includes('"semanticKind":"VertexID"') &&
        vertexJson.includes('"semanticKind":"InstanceID"') &&
        vertexJson.includes('"semanticKind":"VertexInput"') &&
        vertexJson.includes('"semanticKind":"Position"') &&
        vertexJson.includes('"semanticKind":"Attribute"') &&
        vertexJson.includes('"semanticIndex":1')
      ), vertexJson);
      t.recordCheck('Fragment UGLIR JSON should expose stage input, color output, and depth output metadata.', (
        fragmentJson.includes('"semanticKind":"StageInput"') &&
        fragmentJson.includes('"semanticKind":"Color"') &&
        fragmentJson.includes('"semanticKind":"Depth"')
      ), fragmentJson);

      await t.expectFileContains(`msl/${vertexStem}.msl`, '[[vertex_id]]', 'MSL vertex entry should map VertexID to Metal vertex_id.');
      await t.expectFileContains(`msl/${vertexStem}.msl`, '[[instance_id]]', 'MSL vertex entry should map InstanceID to Metal instance_id.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, '[[stage_in]]', 'MSL fragment entry should materialize the stage input record.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, '[[depth(any)]]', 'MSL fragment output should expose native depth only for the active depth field.');
      await t.expectFileContains(`spv/${vertexStem}.raw.spvasm`, 'OpEntryPoint Vertex', 'Raw direct SPIR-V should declare a vertex entry point.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpEntryPoint Fragment', 'Raw direct SPIR-V should declare a fragment entry point.');
      await t.expectFileContains(`spv/${vertexStem}.raw.spvasm`, 'BuiltIn VertexIndex', 'Raw direct SPIR-V should decorate VertexID with VertexIndex.');
      await t.expectFileContains(`spv/${vertexStem}.raw.spvasm`, 'BuiltIn InstanceIndex', 'Raw direct SPIR-V should decorate InstanceID with InstanceIndex.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'DepthReplacing', 'Raw direct SPIR-V should declare depth replacement only for the active native depth output.');
    }
  });
}

/** Creates the UGLIR fixture that locks compute-safe intrinsic call metadata. */
function createExperimentalUglirIntrinsicCallKindsComputeCase() {
  const stem = 'ExperimentalUGLIRIntrinsicCallKindsComputePass';
  return createFixtureCase({
    id: 'experimental-uglir-intrinsic-call-kinds-compute',
    title: 'Experimental UGLIR: compute intrinsic call kinds',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'intrinsic-call-kind', 'compute', 'spirv'],
    fixtureDir: 'experimental-uglir-intrinsic-call-kinds-compute',
    sourceFile: 'ExperimentalUGLIRIntrinsicCallKindsCompute.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Compiles compute-safe math, bitcast, barrier, atomic, all, sign, and sincos calls through structured IntrinsicCallKind metadata.',
    validates: [
      'UGLIR dump exposes intrinsicKind strings backed by IntrinsicCallKind metadata.',
      'Raw direct SPIR-V includes the expected intrinsic instruction families.',
      'Optimized SPIR-V exists and experimental HLSL remains bypassed.'
    ],
    verify: async (t) => {
      await expectExperimentalSpirvOnlyPolicy(t, [stem]);
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic math_sincos', 'UGLIR text should classify sincos as an intrinsic call.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic atomic_add', 'UGLIR text should classify atomicAdd as an intrinsic call.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic group_memory_barrier_with_group_sync', 'UGLIR text should classify group sync barrier as an intrinsic call.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic math_all', 'UGLIR text should classify all(boolN) as an intrinsic call.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic math_sign', 'UGLIR text should classify sign(halfN) as an intrinsic call.');
      await t.expectFileContains(`uglir/${stem}.uglir.txt`, 'intrinsic math_atan2', 'UGLIR text should classify atan2 as an intrinsic call.');

      const text = JSON.stringify(JSON.parse(await t.readArtifact(`uglir/${stem}.uglir.json`)));
      t.recordCheck('UGLIR JSON should expose compute intrinsic call kinds.', (
        text.includes('"intrinsicKind":"math_sincos"') &&
        text.includes('"intrinsicKind":"bitcast_asuint"') &&
        text.includes('"intrinsicKind":"bitcast_asfloat"') &&
        text.includes('"intrinsicKind":"atomic_add"') &&
        text.includes('"intrinsicKind":"math_all"') &&
        text.includes('"intrinsicKind":"math_sign"') &&
        text.includes('"intrinsicKind":"math_atan2"')
      ), text);

      await t.expectFileContains(`spv/${stem}.raw.spvasm`, ' Sin ', 'Raw direct SPIR-V should emit sine for sincos.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, ' Cos ', 'Raw direct SPIR-V should emit cosine for sincos.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpAtomicIAdd', 'Raw direct SPIR-V should emit an atomic add.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpControlBarrier', 'Raw direct SPIR-V should emit a control barrier.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpBitcast', 'Raw direct SPIR-V should emit bitcasts.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'OpAll', 'Raw direct SPIR-V should lower all(boolN) to OpAll.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, 'FSign', 'Raw direct SPIR-V should lower sign(halfN) through GLSL.std.450.');
      await t.expectFileContains(`spv/${stem}.raw.spvasm`, ' Atan2 ', 'Raw direct SPIR-V should lower atan2 through GLSL.std.450.');
    }
  });
}

/** Creates the UGLIR fixture that locks fragment texture and derivative intrinsic call metadata. */
function createExperimentalUglirIntrinsicCallKindsFragmentCase() {
  const vertexStem = 'ExperimentalUGLIRIntrinsicCallKindsFragmentPass__vertex';
  const fragmentStem = 'ExperimentalUGLIRIntrinsicCallKindsFragmentPass__fragment';
  return createFixtureCase({
    id: 'experimental-uglir-intrinsic-call-kinds-fragment',
    title: 'Experimental UGLIR: fragment intrinsic call kinds',
    group: 'uglir',
    labels: ['uglir', 'uglir', 'intrinsic-call-kind', 'fragment', 'texture', 'spirv', 'msl'],
    fixtureDir: 'experimental-uglir-intrinsic-call-kinds-fragment',
    sourceFile: 'ExperimentalUGLIRIntrinsicCallKindsFragment.hpp',
    uglcArgs: ['--shader-pipeline=uglir'],
    expectedExitCode: 0,
    description: 'Compiles fragment texture, derivative, dimensions, discard, and clip calls through structured IntrinsicCallKind metadata.',
    validates: [
      'UGLIR dump exposes texture, derivative, dimensions, discard, and clip intrinsic call kinds.',
      'MSL emits native texture APIs, derivatives, and the clip helper.',
      'Raw direct SPIR-V emits image, derivative, discard, and conditional clip kill operations.'
    ],
    verify: async (t) => {
      await expectExperimentalSpirvOnlyPolicy(t, [vertexStem, fragmentStem]);
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_sample', 'UGLIR text should classify texture sample as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_sample_level', 'UGLIR text should classify sampleLevel as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_sample_grad', 'UGLIR text should classify sampleGrad as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_gather_red', 'UGLIR text should classify gatherRed as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_read', 'UGLIR text should classify texture read as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic texture_get_dimensions', 'UGLIR text should classify getDimensions as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic math_ddx', 'UGLIR text should classify ddx as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic math_ddy', 'UGLIR text should classify ddy as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic discard_fragment', 'UGLIR text should classify discard_fragment as an intrinsic call.');
      await t.expectFileContains(`uglir/${fragmentStem}.uglir.txt`, 'intrinsic clip', 'UGLIR text should classify clip as an intrinsic call.');

      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'dfdx', 'MSL should lower ddx to dfdx.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'dfdy', 'MSL should lower ddy to dfdy.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'sample(', 'MSL should emit native texture sampling.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'level(', 'MSL should emit explicit level sampling.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'gradient2d', 'MSL should emit explicit gradient sampling.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'gather', 'MSL should emit texture gather.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'discard_fragment', 'MSL should emit fragment discard.');
      await t.expectFileContains(`msl/${fragmentStem}.msl`, 'UGLC_clip', 'MSL should lower clip to the prelude helper.');

      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpImageSampleImplicitLod', 'Raw direct SPIR-V should emit implicit-lod sampling.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpImageSampleExplicitLod', 'Raw direct SPIR-V should emit explicit-lod sampling.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'Grad', 'Raw direct SPIR-V should include gradient image operands.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpImageGather', 'Raw direct SPIR-V should emit texture gather.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpImageFetch', 'Raw direct SPIR-V should emit texture read/load.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpImageQuerySize', 'Raw direct SPIR-V should emit texture dimensions query.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpDPdx', 'Raw direct SPIR-V should emit ddx.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpDPdy', 'Raw direct SPIR-V should emit ddy.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpKill', 'Raw direct SPIR-V should emit discard.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpBranchConditional', 'Raw direct SPIR-V should emit conditional control flow for clip.');
      await t.expectFileContains(`spv/${fragmentStem}.raw.spvasm`, 'OpAny', 'Raw direct SPIR-V should reduce vector clip comparisons with OpAny.');
    }
  });
}

async function expectSnippetsInOrder(t, relativePath, snippets, description) {
  let content = '';
  try {
    content = await t.readArtifact(relativePath);
  } catch (error) {
    t.recordCheck(description, false, `Missing artifact: ${relativePath}`);
    return false;
  }

  let searchStart = 0;
  for (const snippet of snippets) {
    const index = content.indexOf(snippet, searchStart);
    if (index === -1) {
      t.recordCheck(description, false, `Could not find expected text in order: ${snippet}`);
      return false;
    }
    searchStart = index + snippet.length;
  }

  t.recordCheck(description, true, `Confirmed ordered snippets in ${relativePath}`);
  return true;
}

/**
 * Extracts the embedded MSL and HLSL source payloads from one generated shader artifact header.
 */
async function readGeneratedShaderSources(t, relativePath) {
  let content = '';
  try {
    content = await t.readArtifact(relativePath);
  } catch (error) {
    t.recordCheck(`Read generated shader sources from ${relativePath}.`, false, `Missing artifact: ${relativePath}`);
    return { mslSource: '', hlslSource: '' };
  }

  const mslOpenMarker = '__UGL__Global__MSLHeader + R"(';
  const mslStart = content.indexOf(mslOpenMarker);
  const mslContentStart = mslStart === -1 ? -1 : mslStart + mslOpenMarker.length;
  const mslEnd = mslContentStart === -1 ? -1 : content.indexOf(')",', mslContentStart);
  const hlslStart = mslEnd === -1 ? -1 : content.indexOf('R"(', mslEnd);
  const hlslContentStart = hlslStart === -1 ? -1 : hlslStart + 'R"('.length;
  const hlslEnd = hlslContentStart === -1 ? -1 : content.indexOf(')",', hlslContentStart);
  const mslSource = mslContentStart === -1 || mslEnd === -1 ? '' : content.slice(mslContentStart, mslEnd);
  const hlslSource = hlslContentStart === -1 || hlslEnd === -1 ? '' : content.slice(hlslContentStart, hlslEnd);
  return { mslSource, hlslSource };
}

async function readSpirvWordArray(t, relativePath, arrayName, description) {
  let content = '';
  try {
    content = await t.readArtifact(relativePath);
  } catch (error) {
    t.recordCheck(description, false, `Missing artifact: ${relativePath}`);
    return null;
  }

  const escapedArrayName = arrayName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const arrayPattern = new RegExp(`static constexpr uint32_t ${escapedArrayName}\\[\\] = \\{([\\s\\S]*?)\\n\\s*\\};`);
  const match = arrayPattern.exec(content);
  if (!match) {
    t.recordCheck(description, false, `Missing SPIR-V word array: ${arrayName}`);
    return null;
  }

  const words = [];
  for (const wordMatch of match[1].matchAll(/0x([0-9A-Fa-f]+)u?/g)) {
    words.push(Number.parseInt(wordMatch[1], 16) >>> 0);
  }

  if (words.length < 5 || words[0] !== 0x07230203) {
    t.recordCheck(description, false, `${arrayName} does not look like a SPIR-V module.`);
    return null;
  }

  return words;
}

function spirvHasInstruction(words, opcode, predicate) {
  for (let offset = 5; offset < words.length;) {
    const instruction = words[offset];
    const wordCount = instruction >>> 16;
    const currentOpcode = instruction & 0xffff;
    if (wordCount === 0 || offset + wordCount > words.length) {
      return false;
    }

    if (currentOpcode === opcode && predicate(words, offset, wordCount)) {
      return true;
    }
    offset += wordCount;
  }

  return false;
}

async function expectSpirvHasTypeFloatWidth(t, relativePath, arrayName, width, description) {
  const words = await readSpirvWordArray(t, relativePath, arrayName, description);
  if (!words) {
    return false;
  }

  const found = spirvHasInstruction(words, 22, (moduleWords, offset, wordCount) => wordCount >= 3 && moduleWords[offset + 2] === width);
  t.recordCheck(description, found, found
    ? `Found OpTypeFloat ${width} in ${arrayName}.`
    : `Did not find OpTypeFloat ${width} in ${arrayName}.`);
  return found;
}

async function expectSpirvHasCapability(t, relativePath, arrayName, capability, description) {
  const words = await readSpirvWordArray(t, relativePath, arrayName, description);
  if (!words) {
    return false;
  }

  const found = spirvHasInstruction(words, 17, (moduleWords, offset, wordCount) => wordCount >= 2 && moduleWords[offset + 1] === capability);
  t.recordCheck(description, found, found
    ? `Found SPIR-V capability ${capability} in ${arrayName}.`
    : `Did not find SPIR-V capability ${capability} in ${arrayName}.`);
  return found;
}

/** Verifies public pipeline selection independently of backend-specific legacy fixtures. */
function createPipelineSelectionCases(enabled) {
  return [
    { id: 'default', args: [], pipeline: 'UGLIR' },
    { id: 'legacy', args: ['--shader-pipeline=legacy'], pipeline: 'LegacyAST', error: enabled ? '' : 'Legacy shader pipeline is not built' },
    { id: 'uglir', args: ['--shader-pipeline=uglir'], pipeline: 'UGLIR' },
    { id: 'removed-alias', args: ['--experimental-uglir-shader-pipeline'], error: 'does not exist' },
    { id: 'removed-optimization', args: ['--experimental-uglir-no-spirv-optimization'], error: 'does not exist' },
    { id: 'duplicate', args: ['--shader-pipeline=uglir', '--shader-pipeline=legacy'], error: 'Specify --shader-pipeline only once' },
    { id: 'invalid', args: ['--shader-pipeline=invalid'], error: 'must be uglir or legacy' }
  ].map(spec => createFixtureCase({
    id: `shader-pipeline-${spec.id}`, title: `Public shader pipeline: ${spec.id}`,
    group: 'regressions', labels: ['pipeline', 'regression'],
    fixtureDir: 'compute-basic', sourceFile: 'ComputeBasic.hpp', uglcArgs: spec.args,
    expectedExitCode: spec.error ? 1 : 0, description: 'Locks the default and explicit pipeline selection without fallback.',
    verify: async (t) => {
      if (spec.error) {
        await t.expectStepOutputContains('uglc', spec.error, 'Invalid pipeline selection must fail explicitly.');
        await t.expectArtifactNotExists('generate_result.hpp', 'Selection failure must not publish an artifact.');
      } else {
        await t.expectStepOutputContains('uglc', `UGLC shader pipeline: ${spec.pipeline}`, 'The requested pipeline must actually be selected.');
        await t.expectArtifactExists('generate_result.hpp', 'Compilation must publish a host artifact.');
        if (spec.pipeline === 'UGLIR') {
          const provenance = JSON.parse(await t.readArtifact('shader-compilation.json'));
          t.recordCheck('Default UGLIR is formal and never falls back.', provenance.pipeline === 'UGLIR' && !("experimental" in provenance) && provenance.fallbackUsed === false);
          await t.expectArtifactExists('spv/ComputeBasicPass.spvasm', 'UGLIR must emit direct SPIR-V.');
        } else {
          await t.expectArtifactNotExists('shader-compilation.json', 'Legacy must not execute the direct provider.');
        }
      }
    }
  }));
}

export function getTestRegistry(sourceDir, profile) {
  return [
    ...createPipelineSelectionCases(profile.legacyEnabled),
    ...[
      { id: 'float3-storage', sourceFile: 'Float3Storage.hpp', diagnostic: 'MSL/Metal StructuredBuffer buffer layout mismatch' },
      { id: 'struct-tail-storage', sourceFile: 'StructTailStorage.hpp', diagnostic: 'implicit tail padding' },
      { id: 'scalar-vector-storage', sourceFile: 'ScalarVectorStorage.hpp', diagnostic: 'DSL/C++ offset = 4 bytes' },
      { id: 'three-component-matrix-storage', sourceFile: 'ThreeComponentMatrixStorage.hpp', diagnostic: 'DSL/C++ size = 24 bytes' },
      { id: 'narrow-uniform-matrix', sourceFile: 'NarrowUniformMatrix.hpp', diagnostic: 'DSL/C++ size = 32 bytes',
        experimentalDiagnostic: 'matrix with stride 8 not satisfying alignment to 16' }
    ].flatMap(layout => [
      createBufferLayoutDiagnosticCase({ ...layout, id: `invalid-layout-${layout.id}` }),
      ...[createBufferLayoutDiagnosticCase({ ...layout,
        id: `experimental-uglir-invalid-layout-${layout.id}`, experimental: true,
        diagnostic: layout.experimentalDiagnostic ?? layout.diagnostic })]
    ]),
    createFixtureCase({
      id: 'shader-pipeline-legacy-artifact',
      title: 'Pipeline selection: explicit Legacy retains its HLSL artifact',
      group: 'regressions', labels: ['pipeline', 'legacy'],
      fixtureDir: 'compute-basic', sourceFile: 'ComputeBasic.hpp',
      uglcArgs: ['--shader-pipeline=legacy'],
      description: 'Compiles through the retained Legacy path even when the direct implementation is built.',
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'The Legacy invocation must compile successfully.');
        await t.expectArtifactNotExists('uglir', 'The Legacy invocation must not lower or dump UGLIR.');
        const sources = await readGeneratedShaderSources(t, 'generate_result.hpp');
        t.recordCheck('The Legacy artifact must retain its HLSL payload.', sources.hlslSource.length > 0, sources.hlslSource);
      }
    }),
    createFixtureCase({
      id: 'render-basic-success',
      title: 'Render smoke: texture bind group and fragment target generation',
      group: 'smoke',
      labels: ['smoke', 'render', 'bindgroup', 'visibility'],
      fixtureDir: 'render-basic',
      sourceFile: 'RenderBasic.hpp',
      description: 'Uses a minimal texture+sampler render class to verify the main render codegen path still produces vertex/fragment visibility, explicit bind-group resource bindings, vertex layout, and render target setup.',
      validates: [
        'UGLC can generate `generate_result.hpp` and `exports.hpp` for a valid render DSL fixture.',
        'Bind-group visibility for a render-only bind group includes both vertex and fragment stages.',
        'Vertex attribute layout, explicit bind-group resource bindings, and fragment target array generation remain intact.'
      ],
      watchouts: [
        'This case is the quickest signal if core render pipeline generation regresses after visitor changes.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Generated render header should exist.');
        await t.expectArtifactExists('exports.hpp', 'Generated exports header should exist.');
        await t.expectArtifactExists('dsl_single_header.hpp', 'Merged DSL single-header artifact should exist by default.');
        await t.expectFileContains('dsl_single_header.hpp', 'class RenderBasicPass final : public IRenderClass', 'Merged DSL single-header should contain the original DSL class declaration.');
        await t.expectFileNotContains('dsl_single_header.hpp', 'struct ShaderArtifact', 'Merged DSL single-header should not contain generated shader artifact support.');
        await t.expectFileContains('generate_result.hpp', 'struct ShaderArtifact', 'Generated headers should define the shared shader artifact bundle support type.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact vertexShaderArtifact', 'Render fixtures should emit a structured vertex shader artifact bundle.');
        await t.expectFileContains('generate_result.hpp', 'eastl::string hlslSource;', 'Generated shader artifact support should retain paired HLSL source for render stages too.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t vertexShaderArtifact_SpirvWords[] = {', 'Render vertex stages should embed compiled SPIR-V into the generated header.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t fragmentShaderArtifact_SpirvWords[] = {', 'Render fragment stages should embed compiled SPIR-V into the generated header.');
        await t.expectFileContains('generate_result.hpp', 'float4 pos : TEXCOORD0;', 'Render vertex HLSL should preserve vertex-input location mapping through numbered semantics.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(1, 0)]] Texture2D<float4> bindGroup_texture0 : register(t1, space0);', 'Render HLSL should emit explicit Vulkan binding coordinates for sampled textures.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> bindGroup_texture0 : register(t1, space0);', 'Render HLSL should preserve sampled texture register bindings.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] SamplerState bindGroup_sampler0 : register(s0, space0);', 'Render HLSL should emit explicit Vulkan binding coordinates for samplers.');
        await t.expectFileContains('generate_result.hpp', 'SamplerState bindGroup_sampler0 : register(s0, space0);', 'Render HLSL should preserve sampler register bindings.');
        await t.expectFileContains('generate_result.hpp', 'inline GVM::RHI::ShaderModuleDescriptor MakeShaderModuleDescriptor(', 'Generated shader artifact support should expose a shared shader-module descriptor helper.');
        await t.expectFileContains('generate_result.hpp', 'descriptor.code = artifact.mslSource;', 'Generated shader module descriptors should always preserve the MSL payload.');
        await t.expectFileContains('generate_result.hpp', 'descriptor.spirv.assign(artifact.spv, artifact.spv + artifact.spvWordCount);', 'Generated shader module descriptors should forward embedded SPIR-V payloads when present.');
        await t.expectFileContains('generate_result.hpp', 'createShaderModule(UGLC::Generated::MakeShaderModuleDescriptor("RenderBasicPassVertexShader", vertexShaderArtifact))', 'Render create() code should use the shared shader-module descriptor helper for the vertex stage.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].binding = 1', 'Bind-group layout entry 0 should follow the explicit [[Binding1]] annotation.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[1].binding = 0', 'Bind-group layout entry 1 should follow the explicit [[Binding0]] annotation.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment', 'Render bind-group visibility should include both vertex and fragment.');
        await t.expectFileContains('generate_result.hpp', 'texture0 [[id(1)]]', 'Generated MSL bind-group resources should use the explicit [[Binding1]] id.');
        await t.expectFileContains('generate_result.hpp', 'sampler0 [[id(0)]]', 'Generated MSL bind-group resources should use the explicit [[Binding0]] id.');
        await t.expectFileContains('generate_result.hpp', 'fragmentState.targets.resize(1)', 'Render fixture should allocate exactly one color target.');
        await t.expectFileContains('generate_result.hpp', 'vertexState.buffers[0].attributes[0].shaderLocation = 0', 'First vertex attribute should stay bound to shader location 0.');
        await t.expectFileContains('generate_result.hpp', 'vertexState.buffers[0].attributes[1].shaderLocation = 1', 'Second vertex attribute should stay bound to shader location 1.');
      }
    }),
    createFixtureCase({
      id: 'shader-explicit-precision-conversion',
      title: 'Regression: shader precision conversions must be explicit',
      group: 'regressions',
      labels: ['regression', 'shader', 'half', 'conversion', 'typing'],
      fixtureDir: 'shader-explicit-precision-conversion',
      sourceFile: 'ShaderExplicitPrecisionConversion.hpp',
      description: 'Confirms explicit half and cross-precision vector constructors remain available while ordinary same-type vector expressions and swizzle reads still compile.',
      validates: [
        'Shader code can explicitly construct half and half vectors from float values.',
        'Same-type vector expressions and swizzle reads remain usable after implicit cross-precision conversions are disabled.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Explicit precision conversion fixture should generate successfully.');
        await t.expectArtifactExists('exports.hpp', 'Generated exports header should exist.');
      }
    }),
    createFixtureCase({
      id: 'dsl-single-header-disabled',
      title: 'Artifact option: merged DSL single-header can be disabled',
      group: 'artifacts',
      labels: ['artifact', 'dsl', 'single-header', 'cli'],
      fixtureDir: 'render-basic',
      sourceFile: 'RenderBasic.hpp',
      uglcArgs: ['--emit-dsl-single-header=off'],
      description: 'Confirms the default merged DSL artifact can be disabled without affecting the generated runtime headers.',
      validates: [
        '`--emit-dsl-single-header=off` suppresses only `dsl_single_header.hpp`.',
        'The normal generated runtime header and exports header are still emitted.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Generated render header should still exist when merged DSL output is disabled.');
        await t.expectArtifactExists('exports.hpp', 'Generated exports header should still exist when merged DSL output is disabled.');
        await t.expectArtifactNotExists('dsl_single_header.hpp', 'Merged DSL single-header artifact should not be written when disabled.');
      }
    }),
    createFixtureCase({
      id: 'invalid-dsl-single-header-option',
      title: 'Diagnostic: merged DSL single-header option validates on/off',
      group: 'diagnostics',
      labels: ['diagnostic', 'artifact', 'dsl', 'single-header', 'cli'],
      fixtureDir: 'render-basic',
      sourceFile: 'RenderBasic.hpp',
      uglcArgs: ['--emit-dsl-single-header=maybe'],
      description: 'Confirms UGLC rejects invalid values for the merged DSL single-header command-line switch.',
      expectedExitCode: 1,
      validates: [
        '`--emit-dsl-single-header` accepts only on/off values.',
        'Invalid values fail explicitly before artifacts are emitted with ambiguous configuration.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', '--emit-dsl-single-header', 'Diagnostic output should name the invalid option.');
        await t.expectStepOutputContains('uglc', 'on/off', 'Diagnostic output should state the accepted on/off values.');
      }
    }),
    createFixtureCase({
      id: 'invalid-implicit-half-conversion',
      title: 'Diagnostic: half scalar construction must be explicit',
      group: 'diagnostics',
      labels: ['diagnostic', 'shader', 'half', 'conversion', 'typing'],
      fixtureDir: 'invalid-implicit-half-conversion',
      sourceFile: 'InvalidImplicitHalfConversion.hpp',
      description: 'Confirms shader code cannot copy-initialize half from a float literal.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'no viable conversion', 'Clang should reject implicit float-to-half copy initialization.');
        await t.expectStepOutputContains('uglc', 'half', 'Diagnostic output should name the half destination type.');
      }
    }),
    createFixtureCase({
      id: 'invalid-implicit-vector-precision-conversion',
      title: 'Diagnostic: vector precision conversion must be explicit',
      group: 'diagnostics',
      labels: ['diagnostic', 'shader', 'half', 'vector', 'conversion', 'typing'],
      fixtureDir: 'invalid-implicit-vector-precision-conversion',
      sourceFile: 'InvalidImplicitVectorPrecisionConversion.hpp',
      description: 'Confirms shader code cannot copy-initialize a half vector from a float vector.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'no viable conversion', 'Clang should reject implicit float-vector to half-vector copy initialization.');
        await t.expectStepOutputContains('uglc', 'half4', 'Diagnostic output should name the half4 destination type.');
      }
    }),
    createFixtureCase({
      id: 'invalid-implicit-framebuffer-payload-conversion',
      title: 'Diagnostic: framebuffer payload precision conversion must be explicit',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'framebuffer', 'half', 'conversion', 'typing'],
      fixtureDir: 'invalid-implicit-framebuffer-payload-conversion',
      sourceFile: 'InvalidImplicitFramebufferPayloadConversion.hpp',
      description: 'Confirms ColorAttachment payload assignment follows the same explicit precision-conversion rule as ordinary shader values.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', "no viable overloaded '='", 'Clang should reject implicit float4 assignment to a half4 framebuffer payload.');
        await t.expectStepOutputContains('uglc', 'ColorAttachment', 'Diagnostic output should identify the framebuffer attachment assignment.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-class-helper-method',
      title: 'Diagnostic: RenderClass cannot declare helper methods',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'shader-class', 'validation'],
      fixtureDir: 'invalid-render-class-helper-method',
      sourceFile: 'InvalidRenderClassHelperMethod.hpp',
      description: 'Confirms IRenderClass only accepts constructor, vertex, and fragment methods.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'RenderClass "InvalidRenderClassHelperPass" cannot declare helper method "helper"', 'Diagnostic output should name the invalid render helper method.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into fragment().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-class-create-overload',
      title: 'Diagnostic: RenderClass cannot declare create overload helpers',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'shader-class', 'validation', 'create'],
      fixtureDir: 'invalid-render-class-create-overload',
      sourceFile: 'InvalidRenderClassCreateOverload.hpp',
      description: 'Confirms IRenderClass accepts only the annotated constructor create method and rejects ordinary create overloads.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'RenderClass "InvalidRenderClassCreateOverloadPass" cannot declare helper method "create"', 'Diagnostic output should name the invalid create overload.');
        await t.expectStepOutputContains('uglc', 'Only the selected constructor and shader entry methods are allowed.', 'Diagnostic output should explain that ordinary shader-class methods are not part of the DSL surface.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-class-helper-method',
      title: 'Diagnostic: PixelLocalRenderClass cannot declare helper methods',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'shader-class', 'validation'],
      fixtureDir: 'invalid-pixel-local-class-helper-method',
      sourceFile: 'InvalidPixelLocalClassHelperMethod.hpp',
      description: 'Confirms IPixelLocalRenderClass only accepts constructor and pixel methods.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalRenderClass "InvalidPixelLocalClassHelperPass" cannot declare helper method "resolveMaterial"', 'Diagnostic output should name the invalid pixel-local helper method.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into pixel().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'invalid-compute-class-helper-method',
      title: 'Diagnostic: ComputeClass cannot declare helper methods',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'shader-class', 'validation'],
      fixtureDir: 'invalid-compute-class-helper-method',
      sourceFile: 'InvalidComputeClassHelperMethod.hpp',
      description: 'Confirms IComputeClass only accepts constructor and compute methods.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ComputeClass "InvalidComputeClassHelperPass" cannot declare helper method "helper"', 'Diagnostic output should name the invalid compute helper method.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into compute().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'invalid-dsl-reserved-vertex-local',
      title: 'Diagnostic: shader local variables cannot use reserved DSL name vertex',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'dsl', 'reserved-identifier'],
      fixtureDir: 'invalid-dsl-reserved-vertex-local',
      sourceFile: 'InvalidDSLReservedVertexLocal.hpp',
      description: 'Confirms shader body locals cannot use `vertex`, which the DSL reserves for shader stage naming.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'Diagnostic output should explain that vertex is a DSL-reserved variable identifier.');
        await t.expectStepOutputContains('uglc', 'Rename local variable "vertex"', 'Diagnostic output should identify the local variable role.');
      }
    }),
    createFixtureCase({
      id: 'invalid-dsl-reserved-vertex-parameter',
      title: 'Diagnostic: shader helper parameters cannot use reserved DSL name vertex',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'dsl', 'reserved-identifier', 'helper'],
      fixtureDir: 'invalid-dsl-reserved-vertex-parameter',
      sourceFile: 'InvalidDSLReservedVertexParameter.hpp',
      description: 'Confirms referenced free shader helpers cannot use `vertex` as a parameter name.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'Diagnostic output should explain that vertex is a DSL-reserved variable identifier.');
        await t.expectStepOutputContains('uglc', 'Rename parameter "vertex"', 'Diagnostic output should identify the parameter role.');
      }
    }),
    createFixtureCase({
      id: 'invalid-dsl-reserved-vertex-field',
      title: 'Diagnostic: shader-visible fields cannot use reserved DSL name vertex',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'dsl', 'reserved-identifier', 'record'],
      fixtureDir: 'invalid-dsl-reserved-vertex-field',
      sourceFile: 'InvalidDSLReservedVertexField.hpp',
      description: 'Confirms shader-visible records cannot expose `vertex` as a field name.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'Diagnostic output should explain that vertex is a DSL-reserved variable identifier.');
        await t.expectStepOutputContains('uglc', 'Rename field "vertex"', 'Diagnostic output should identify the field role.');
      }
    }),
    createFixtureCase({
      id: 'invalid-dsl-reserved-vertex-binding',
      title: 'Diagnostic: shader binding variables cannot use reserved DSL name vertex',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'dsl', 'reserved-identifier', 'binding'],
      fixtureDir: 'invalid-dsl-reserved-vertex-binding',
      sourceFile: 'InvalidDSLReservedVertexBinding.hpp',
      description: 'Confirms shader-class BindGroup/RenderSet binding variables cannot use `vertex` as their shader-visible name.',
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'Diagnostic output should explain that vertex is a DSL-reserved variable identifier.');
        await t.expectStepOutputContains('uglc', 'Rename shader binding variable "vertex"', 'Diagnostic output should identify the shader binding variable role.');
      }
    }),
    createFixtureCase({
      id: 'pixel-local-deferred-screen',
      title: 'Render feature: pixel-local deferred lighting renderPass phase',
      group: 'render-feature',
      labels: ['render-feature', 'render', 'pixel-local', 'deferred', 'node'],
      fixtureDir: 'pixel-local-deferred-screen',
      sourceFile: 'PixelLocalDeferredScreen.hpp',
      description: 'Covers the node-test render-feature category for the pixel-local deferred path: GBuffer, Lighting, Tonemap, pixelLocalPass, nextPixelLocalPass, and queue->renderPass phase lowering.',
      validates: [
        'UGLC accepts the strict pixel-local DSL names and renderPass phase syntax.',
        'Raster GBuffer lowers to GVM::Core::IRenderClass, while pixel-only Lighting and Tonemap lower to GVM::Core::IPixelLocalRenderClass.',
        'Lighting and tonemap pixel passes read only the previous in-tile attachments they actually use through [[PixelLocalInput]] and Vulkan SubpassInput lowering.',
        'The generated renderer uses pixelLocalPass and nextPixelLocalPass inside queue->renderPass from the node render-feature category.'
      ],
      watchouts: [
        'The matching visual validation belongs to the GVM render-feature window suite, so this node case stays focused on generated source and host compilation.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated pixel-local render-feature header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Pixel-local deferred screen fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'queue->renderPass("PixelLocalDeferredScreen", deferredFrame, GVM::Core::pixelLocalPass(', 'Renderer should submit the pixel-local phase through queue->renderPass.');
        await t.expectFileContains('generate_result.hpp', 'class PixelLocalGBufferPass : public GVM::Core::IRenderClass', 'GBuffer should lower as a normal raster render class that can participate in pixelLocalPass.');
        await t.expectFileOccurrenceCount('generate_result.hpp', 'public GVM::Core::IPixelLocalRenderClass', 2, 'Lighting and tonemap should lower to GVM::Core::IPixelLocalRenderClass.');
        await t.expectFileNotContains('generate_result.hpp', 'this->pixelLocal = true', 'Generated pixel-local passes should rely on explicit pipelineDescriptor.pixelLocalAttachmentAccess metadata instead of a dead host-side flag.');
        await t.expectFileContains('generate_result.hpp', 'gbufferPass->run(3u, 1u, 0u, 0u)', 'Pixel-local phase should preserve ordinary raster draw tasks.');
        await t.expectFileOccurrenceCount('generate_result.hpp', 'GVM::Core::nextPixelLocalPass()', 2, 'Pixel-local phase should mark two subpass boundaries.');
        await t.expectFileContains('generate_result.hpp', 'lightingPass->run()', 'Lighting should draw the full attachment extent in the pixel-local phase.');
        await t.expectFileContains('generate_result.hpp', 'tonemapPass->run()', 'Tonemap should draw the full attachment extent in the pixel-local phase.');
        await t.expectFileNotContains('generate_result.hpp', ['lightingPass->run', '(width, height)'].join(''), 'Lighting should no longer carry an explicit pixel extent.');
        await t.expectFileNotContains('generate_result.hpp', ['tonemapPass->run', '(width, height)'].join(''), 'Tonemap should no longer carry an explicit pixel extent.');
        await t.expectFileNotContains('generate_result.hpp', 'GVM::Core::pixelLocalTasks', 'Generated output should not use the old pixelLocalTasks API.');
        await t.expectFileNotContains('generate_result.hpp', 'class PixelLocalGBufferPass : public GVM::Core::IPixelLocalRenderClass', 'GBuffer should no longer lower as a pixel-only render class.');
        await t.expectFileNotContains('generate_result.hpp', 'tasks.drawPixels', 'Generated output should not use removed pixel-local builder methods.');
        await t.expectFileNotContains('generate_result.hpp', 'PixelLocalPassTaskDescriptor<', 'Generated output should not use typed pixel-local task descriptors.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact fragmentShaderArtifact', 'Pixel-local passes should still emit fragment-stage shader artifacts.');
        await t.expectFileContains('generate_result.hpp', 'fragmentShaderArtifact_SpirvWords', 'Pixel-local fragment shaders should embed SPIR-V for the Vulkan path.');
        await t.expectFileContains('generate_result.hpp', 'SubpassInput<float4> _UGLC_PixelLocalInput_inputValue_albedo', 'Lighting should materialize albedo as a Vulkan subpass input.');
        await t.expectFileContains('generate_result.hpp', 'SubpassInput<float> _UGLC_PixelLocalInput_inputValue_gbufferDepth', 'Lighting should materialize explicit GBuffer depth as a pixel-local color input, not as native depth input.');
        await t.expectFileContains('generate_result.hpp', '[[vk::input_attachment_index(0)]] SubpassInput<float4> _UGLC_PixelLocalInput_inputValue_lighting', 'Tonemap should use a compact input attachment slot for lighting instead of preserving framebuffer holes.');
        await t.expectFileNotContains('generate_result.hpp', '[[vk::input_attachment_index(1)]] SubpassInput<float4> _UGLC_PixelLocalInput_inputValue_lighting', 'Tonemap should not keep a sparse Vulkan input attachment index for lighting.');
        await t.expectFileContains('generate_result.hpp', 'inputValue.albedo = _UGLC_PixelLocalInput_inputValue_albedo.SubpassLoad();', 'Pixel-local read() should lower to current-pixel SubpassLoad for GBuffer albedo.');
        await t.expectFileContains('generate_result.hpp', 'inputValue.gbufferDepth = _UGLC_PixelLocalInput_inputValue_gbufferDepth.SubpassLoad();', 'Pixel-local read() should lower explicit GBuffer depth through color input attachments.');
        await t.expectFileContains('generate_result.hpp', 'fragment PixelLocalDeferredFrame fragmentMain', 'Default MSL pixel-local path should emit a framebuffer-fetch fragment entry.');
        await t.expectFileContains('generate_result.hpp', '[[color(0)]]', 'Default MSL pixel-local path should read previous pixel-local attachments through framebuffer fetch color inputs.');
        await t.expectFileNotContains('generate_result.hpp', ['PixelLocalDeferredFrame_PixelLocal', 'Imageblock'].join(''), 'MSL pixel-local code should not emit the removed framebuffer struct.');
        await t.expectFileNotContains('generate_result.hpp', ['image', 'block<PixelLocalDeferredFrame_PixelLocal', 'Imageblock'].join(''), 'MSL pixel-local code should not emit removed local-storage parameters.');
        await t.expectFileNotContains('generate_result.hpp', 'kernel void fragmentMain', 'MSL pixel-local code should emit a fragment entry, not a kernel entry.');
        await t.expectFileNotContains('generate_result.hpp', 'inputValue.depth = _UGLC_currentPixel.depth;', 'MSL must not materialize native depth as a pixel-local input value.');
        await t.expectFileNotContains('generate_result.hpp', '[[depth(any)]]', 'MSL pixel-local framebuffer-fetch inputs must not contain native depth fields.');
        await t.expectFileNotContains('generate_result.hpp', 'pixelLocalAttachmentAccess.depthRead', 'Pipeline metadata should not expose unsupported native depth reads.');
        await t.expectFileContains('generate_result.hpp', 'inputValue.lighting = _UGLC_PixelLocalInput_inputValue_lighting.SubpassLoad();', 'Tonemap should read the lighting attachment through SubpassLoad.');
        await t.expectFileOccurrenceCount('generate_result.hpp', ' : SV_Target0;', 1, 'Vulkan pixel-local GBuffer should emit only the albedo color output it writes.');
        await t.expectFileOccurrenceCount('generate_result.hpp', ' : SV_Target1;', 1, 'Vulkan pixel-local GBuffer should emit the explicit GBuffer depth color output it writes.');
        await t.expectFileOccurrenceCount('generate_result.hpp', ' : SV_Target2;', 1, 'Vulkan pixel-local lighting should emit only the lighting color output it writes.');
        await t.expectFileOccurrenceCount('generate_result.hpp', ' : SV_Target3;', 1, 'Vulkan pixel-local tonemap should emit only the present color output it writes.');
      }
    }),
    createFixtureCase({
      id: 'pixel-local-clip',
      title: 'Render feature: pixel-local clip builtin lowers in pixel passes',
      group: 'render-feature',
      labels: ['render-feature', 'render', 'pixel-local', 'clip', 'node'],
      fixtureDir: 'pixel-local-clip',
      sourceFile: 'PixelLocalClip.hpp',
      description: 'Covers `clip()` inside IPixelLocalRenderClass::pixel() while preserving pixelLocalPass and full-attachment run() lowering.',
      validates: [
        'UGLC accepts clip inside pixel-local pixel shader entries.',
        'The generated renderer still uses pixelLocalPass and nextPixelLocalPass.',
        'The pixel-local shader source lowers clip through backend-native HLSL and MSL paths.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated pixel-local clip header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Pixel-local clip fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'queue->renderPass("PixelLocalClip", frame, GVM::Core::pixelLocalPass(', 'Renderer should submit the clip pass through pixelLocalPass.');
        await t.expectFileContains('generate_result.hpp', 'gbufferPass->run(3u, 1u, 0u, 0u)', 'Pixel-local clip phase should preserve the raster producer draw task.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::nextPixelLocalPass()', 'Pixel-local clip phase should keep the explicit phase boundary.');
        await t.expectFileContains('generate_result.hpp', 'lightingPass->run()', 'Pixel-local clip pass should draw the full attachment extent.');
        await t.expectFileContains('generate_result.hpp', 'clip(', 'HLSL pixel-local source should lower clip to the native HLSL intrinsic.');
        await t.expectFileContains('generate_result.hpp', 'UGLC_clip', 'MSL pixel-local source should lower clip to the prelude helper.');
        await t.expectFileNotContains('generate_result.hpp', ['lightingPass->run', '(width, height)'].join(''), 'Pixel-local clip pass should not gain an extent-based run API.');
      }
    }),
    createFixtureCase({
      id: 'render-vertex-only',
      title: 'Regression: vertex-only render classes do not synthesize fragment pipeline state',
      group: 'regressions',
      labels: ['regression', 'render', 'vertex-only', 'pipeline'],
      fixtureDir: 'render-vertex-only',
      sourceFile: 'RenderVertexOnly.hpp',
      description: 'Covers the vertex-only render path so optional fragment support does not regress back into unconditional fragment shader creation.',
      validates: [
        'Render classes without a fragment entry still generate successfully.',
        'Generated create() code does not reference `fragmentShaderArtifact`, fragment shader module creation, or fragment state setup.',
        'The generated header stays syntactically valid for host compilation.'
      ],
      watchouts: [
        'Without this regression, vertex-only or future depth-only passes can silently pick up undefined fragment references during code generation.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated vertex-only render header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Vertex-only render fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact vertexShaderArtifact', 'Vertex-only render fixture should still emit a vertex shader artifact bundle.');
        await t.expectFileContains('generate_result.hpp', 'uint32_t spvWordCount = 0', 'Generated shader artifact support should reserve a stable SPIR-V slot.');
        await t.expectFileNotContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact fragmentShaderArtifact', 'Vertex-only render fixture should not synthesize a fragment shader artifact bundle.');
        await t.expectFileNotContains('generate_result.hpp', 'createShaderModule({.label = "RenderVertexOnlyPassFragmentShader"', 'Vertex-only render fixture should not create a fragment shader module.');
        await t.expectFileNotContains('generate_result.hpp', 'this->pipelineDescriptor.fragment = fragmentState', 'Vertex-only render fixture should not synthesize fragment state assignment.');
      }
    }),
    createFixtureCase({
      id: 'render-discard-fragment',
      title: 'Regression: fragment discard builtin lowers to backend-native shader code',
      group: 'regressions',
      labels: ['regression', 'render', 'fragment', 'discard', 'hlsl', 'msl'],
      fixtureDir: 'render-discard-fragment',
      sourceFile: 'RenderDiscardFragment.hpp',
      description: 'Covers `UGL::discard_fragment()` and unqualified `discard_fragment()` inside a fragment shader so the DSL builtin lowers to backend-native fragment discard instructions instead of leaking as a fake function call.',
      validates: [
        'Fragment shaders using the discard builtin generate successfully for both MSL and HLSL/SPIR-V.',
        'HLSL lowers the DSL call to the `discard` statement.',
        'MSL lowers the DSL call to `discard_fragment()` without retaining the `UGL::` qualifier.'
      ],
      watchouts: [
        'If this regresses, DXC can fail on an unresolved `discard_fragment()` symbol or Metal can see a qualified `UGL::discard_fragment()` call.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Discard fragment fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t fragmentShaderArtifact_SpirvWords[] = {', 'Fragment discard fixture should still embed compiled SPIR-V.');
        await t.expectFileContains('generate_result.hpp', 'discard;', 'HLSL fragment source should lower discard_fragment() to the native discard statement.');
        await t.expectFileContains('generate_result.hpp', 'discard_fragment();', 'MSL fragment source should lower discard_fragment() to the native Metal discard call.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::discard_fragment', 'Generated shader sources must not leak the DSL-qualified discard builtin.');
      }
    }),
    createFixtureCase({
      id: 'render-clip-fragment',
      title: 'Regression: fragment clip builtin lowers to backend-native shader code',
      group: 'regressions',
      labels: ['regression', 'render', 'fragment', 'clip', 'hlsl', 'msl'],
      fixtureDir: 'render-clip-fragment',
      sourceFile: 'RenderClipFragment.hpp',
      description: 'Covers qualified scalar `UGL::clip()` and unqualified vector `clip()` inside a normal fragment shader.',
      validates: [
        'Fragment shaders using clip generate successfully for both MSL and HLSL/SPIR-V.',
        'HLSL lowers the DSL call to the native `clip` intrinsic.',
        'MSL lowers the DSL call to the prelude `UGLC_clip` helper.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Clip fragment fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t fragmentShaderArtifact_SpirvWords[] = {', 'Fragment clip fixture should still embed compiled SPIR-V.');
        await t.expectFileContains('generate_result.hpp', 'clip(', 'HLSL fragment source should lower clip() to the native clip intrinsic.');
        await t.expectFileContains('generate_result.hpp', 'UGLC_clip', 'MSL fragment source should lower clip() to the prelude helper.');
        await t.expectFileContains('generate_result.hpp', 'discard_fragment();', 'MSL clip helper should discard fragments when the clip test fails.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::clip', 'Generated shader sources must not leak the DSL-qualified clip builtin.');
      }
    }),
    createFixtureCase({
      id: 'do-stmt-macro-regression',
      title: 'Regression: do-while macro statements lower through shared AST statement translation',
      group: 'regressions',
      labels: ['regression', 'statements', 'macros', 'host'],
      fixtureDir: 'do-stmt-macro-regression',
      sourceFile: 'DoStmtMacroRegression.hpp',
      description: 'Covers host-side helper code that expands a common `do { ... } while (0)` macro inside a render class constructor path so BaseASTVisitor keeps lowering `clang::DoStmt` instead of failing with an unsupported-statement diagnostic.',
      validates: [
        'UGLC generates successfully when a host helper method contains a `do { ... } while (0)` macro expansion.',
        'The generated host header preserves a concrete `do` / `while (0)` statement sequence instead of bailing out during AST lowering.'
      ],
      watchouts: [
        'This is the exact statement shape produced by logging and assertion macros in larger downstream render fixtures.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'DoStmt regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'do', 'Generated host code should contain the lowered do-statement keyword.');
        await t.expectFileContains('generate_result.hpp', 'while (0)', 'Generated host code should preserve the macro-style do-while condition.');
        await t.expectFileNotContains('generate_result.hpp', 'does not support statement "DoStmt"', 'DoStmt lowering should no longer fall back to the unsupported-statement placeholder.');
      }
    }),
    createFixtureCase({
      id: 'template-helper-operator-regression',
      title: 'Regression: shader helper templates can keep dependent operator lookups unresolved until instantiation',
      group: 'regressions',
      labels: ['regression', 'templates', 'shader', 'operators', 'ast'],
      fixtureDir: 'template-helper-operator-regression',
      sourceFile: 'TemplateHelperOperatorRegression.hpp',
      description: 'Covers a namespace-scoped helper template that performs vector arithmetic on a dependent `T`, matching the AST shape used by downstream tessellation helpers such as `Tess::berp`.',
      validates: [
        'UGLC still generates successfully when shader code references a helper function template whose body contains dependent operator lookup expressions.',
        'The generated output keeps the helper template definition instead of failing on `UnresolvedLookupExpr` during reference collection or shader emission.'
      ],
      watchouts: [
        'Without this regression, helper templates that work in downstream integrations can start failing after unrelated visitor or diagnostics refactors.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Template helper regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'template<class T>', 'Generated output should preserve the helper template declaration.');
        await t.expectFileContains('generate_result.hpp', 'TemplateHelperOperatorRegressionHelpers::berp', 'Generated output should still reference the namespace-scoped helper template.');
        await t.expectFileContains('generate_result.hpp', 'u.x * (b - a)', 'Generated helper template body should preserve the first dependent arithmetic grouping.');
        await t.expectFileContains('generate_result.hpp', 'u.y * (c - a)', 'Generated helper template body should preserve the second dependent arithmetic grouping.');
      }
    }),
    createFixtureCase({
      id: 'template-function-class-callback',
      title: 'Regression: shader function templates can instantiate class callback policies',
      group: 'regressions',
      labels: ['regression', 'templates', 'shader', 'callback', 'compute', 'hlsl', 'msl'],
      fixtureDir: 'template-function-class-callback',
      sourceFile: 'TemplateFunctionClassCallback.hpp',
      description: 'Covers a namespace-scoped function template that takes a callback class as an explicit template argument, constructs that callback inside the template body, and calls policy methods from shader code.',
      validates: [
        'UGLC keeps the function template definition available to HLSL and MSL backend compilers.',
        'Explicit class callback template arguments are preserved at shader call sites.',
        'Dependent callback construction and member calls remain in the emitted template body instead of falling into unsupported AST placeholders.'
      ],
      watchouts: [
        'This matches the downstream Nanite-style traversal pattern where a function template drives policy callbacks such as Init(), ShouldVisitChild(), and StoreChildNode().'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Template function class-callback fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'template<class CallbackType>', 'Generated output should preserve the callback function template declaration.');
        await t.expectFileContains('generate_result.hpp', 'runCallback', 'Generated output should include the callback template helper.');
        await t.expectFileContains('generate_result.hpp', 'runCallback<TemplateFunctionClassCallbackHelpers::EvenCallback>', 'Generated shader code should call the helper with the even callback policy.');
        await t.expectFileContains('generate_result.hpp', 'runCallback<TemplateFunctionClassCallbackHelpers::AlwaysCallback>', 'Generated shader code should call the helper with the always callback policy.');
        await t.expectFileContains('generate_result.hpp', 'CallbackType callback;', 'Generated template body should construct the dependent callback class.');
        await t.expectFileContains('generate_result.hpp', 'callback.shouldWrite', 'Generated template body should preserve the dependent predicate call.');
        await t.expectFileContains('generate_result.hpp', 'callback.transform', 'Generated template body should preserve the dependent transform call.');
        await t.expectFileNotContains('generate_result.hpp', 'does not support expression', 'Generated output should not contain unsupported-expression placeholders.');
        await t.expectFileNotContains('generate_result.hpp', 'does not support statement', 'Generated output should not contain unsupported-statement placeholders.');
      }
    }),
    createFixtureCase({
      id: 'temporary-string-label-regression',
      title: 'Regression: temporary host string chains inside parentheses still lower through c_str() calls',
      group: 'regressions',
      labels: ['regression', 'host', 'temporary', 'string', 'ast'],
      fixtureDir: 'temporary-string-label-regression',
      sourceFile: 'TemporaryStringLabelRegression.hpp',
      description: 'Covers host-side temporary string concatenation wrapped in parentheses before a trailing `.c_str()` member call, matching the AST shape used by downstream texture-view labels.',
      validates: [
        'UGLC still generates successfully when host code calls `.c_str()` on a parenthesized temporary string expression.',
        'The generated host output keeps the full concatenation chain instead of failing on `ParenExpr` / `CXXBindTemporaryExpr` wrappers.'
      ],
      watchouts: [
        'Without this regression, host utility code that formats labels through temporary EASTL strings can fail after expression-wrapper refactors.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Temporary string label regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'viewDescriptor.label = (', 'Generated host code should keep the parenthesized temporary string assignment.');
        await t.expectFileContains('generate_result.hpp', '"Hiz_"', 'Generated host code should preserve the string-prefix literal in the temporary chain.');
        await t.expectFileContains('generate_result.hpp', 'eastl::to_string(mipOffset)).c_str();', 'Generated host code should keep the temporary string concatenation feeding the trailing c_str() call.');
      }
    }),
    createFixtureCase({
      id: 'host-expression-precedence-regression',
      title: 'Regression: host C++ expression lowering preserves grouped infix operands',
      group: 'regressions',
      labels: ['regression', 'host', 'expressions', 'precedence', 'operators'],
      fixtureDir: 'host-expression-precedence-regression',
      sourceFile: 'HostExpressionPrecedenceRegression.hpp',
      description: 'Covers host-side constructor expressions such as `(heightSamples.size() - 1u) * 0.05f`, where dropping the grouping changes C++ operator precedence and silently changes runtime values.',
      validates: [
        'UGLC keeps user-authored grouping around lower-precedence infix operands when the parent expression has higher precedence.',
        'Host C++ overloaded binary operator lowering uses the same grouped operand policy as builtin binary operators.',
        'The generated host header does not rewrite `(a - b) * c` into `a - b * c`.'
      ],
      watchouts: [
        'This is a semantic regression guard: the generated C++ can still compile after losing parentheses, so ordinary build-only checks do not catch it.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated host precedence regression header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host expression precedence fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '(heightSamples.size() - 1u) * 0.05f', 'Generated host code should preserve grouping for the height-sample spacing expression.');
        await t.expectFileNotContains('generate_result.hpp', 'heightSamples.size() - 1u * 0.05f', 'Generated host code must not let multiplication bind to the subtraction RHS.');
        await t.expectFileContains('generate_result.hpp', '(base - offset) * 0.50f', 'Generated host code should preserve grouping for overloaded binary operators as well.');
        await t.expectFileNotContains('generate_result.hpp', 'base - offset * 0.50f', 'Overloaded operator lowering must not drop grouping before multiplication.');
      }
    }),
    createFixtureCase({
      id: 'render-blend-state-regression',
      title: 'Regression: render BlendState lowers from UGL pipeline types to GVM::RHI pipeline types',
      group: 'regressions',
      labels: ['regression', 'render', 'pipeline', 'blend-state', 'host-compile'],
      fixtureDir: 'render-blend-state-regression',
      sourceFile: 'RenderBlendStateRegression.hpp',
      description: 'Covers the render-class constructor path that configures blend state through the DSL-facing `UGL::BlendState` family before calling `IRenderClass::setBlendState(...)`.',
      validates: [
        'UGLC rewrites `UGL::BlendState`, `BlendComponent`, `BlendFactor`, and `BlendOperation` to their `GVM::RHI` equivalents in generated host code.',
        'Generated render headers remain host-compilable when a constructor calls `setBlendState(...)` with a local DSL blend-state variable.',
        'Existing `CullMode` translation stays consistent alongside the new blend-state lowering.'
      ],
      watchouts: [
        'Without this regression, generated render headers can still compile for passes that never touch blend state while silently breaking any downstream pass that configures alpha blending.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated blend-state render header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Blend-state regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::BlendState blendState = {', 'Generated host code should rewrite the local BlendState variable to GVM::RHI.');
        await t.expectFileContains('generate_result.hpp', 'blendState.color.operation = GVM::RHI::BlendOperation::Add;', 'Generated host code should rewrite BlendOperation enum references.');
        await t.expectFileContains('generate_result.hpp', 'blendState.color.srcFactor = GVM::RHI::BlendFactor::SrcAlpha;', 'Generated host code should rewrite BlendFactor enum references.');
        await t.expectFileContains('generate_result.hpp', 'blendState.alpha.dstFactor = GVM::RHI::BlendFactor::OneMinusSrcAlpha;', 'Generated host code should keep alpha blend factors on GVM::RHI types too.');
        await t.expectFileContains('generate_result.hpp', 'setBlendState(0u, blendState);', 'Generated host code should still call the runtime blend-state mutator.');
        await t.expectFileContains('generate_result.hpp', 'setCullMode(GVM::RHI::CullMode::Back);', 'CullMode translation should continue to target GVM::RHI in the same constructor.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::BlendState blendState', 'Generated host code should no longer leak DSL BlendState types into runtime-facing constructors.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::BlendOperation::', 'Generated host code should no longer leak DSL BlendOperation enums.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::BlendFactor::', 'Generated host code should no longer leak DSL BlendFactor enums.');
      }
    }),
    createFixtureCase({
      id: 'render-depth-compare-function-regression',
      title: 'Regression: render depth compare function is explicit pipeline state',
      group: 'regressions',
      labels: ['regression', 'render', 'depth', 'pipeline', 'compare-function', 'host-compile'],
      fixtureDir: 'render-depth-compare-function-regression',
      sourceFile: 'RenderDepthCompareFunctionRegression.hpp',
      description: 'Covers the breaking-change path where depth write patterns only control shader depth-output semantics and the pipeline compare function is set explicitly through IRenderClass::setDepthCompareFunction(...).',
      validates: [
        'UGLC rewrites DSL CompareFunction values to GVM::RHI when lowering setDepthCompareFunction(...).',
        'DepthStencilAttachmentWritePattern::Less still lowers to the shader depth-output semantic.',
        'Generated depth state does not infer pipeline compare from the depth write pattern.'
      ],
      watchouts: [
        'Without this regression, render passes can accidentally depend on the removed pattern-to-compare coupling.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated explicit depth-compare render header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Depth compare function fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'setDepthCompareFunction(GVM::RHI::CompareFunction::LessEqual);', 'Generated host code should lower the explicit compare mutator call to GVM::RHI.');
        await t.expectFileContains('generate_result.hpp', 'float depth : SV_DepthLessEqual;', 'Depth write pattern should still control the HLSL depth output semantic.');
        await t.expectFileContains('generate_result.hpp', 'float depth[[depth(less)]];', 'Depth write pattern should still control the MSL depth output attribute.');
        await t.expectFileNotContains('generate_result.hpp', '.depthCompare = GVM::RHI::CompareFunction::Less', 'Generated depth state must not infer compare from DepthStencilAttachmentWritePattern::Less.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::CompareFunction::', 'Generated host code should not leak DSL CompareFunction enums.');
      }
    }),
    createFixtureCase({
      id: 'render-depth-write-enabled-regression',
      title: 'Regression: render depth write enabled is explicit pipeline state',
      group: 'regressions',
      labels: ['regression', 'render', 'depth', 'pipeline', 'depth-write', 'host-compile'],
      fixtureDir: 'render-depth-write-enabled-regression',
      sourceFile: 'RenderDepthWriteEnabledRegression.hpp',
      description: 'Covers the render-class constructor path that disables pipeline depth writes through IRenderClass::setDepthWriteEnabled(...).',
      validates: [
        'Generated render constructors preserve the explicit setDepthWriteEnabled(false) call.',
        'The explicit depth-write mutator runs after default depth state initialization and before render pipeline creation.',
        'DepthStencilAttachmentWritePattern::Less still lowers to shader depth-output semantics.'
      ],
      watchouts: [
        'Without this regression, generated pipelines can keep the default depth write state even when a render pass explicitly disables depth writes.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated explicit depth-write render header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Depth write enabled fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'setDepthCompareFunction(GVM::RHI::CompareFunction::LessEqual);', 'Generated host code should still lower the explicit compare mutator call to GVM::RHI.');
        await t.expectFileContains('generate_result.hpp', 'setDepthWriteEnabled(false);', 'Generated host code should preserve the explicit depth-write mutator call.');
        await t.expectFileContainsInOrder(
          'generate_result.hpp',
          [
            'this->pipelineDescriptor.depthStencil = depthStencilState',
            'setDepthWriteEnabled(false);',
            'this->pipeline = this->mDevice->createRenderPipeline(this->pipelineDescriptor)'
          ],
          'Depth write mutator should override generated depth state before pipeline creation.'
        );
        await t.expectFileContains('generate_result.hpp', 'float depth : SV_DepthLessEqual;', 'Depth write pattern should still control the HLSL depth output semantic.');
        await t.expectFileContains('generate_result.hpp', 'float depth[[depth(less)]];', 'Depth write pattern should still control the MSL depth output attribute.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::CompareFunction::', 'Generated host code should not leak DSL CompareFunction enums.');
      }
    }),
    createFixtureCase({
      id: 'sampler-descriptor-compare-regression',
      title: 'Regression: SamplerDescriptor and CompareFunction lower to GVM::RHI for createSampler',
      group: 'regressions',
      labels: ['regression', 'host', 'sampler', 'descriptor', 'compare-function', 'host-compile'],
      fixtureDir: 'sampler-descriptor-compare-regression',
      sourceFile: 'SamplerDescriptorCompareRegression.hpp',
      description: 'Covers the host-side device path where callers build a DSL `SamplerDescriptor`, assign `CompareFunction`, and pass it into `Device::createSampler(...)`.',
      validates: [
        'UGLC rewrites `UGL::SamplerDescriptor` and `UGL::CompareFunction` to `GVM::RHI` in generated host code.',
        'Both local-descriptor variables and direct designated-init `createSampler({...})` calls stay host-compilable.',
        'Existing address/filter enum lowering keeps working in the same sampler construction flow.'
      ],
      watchouts: [
        'Without this regression, sampler creation can look fine for descriptors that never touch compare mode, while shadow/depth sampling paths fail only when host code finally sets `compare`.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated sampler-descriptor host header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Sampler descriptor regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::SamplerDescriptor descriptor = {', 'Generated host code should rewrite the local SamplerDescriptor variable to GVM::RHI.');
        await t.expectFileContains('generate_result.hpp', 'descriptor.compare = GVM::RHI::CompareFunction::LessEqual;', 'Generated host code should rewrite CompareFunction assignments on local sampler descriptors.');
        await t.expectFileContains('generate_result.hpp', 'samplerFromDescriptor = device->createSampler(descriptor);', 'Generated host code should still pass the translated descriptor into createSampler.');
        await t.expectFileContains('generate_result.hpp', '.compare = GVM::RHI::CompareFunction::GreaterEqual', 'Generated host code should also rewrite CompareFunction inside designated sampler initializers.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::SamplerDescriptor', 'Generated host code should no longer leak DSL SamplerDescriptor types into runtime-facing calls.');
        await t.expectFileNotContains('generate_result.hpp', 'UGL::CompareFunction::', 'Generated host code should no longer leak DSL CompareFunction enums.');
      }
    }),
    createFixtureCase({
      id: 'host-sampler-record-preserves-body',
      title: 'Regression: host records with Sampler fields keep their method bodies',
      group: 'regressions',
      labels: ['regression', 'host', 'sampler', 'uobject', 'resource-classification', 'host-compile'],
      fixtureDir: 'host-sampler-record-preserves-body',
      sourceFile: 'HostSamplerRecordPreservesBody.hpp',
      description: 'Covers ordinary host objects that cache UGL::Sampler handles while also creating bind groups from those samplers.',
      validates: [
        'UGL::Sampler fields lower to GVM::RHI::Sampler in generated host records.',
        'Host record constructors and getters are preserved instead of being replaced by shader-only empty shells.',
        'Shader-resource behavior shell detection does not treat sampler-only host records as shader-only behavior records.'
      ],
      watchouts: [
        'Without this regression, render-node style host objects that store a sampler can return null bind groups after code generation.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated host sampler record header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host sampler record fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::Sampler cachedSampler;', 'Generated host class should keep the sampler field as a host RHI sampler.');
        await t.expectFileContains('generate_result.hpp', 'cachedSampler = device->createSampler({', 'Generated host constructor should preserve sampler creation.');
        await t.expectFileContains('generate_result.hpp', 'cachedBindGroup = device->createBindGroup<HostSamplerRecordPreservesBodyBindGroup>(cachedSampler);', 'Generated host constructor should preserve bind-group creation from the sampler.');
        await t.expectFileContains('generate_result.hpp', 'return cachedBindGroup;', 'Generated host getter should return the stored bind group instead of a default value.');
        await t.expectFileNotContains('generate_result.hpp', 'return {};', 'Sampler-only host records should not be emitted as empty shader-resource behavior shells.');
      }
    }),
    createFixtureCase({
      id: 'host-throw-regression',
      title: 'Regression: host methods can preserve throw expressions',
      group: 'regressions',
      labels: ['regression', 'host', 'exceptions', 'ast'],
      fixtureDir: 'host-throw-regression',
      sourceFile: 'HostThrowRegression.hpp',
      description: 'Covers a host-side guard branch that uses a bare `throw;`, matching the control-flow shape that previously failed inside a downstream renderer integration.',
      validates: [
        'UGLC still generates successfully when host code contains a throw expression.',
        'The generated host output preserves the throw statement instead of failing on `CXXThrowExpr`.'
      ],
      watchouts: [
        'Shader visitors still reject throw expressions explicitly; this regression only protects host-side lowering.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host throw regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'throw;', 'Generated host code should preserve the throw statement.');
        await t.expectFileContains('generate_result.hpp', '#include "UGL.h"', 'Generated host output should preserve the shared UGL umbrella include instead of flattening it.');
        await t.expectFileNotContains('generate_result.hpp', '#include <glm/glm.hpp>', 'Generated host output must not inline the GLM-backed fallback branch from external UGL headers.');
        await t.expectFileNotContains('generate_result.hpp', 'HLSL.h - C++ HLSL Emulation Header with GLM Integration', 'Generated host output must not flatten third-party GLM implementation text into the merged header.');
      }
    }),
    createFixtureCase({
      id: 'cpp-swizzle-constructor-no-vec-cast',
      title: 'Regression: CPP swizzle vector constructors do not emit vec_cast',
      group: 'regressions',
      labels: ['regression', 'cpp', 'host', 'swizzle', 'constructor'],
      fixtureDir: 'cpp-swizzle-constructor-no-vec-cast',
      sourceFile: 'CppSwizzleConstructorNoVecCast.hpp',
      description: 'Covers CPP host lowering for vector constructors whose single argument is a swizzle expression, so the backend emits materialized vector constructors instead of leaking an implementation-only vec_cast helper.',
      validates: [
        'Same-scalar swizzle constructors lower as materialized vector constructors.',
        'Cross-scalar swizzle constructors lower as materialized vector constructors.',
        'Two-component and unsigned-integer swizzle constructors lower as materialized vector constructors.',
        'Generated host C++ never references `UGL::vec_cast`.'
      ],
      watchouts: [
        '`UGL::vec_cast` is not a DSL symbol and must not be required by generated host output.'
      ],
      hostCompileSteps: [{
        id: 'host-compile-swizzle-and-half',
        title: 'Compile host half math and side-effecting repeated swizzles',
        compilerArgs: ['-DDISABLE_UGL', '-DGLM_ENABLE_EXPERIMENTAL'],
        sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
        expectedExitCode: 0
      }],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Swizzle constructor fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::Math::convertShaderSwizzle<float3>(floatValue.xyz)', 'Same-type swizzle constructor should materialize its swizzle once.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::Math::convertShaderSwizzle<float3>(intValue.xyz)', 'Cross-scalar swizzle constructor should materialize its swizzle once.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::Math::convertShaderSwizzle<float2>(floatValue.xz)', 'Two-component swizzle constructor should materialize its swizzle once.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::Math::convertShaderSwizzle<float2>(index.xy)', 'Unsigned 2D swizzle constructor should materialize its swizzle once.');
        await t.expectFileContains('generate_result.hpp', 'GVM::Core::Math::convertShaderSwizzle<float3>(resolution.xyz)', 'Unsigned 3D swizzle constructor should materialize its swizzle once.');
        await t.expectFileContains('generate_result.hpp', 'float3(intValue.xyz - otherIntValue.xyz)', 'Binary swizzle constructor should stay as one vector expression.');
        await t.expectFileNotContains('generate_result.hpp', 'float3(intValue.xyz - otherIntValue.x, intValue.xyz - otherIntValue.y', 'Binary swizzle constructor must not be misexpanded by string matching.');
        await t.expectFileContains('generate_result.hpp', '>(values[cursor++].xyx)', 'A repeated swizzle must evaluate its source index once.');
        await t.expectFileNotContains('generate_result.hpp', 'values[cursor++].x, values[cursor++].y', 'Host lowering must not duplicate an incrementing index.');
        await t.expectFileNotContains('generate_result.hpp', 'vec_cast', 'Generated CPP output must not expose vec_cast.');
      }
    }),
    createFixtureCase({
      id: 'cpp-structured-buffer-helper-host-delete',
      title: 'Regression: CPP host deletes shader-only structured-buffer helpers',
      group: 'regressions',
      labels: ['regression', 'cpp', 'host', 'structured-buffer', 'shader-only'],
      fixtureDir: 'cpp-structured-buffer-helper-host-delete',
      sourceFile: 'CppStructuredBufferHelperHostDelete.hpp',
      description: 'Covers namespace helpers that index StructuredBuffer/RWStructuredBuffer resources so CPP host lowering emits deleted declarations instead of invalid GVM::RHI::Buffer subscript bodies.',
      validates: [
        'Free helpers that subscript RWStructuredBuffer resources are deleted on the host side.',
        'The generated host header does not contain a GVM::RHI::Buffer helper body with resource subscript access.',
        'Shader source generation still keeps the structured-buffer helper body for HLSL/MSL.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Structured-buffer helper fixture should generate successfully.');
        const content = await t.readArtifact('generate_result.hpp');
        const readDeleted = content.includes('bool readShaderOnlyStatusFlag(GVM::RHI::Buffer renderEntityStatus, uint index) = delete;');
        t.recordCheck('Read helper should be deleted in host C++.', readDeleted, readDeleted ? 'Found deleted read helper declaration.' : 'Missing deleted read helper declaration.');
        const writeDeleted = content.includes('void setShaderOnlyStatusFlag(GVM::RHI::Buffer renderEntityStatus, uint index, bool value) = delete;');
        t.recordCheck('Write helper should be deleted in host C++.', writeDeleted, writeDeleted ? 'Found deleted write helper declaration.' : 'Missing deleted write helper declaration.');
        const hasBadReadBody = /bool readShaderOnlyStatusFlag\(GVM::RHI::Buffer renderEntityStatus, uint index\)\s*\{[\s\S]*?renderEntityStatus\[index\]/u.test(content);
        t.recordCheck('Host read helper must not emit a buffer subscript body.', !hasBadReadBody, hasBadReadBody ? 'Found invalid host buffer subscript body.' : 'No invalid host read helper body found.');
        const hasBadWriteBody = /void setShaderOnlyStatusFlag\(GVM::RHI::Buffer renderEntityStatus, uint index, bool value\)\s*\{[\s\S]*?renderEntityStatus\[index\]/u.test(content);
        t.recordCheck('Host write helper must not emit a buffer subscript body.', !hasBadWriteBody, hasBadWriteBody ? 'Found invalid host buffer subscript body.' : 'No invalid host write helper body found.');
        await t.expectFileContains('generate_result.hpp', 'RWStructuredBuffer<unsigned int> bindGroup_renderEntityStatus', 'HLSL should still expose the RWStructuredBuffer binding.');
        await t.expectFileContains('generate_result.hpp', 'renderEntityStatus[index] |= 1u;', 'Shader source should still keep the structured-buffer write body.');
      }
    }),
    createFixtureCase({
      id: 'namespace-visit-anonymous-child',
      title: 'Regression: named namespaces with anonymous children still traverse their DSL declarations',
      group: 'regressions',
      labels: ['regression', 'namespace', 'visitor', 'traversal'],
      fixtureDir: 'namespace-visit-anonymous-child',
      sourceFile: 'NamespaceVisitAnonymousChild.hpp',
      description: 'Covers the AST visitor path where a named namespace also owns an anonymous child namespace. This keeps namespace traversal from depending on a confusing boolean shortcut in `VisitNamespaceDecl()`.',
      validates: [
        'UGLC still visits DSL declarations that live in a named namespace even when that namespace also contains an anonymous namespace.',
        'The render-class rewrite path remains active for namespace-scoped DSL classes.'
      ],
      watchouts: [
        'Without this regression, future namespace refactors can silently stop visiting valid DSL classes when anonymous helper namespaces are present in the same scope.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Namespace traversal fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'class NamespaceVisitPass : public GVM::Core::IRenderClass', 'Namespace-scoped render classes should still be rewritten by UGLC.');
        await t.expectFileContains('generate_result.hpp', 'namespace NamespaceVisitCase', 'The surrounding named namespace should remain present in generated output.');
        await t.expectFileContains('generate_result.hpp', 'RenderPipeline', 'The render pipeline generation path should still execute for the namespace-scoped class.');
      }
    }),
    createFixtureCase({
      id: 'compute-basic-success',
      title: 'Compute smoke: workgroup and compute visibility generation',
      group: 'smoke',
      labels: ['smoke', 'compute', 'bindgroup', 'workgroup'],
      fixtureDir: 'compute-basic',
      sourceFile: 'ComputeBasic.hpp',
      description: 'Runs a minimal compute class with one storage buffer bind group and explicit local workgroup size so we keep a fast signal on the compute pipeline path.',
      validates: [
        'Bind-group visibility for compute-only usage resolves to `ShaderStage::Compute`.',
        'Generated compute descriptor keeps the declared local workgroup size.',
        'The main compute class generation path still emits the expected artifacts.'
      ],
      watchouts: [
        'This catches regressions where compute classes silently fall back to fragment visibility or lose workgroup metadata.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Generated compute header should exist.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact computeShaderArtifact', 'Compute fixtures should emit a structured compute shader artifact bundle.');
        await t.expectFileContains('generate_result.hpp', 'createShaderModule(UGLC::Generated::MakeShaderModuleDescriptor("ComputeBasicPassComputeShader", computeShaderArtifact))', 'Compute create() code should use the shared shader-module descriptor helper.');
        await t.expectFileContains('generate_result.hpp', 'eastl::string hlslSource;', 'Generated shader artifact support should retain the emitted HLSL source alongside MSL.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {', 'Compute fixtures should embed the compiled SPIR-V words into the generated header.');
        await t.expectFileContains('generate_result.hpp', '0x07230203', 'Embedded SPIR-V should retain the SPIR-V magic number in the generated header.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] RWStructuredBuffer<unsigned int> bindGroup_values : register(u0, space0);', 'Compute HLSL should emit explicit Vulkan binding coordinates for storage buffers.');
        await t.expectFileContains('generate_result.hpp', 'RWStructuredBuffer<unsigned int> bindGroup_values : register(u0, space0);', 'Generated headers should retain the emitted HLSL source for the compute shader.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].visibility = GVM::RHI::ShaderStage::Compute', 'Compute bind-group visibility should remain compute-only.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupX = 8', 'Workgroup X dimension should match the DSL attribute.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupY = 1', 'Workgroup Y dimension should match the DSL attribute.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupZ = 1', 'Workgroup Z dimension should match the DSL attribute.');
      }
    }),
    createFixtureCase({
      id: 'shader-static-variant-render',
      title: 'Static shader variants: render class template specialization materialization',
      group: 'regressions',
      labels: ['regression', 'render', 'template', 'variant'],
      fixtureDir: 'shader-static-variant-render',
      sourceFile: 'ShaderStaticVariantRender.hpp',
      description: 'Instantiates one render class template twice with different policy, bind group, framebuffer, vertex input, and integer tile-size arguments.',
      validates: [
        'UGLC emits only concrete render class template specializations instead of a shader artifact for the template primary.',
        'Variant labels remain distinct for shader modules and pipeline descriptors.',
        'Template arguments propagate into bind-group layout, framebuffer format, and vertex layout generation.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Static render variant fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantRenderPass;', 'The render template primary should be rewritten as a forward declaration.');
        await t.expectFileNotContains('generate_result.hpp', 'template<>', 'Concrete render variants should be emitted as ordinary erased classes, not explicit specializations.');
        await t.expectFileNotContains('generate_result.hpp', 'template class StaticVariantRenderPass', 'Users should not need explicit C++ template instantiation definitions.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantRenderPass__OpaqueMaterialPolicy__StaticVariantBindGroup_OpaqueMaterialPolicy__StaticVariantFrameBuffer_OpaqueMaterialPolicy__StaticVariantVertexInput_OpaqueMaterialPolicy__16', 'Opaque render variant should be emitted as an ordinary erased class.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantRenderPass__CutoutMaterialPolicy__StaticVariantBindGroup_CutoutMaterialPolicy__StaticVariantFrameBuffer_CutoutMaterialPolicy__StaticVariantVertexInput_CutoutMaterialPolicy__32', 'Cutout render variant should be emitted as an ordinary erased class.');
        await t.expectFileContains('generate_result.hpp', 'using OpaqueStaticVariantPass = StaticVariantRenderPass__OpaqueMaterialPolicy__StaticVariantBindGroup_OpaqueMaterialPolicy__StaticVariantFrameBuffer_OpaqueMaterialPolicy__StaticVariantVertexInput_OpaqueMaterialPolicy__16;', 'Opaque alias should point at the erased ordinary class.');
        await t.expectFileContains('generate_result.hpp', 'using CutoutStaticVariantPass = StaticVariantRenderPass__CutoutMaterialPolicy__StaticVariantBindGroup_CutoutMaterialPolicy__StaticVariantFrameBuffer_CutoutMaterialPolicy__StaticVariantVertexInput_CutoutMaterialPolicy__32;', 'Cutout alias should point at the erased ordinary class.');
        await t.expectFileContains('generate_result.hpp', 'StaticVariantRenderPass__OpaqueMaterialPolicy__StaticVariantBindGroup_OpaqueMaterialPolicy__StaticVariantFrameBuffer_OpaqueMaterialPolicy__StaticVariantVertexInput_OpaqueMaterialPolicy__16VertexShader', 'Opaque vertex shader label should include a stable specialization suffix.');
        await t.expectFileContains('generate_result.hpp', 'StaticVariantRenderPass__CutoutMaterialPolicy__StaticVariantBindGroup_CutoutMaterialPolicy__StaticVariantFrameBuffer_CutoutMaterialPolicy__StaticVariantVertexInput_CutoutMaterialPolicy__32VertexShader', 'Cutout vertex shader label should include a stable specialization suffix.');
        await t.expectFileContains('generate_result.hpp', 'fragmentState.targets[0].format = GVM::RHI::TextureFormat::RGBA8Unorm', 'Opaque framebuffer policy should resolve to RGBA8Unorm.');
        await t.expectFileContains('generate_result.hpp', 'fragmentState.targets[0].format = GVM::RHI::TextureFormat::RGBA16Float', 'Cutout framebuffer policy should resolve to RGBA16Float.');
        await t.expectFileContains('generate_result.hpp', 'vertexState.buffers[0].arrayStride = 24', 'Opaque vertex input specialization should use float4 + float2 stride.');
        await t.expectFileContains('generate_result.hpp', 'vertexState.buffers[0].arrayStride = 28', 'Cutout vertex input specialization should use float4 + float3 stride.');
      }
    }),
    createFixtureCase({
      id: 'shader-static-variant-diagnostics-render',
      title: 'Static shader variants: render diagnostics policy isolation',
      group: 'regressions',
      labels: ['regression', 'render', 'template', 'variant', 'diagnostics', 'atomic'],
      fixtureDir: 'shader-static-variant-diagnostics-render',
      sourceFile: 'ShaderStaticVariantDiagnosticsRender.hpp',
      description: 'Instantiates one render shader class template twice with a no-op diagnostics policy and an instrumented diagnostics policy so shared shader code can be reused without leaking diagnostic work into the normal variant.',
      validates: [
        'UGLC emits two erased ordinary render classes for the diagnostics policy specializations.',
        'No-op diagnostics methods share the instrumented policy call signatures but emit no diagnostic storage buffer or atomic work in the normal variant.',
        'Instrumented diagnostics methods store the variant bind group as a policy member and use that stored handle for counter writes.'
      ],
      verify: async (t) => {
        const generated = await t.expectArtifactExists('generate_result.hpp', 'Static diagnostics render variant fixture should generate successfully.');
        if (!generated) {
          return;
        }
        await t.expectFileNotContains('generate_result.hpp', 'template<>', 'Concrete diagnostics variants should be emitted as ordinary erased classes, not explicit specializations.');
        await t.expectFileContains('dsl_single_header.hpp', 'BindGroup<BindGroupType> diagnosticBindGroup;', 'Instrumented diagnostics policy should store the bind group selected by the variant.');
        await t.expectFileContains('dsl_single_header.hpp', 'diagnostics.markVertex(vertexID);', 'Shared shader code should call vertex diagnostics through the stored-bind-group policy object.');
        await t.expectFileContains('dsl_single_header.hpp', 'diagnostics.markFragment(inputValue.diagnosticIndex);', 'Shared shader code should call fragment diagnostics through the stored-bind-group policy object.');
        await t.expectFileContains('dsl_single_header.hpp', 'uint makeDiagnosticIndex(uint seed)', 'Diagnostics policies should expose a matching non-void member method.');
        await t.expectFileContains('dsl_single_header.hpp', 'diagnostics.makeDiagnosticIndex(vertexID)', 'Shared shader code should use the non-void diagnostics member call in an expression.');
        await t.expectFileNotContains('dsl_single_header.hpp', 'diagnostics.markVertex(bindGroup', 'Shared shader code should not pass bind groups to every diagnostics mark call.');

        const content = await t.readArtifact('generate_result.hpp');
        const noOpClassName = 'StaticVariantDiagnosticRenderPass__NoOpRenderDiagnostics_StaticVariantDiagnosticNoOpBindGroup__StaticVariantDiagnosticNoOpBindGroup';
        const instrumentedClassName = 'StaticVariantDiagnosticRenderPass__InstrumentedRenderDiagnostics_StaticVariantDiagnosticInstrumentedBindGroup__StaticVariantDiagnosticInstrumentedBindGroup';
        const noOpClassMarker = `class ${noOpClassName}`;
        const instrumentedClassMarker = `class ${instrumentedClassName}`;
        const noOpClassStart = content.indexOf(noOpClassMarker);
        const instrumentedClassStart = content.indexOf(instrumentedClassMarker);
        const noOpClassEnd = noOpClassStart < 0 ? -1 : content.indexOf('\nclass ', noOpClassStart + noOpClassMarker.length);
        const instrumentedClassEnd = instrumentedClassStart < 0 ? -1 : content.indexOf('\nclass ', instrumentedClassStart + instrumentedClassMarker.length);
        const noOpSection = noOpClassStart < 0 ? '' : content.slice(noOpClassStart, noOpClassEnd < 0 ? content.length : noOpClassEnd);
        const instrumentedSection = instrumentedClassStart < 0 ? '' : content.slice(instrumentedClassStart, instrumentedClassEnd < 0 ? content.length : instrumentedClassEnd);

        t.recordCheck('No-op diagnostics render variant class should be emitted.', noOpClassStart >= 0, noOpClassStart >= 0 ? `Found ${noOpClassName}.` : `Missing ${noOpClassName}.`);
        t.recordCheck('Instrumented diagnostics render variant class should be emitted.', instrumentedClassStart >= 0, instrumentedClassStart >= 0 ? `Found ${instrumentedClassName}.` : `Missing ${instrumentedClassName}.`);
        await t.expectFileContains('generate_result.hpp', `using NoOpDiagnosticRenderPass = ${noOpClassName};`, 'No-op diagnostics alias should point at the erased class.');
        await t.expectFileContains('generate_result.hpp', `using InstrumentedDiagnosticRenderPass = ${instrumentedClassName};`, 'Instrumented diagnostics alias should point at the erased class.');
        await t.expectFileContains('generate_result.hpp', `${noOpClassName}VertexShader`, 'No-op diagnostics vertex shader label should include the stable variant suffix.');
        await t.expectFileContains('generate_result.hpp', `${instrumentedClassName}VertexShader`, 'Instrumented diagnostics vertex shader label should include the stable variant suffix.');
        await t.expectFileContains('generate_result.hpp', `${instrumentedClassName}FragmentShader`, 'Instrumented diagnostics fragment shader label should include the stable variant suffix.');

        const noOpHasDiagnosticStorage = noOpSection.includes('bindGroup_counters') || noOpSection.includes('InterlockedAdd(bindGroup_counters') || noOpSection.includes('atomicAdd(diagnosticBindGroup') || noOpSection.includes('layoutEntry[2]');
        t.recordCheck('No-op diagnostics variant should not emit counter resources or atomic work.', !noOpHasDiagnosticStorage, noOpHasDiagnosticStorage ? 'Found diagnostic storage or atomic text in the no-op variant section.' : 'No diagnostic storage or atomic text found in the no-op variant section.');
        t.recordCheck('Instrumented diagnostics variant should emit the counter storage buffer.', instrumentedSection.includes('RWStructuredBuffer<unsigned int> bindGroup_counters'), 'Expected HLSL counter storage buffer in the instrumented variant section.');
        t.recordCheck('Instrumented diagnostics HLSL should emit a BindGroup handle struct.', instrumentedSection.includes('struct StaticVariantDiagnosticInstrumentedBindGroup_UGLBindGroupHandle') && instrumentedSection.includes('RWStructuredBuffer<unsigned int> counters;'), 'Expected a local BindGroup handle struct with the diagnostics counter resource.');
        t.recordCheck('Instrumented diagnostics behavior struct should preserve a stored BindGroup handle field.', instrumentedSection.includes('StaticVariantDiagnosticInstrumentedBindGroup_UGLBindGroupHandle diagnosticBindGroup;'), 'Expected the diagnostics behavior object to keep a concrete BindGroup handle field.');
        t.recordCheck('Instrumented diagnostics behavior struct should preserve a member method body.', instrumentedSection.includes('void markVertex(uint vertexID)') && instrumentedSection.includes('void markFragment(uint diagnosticIndex)'), 'Expected HLSL behavior methods to remain inside the generated diagnostics struct.');
        t.recordCheck('Instrumented diagnostics init should preserve handle assignment.', instrumentedSection.includes('diagnosticBindGroup = bindGroup;'), 'Expected the behavior init method to preserve ordinary BindGroup handle assignment.');
        t.recordCheck('Instrumented diagnostics behavior should support non-void member methods.', instrumentedSection.includes('uint makeDiagnosticIndex(uint seed)') && instrumentedSection.includes('return diagnosticBindGroup.counters[5] + seed'), 'Expected non-void behavior method body to return a value read through the stored BindGroup handle.');
        t.recordCheck('Instrumented diagnostics call sites should preserve object member calls.', instrumentedSection.includes('diagnostics.markVertex(vertexID)') && instrumentedSection.includes('diagnostics.makeDiagnosticIndex(vertexID)'), 'Expected generated HLSL call sites to keep diagnostics object member calls.');
        t.recordCheck('Instrumented diagnostics entry should materialize a local BindGroup handle from flat globals.', instrumentedSection.includes('StaticVariantDiagnosticInstrumentedBindGroup_UGLBindGroupHandle bindGroup;') && instrumentedSection.includes('bindGroup.counters = bindGroup_counters;'), 'Expected shader entry to materialize a local handle from flat descriptor globals.');
        t.recordCheck('Instrumented diagnostics variant should lower counter atomics through the stored handle.', instrumentedSection.includes('InterlockedAdd(diagnosticBindGroup.counters'), 'Expected HLSL InterlockedAdd lowering for instrumented diagnostics counters through the stored handle.');
        t.recordCheck('Instrumented diagnostics bind-group layout should include the counter binding.', content.includes('layoutEntry[2].binding = 2'), 'Expected binding 2 layout entry for the diagnostics counter buffer.');
        await t.expectFileNotContains('generate_result.hpp', 'diagnostics.markVertex(bindGroup', 'Generated shader body should not preserve the old diagnostics call shape that passes bind groups to every mark call.');
        await t.expectFileNotContains('generate_result.hpp', 'DiagnosticPolicy::mark', 'Diagnostics policy calls should not be emitted as static method calls.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-bindgroup-handle-same-type-multi',
      title: 'Regression: HLSL BindGroup handle structs distinguish same-type slots',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'bindgroup', 'helpers', 'behavior', 'multi-bindgroup'],
      fixtureDir: 'hlsl-bindgroup-handle-same-type-multi',
      sourceFile: 'HLSLBindGroupHandleSameTypeMulti.hpp',
      description: 'Covers helper behavior objects that store BindGroup handles when two shader slots use the same concrete BindGroup type.',
      validates: [
        'HLSL emits one local BindGroup handle struct for the shared BindGroup layout.',
        'Shader entry code materializes two distinct local handle variables from two flat descriptor spaces.',
        'Behavior init calls pass the selected handle object instead of resolving resources by BindGroup type.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Same-type BindGroup handle fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'struct SameTypeMultiBindGroup_UGLBindGroupHandle', 'HLSL should emit the shared BindGroup handle struct.');
        await t.expectFileContains('generate_result.hpp', 'SameTypeMultiBindGroup_UGLBindGroupHandle sourceBindGroup;', 'Entry code should materialize the first same-type BindGroup handle.');
        await t.expectFileContains('generate_result.hpp', 'SameTypeMultiBindGroup_UGLBindGroupHandle destinationBindGroup;', 'Entry code should materialize the second same-type BindGroup handle.');
        await t.expectFileContains('generate_result.hpp', 'sourceBindGroup.values = sourceBindGroup_values;', 'First handle should bind to the first flat descriptor global.');
        await t.expectFileContains('generate_result.hpp', 'destinationBindGroup.values = destinationBindGroup_values;', 'Second handle should bind to the second flat descriptor global.');
        await t.expectFileContains('generate_result.hpp', 'SameTypeMultiBindGroup_UGLBindGroupHandle storedBindGroup;', 'Behavior object should store a concrete BindGroup handle field.');
        await t.expectFileContains('generate_result.hpp', 'void init(SameTypeMultiBindGroup_UGLBindGroupHandle bindGroup)', 'Behavior init should receive one BindGroup handle object.');
        await t.expectFileContains('generate_result.hpp', 'sourceHandleAccess.init(sourceBindGroup);', 'Source behavior object should be initialized with the source handle.');
        await t.expectFileContains('generate_result.hpp', 'destinationHandleAccess.init(destinationBindGroup);', 'Destination behavior object should be initialized with the destination handle.');
        await t.expectFileContains('generate_result.hpp', 'storedBindGroup.values[index] = value;', 'Behavior write method should access storage through the stored handle.');
        await t.expectFileNotContains('generate_result.hpp', 'matches more than one shader bind group', 'Same-type BindGroup slots must not hit the old type-based ambiguity diagnostic.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-bindgroup-handle-uniform-buffer',
      title: 'Regression: HLSL BindGroup handle structs preserve UniformBuffer access',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'bindgroup', 'helpers', 'behavior', 'uniform-buffer'],
      fixtureDir: 'hlsl-bindgroup-handle-uniform-buffer',
      sourceFile: 'HLSLBindGroupHandleUniformBuffer.hpp',
      description: 'Covers helper behavior objects that store BindGroup handles containing UniformBuffer resources.',
      validates: [
        'HLSL emits the uniform wrapper type before the BindGroup handle struct.',
        'The handle struct stores the uniform resource as ConstantBuffer<Wrapper>.',
        'Behavior methods can call UniformBuffer::read() through a stored BindGroup handle.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Uniform-buffer BindGroup handle fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'struct BindGroupHandleUniformBindGroup_params_UniformValue', 'HLSL should emit the uniform wrapper type.');
        await t.expectFileContains('generate_result.hpp', 'struct BindGroupHandleUniformBindGroup_UGLBindGroupHandle', 'HLSL should emit the BindGroup handle struct.');
        await t.expectFileContains('generate_result.hpp', 'ConstantBuffer<BindGroupHandleUniformBindGroup_params_UniformValue> params;', 'Handle struct should store UniformBuffer resources as ConstantBuffer wrapper fields.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHandleUniformBindGroup_UGLBindGroupHandle storedBindGroup;', 'Behavior object should store the concrete BindGroup handle field.');
        await t.expectFileContains('generate_result.hpp', 'storedBindGroup = bindGroup;', 'Behavior init should preserve BindGroup handle assignment.');
        await t.expectFileContains('generate_result.hpp', 'const BindGroupHandleUniformParams paramsValue = storedBindGroup.params.value;', 'UniformBuffer::read should lower through the stored handle wrapper value.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup.params = bindGroup_params;', 'Entry code should materialize the uniform resource handle from the flat global.');
        await t.expectFileContains('generate_result.hpp', 'handleAccess.writeOutput(index, handleAccess.readUniformValue(index));', 'Call site should preserve behavior member calls that return values.');
      }
    }),
    createFixtureCase({
      id: 'shader-resource-handle-behavior-fields',
      title: 'Regression: shader-resource behavior records store direct resource handles',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'msl', 'resource', 'behavior', 'texture', 'sampler', 'structured-buffer'],
      fixtureDir: 'shader-resource-handle-behavior-fields',
      sourceFile: 'ShaderResourceHandleBehaviorFields.hpp',
      description: 'Covers shader-only behavior records that store direct Texture2D, Sampler, and RWStructuredBuffer handles as fields.',
      validates: [
        'HLSL and MSL generation allow direct shader resource fields in behavior records.',
        'The behavior struct and non-static member methods are preserved in generated shader code.',
        'Stored direct texture, sampler, and writable buffer handles remain usable from behavior methods.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Direct resource-handle behavior fixture should generate successfully.');
        await t.expectFileContains('dsl_single_header.hpp', 'Texture2D<TextureFormat::RGBA16Float> texture;', 'Fixture source should store a direct Texture2D handle field.');
        await t.expectFileContains('dsl_single_header.hpp', 'RWStructuredBuffer<float4> outputValues;', 'Fixture source should store a direct writable buffer handle field.');
        await t.expectFileContains('generate_result.hpp', 'struct DirectResourceHandleBehavior', 'Generated shader should keep the direct resource behavior struct.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> texture;', 'HLSL behavior struct should keep the direct sampled texture field.');
        await t.expectFileContains('generate_result.hpp', 'SamplerState sampler0;', 'HLSL behavior struct should keep the direct sampler field.');
        await t.expectFileContains('generate_result.hpp', 'RWStructuredBuffer<float4> outputValues;', 'HLSL behavior struct should keep the direct writable buffer field.');
        await t.expectFileContains('generate_result.hpp', 'void init(Texture2D<float4> inputTexture, SamplerState inputSampler, RWStructuredBuffer<float4> outputBuffer)', 'Behavior init should receive direct resource handles.');
        await t.expectFileContains('generate_result.hpp', 'return float4(texture.SampleLevel(sampler0, uv, 0.0f));', 'Texture intrinsic should lower through the stored direct texture handle.');
        await t.expectFileContains('generate_result.hpp', 'outputValues[index] = value;', 'Behavior write method should access the stored writable buffer handle.');
        await t.expectFileContains('generate_result.hpp', 'behavior.init(bindGroup.sceneTexture, bindGroup.sceneSampler, bindGroup.outputValues);', 'Entry call site should pass direct handles from the local BindGroup handle object.');
        await t.expectFileContains('generate_result.hpp', 'behavior.writeValue(threadID.x, behavior.sampleColor(float2(0.25f, 0.75f)));', 'Call site should preserve object member-call semantics.');
        await t.expectFileNotContains('generate_result.hpp', 'GVM::RHI::Texture texture;', 'CPU shell should not store shader-only direct texture fields.');
        await t.expectFileNotContains('generate_result.hpp', 'GVM::RHI::Sampler sampler0;', 'CPU shell should not store shader-only direct sampler fields.');
        await t.expectFileNotContains('generate_result.hpp', 'must return void in the current DSL subset', 'Direct resource behavior methods must not be limited to void returns.');
      }
    }),
    createFixtureCase({
      id: 'shader-class-nttp-direct-specialization',
      title: 'Shader class NTTP direct specialization substitution',
      group: 'regressions',
      labels: ['regression', 'compute', 'template', 'variant', 'nttp', 'hlsl', 'msl'],
      fixtureDir: 'shader-class-nttp-direct-specialization',
      sourceFile: 'ShaderClassNttpDirectSpecialization.hpp',
      description: 'Instantiates compute shader class templates directly in ComputeClass<T> and createComputeClass<T>(), without aliases, and verifies non-type template parameter references are erased to constants.',
      validates: [
        'Direct ComputeClass<PassT<1u, 2u>> usage materializes a concrete shader class variant.',
        'Namespace-scoped constant template arguments are substituted before backend qualified-name lowering.',
        'Direct template shader specializations collect record dependencies from UniformBuffer<T>, helper signatures, and entry locals before HLSL helper prototypes.',
        'HLSL and MSL generated shader sources do not leak primary-template parameter names such as PassT::A or PassT::B.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Direct shader class NTTP specialization fixture should generate successfully.');
        await t.expectFileContains('dsl_single_header.hpp', 'ComputeClass<DirectNttpPassT<1u, 2u>> literalPass;', 'Fixture should use a direct literal template specialization in ComputeClass<T>.');
        await t.expectFileContains('dsl_single_header.hpp', 'createComputeClass<DirectNttpPassT<1u, 2u>>', 'Fixture should create the literal specialization directly without a using alias.');
        await t.expectFileContains('dsl_single_header.hpp', 'NamespacedNttpPassT<ShaderClassNttpDirectSpecializationValues::NamespaceA, ShaderClassNttpDirectSpecializationValues::NamespaceB>', 'Fixture should use namespace constants as direct template arguments.');
        await t.expectFileContains('dsl_single_header.hpp', 'ComputeClass<DirectNttpConfigPassT<1u>> configPass;', 'Fixture should use a direct config-record template specialization in ComputeClass<T>.');
        await t.expectFileContains('dsl_single_header.hpp', 'createComputeClass<DirectNttpConfigPassT<1u>>', 'Fixture should create the config-record specialization directly without a using alias.');

        const generated = await t.readArtifact('generate_result.hpp');
        t.recordCheck('Generated shader sources should contain the literal specialization value for A.', generated.includes('const uint x = 1;'), 'Expected `const uint x = 1;` in generated shader source.');
        t.recordCheck('Generated shader sources should contain the literal specialization value for B.', generated.includes('const uint y = 2;'), 'Expected `const uint y = 2;` in generated shader source.');
        t.recordCheck('Generated shader sources should contain the namespace specialization value for A.', generated.includes('const uint x = 3;'), 'Expected `const uint x = 3;` in generated shader source.');
        t.recordCheck('Generated shader sources should contain the namespace specialization value for B.', generated.includes('const uint y = 4;'), 'Expected `const uint y = 4;` in generated shader source.');
        t.recordCheck('Generated shader sources should define the config record used by a direct shader specialization.', generated.includes('struct ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig'), 'Expected flattened `struct ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig` in generated shader source.');
        t.recordCheck('Generated shader sources should emit the config helper signature.', generated.includes('uint readConfig(ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig config)'), 'Expected flattened `uint readConfig(ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig config)` in generated shader source.');
        t.recordCheck('Generated shader sources should emit the config record before the HLSL helper prototype.',
          generated.indexOf('struct ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig') >= 0 &&
          generated.indexOf('uint readConfig(ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig config);') >= 0 &&
          generated.indexOf('struct ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig') < generated.indexOf('uint readConfig(ShaderClassNttpDirectSpecializationRecords_DirectNttpConfig config);'),
          'Expected flattened config record before the HLSL `readConfig(...)` prototype.');
        t.recordCheck('Generated shader sources should contain the direct config specialization value for Mode.', generated.includes('readConfig(config) + 1'), 'Expected config specialization to add concrete mode value `1`.');
        t.recordCheck('HLSL/MSL source should not reference literal primary-template NTTP names.', !generated.includes('DirectNttpPassT::A') && !generated.includes('DirectNttpPassT::B'), 'Unexpected direct template parameter name in generated shader source.');
        t.recordCheck('HLSL/MSL source should not reference namespaced primary-template NTTP names.', !generated.includes('NamespacedNttpPassT::A') && !generated.includes('NamespacedNttpPassT::B'), 'Unexpected namespaced template parameter name in generated shader source.');
        t.recordCheck('HLSL/MSL source should not reference config primary-template NTTP names.', !generated.includes('DirectNttpConfigPassT::Mode'), 'Unexpected config template parameter name in generated shader source.');
      }
    }),
    createFixtureCase({
      id: 'shader-static-variant-compute',
      title: 'Static shader variants: compute NTTP workgroup and if constexpr',
      group: 'regressions',
      labels: ['regression', 'compute', 'template', 'variant', 'constexpr'],
      fixtureDir: 'shader-static-variant-compute',
      sourceFile: 'ShaderStaticVariantCompute.hpp',
      description: 'Instantiates a compute shader class template at two integer workgroup sizes and folds an if constexpr branch inside compute().',
      validates: [
        'LocalWorkGroupSize template arguments are evaluated into concrete host descriptor values.',
        'Each compute specialization gets a distinct shader module label.',
        'if constexpr branches are folded before shader backend emission.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Static compute variant fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantComputePass;', 'The compute template primary should be rewritten as a forward declaration.');
        await t.expectFileNotContains('generate_result.hpp', 'template<>', 'Concrete compute variants should be emitted as ordinary erased classes, not explicit specializations.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantComputePass__8', 'Workgroup-8 compute variant should be emitted as an ordinary erased class.');
        await t.expectFileContains('generate_result.hpp', 'class StaticVariantComputePass__16', 'Workgroup-16 compute variant should be emitted as an ordinary erased class.');
        await t.expectFileContains('generate_result.hpp', 'StaticVariantComputePass__8ComputeShader', 'Workgroup-8 compute shader label should include its NTTP suffix.');
        await t.expectFileContains('generate_result.hpp', 'StaticVariantComputePass__16ComputeShader', 'Workgroup-16 compute shader label should include its NTTP suffix.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupX = 8', 'Workgroup-8 specialization should emit a concrete X dimension.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupX = 16', 'Workgroup-16 specialization should emit a concrete X dimension.');
        await t.expectFileNotContains('generate_result.hpp', 'computeDesp.workgroupX = WorkGroupSize', 'Generated host code should not reference the template parameter name after specialization.');
        await t.expectFileNotContains('generate_result.hpp', 'if constexpr', 'if constexpr should be folded before generated shader/host method bodies are emitted.');
      }
    }),
    createFixtureCase({
      id: 'shader-static-variant-render-set',
      title: 'Static shader variants: RenderSet template specialization materialization',
      group: 'regressions',
      labels: ['regression', 'render-set', 'template', 'variant'],
      fixtureDir: 'shader-static-variant-render-set',
      sourceFile: 'ShaderStaticVariantRenderSet.hpp',
      description: 'Instantiates a RenderSet template twice with different vertex component element types and texture component counts.',
      validates: [
        'RenderSet template primaries are not emitted as generic runtime layouts.',
        'Concrete RenderSet specializations preserve element type and texture count in createInfo.',
        'Generated RenderSet labels remain distinct across specializations.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Static RenderSet variant fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'struct StaticVariantRenderSet;', 'The RenderSet template primary should be rewritten as a forward declaration.');
        await t.expectFileContains('generate_result.hpp', 'struct StaticVariantRenderSet__float4__4', 'Mesh RenderSet specialization should be emitted as an ordinary erased struct.');
        await t.expectFileContains('generate_result.hpp', 'struct StaticVariantRenderSet__float3__8', 'Billboard RenderSet specialization should be emitted as an ordinary erased struct.');
        await t.expectFileContains('generate_result.hpp', 'using StaticVariantMeshSet = StaticVariantRenderSet__float4__4;', 'Mesh RenderSet alias should point at the erased ordinary struct.');
        await t.expectFileContains('generate_result.hpp', 'using StaticVariantBillboardSet = StaticVariantRenderSet__float3__8;', 'Billboard RenderSet alias should point at the erased ordinary struct.');
        await t.expectFileContains('generate_result.hpp', 'createInfo.renderSetName = "StaticVariantRenderSet__float4__4"', 'Mesh RenderSet createInfo should use a stable variant label.');
        await t.expectFileContains('generate_result.hpp', 'createInfo.renderSetName = "StaticVariantRenderSet__float3__8"', 'Billboard RenderSet createInfo should use a stable variant label.');
        await t.expectFileContains('generate_result.hpp', 'sizeof(float4)', 'Mesh vertex component should keep the float4 element type.');
        await t.expectFileContains('generate_result.hpp', 'sizeof(float3)', 'Billboard vertex component should keep the float3 element type.');
        await t.expectFileContains('generate_result.hpp', '.maxResourceCount = 4', 'Mesh texture component count should remain 4.');
        await t.expectFileContains('generate_result.hpp', '.maxResourceCount = 8', 'Billboard texture component count should remain 8.');
      }
    }),
    createFixtureCase({
      id: 'dsl-reserved-vertex-host-only',
      title: 'Regression: host-only vertex local name is outside shader DSL reserved-name validation',
      group: 'regressions',
      labels: ['regression', 'compute', 'dsl', 'reserved-identifier', 'host'],
      fixtureDir: 'dsl-reserved-vertex-host-only',
      sourceFile: 'DSLReservedVertexHostOnly.hpp',
      description: 'Confirms the DSL reserved-name validator scans shader artifact declarations instead of host-only helper bodies.',
      validates: [
        'Host-only helper bodies may keep ordinary C++ local names.',
        'Shader artifact validation still allows a valid compute pass in the same fixture.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host-only reserved-name control fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'makeHostOnlyVertexValue', 'The host-only helper should remain in generated host output.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact computeShaderArtifact', 'The valid compute shader artifact should still be emitted.');
      }
    }),
    createFixtureCase({
      id: 'storage-texture-vulkan-format-lowering',
      title: 'Regression: storage textures keep explicit Vulkan image_format annotations in HLSL',
      group: 'regressions',
      labels: ['regression', 'compute', 'texture', 'storage-texture', 'hlsl', 'vulkan'],
      fixtureDir: 'storage-texture-vulkan-format-lowering',
      sourceFile: 'StorageTextureVulkanFormatLowering.hpp',
      description: 'Adds direct coverage for Vulkan storage-texture lowering so concrete DSL formats such as `RGBA8Unorm` and `R32Float` no longer collapse to bare `RWTexture2D<T>` declarations without the required `vk::image_format` attribute.',
      validates: [
        'Generated HLSL storage-texture declarations retain explicit `[[vk::image_format("...")]]` annotations.',
        'The HLSL declaration still uses the correct typed `RWTexture2D<T>` element type for each DSL format.',
        'Generated host bind-group layout entries continue to preserve the declared storage texture formats.'
      ],
      watchouts: [
        'Without this regression, Vulkan reflection can reinterpret `RWTexture2D<float4>` as `Rgba32f` and reject the pipeline against an `RGBA8Unorm` host layout.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Storage-texture Vulkan lowering fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] [[vk::image_format("rgba8")]] RWTexture2D<float4> bindGroup_rgbaTexture : register(u0, space0);', 'Storage-texture lowering should pair Vulkan binding coordinates with the explicit rgba8 image_format attribute.');
        await t.expectFileContains('generate_result.hpp', '[[vk::image_format("rgba8")]] RWTexture2D<float4> bindGroup_rgbaTexture : register(u0, space0);', 'RGBA8Unorm storage textures should lower to an explicit Vulkan rgba8 image_format attribute.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(1, 0)]] [[vk::image_format("r32f")]] RWTexture2D<float> bindGroup_scalarTexture : register(u1, space0);', 'Storage-texture lowering should pair Vulkan binding coordinates with the explicit r32f image_format attribute.');
        await t.expectFileContains('generate_result.hpp', '[[vk::image_format("r32f")]] RWTexture2D<float> bindGroup_scalarTexture : register(u1, space0);', 'R32Float storage textures should lower to an explicit Vulkan r32f image_format attribute.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].storageTexture.format = GVM::RHI::TextureFormat::RGBA8Unorm', 'Host bind-group layout should preserve the RGBA8Unorm storage texture format.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[1].storageTexture.format = GVM::RHI::TextureFormat::R32Float', 'Host bind-group layout should preserve the R32Float storage texture format.');
      }
    }),
    createFixtureCase({
      id: 'texture3d-rwtexture3d-lowering',
      title: 'Feature: Texture3D and RWTexture3D lower across HLSL, MSL, and host layout',
      group: 'regressions',
      labels: ['feature', 'compute', 'texture', 'texture3d', 'storage-texture', 'hlsl', 'msl'],
      fixtureDir: 'texture3d-rwtexture3d-lowering',
      sourceFile: 'Texture3DRWTexture3DLowering.hpp',
      description: 'Covers sampled 3D texture and read-write 3D storage texture DSL calls so UGLC keeps the shader and host texture-view dimensions aligned.',
      validates: [
        'HLSL emits Texture3D and RWTexture3D declarations with explicit Vulkan image_format metadata for storage textures.',
        'MSL emits texture3d declarations and maps read, write, sample, sampleLevel, and sampleGrad calls to Metal 3D texture APIs.',
        'Host bind-group layout entries preserve TextureViewDimension::e3D for sampled and storage 3D textures.'
      ],
      watchouts: [
        'This fixture intentionally avoids Texture3DArray, 3D gather, and 3D render attachments because those are outside the current TODO boundary.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Texture3D/RWTexture3D fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'Texture3D<float4> bindGroup_volumeTexture', 'HLSL sampled 3D textures should lower to Texture3D.');
        await t.expectFileContains('generate_result.hpp', '[[vk::image_format("rgba8")]] RWTexture3D<float4> bindGroup_outputVolume', 'HLSL storage 3D textures should lower to RWTexture3D with explicit image_format metadata.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup.volumeTexture.Load(int4(coord, 0u))', 'HLSL Texture3D read should use Load(int4(coord, mip)) through the local bind-group handle.');
        await t.expectFileContains('generate_result.hpp', 'texture3d<float', 'MSL sampled 3D textures should lower to texture3d.');
        await t.expectFileContains('generate_result.hpp', 'texture3d<half, access::read_write>', 'MSL RWTexture3D should lower to texture3d with read_write access.');
        await t.expectFileContains('generate_result.hpp', '.read(uint3(coord)', 'MSL Texture3D read lowering should use read(uint3(...)).');
        await t.expectFileContains('generate_result.hpp', '.write(', 'MSL RWTexture3D write lowering should use Metal write().');
        await t.expectFileContains('generate_result.hpp', '.sample(', 'MSL Texture3D sampling should use Metal sample().');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].texture.viewDimension = GVM::RHI::TextureViewDimension::e3D', 'Host sampled texture layout should use e3D.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[1].storageTexture.viewDimension = GVM::RHI::TextureViewDimension::e3D', 'Host storage texture layout should use e3D.');
      }
    }),
    createFixtureCase({
      id: 'astc4x4-unorm-texture-format',
      title: 'Feature: ASTC4x4Unorm sampled textures lower across HLSL, MSL, and host code',
      group: 'regressions',
      labels: ['feature', 'compute', 'texture', 'texture3d', 'astc', 'hlsl', 'msl'],
      fixtureDir: 'astc4x4-unorm-texture-format',
      sourceFile: 'Astc4x4UnormTextureFormat.hpp',
      description: 'Covers ASTC4x4Unorm as a first-class sampled texture format for Texture2D, Texture2DArray, Texture3D, and host Texture creation.',
      validates: [
        'HLSL sampled ASTC textures lower to float4 image interfaces.',
        'MSL sampled ASTC textures lower to half scalar texture declarations.',
        'Host createTexture preserves ASTC4x4Unorm format and e3D texture dimension.'
      ],
      watchouts: [
        'ASTC4x4Unorm is intentionally sampled-only; storage and attachment diagnostics are covered by separate negative fixtures.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'ASTC4x4Unorm fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> bindGroup_texture2D', 'HLSL ASTC Texture2D should expose a float4 sampled interface.');
        await t.expectFileContains('generate_result.hpp', 'Texture2DArray<float4> bindGroup_textureArray', 'HLSL ASTC Texture2DArray should expose a float4 sampled interface.');
        await t.expectFileContains('generate_result.hpp', 'Texture3D<float4> bindGroup_texture3D', 'HLSL ASTC Texture3D should expose a float4 sampled interface.');
        await t.expectFileContains('generate_result.hpp', 'texture2d<half> texture2D', 'MSL ASTC Texture2D should lower to texture2d<half>.');
        await t.expectFileContains('generate_result.hpp', 'texture2d_array<half> textureArray', 'MSL ASTC Texture2DArray should lower to texture2d_array<half>.');
        await t.expectFileContains('generate_result.hpp', 'texture3d<half> texture3D', 'MSL ASTC Texture3D should lower to texture3d<half>.');
        await t.expectFileContains('generate_result.hpp', '.format = GVM::RHI::TextureFormat::ASTC4x4Unorm', 'Host texture creation should preserve ASTC4x4Unorm.');
        await t.expectFileContains('generate_result.hpp', '.dimension = GVM::RHI::TextureDimension::e3D', 'Host texture creation should preserve e3D dimension.');
        await t.expectFileContains('generate_result.hpp', '.size = {.width = (uint32_t)128, .height = (uint32_t)128, .depth = (uint32_t)64}', 'Host ASTC 3D texture creation should preserve 128x128x64 extent.');
        await t.expectFileContains('generate_result.hpp', '.mipLevelCount = 8', 'Host ASTC 3D texture creation should preserve explicit mip count.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-native-half-vulkan',
      title: 'Regression: Vulkan HLSL uses native half arithmetic without changing image interfaces',
      group: 'regressions',
      labels: ['regression', 'compute', 'hlsl', 'vulkan', 'half', 'spirv'],
      fixtureDir: 'hlsl-native-half-vulkan',
      sourceFile: 'HLSLNativeHalfVulkan.hpp',
      description: 'Covers the DXC path that enables native 16-bit HLSL arithmetic while keeping sampled and storage image declarations in DXC/Vulkan-compatible float element types.',
      validates: [
        'Generated HLSL keeps `Texture2D<half4>` DSL resources lowered to Vulkan-compatible `Texture2D<float4>`.',
        'Generated HLSL keeps `RGBA16Float` storage images as `RWTexture2D<float4>` plus an explicit `rgba16f` image_format annotation.',
        'Compiled SPIR-V contains a native 16-bit float type, proving DXC was invoked with 16-bit type support instead of demoting all half arithmetic to float.'
      ],
      watchouts: [
        'This intentionally does not place half in host-visible buffer structs, because native half changes HLSL buffer layout and must not be tested through an unrelated ABI path.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Native-half Vulkan fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] Texture2D<float4> bindGroup_inputTexture : register(t0, space0);', 'Sampled half textures should keep a float4 image interface in HLSL for DXC/Vulkan.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(2, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> bindGroup_outputTexture : register(u2, space0);', 'RGBA16Float storage textures should keep a float4 interface with an explicit rgba16f Vulkan image_format.');
        await t.expectFileContains('generate_result.hpp', 'half4 sampled = half4(bindGroup.inputTexture.SampleLevel(bindGroup.textureSampler, uv, 0.0f));', 'Local sampled value should remain a half4 expression in generated HLSL.');
        await t.expectFileContains('generate_result.hpp', 'half4 scaled = (sampled * half(0.5f)) + half4(half(0.125f), half(0.25f), half(0.5f), half(0.0f));', 'Local arithmetic should remain in half types in generated HLSL.');
        await expectSpirvHasCapability(t, 'generate_result.hpp', 'computeShaderArtifact_SpirvWords', 9, 'SPIR-V should declare Float16 capability for native half arithmetic.');
        await expectSpirvHasTypeFloatWidth(t, 'generate_result.hpp', 'computeShaderArtifact_SpirvWords', 16, 'SPIR-V should contain a native 16-bit float type.');
        await expectSpirvHasTypeFloatWidth(t, 'generate_result.hpp', 'computeShaderArtifact_SpirvWords', 32, 'SPIR-V should retain 32-bit float types for image interfaces and ordinary float expressions.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-texture-helper-parameter-sample-level',
      title: 'Regression: HLSL standalone texture helper parameters lower sampleLevel before behavior records',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'texture', 'sampler', 'helpers', 'sample-level'],
      fixtureDir: 'hlsl-texture-helper-parameter-sample-level',
      sourceFile: 'HLSLTextureHelperParameterSampleLevel.hpp',
      description: 'Covers shader helpers that take Texture2D and Sampler directly so texture intrinsics are lowered before shader-resource behavior handling.',
      validates: [
        'HLSL helper signatures can contain standalone Texture2D/Sampler parameters.',
        'Texture2D::sampleLevel lowers to HLSL SampleLevel inside a helper body.',
        'An unused valid texture helper can remain in the fixture without tripping shader-resource behavior validation.'
      ],
      watchouts: [
        'This keeps shader-resource behavior handling from intercepting UGL texture intrinsic methods.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Standalone texture helper fixture should generate successfully.');
        await t.expectFileContains('dsl_single_header.hpp', 'inline float4 unusedSampleColor(Texture2D<TextureFormat::RGBA16Float> texture, Sampler sampler, float2 uv)', 'The fixture should keep an unused standalone texture helper in source form.');
        await t.expectFileContains('generate_result.hpp', 'float4 sampleColor(Texture2D<float4> texture, SamplerState sampler_, float2 uv)', 'HLSL helper signature should keep standalone texture and sampler handles.');
        await t.expectFileContains('generate_result.hpp', 'return float4(texture.SampleLevel(sampler_, uv, 0.0f));', 'HLSL helper body should lower sampleLevel to SampleLevel.');
        await t.expectFileNotContains('generate_result.hpp', 'texture->sampleLevel', 'Generated shader artifacts must not keep raw DSL texture arrow calls.');
        await t.expectFileNotContains('generate_result.hpp', 'must return void in the current DSL subset', 'Texture intrinsic calls must not be reported as old behavior-method return violations.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-texture-helper-parameter-intrinsics',
      title: 'Regression: HLSL standalone sampled texture helper parameters lower texture intrinsics',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'texture', 'sampler', 'helpers', 'sample', 'sample-grad', 'gather'],
      fixtureDir: 'hlsl-texture-helper-parameter-intrinsics',
      sourceFile: 'HLSLTextureHelperParameterIntrinsics.hpp',
      description: 'Covers shader helpers that take Texture2D, Texture2DArray, Texture3D, and Sampler directly so HLSL intrinsic lowering works for standalone sampled texture parameters.',
      validates: [
        'HLSL helper signatures can contain standalone Texture2D, Texture2DArray, Texture3D, and Sampler parameters.',
        'Texture2D helper parameters lower sample, sampleGrad, and gatherRed to native HLSL calls.',
        'Texture2DArray helper parameters lower sample and gatherGreen with float3(uv, layer) array coordinates.',
        'Texture3D helper parameters lower sample and sampleGrad to native HLSL calls.'
      ],
      watchouts: [
        'Texture3D gather remains unsupported by design and is not part of this fixture.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Standalone sampled texture intrinsic helper fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'float4 sampleTexture2D(Texture2D<float4> texture, SamplerState sampler_, float2 uv)', 'Texture2D helper signature should keep standalone texture and sampler handles.');
        await t.expectFileContains('generate_result.hpp', 'float4 sampleTexture2DArray(Texture2DArray<float4> texture, SamplerState sampler_, float2 uv, uint layer)', 'Texture2DArray helper signature should keep standalone texture-array and sampler handles.');
        await t.expectFileContains('generate_result.hpp', 'float4 sampleTexture3D(Texture3D<float4> texture, SamplerState sampler_, float3 uvw)', 'Texture3D helper signature should keep standalone texture and sampler handles.');
        await t.expectFileContains('generate_result.hpp', 'texture.Sample(sampler_, uv)', 'Texture2D::sample should lower to HLSL Sample.');
        await t.expectFileContains('generate_result.hpp', 'texture.SampleGrad(sampler_, uv, float2(0.01f, 0.0f), float2(0.0f, 0.01f))', 'Texture2D::sampleGrad should lower to HLSL SampleGrad.');
        await t.expectFileContains('generate_result.hpp', 'texture.GatherRed(sampler_, uv)', 'Texture2D::gatherRed should lower to HLSL GatherRed.');
        await t.expectFileContains('generate_result.hpp', 'texture.Sample(sampler_, float3(uv, layer))', 'Texture2DArray::sample should lower to HLSL Sample with float3 array coordinates.');
        await t.expectFileContains('generate_result.hpp', 'texture.GatherGreen(sampler_, float3(uv, layer))', 'Texture2DArray::gatherGreen should lower to HLSL GatherGreen with float3 array coordinates.');
        await t.expectFileContains('generate_result.hpp', 'texture.Sample(sampler_, uvw)', 'Texture3D::sample should lower to HLSL Sample.');
        await t.expectFileContains('generate_result.hpp', 'texture.SampleGrad(sampler_, uvw, float3(0.01f, 0.0f, 0.0f), float3(0.0f, 0.01f, 0.0f))', 'Texture3D::sampleGrad should lower to HLSL SampleGrad.');
        await t.expectFileNotContains('generate_result.hpp', 'texture->sample', 'Generated shader artifacts must not keep raw DSL texture arrow sample calls.');
        await t.expectFileNotContains('generate_result.hpp', 'texture->sampleGrad', 'Generated shader artifacts must not keep raw DSL texture arrow sampleGrad calls.');
        await t.expectFileNotContains('generate_result.hpp', 'texture->gather', 'Generated shader artifacts must not keep raw DSL texture arrow gather calls.');
        await t.expectFileNotContains('generate_result.hpp', 'must return void in the current DSL subset', 'Texture intrinsic calls must not be reported as old behavior-method return violations.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-matrix-subscript-metal-semantics',
      title: 'Regression: HLSL matrix subscripts preserve Metal/DSL column semantics',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'matrix', 'vulkan', 'metal-semantics'],
      fixtureDir: 'hlsl-matrix-subscript-metal-semantics',
      sourceFile: 'MatrixSubscriptMetalSemantics.hpp',
      description: 'Covers UGL matrix operator[] lowering for HLSL so direct projection-matrix element reads such as `proj[3][2]` keep the same column-vector semantics used by Metal and the DSL.',
      validates: [
        'Generated HLSL transposes matrix values before applying the first matrix subscript.',
        'Nested element reads like `projection[3][2]` no longer silently become HLSL row-major reads.',
        'Nested element writes lower to the transposed HLSL storage coordinate instead of assigning through a temporary transpose result.',
        'Whole-column reads like `projection[3]` stay valid for DXC/SPIR-V generation.'
      ],
      watchouts: [
        'If this regresses, infinite reversed-Z helpers can read `proj[2][3]` and `proj[3][2]` swapped on Vulkan while ordinary matrix multiplication still appears correct.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Matrix subscript semantics fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'projection[2][3] = 0.25f;', 'HLSL should lower `projection[3][2] = ...` to an assignable transposed element write.');
        await t.expectFileContains('generate_result.hpp', 'projection[3][2] = 0.75f;', 'HLSL should lower `projection[2][3] = ...` to an assignable transposed element write.');
        await t.expectFileNotContains('generate_result.hpp', 'transpose(projection)[3][2] =', 'HLSL must not assign through a temporary transpose expression.');
        await t.expectFileNotContains('generate_result.hpp', 'transpose(projection)[2][3] =', 'HLSL must not assign through a temporary transpose expression.');
        await t.expectFileContains('generate_result.hpp', 'float nearPlane = transpose(projection)[3][2];', 'HLSL should lower `projection[3][2]` as a Metal/DSL column read.');
        await t.expectFileContains('generate_result.hpp', 'float wScale = transpose(projection)[2][3];', 'HLSL should lower `projection[2][3]` as a Metal/DSL column read.');
        await t.expectFileContains('generate_result.hpp', 'float4 projectionColumn = transpose(projection)[3];', 'HLSL should lower whole matrix-column reads through transpose before indexing.');
      }
    }),
    createFixtureCase({
      id: 'implicit-this-shader-helper-regression',
      title: 'Diagnostic: compute shader class rejects member helpers with implicit this',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'helpers', 'this', 'shader-class', 'validation'],
      fixtureDir: 'implicit-this-shader-helper-regression',
      sourceFile: 'ImplicitThisShaderHelperRegression.hpp',
      description: 'Confirms compute shader classes reject ordinary member helpers before implicit-this lowering can hide unsupported shader-class scope.',
      validates: [
        'IComputeClass allows constructor and compute only.',
        'The diagnostic names the first illegal member helper instead of continuing into backend lowering.'
      ],
      watchouts: [
        'This fixture used to validate member helper lowering; it now guards the stricter shader-class boundary.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ComputeClass "ImplicitThisShaderHelperRegressionPass" cannot declare helper method "loadValue"', 'Diagnostic output should name the invalid member helper.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into compute().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-helper-parameter',
      title: 'Regression: shader helpers can take BindGroup parameters and forward them across HLSL/MSL',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'helpers', 'hlsl', 'msl'],
      fixtureDir: 'bindgroup-helper-parameter',
      sourceFile: 'BindGroupHelperParameter.hpp',
      description: 'Covers namespace-scoped shader helpers that take `BindGroup<T>` directly so Metal can keep one argument-buffer pointer while HLSL passes one local handle struct backed by flat descriptor globals.',
      validates: [
        'MSL helper signatures accept `BindGroup<T>` as a `const constant T*` argument-buffer pointer.',
        'HLSL helper signatures lower `BindGroup<T>` parameters to one generated handle struct.',
        'Helper-to-helper and entry-to-helper calls preserve one bind-group handle argument on the HLSL path.'
      ],
      watchouts: [
        'Without this regression, helper signatures can look correct while call sites or nested helper forwarding silently drift from the HLSL handle ABI.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Bind-group helper-parameter fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'SampleRed(const constant BindGroupHelperParameterBindGroup* bg', 'MSL helper signatures should lower BindGroup parameters to argument-buffer pointers.');
        await t.expectFileContains('generate_result.hpp', 'struct BindGroupHelperParameterBindGroup_UGLBindGroupHandle', 'HLSL should generate a local bind-group handle struct for helper parameters.');
        await t.expectFileContains('generate_result.hpp', 'void StoreValue(BindGroupHelperParameterBindGroup_UGLBindGroupHandle bg, uint index, uint value)', 'HLSL helper signatures should accept one bind-group handle struct parameter.');
        await t.expectFileContains('generate_result.hpp', 'return uint(bg.texture0.Sample(bg.sampler0, uv).x * 255.0f);', 'HLSL helpers should access resources through handle members.');
        await t.expectFileContains('generate_result.hpp', 'StoreValue(bg, index, SampleRed(bg, float2(0.5f, 0.25f)))', 'HLSL helper-to-helper forwarding should pass the same handle object while keeping same-namespace helper calls unqualified.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHelperParameterBindGroup_UGLBindGroupHandle bindGroup;', 'HLSL entry code should materialize a local bind-group handle.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup.values = bindGroup_values;', 'HLSL entry code should initialize handle members from flat descriptor globals.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHelperParameterHelpers::SampleAndStore(bindGroup, threadID.x);', 'HLSL entry-to-helper calls should pass the local bind-group handle.');
      }
    }),
    createFixtureCase({
      id: 'namespace-reachable-helper-only',
      title: 'Regression: shader artifacts emit only reachable namespace helpers',
      group: 'regressions',
      labels: ['regression', 'namespace', 'reachability', 'hlsl', 'msl'],
      fixtureDir: 'namespace-reachable-helper-only',
      sourceFile: 'NamespaceReachableHelperOnly.hpp',
      description: 'Covers a namespace that contains both a reachable shader helper and an unreachable host-only poison helper, so HLSL/MSL only emit the entry-reachable declaration set.',
      validates: [
        'A reachable namespace helper remains emitted and callable from shader entry code.',
        'Unreachable namespace helpers and host-only records in the same namespace do not enter shader backend source.',
        'The generated shader artifact does not fail just because the namespace contains unused host-only resource declarations.'
      ],
      watchouts: [
        'This protects the WVM-style failure where referencing one namespace helper pulled an entire shader library namespace into the current pass.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Reachable namespace-helper fixture should generate successfully.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        t.recordCheck('MSL shader source should be extractable for namespace reachability checks.', mslSource.length > 0, mslSource.length > 0 ? 'Extracted MSL shader source.' : 'Could not locate the MSL shader source raw string.');
        t.recordCheck('HLSL shader source should be extractable for namespace reachability checks.', hlslSource.length > 0, hlslSource.length > 0 ? 'Extracted HLSL shader source.' : 'Could not locate the HLSL shader source raw string.');
        await t.expectFileContains('generate_result.hpp', 'NamespaceReachableHelperOnlyHelpers::reachableValue(threadID.x)', 'Entry code should still call the reachable namespace helper.');
        t.recordCheck('MSL should emit the reachable namespace helper.', mslSource.includes('uint reachableValue( uint value)') || mslSource.includes('uint reachableValue(uint value)'), 'Expected reachable helper in MSL source.');
        t.recordCheck('HLSL should emit the reachable namespace helper.', hlslSource.includes('uint reachableValue(uint value)'), 'Expected reachable helper in HLSL source.');
        t.recordCheck('MSL should not emit the unreachable poison helper.', !mslSource.includes('unusedHostOnlyPoison'), 'Unexpected unreachable poison helper in MSL source.');
        t.recordCheck('HLSL should not emit the unreachable poison helper.', !hlslSource.includes('unusedHostOnlyPoison'), 'Unexpected unreachable poison helper in HLSL source.');
        t.recordCheck('MSL should not emit the unreachable host-only record.', !mslSource.includes('UnusedHostOnlyPack'), 'Unexpected host-only record in MSL source.');
        t.recordCheck('HLSL should not emit the unreachable host-only record.', !hlslSource.includes('UnusedHostOnlyPack'), 'Unexpected host-only record in HLSL source.');
      }
    }),
    createFixtureCase({
      id: 'namespace-global-helper-dependency',
      title: 'Regression: namespace helpers can depend on later global helper definitions',
      group: 'regressions',
      labels: ['regression', 'namespace', 'helper', 'prototype', 'hlsl', 'msl'],
      fixtureDir: 'namespace-global-helper-dependency',
      sourceFile: 'NamespaceGlobalHelperDependency.hpp',
      description: 'Covers a namespace helper that calls a global helper whose definition appears later in the source file, forcing HLSL/MSL to emit stable helper prototypes before definitions.',
      validates: [
        'Global helper prototypes are emitted before helper definitions.',
        'Namespace helper definitions stay wrapped in their lexical namespace.',
        'A namespaced helper can call a reachable global helper without depending on source-definition order.'
      ],
      watchouts: [
        'Without a prototype pass, this pattern can compile as C++ but fail in generated HLSL/MSL due to undeclared helper calls.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Namespace/global helper dependency fixture should generate successfully.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        const hlslPrototype = hlslSource.indexOf('float3 namespaceGlobalScale(NamespaceGlobalHelperDependencyData data, float scale);');
        const hlslNamespaceHelper = hlslSource.indexOf('float3 buildScaledValue(float baseValue)');
        const hlslHelperCall = hlslSource.indexOf('return namespaceGlobalScale(data, 2.0f);');
        const mslPrototype = Math.max(
          mslSource.indexOf('float3 namespaceGlobalScale( NamespaceGlobalHelperDependencyData data,  float scale);'),
          mslSource.indexOf('float3 namespaceGlobalScale(NamespaceGlobalHelperDependencyData data, float scale);')
        );
        const mslNamespaceHelper = Math.max(
          mslSource.indexOf('float3 buildScaledValue( float baseValue)'),
          mslSource.indexOf('float3 buildScaledValue(float baseValue)')
        );
        const mslHelperCall = mslSource.indexOf('return namespaceGlobalScale(data, 2.0f);');
        t.recordCheck('HLSL should emit a global helper prototype before the namespace helper call.', hlslPrototype >= 0 && hlslNamespaceHelper >= 0 && hlslHelperCall >= 0 && hlslPrototype < hlslHelperCall, 'Expected HLSL prototype before namespace helper call.');
        t.recordCheck('MSL should emit a wave-compatible global helper prototype before the namespace helper call.', mslPrototype >= 0 && mslNamespaceHelper >= 0 && mslHelperCall >= 0 && mslPrototype < mslHelperCall, 'Expected MSL prototype before namespace helper call.');
        t.recordCheck('HLSL should keep the namespace helper wrapped in its lexical namespace.', hlslSource.includes('namespace NamespaceGlobalHelperDependencyHelpers') && hlslNamespaceHelper >= 0, 'Expected HLSL namespace helper wrapper.');
        t.recordCheck('MSL should keep the namespace helper wrapped in its lexical namespace.', mslSource.includes('namespace NamespaceGlobalHelperDependencyHelpers') && mslNamespaceHelper >= 0, 'Expected MSL namespace helper wrapper.');
        await t.expectFileContains('generate_result.hpp', 'NamespaceGlobalHelperDependencyHelpers::buildScaledValue(float(threadID.x))', 'Entry code should call the namespace helper.');
      }
    }),
    createFixtureCase({
      id: 'record-method-global-helper-ordering',
      title: 'Regression: record methods can call global helpers emitted as prototypes',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'msl', 'record', 'helper', 'prototype', 'namespace'],
      fixtureDir: 'record-method-global-helper-ordering',
      sourceFile: 'RecordMethodGlobalHelperOrdering.hpp',
      description: 'Covers a namespace record whose method calls a reachable global helper and a namespace constant, matching WVM Clipmap and Lighting record methods in generated shader code.',
      validates: [
        'HLSL and MSL emit selected record forward declarations before helper prototypes.',
        'HLSL and MSL emit global helper prototypes before full record method bodies that call those helpers.',
        'HLSL and MSL keep namespace constants visible before record method bodies that reference them.',
        'The generated shader still compiles through both MSL and HLSL backend validation.'
      ],
      watchouts: [
        'Without the forward-declaration/prototype phase, shader backends can reject a record method body with an undeclared global helper even though the DSL source is valid C++.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Record-method helper ordering fixture should generate successfully.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        const mslPayloadForward = mslSource.indexOf('struct RecordMethodGlobalHelperOrderingPayload;');
        const mslPrototype = Math.max(
          mslSource.indexOf('float3 recordMethodGlobalScale( RecordMethodGlobalHelperOrderingPayload payload,  float scale);'),
          mslSource.indexOf('float3 recordMethodGlobalScale(RecordMethodGlobalHelperOrderingPayload payload, float scale);')
        );
        const mslAccumulatorForward = mslSource.indexOf('struct Accumulator;');
        const mslAccumulatorDefinition = mslAccumulatorForward >= 0 ? mslSource.indexOf('struct Accumulator', mslAccumulatorForward + 1) : -1;
        const mslHelperCall = Math.max(
          mslSource.indexOf('return recordMethodGlobalScale(payload, AccumulatorScale);'),
          mslSource.indexOf('return recordMethodGlobalScale(payload, RecordMethodGlobalHelperOrderingTypes::AccumulatorScale);')
        );
        const mslScaleDefinition = mslSource.search(/AccumulatorScale\s*=\s*2\.0/);
        const hlslPayloadForward = hlslSource.indexOf('struct RecordMethodGlobalHelperOrderingPayload;');
        const hlslPrototype = hlslSource.indexOf('float3 recordMethodGlobalScale(RecordMethodGlobalHelperOrderingPayload payload, float scale);');
        const hlslAccumulatorForward = hlslSource.indexOf('struct RecordMethodGlobalHelperOrderingTypes_Accumulator;');
        const hlslAccumulatorDefinition = hlslAccumulatorForward >= 0 ? hlslSource.indexOf('struct RecordMethodGlobalHelperOrderingTypes_Accumulator', hlslAccumulatorForward + 1) : -1;
        const hlslHelperCall = Math.max(
          hlslSource.indexOf('return recordMethodGlobalScale(payload, AccumulatorScale);'),
          hlslSource.indexOf('return recordMethodGlobalScale(payload, RecordMethodGlobalHelperOrderingTypes::AccumulatorScale);')
        );
        const hlslScaleDefinition = hlslSource.search(/AccumulatorScale\s*=\s*2\.0/);
        t.recordCheck('MSL should forward-declare payload records before helper prototypes.', mslPayloadForward >= 0 && mslPrototype >= 0 && mslPayloadForward < mslPrototype, 'Expected MSL payload forward declaration before the global helper prototype.');
        t.recordCheck('MSL should forward-declare namespace records before their full definitions.', mslAccumulatorForward >= 0 && mslAccumulatorDefinition >= 0 && mslAccumulatorForward < mslAccumulatorDefinition, 'Expected MSL namespace record forward declaration before its definition.');
        t.recordCheck('MSL should emit the global helper prototype before the record method call.', mslPrototype >= 0 && mslHelperCall >= 0 && mslPrototype < mslHelperCall, 'Expected MSL global helper prototype before the record method call.');
        t.recordCheck('MSL should emit namespace constants before record methods that reference them.', mslScaleDefinition >= 0 && mslHelperCall >= 0 && mslScaleDefinition < mslHelperCall, 'Expected MSL namespace constant definition before the record method call.');
        t.recordCheck('HLSL should forward-declare payload records before helper prototypes.', hlslPayloadForward >= 0 && hlslPrototype >= 0 && hlslPayloadForward < hlslPrototype, 'Expected HLSL payload forward declaration before the global helper prototype.');
        t.recordCheck('HLSL should forward-declare namespace records before their full definitions.', hlslAccumulatorForward >= 0 && hlslAccumulatorDefinition >= 0 && hlslAccumulatorForward < hlslAccumulatorDefinition, 'Expected HLSL namespace record forward declaration before its definition.');
        t.recordCheck('HLSL should emit the global helper prototype before the record method call.', hlslPrototype >= 0 && hlslHelperCall >= 0 && hlslPrototype < hlslHelperCall, 'Expected HLSL global helper prototype before the record method call.');
        t.recordCheck('HLSL should emit namespace constants before record methods that reference them.', hlslScaleDefinition >= 0 && hlslHelperCall >= 0 && hlslScaleDefinition < hlslHelperCall, 'Expected HLSL namespace constant definition before the record method call.');
        t.recordCheck('HLSL should use the same flattened namespace record name for forward declaration and definition.', hlslAccumulatorForward >= 0 && hlslAccumulatorDefinition >= 0, 'Expected HLSL flattened namespace record forward declaration and definition.');
        t.recordCheck('MSL should keep the namespace record wrapper.', mslSource.includes('namespace RecordMethodGlobalHelperOrderingTypes') && mslAccumulatorForward >= 0, 'Expected MSL namespace record wrapper.');
        await t.expectFileContains('generate_result.hpp', 'RecordMethodGlobalHelperOrderingTypes::Accumulator accumulator;', 'Entry code should instantiate the namespace record.');
      }
    }),
    createFixtureCase({
      id: 'record-field-default-namespace-constant',
      title: 'Regression: record field defaults collect namespace constants',
      group: 'regressions',
      labels: ['regression', 'record', 'namespace', 'constant', 'reachability', 'hlsl', 'msl'],
      fixtureDir: 'record-field-default-namespace-constant',
      sourceFile: 'RecordFieldDefaultNamespaceConstant.hpp',
      description: 'Covers a shader record whose field default initializer references a namespace-scoped constant, matching WVM SpdHiZParams reduceMode defaults.',
      validates: [
        'Field default initializers are scanned during record dependency collection.',
        'Namespace constants referenced only from record field defaults are emitted into HLSL and MSL shader artifacts.',
        'The generated Metal shader does not fail with an undeclared namespace constant at runtime compile.'
      ],
      watchouts: [
        'Without field-initializer reachability, a record definition can reference a namespace constant that was never emitted into the shader source.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Record field default namespace constant fixture should generate successfully.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        t.recordCheck('HLSL should emit the namespace constant used only by the record field default.', hlslSource.includes('DefaultMode = 7u'), 'Expected DefaultMode in HLSL source.');
        t.recordCheck('MSL should emit the namespace constant used only by the record field default.', mslSource.includes('DefaultMode = 7u'), 'Expected DefaultMode in MSL source.');
        t.recordCheck('MSL should preserve the record field default reference.', mslSource.includes('mode = RecordFieldDefaultNamespaceConstantValues::DefaultMode'), 'Expected MSL record field default to reference DefaultMode.');
      }
    }),
    createFixtureCase({
      id: 'init-list-helper-dependency',
      title: 'Regression: initializer-list helper calls are entry-reachable',
      group: 'regressions',
      labels: ['regression', 'reachability', 'initializer-list', 'helper', 'hlsl', 'msl'],
      fixtureDir: 'init-list-helper-dependency',
      sourceFile: 'InitListHelperDependency.hpp',
      description: 'Covers helper calls nested inside array initializer lists so ShaderReferenceVisitor traverses `InitListExpr` and collects those callees before backend codegen.',
      validates: [
        'Helper calls inside initializer-list elements are treated as shader-entry reachable.',
        'HLSL and MSL emit the helper definitions used by initializer-list expressions.',
        'The generated shader backend source does not fail with undeclared helper calls from array initializers.'
      ],
      watchouts: [
        'This mirrors WVM tessellation helpers that build arrays with calls like `getClipSpacePos(...)` inside initializer lists.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Initializer-list helper dependency fixture should generate successfully.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        t.recordCheck('HLSL should emit the clip helper used inside the initializer list.', hlslSource.includes('initListMakeClip'), 'Expected initListMakeClip in HLSL source.');
        t.recordCheck('HLSL should emit the screen helper used inside the nested initializer list.', hlslSource.includes('initListMakeScreen'), 'Expected initListMakeScreen in HLSL source.');
        t.recordCheck('MSL should emit the clip helper used inside the initializer list.', mslSource.includes('initListMakeClip'), 'Expected initListMakeClip in MSL source.');
        t.recordCheck('MSL should emit the screen helper used inside the nested initializer list.', mslSource.includes('initListMakeScreen'), 'Expected initListMakeScreen in MSL source.');
        t.recordCheck('HLSL should not leave unsupported AST placeholders for initializer lists.', !hlslSource.includes('does not support expression') && !hlslSource.includes('does not support statement'), 'Unexpected unsupported placeholder in HLSL source.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-resource-element-method-record-ordering',
      title: 'Regression: HLSL resource element method records are ordered before resource declarations',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'resource', 'structured-buffer', 'record-methods', 'ordering'],
      fixtureDir: 'hlsl-resource-element-method-record-ordering',
      sourceFile: 'HLSLResourceElementMethodRecordOrdering.hpp',
      description: 'Covers a `StructuredBuffer<T>` element record that has methods and helper dependencies, forcing HLSL to emit scalar helper prototypes, the element record, resource declarations, and record-typed helper signatures in a valid order.',
      validates: [
        'Resource element records with method bodies are defined before HLSL handle structs and resource declarations use them.',
        'Scalar helper prototypes needed by resource element record methods are emitted before those method bodies.',
        'Helpers whose signatures mention the resource element record are not emitted before the record exists.'
      ],
      watchouts: [
        'This mirrors WVM structured-buffer element records such as tessellation key data.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Resource element method-record ordering fixture should generate successfully.');
        const { hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        const helperPrototype = hlslSource.indexOf('uint incrementKey(uint value);');
        const recordDefinition = hlslSource.indexOf('struct HLSLResourceElementMethodRecordOrderingTypes_KeyData');
        const recordMethodCall = hlslSource.indexOf('return HLSLResourceElementMethodRecordOrderingTypes::incrementKey(key);');
        const processPrototype = hlslSource.indexOf('HLSLResourceElementMethodRecordOrderingTypes_KeyData processKey(HLSLResourceElementMethodRecordOrderingTypes_KeyData data, uint delta);');
        const resourceDeclaration = hlslSource.indexOf('StructuredBuffer<HLSLResourceElementMethodRecordOrderingTypes_KeyData> bindGroup_inputKeys');
        t.recordCheck('HLSL should emit scalar helper prototypes before resource element record methods.', helperPrototype >= 0 && recordDefinition >= 0 && recordMethodCall >= 0 && helperPrototype < recordMethodCall, 'Expected incrementKey prototype before KeyData method body.');
        t.recordCheck('HLSL should emit record-typed helper prototypes only after the record definition.', recordDefinition >= 0 && processPrototype >= 0 && recordDefinition < processPrototype, 'Expected processKey prototype after KeyData definition.');
        t.recordCheck('HLSL should emit resource element record definitions before structured-buffer declarations.', recordDefinition >= 0 && resourceDeclaration >= 0 && recordDefinition < resourceDeclaration, 'Expected KeyData definition before StructuredBuffer declaration.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-namespace-source-order',
      title: 'Regression: HLSL namespace helpers are prototyped before record methods',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'namespace', 'ordering'],
      fixtureDir: 'hlsl-namespace-source-order',
      sourceFile: 'NamespaceSourceOrder.hpp',
      description: 'Covers namespace-scoped helpers declared before a record with methods that call them, so HLSL emits a visible helper prototype before the flattened record method body.',
      validates: [
        'Namespace free functions remain inside the original namespace.',
        'A namespace helper prototype is emitted before a namespace record method body that calls it.',
        'Record methods use namespace-qualified helper calls that DXC resolves inside struct method bodies.'
      ],
      watchouts: [
        'This guards the exact pattern used by Clipmap helpers where a struct method calls a same-namespace free function declared earlier in the DSL.'
      ],
      verify: async (t) => {
        const { hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        await t.expectArtifactExists('generate_result.hpp', 'Namespace source-order fixture should generate successfully.');
        const hlslNamespaceFunction = hlslSource.indexOf('namespace HlslNamespaceSourceOrder');
        const hlslPrototype = hlslSource.indexOf('uint AddOne(uint value);');
        const hlslHelperDefinition = hlslSource.indexOf('uint AddOne(uint value)', hlslPrototype >= 0 ? hlslPrototype + 1 : 0);
        const hlslRecordForward = hlslSource.indexOf('struct HlslNamespaceSourceOrder_Accumulator;');
        const hlslRecord = hlslRecordForward >= 0 ? hlslSource.indexOf('struct HlslNamespaceSourceOrder_Accumulator', hlslRecordForward + 1) : -1;
        const hlslMethodCall = hlslSource.indexOf('return HlslNamespaceSourceOrder::AddOne(value);');
        t.recordCheck('HLSL should emit the namespace helper prototype before the flattened record method that calls it.', hlslNamespaceFunction >= 0 && hlslPrototype >= 0 && hlslRecord >= 0 && hlslMethodCall >= 0 && hlslNamespaceFunction < hlslPrototype && hlslPrototype < hlslRecord && hlslRecord < hlslMethodCall, 'Expected HLSL namespace helper prototype before flattened record method.');
        t.recordCheck('HLSL should still emit the namespace helper definition.', hlslHelperDefinition >= 0, 'Expected HLSL namespace helper definition.');
        await t.expectFileContainsInOrder('generate_result.hpp', [
          'namespace HlslNamespaceSourceOrder',
          'uint AddOne(uint value);',
          'return HlslNamespaceSourceOrder::AddOne(value);'
        ], 'Generated output should keep the helper prototype before the record method that calls it.');
        t.recordCheck('HLSL flattened record methods should namespace-qualify same-namespace free function calls.', hlslSource.includes('return HlslNamespaceSourceOrder::AddOne(value);'), 'Expected qualified helper call in HLSL record method.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-helper-parameter-multi',
      title: 'Regression: multiple BindGroup helper parameters keep expansion order stable',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'helpers', 'abi', 'hlsl', 'msl'],
      fixtureDir: 'bindgroup-helper-parameter-multi',
      sourceFile: 'BindGroupHelperParameterMulti.hpp',
      description: 'Covers helper functions that take more than one `BindGroup<T>` parameter with a scalar in between, so HLSL keeps one handle struct per bind group while Metal keeps the original pointer-shaped helper ABI.',
      validates: [
        'MSL helper signatures preserve both bind-group pointer parameters around the scalar parameter.',
        'HLSL helper signatures preserve both bind-group handle parameters around the scalar parameter.',
        'Nested helper calls and entry calls keep the same multi-bind-group handle order.'
      ],
      watchouts: [
        'Without this regression, a backend can still compile but silently drift helper ABI ordering once more than one bind group handle participates in the same call.'
      ],
      verify: async (t) => {
        const wrapperType = 'BindGroupHelperParameterMultiControlBindGroup_params_UniformValue';
        const hlslComputeSignature = 'uint ComputeValue(BindGroupHelperParameterMultiSampledBindGroup_UGLBindGroupHandle sampledBg, uint index, BindGroupHelperParameterMultiControlBindGroup_UGLBindGroupHandle controlBg)';
        await t.expectArtifactExists('generate_result.hpp', 'Multi bind-group helper-parameter fixture should generate successfully.');
        await t.expectFileNotContains('generate_result.hpp', `struct ${wrapperType};`, 'HLSL multi-bind-group helpers should not rely on an incomplete forward declaration for the uniform wrapper.');
        await t.expectFileOccurrenceCount('generate_result.hpp', `struct ${wrapperType}\n`, 1, 'HLSL multi-bind-group helpers should share one canonical uniform wrapper definition.');
        await t.expectFileContainsInOrder('generate_result.hpp', [
          `struct ${wrapperType}\n`,
          'struct BindGroupHelperParameterMultiSampledBindGroup_UGLBindGroupHandle',
          'struct BindGroupHelperParameterMultiControlBindGroup_UGLBindGroupHandle',
          'namespace BindGroupHelperParameterMultiHelpers',
          hlslComputeSignature
        ], 'HLSL should define the uniform wrapper and handle structs before multi-bind-group namespace helper signatures.');
        await t.expectFileContains('generate_result.hpp', 'ComputeValue(const constant BindGroupHelperParameterMultiSampledBindGroup* sampledBg', 'MSL helper signatures should keep the first bind-group helper parameter as an argument-buffer pointer.');
        await t.expectFileContains('generate_result.hpp', 'uint index, const constant BindGroupHelperParameterMultiControlBindGroup* controlBg)', 'MSL helper signatures should keep the scalar parameter between the two bind-group pointers.');
        await t.expectFileContains('generate_result.hpp', hlslComputeSignature, 'HLSL helper signatures should preserve both bind-group handle parameters in left-to-right order.');
        await t.expectFileContains('generate_result.hpp', 'controlBg.values[index] = ComputeValue(sampledBg, index, controlBg);', 'Nested HLSL helper calls should keep the same multi-bind-group handle order with same-namespace lookup.');
        await t.expectFileContains('generate_result.hpp', 'sampledBindGroup.texture0 = sampledBindGroup_texture0;', 'HLSL entry code should materialize the first bind-group handle from flat globals.');
        await t.expectFileContains('generate_result.hpp', 'controlBindGroup.values = controlBindGroup_values;', 'HLSL entry code should materialize the second bind-group handle from flat globals.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHelperParameterMultiHelpers::WriteValue(sampledBindGroup, threadID.x, controlBindGroup);', 'HLSL entry calls should pass both shader-class bind-group handles in the helper ABI order.');
      }
    }),
    createFixtureCase({
      id: 'structured-buffer-readonly',
      title: 'Regression: read-only StructuredBuffer binds as t-register storage with read-only host access',
      group: 'regressions',
      labels: ['regression', 'compute', 'structured-buffer', 'readonly'],
      fixtureDir: 'structured-buffer-readonly',
      sourceFile: 'StructuredBufferReadOnly.hpp',
      description: 'Covers the new read-only StructuredBuffer path so the compiler emits a read-only storage-buffer binding model instead of silently treating every structured buffer as RW.',
      validates: [
        'Read-only `StructuredBuffer<T>` bind-group fields lower to HLSL `StructuredBuffer<T>` declarations on `t` registers.',
        'Generated host bind-group layout marks read-only structured buffers with `StorageBufferAccess::ReadOnly`.',
        'The compute codegen path still succeeds for shaders that only read from a structured buffer.'
      ],
      watchouts: [
        'This is the key regression guard for the StructuredBuffer/RWStructuredBuffer split. Without it, future refactors can accidentally collapse the two back into read-write bindings.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Read-only StructuredBuffer fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] StructuredBuffer<unsigned int> bindGroup_values : register(t0, space0);', 'Read-only StructuredBuffer lowering should emit explicit Vulkan binding coordinates.');
        await t.expectFileContains('generate_result.hpp', 'StructuredBuffer<unsigned int> bindGroup_values : register(t0, space0);', 'Read-only structured buffers should lower to HLSL StructuredBuffer on t-registers.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly', 'Host bind-group layout should preserve read-only storage-buffer access.');
      }
    }),
    createFixtureCase({
      id: 'structured-buffer-atomic-load-readonly',
      title: 'Regression: read-only StructuredBuffer atomicLoad keeps const Metal overloads',
      group: 'regressions',
      labels: ['regression', 'compute', 'structured-buffer', 'atomic', 'msl'],
      fixtureDir: 'structured-buffer-atomic-load-readonly',
      sourceFile: 'StructuredBufferAtomicLoadReadOnly.hpp',
      description: 'Covers the Metal path where a read-only StructuredBuffer plain uint field is only accessed through atomicLoad, so UGLC infers the atomic layout and the embedded MSL prelude keeps const-qualified device/threadgroup atomicLoad overloads.',
      validates: [
        'Read-only `StructuredBuffer<T>` with atomicLoad on plain fields still generates successfully.',
        'Generated HLSL keeps the bind-group resource on a read-only `t` register.',
        'Generated MSL prelude exposes const-qualified `device` and `threadgroup` atomicLoad overloads.'
      ],
      watchouts: [
        'Without this, a prelude refactor can silently make `atomicLoad` require mutable atomics again and break valid read-only StructuredBuffer shaders in Metal.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Read-only StructuredBuffer atomic-load fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(0, 0)]] StructuredBuffer<StructuredBufferAtomicLoadInputCounter> tessBindGroup_inputCounter : register(t0, space0);', 'Read-only atomic StructuredBuffer lowering should emit explicit Vulkan binding coordinates.');
        await t.expectFileContains('generate_result.hpp', 'StructuredBuffer<StructuredBufferAtomicLoadInputCounter> tessBindGroup_inputCounter : register(t0, space0);', 'Read-only atomic StructuredBuffer should stay on a t-register in HLSL.');
        await t.expectFileContains('generate_result.hpp', 'inline T atomicLoad(const device atomic<T>& a)', 'MSL prelude should accept const device atomics for atomicLoad.');
        await t.expectFileContains('generate_result.hpp', 'inline T atomicLoad(const threadgroup atomic<T>& a)', 'MSL prelude should accept const threadgroup atomics for atomicLoad.');
      }
    }),
    createFixtureCase({
      id: 'atomic-auto-layout',
      title: 'Regression: atomic use infers Metal atomic layout from plain DSL types',
      group: 'regressions',
      labels: ['regression', 'compute', 'structured-buffer', 'atomic', 'msl', 'hlsl'],
      fixtureDir: 'atomic-auto-layout',
      sourceFile: 'AtomicAutoLayout.hpp',
      description: 'Covers the HLSL-style DSL path where shader code calls atomic builtins on ordinary uint lvalues and UGLC infers which Metal resources require atomic<T> layout.',
      validates: [
        'Plain `uint` structured-buffer elements can be passed to atomic builtins without writing `Atomic<uint>` in DSL declarations.',
        'MSL lowers only atomic targets to `atomic<unsigned int>` while leaving ordinary fields plain.',
        'HLSL remains natural and does not expose Metal atomic layout requirements.'
      ],
      watchouts: [
        'This protects the DSL from reintroducing user-visible Atomic<T> resource declarations just to satisfy Metal layout spelling.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Atomic auto-layout fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'device atomic<unsigned int>* counters [[id(0)]]', 'MSL should infer atomic layout for scalar RWStructuredBuffer elements used by atomic operations.');
        await t.expectFileContains('generate_result.hpp', 'device atomic<unsigned int>* masks [[id(3)]]', 'MSL should infer atomic layout for scalar RWStructuredBuffer elements used only by atomicAnd.');
        await t.expectFileContains('generate_result.hpp', 'atomic<unsigned int> keyCounter;', 'MSL should infer atomic layout for struct fields used by atomic operations.');
        await t.expectFileContains('generate_result.hpp', 'atomic<unsigned int> maskCounter;', 'MSL should infer atomic layout for struct fields used only by atomicAnd.');
        await t.expectFileContains('generate_result.hpp', 'uint ordinaryCounter;', 'MSL should leave non-atomic struct fields plain.');
        await t.expectFileContains('generate_result.hpp', 'threadgroup atomic<unsigned int> scratch[1];', 'MSL should infer atomic layout for plain groupshared scalar arrays used by atomic operations.');
        await t.expectFileContains('generate_result.hpp', 'threadgroup atomic<unsigned int> andScratch[1];', 'MSL should infer atomic layout for groupshared scalar arrays used only by atomicAnd.');
        await t.expectFileContains('generate_result.hpp', 'atomic_fetch_and_explicit(&a, (T)b, memory_order_relaxed)', 'MSL prelude should expose atomicAnd through Metal atomic fetch-and.');
        await t.expectFileContains('generate_result.hpp', 'RWStructuredBuffer<unsigned int> bindGroup_counters : register(u0, space0);', 'HLSL should keep the scalar structured buffer in the natural RWStructuredBuffer form.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedAnd(bindGroup.masks[threadID.x]', 'HLSL should lower atomicAnd return-value calls to InterlockedAnd through the local bind-group handle.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedAnd(__uglc_andScratch_groupshared', 'HLSL should lower groupshared atomicAnd calls to InterlockedAnd on the backing storage.');
        await t.expectFileNotContains('generate_result.hpp', 'Atomic<unsigned int>', 'Generated shader code should not require a user-visible Atomic<T> wrapper for the auto-layout fixture.');
      }
    }),
    createFixtureCase({
      id: 'atomic-ordinary-access',
      title: 'Regression: ordinary accesses to inferred Metal atomics stay non-atomic',
      group: 'regressions',
      labels: ['regression', 'compute', 'atomic', 'msl', 'hlsl'],
      fixtureDir: 'atomic-ordinary-access',
      sourceFile: 'AtomicOrdinaryAccess.hpp',
      description: 'Covers DSL code that uses explicit atomic builtins and ordinary reads/writes on the same uint storage so MSL bridges plain access through relaxed load/store without changing HLSL semantics.',
      validates: [
        'MSL still infers atomic storage for scalar buffers, structured fields, and groupshared arrays used by atomic builtins.',
        'Ordinary MSL reads and writes to inferred atomic storage lower through atomicLoad/atomicStore instead of fetch-add operations.',
        'HLSL keeps ordinary compound assignment as a plain read-modify-write and only lowers explicit atomicAdd calls to InterlockedAdd.'
      ],
      watchouts: [
        'Without this regression, Metal-only atomic layout can either fail to compile on ordinary access or accidentally make HLSL-plain operations atomic.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Atomic ordinary-access fixture should generate successfully.');

        const content = await t.readArtifact('generate_result.hpp');
        const mslOpenMarker = '__UGL__Global__MSLHeader + R"(';
        const mslStart = content.indexOf(mslOpenMarker);
        const mslContentStart = mslStart === -1 ? -1 : mslStart + mslOpenMarker.length;
        const mslEnd = mslContentStart === -1 ? -1 : content.indexOf(')",', mslContentStart);
        const hlslStart = mslEnd === -1 ? -1 : content.indexOf('R"(', mslEnd);
        const hlslContentStart = hlslStart === -1 ? -1 : hlslStart + 'R"('.length;
        const hlslEnd = hlslContentStart === -1 ? -1 : content.indexOf(')",', hlslContentStart);
        const mslSource = mslContentStart === -1 || mslEnd === -1 ? '' : content.slice(mslContentStart, mslEnd);
        const hlslSource = hlslContentStart === -1 || hlslEnd === -1 ? '' : content.slice(hlslContentStart, hlslEnd);
        const mslAtomicAddCount = (mslSource.match(/atomicAdd\(/g) ?? []).length;
        const hlslInterlockedAddCount = (hlslSource.match(/InterlockedAdd\((bindGroup\.counters|bindGroup\.states)/g) ?? []).length;

        t.recordCheck('MSL shader source should be extractable for atomic ordinary-access checks.', mslSource.length > 0, mslSource.length > 0 ? 'Extracted MSL shader source.' : 'Could not locate the MSL shader source raw string.');
        t.recordCheck('HLSL shader source should be extractable for atomic ordinary-access checks.', hlslSource.length > 0, hlslSource.length > 0 ? 'Extracted HLSL shader source.' : 'Could not locate the HLSL shader source raw string.');
        t.recordCheck('MSL should infer an atomic scalar buffer binding.', mslSource.includes('device atomic<unsigned int>* counters [[id(0)]]'), 'Expected atomic scalar buffer binding in emitted MSL.');
        t.recordCheck('MSL should infer an atomic struct field.', mslSource.includes('atomic<unsigned int> counter;'), 'Expected atomic struct field in emitted MSL.');
        t.recordCheck('MSL should infer atomic groupshared array storage.', mslSource.includes('threadgroup atomic<unsigned int> scratch[1];'), 'Expected atomic groupshared array in emitted MSL.');
        t.recordCheck('MSL ordinary compound assignments should use relaxed load/store targets.', mslSource.includes('atomicLoad(*__uglc_atomic_plain_target') && mslSource.includes('atomicStore(*__uglc_atomic_plain_target'), 'Expected synthesized load/store block for ordinary compound access.');
        t.recordCheck('MSL should only keep the two explicit atomicAdd calls.', mslAtomicAddCount === 2, `Expected 2 explicit atomicAdd calls in MSL, found ${mslAtomicAddCount}.`);
        t.recordCheck('HLSL ordinary scalar buffer += should remain plain.', hlslSource.includes('bindGroup.counters[threadID.x] += 1u;'), 'Expected plain HLSL compound assignment on the scalar buffer.');
        t.recordCheck('HLSL ordinary struct-field += should remain plain.', hlslSource.includes('bindGroup.states[0].counter += counterValue;'), 'Expected plain HLSL compound assignment on the struct field.');
        t.recordCheck('HLSL should only lower the two explicit atomicAdd calls.', hlslInterlockedAddCount === 2, `Expected 2 InterlockedAdd calls in HLSL, found ${hlslInterlockedAddCount}.`);
      }
    }),
    createFixtureCase({
      id: 'msl-atomic-structured-buffer-helper-parameter',
      title: 'Regression: MSL atomic structured-buffer helper parameters keep atomic pointer types',
      group: 'regressions',
      labels: ['regression', 'compute', 'structured-buffer', 'atomic', 'helpers', 'msl'],
      fixtureDir: 'msl-atomic-structured-buffer-helper-parameter',
      sourceFile: 'MSLAtomicStructuredBufferHelperParameter.hpp',
      description: 'Covers namespace helpers that take RWStructuredBuffer<uint> parameters and use those parameters as atomic targets, matching downstream visibility-buffer append helpers.',
      validates: [
        'MSL helper signatures lower atomic RWStructuredBuffer<uint> parameters to device atomic<unsigned int>*.',
        'MSL argument-buffer fields passed into those helpers are also inferred as atomic pointers.',
        'Non-atomic RWStructuredBuffer<uint> resources remain plain device unsigned int*.',
        'HLSL stays on the natural RWStructuredBuffer<unsigned int> ABI.'
      ],
      watchouts: [
        'Without this, Metal helper signatures accept device unsigned int* and then fail when atomicAdd/atomicMax require device atomic<unsigned int>&.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'MSL atomic helper-parameter fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'device unsigned int* payloads [[id(0)]]', 'MSL should leave non-atomic RWStructuredBuffer resources plain.');
        await t.expectFileContains('generate_result.hpp', 'device atomic<unsigned int>* counter [[id(1)]]', 'MSL should infer atomic layout for the counter bind-group field passed into the helper.');
        await t.expectFileContains('generate_result.hpp', 'device atomic<unsigned int>* overflow [[id(2)]]', 'MSL should infer atomic layout for the overflow bind-group field passed into the helper.');
        await t.expectFileContains('generate_result.hpp', 'allocateIndex(device atomic<unsigned int>* counter, device atomic<unsigned int>* overflow,  uint capacity)', 'MSL helper parameters should preserve atomic pointer types.');
        await t.expectFileContains('generate_result.hpp', 'RWStructuredBuffer<unsigned int> counter, RWStructuredBuffer<unsigned int> overflow, uint capacity)', 'HLSL helper parameters should keep RWStructuredBuffer ABI.');
        await t.expectFileNotContains('generate_result.hpp', 'allocateIndex(device unsigned int* counter, device unsigned int* overflow', 'MSL helper parameters must not lose atomic pointer types.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-binding31',
      title: 'Regression: bind-group resources support explicit [[Binding31]]',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'binding', 'abi'],
      fixtureDir: 'bindgroup-binding31',
      sourceFile: 'BindGroupBinding31.hpp',
      description: 'Locks in the widened bind-group resource binding ABI so higher resource indices do not silently fall back to local macro hacks in downstream DSL codebases.',
      validates: [
        'UGLC accepts bind-group resources annotated with `[[Binding31]]`.',
        'Generated host layout entries keep the explicit binding index.',
        'Generated shader source keeps the same explicit resource id.'
      ],
      watchouts: [
        'This is specifically the compatibility line needed before projects migrate away from ad-hoc local `Binding8+` macro definitions.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Binding31 fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].binding = 31', 'Bind-group layout entries should preserve [[Binding31]].');
        await t.expectFileContains('generate_result.hpp', 'bindgroupEntry[0].binding = 31', 'Bind-group resource entries should preserve [[Binding31]].');
        await t.expectFileContains('generate_result.hpp', 'values [[id(31)]]', 'Generated shader bind-group parameters should use the explicit [[Binding31]] id.');
      }
    }),
    createFixtureCase({
      id: 'resource-usage-reset',
      title: 'Regression: resource usage scratch state resets between translated resources',
      group: 'regressions',
      labels: ['regression', 'resources', 'buffer', 'texture', 'usage'],
      fixtureDir: 'resource-usage-reset',
      sourceFile: 'ResourceUsageReset.hpp',
      description: 'Covers the host-side resource descriptor and explicit resource-free translation paths with multiple buffers and textures that intentionally use different usage masks. This protects against stale scratch usage state bleeding from one translated resource declaration into the next.',
      validates: [
        'Each translated buffer keeps only its own declared usage flags.',
        'Each translated texture keeps only its own declared usage flags.',
        'Device-level freeBuffer/freeTexture DSL calls lower to the generated host DeviceProxy API.',
        'Buffer and texture descriptor generation remain stable for a minimal AbstractRenderer fixture.'
      ],
      watchouts: [
        'Without this regression, later resource declarations can silently inherit usage bits from earlier ones and create invalid RHI descriptors.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated resource-free host header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Resource usage reset fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '.label="VertexBuffer", .usage = GVM::RHI::BufferUsage::Vertex|GVM::RHI::BufferUsage::CopyDst, .size =', 'The first buffer should keep its declared vertex/copy-dst usage mask.');
        await t.expectFileContains('generate_result.hpp', '.label="UniformBuffer", .usage = GVM::RHI::BufferUsage::Uniform, .size =', 'A later buffer should not inherit usage bits from an earlier buffer.');
        await t.expectFileContains('generate_result.hpp', '.label="SampledTexture", .usage = GVM::RHI::TextureUsage::TextureBinding|GVM::RHI::TextureUsage::CopyDst, .dimension =', 'The first texture should keep its declared sampled/copy-dst usage mask.');
        await t.expectFileContains('generate_result.hpp', '.label="StorageTexture", .usage = GVM::RHI::TextureUsage::StorageBinding, .dimension =', 'A later texture should not inherit sampled usage bits from an earlier texture.');
        await t.expectFileContains('generate_result.hpp', 'device->freeBuffer(vertexBuffer)', 'Generated host code should preserve explicit vertex-buffer free calls.');
        await t.expectFileContains('generate_result.hpp', 'device->freeBuffer(uniformBuffer)', 'Generated host code should preserve explicit uniform-buffer free calls.');
        await t.expectFileContains('generate_result.hpp', 'device->freeTexture(sampledTexture)', 'Generated host code should preserve explicit sampled-texture free calls.');
        await t.expectFileContains('generate_result.hpp', 'device->freeTexture(storageTexture)', 'Generated host code should preserve explicit storage-texture free calls.');
      }
    }),
    createFixtureCase({
      id: 'timestamp-profiler-node',
      title: 'Regression: timestamp profiler scopes lower through queue pass nodes',
      group: 'regressions',
      labels: ['regression', 'timestamp', 'rhi', 'queue', 'compute', 'node'],
      fixtureDir: 'timestamp-profiler-node',
      sourceFile: 'TimestampProfilerNode.hpp',
      description: 'Covers the host-side DSL path for creating a GPU timestamp profiler, assigning a pass scope to a compute queue node, resolving it explicitly, and keeping the generated host header buildable.',
      validates: [
        'UGLC preserves the typed timestamp profiler field as the GVM RHI helper.',
        'DeviceProxy timestamp-profiler creation is reachable from DSL host code.',
        'Queue pass overloads preserve the explicit timestamp scope and resolve call.'
      ],
      watchouts: [
        'This protects the no-default-overhead policy: timestamp code only appears when the DSL source explicitly creates and passes a profiler scope.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated timestamp profiler host header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Timestamp profiler node fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::GpuTimestampFrameProfiler frameProfiler', 'Timestamp profiler fields should lower to the RHI helper type.');
        await t.expectFileContains('generate_result.hpp', 'frameProfiler = device->createTimestampFrameProfiler(2u, "TimestampNodeProfiler")', 'DeviceProxy should create and initialize the timestamp profiler explicitly.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::GpuTimestampFrameProfiler::Scope scope = frameProfiler.writePass("TimestampProfilerCompute")', 'Generated host code should allocate a pass-level timestamp scope.');
        await t.expectFileContains('generate_result.hpp', 'queue->computePass("TimestampProfilerCompute", scope, timestampPass->run(64u, 1u, 1u))', 'Queue compute pass should receive the timestamp scope as an explicit argument.');
        await t.expectFileContains('generate_result.hpp', 'queue->resolveTimestampProfiler(frameProfiler)', 'Timestamp resolve should stay explicit after the measured pass.');
      }
    }),
    createFixtureCase({
      id: 'render-overload-entry-selection',
      title: 'Regression: render entry selection remains stable with namespace helpers',
      group: 'regressions',
      labels: ['regression', 'render', 'overload', 'entry-selection'],
      fixtureDir: 'render-overload-entry-selection',
      sourceFile: 'RenderOverloadEntrySelection.hpp',
      description: 'Protects the overload-aware lookup path for render classes while keeping helper logic outside the shader class.',
      validates: [
        'The annotated constructor overload is still selected for pipeline setup.',
        'The real vertex/fragment entry overloads still drive `vertexMain` and `fragmentMain`.',
        'Namespace helper calls remain the legal way to share shader logic.'
      ],
      watchouts: [
        'Shader-class helper overloads are now rejected, so this fixture must not reintroduce same-class helpers.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Render overload selection fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'this->constructorMarker = 17u', 'The annotated constructor overload should remain the pipeline setup body.');
        await t.expectFileNotContains('generate_result.hpp', 'void create(uint wrongValue)', 'Ordinary create overload helpers should not be part of shader classes.');
        await t.expectFileContains('generate_result.hpp', 'vertexMain(uint vid [[vertex_id]], RenderOverloadVertexInput inputValue [[stage_in]]', 'MSL should select the real vertex entry overload.');
        await t.expectFileContains('generate_result.hpp', 'fragmentMain(RenderOverloadVertexOutput inputValue[[stage_in]]', 'MSL should select the real fragment entry overload.');
        await t.expectFileContains('generate_result.hpp', 'RenderOverloadEntrySelectionHelpers::buildVertexOutput(inputValue);', 'The selected vertex entry should call the namespace helper.');
        await t.expectFileContains('generate_result.hpp', 'RenderOverloadEntrySelectionHelpers::buildFrameBuffer(bindGroup, inputValue.uv);', 'The selected fragment entry should call the namespace helper.');
        await t.expectFileOccurrenceCount('generate_result.hpp', '__REVERSED_VERTEX__OUTPUT__', 4, 'Metal clip-space Y correction must be emitted only at the vertex stage entry, not inside helpers returning the same output type.');
      }
    }),
    createFixtureCase({
      id: 'compute-overload-entry-selection',
      title: 'Regression: compute entry selection remains stable with namespace helpers',
      group: 'regressions',
      labels: ['regression', 'compute', 'overload', 'entry-selection'],
      fixtureDir: 'compute-overload-entry-selection',
      sourceFile: 'ComputeOverloadEntrySelection.hpp',
      description: 'Protects the overload-aware lookup path for compute classes while keeping helper logic outside the shader class.',
      validates: [
        'The dispatch-thread overload still drives `computeMain`.',
        'Namespace helper calls remain the legal way to share compute shader logic.',
        'Workgroup metadata remains unchanged while overload disambiguation is active.'
      ],
      watchouts: [
        'Shader-class helper overloads are now rejected, so this fixture must not reintroduce same-class helpers.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Compute overload selection fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'computeDesp.workgroupX = 4', 'The selected compute entry should preserve its declared workgroup size.');
        await t.expectFileContains('generate_result.hpp', 'kernel void computeMain(uint3 threadID [[thread_position_in_grid]]', 'MSL should select the dispatch-thread compute overload.');
        await t.expectFileContains('generate_result.hpp', 'ComputeOverloadEntrySelectionHelpers::writeValue(bindGroup, threadID.x);', 'The dispatch-thread entry should call the namespace helper.');
      }
    }),
    createFixtureCase({
      id: 'render-constructor-order',
      title: 'Regression: render constructor overrides run after descriptor setup and before pipeline creation',
      group: 'regressions',
      labels: ['regression', 'render', 'constructor', 'pipeline', 'ordering'],
      fixtureDir: 'render-constructor-order',
      sourceFile: 'RenderConstructorOrder.hpp',
      description: 'Protects the ordering contract for render-class constructor bodies. User-authored constructor code should see the fully generated pipeline descriptor, and any overrides must happen before the render pipeline object is created.',
      validates: [
        'Render constructor bodies run after the compiler-generated fragment state and descriptor setup.',
        'User-authored descriptor overrides appear before `createRenderPipeline(...)`.',
        'The generated render path still succeeds for a constructor that overrides the pipeline label.'
      ],
      watchouts: [
        'If this ordering regresses, user descriptor edits can be silently overwritten by later generated assignments.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Render constructor ordering fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'this->constructorMarker = 11u', 'Render constructor body should remain present in generated code.');
        await expectSnippetsInOrder(
          t,
          'generate_result.hpp',
          [
            'this->pipelineDescriptor.fragment = fragmentState',
            'this->constructorMarker = 11u',
            'this->pipeline = this->mDevice->createRenderPipeline(this->pipelineDescriptor)'
          ],
          'Render constructor body should run after descriptor setup and before render pipeline creation.'
        );
      }
    }),
    createFixtureCase({
      id: 'compute-constructor-order',
      title: 'Regression: compute constructor overrides run after descriptor setup and before pipeline creation',
      group: 'regressions',
      labels: ['regression', 'compute', 'constructor', 'pipeline', 'ordering'],
      fixtureDir: 'compute-constructor-order',
      sourceFile: 'ComputeConstructorOrder.hpp',
      description: 'Protects the ordering contract for compute-class constructor bodies. User-authored constructor code should see the fully generated compute descriptor and still run before the compute pipeline object is created.',
      validates: [
        'Compute constructor bodies run after the compiler-generated compute stage descriptor is assigned.',
        'User-authored descriptor overrides appear before `createComputePipeline(...)`.',
        'The generated compute path still succeeds for a constructor that overrides the pipeline label.'
      ],
      watchouts: [
        'If this ordering regresses, user compute-pipeline edits can be silently overwritten by later generated assignments.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Compute constructor ordering fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'this->constructorMarker = 19u', 'Compute constructor body should remain present in generated code.');
        await expectSnippetsInOrder(
          t,
          'generate_result.hpp',
          [
            'this->pipelineDescriptor.compute = computeDesp',
            'this->constructorMarker = 19u',
            'this->pipeline = this->mDevice->createComputePipeline(this->pipelineDescriptor)'
          ],
          'Compute constructor body should run after descriptor setup and before compute pipeline creation.'
        );
      }
    }),
    createFixtureCase({
      id: 'bindgroup-sampled-texture-types',
      title: 'Regression: sampled texture bind-group sample types follow DSL element types',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'texture', 'sample-type'],
      fixtureDir: 'bindgroup-sampled-texture-types',
      sourceFile: 'SampledTextureTypes.hpp',
      description: 'Covers the fragile bind-group path where sampled textures used to be emitted as `TextureSampleType::Float` unconditionally. The fixture mixes float, uint, sint, and depth sampled textures so we lock in the type-driven mapping.',
      validates: [
        'Sampled float textures still emit `TextureSampleType::Float`.',
        'Integer sampled textures emit `TextureSampleType::Uint` and `TextureSampleType::Sint` instead of silently falling back to float.',
        'Depth texture wrappers emit `TextureSampleType::Depth` while preserving the correct texture view dimension.'
      ],
      watchouts: [
        'This change touches a historically brittle area of bind-group generation, so the assertions intentionally target the exact generated layout entries.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Sampled texture sample-type fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].texture.sampleType = GVM::RHI::TextureSampleType::Float', 'Float sampled textures should remain float sample types.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[1].texture.sampleType = GVM::RHI::TextureSampleType::Uint', 'Unsigned integer sampled textures should emit Uint sample types.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[2].texture.sampleType = GVM::RHI::TextureSampleType::Sint', 'Signed integer sampled texture arrays should emit Sint sample types.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[2].texture.viewDimension = GVM::RHI::TextureViewDimension::e2DArray', 'Sampled texture arrays should preserve their array view dimension.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[3].texture.sampleType = GVM::RHI::TextureSampleType::Depth', 'Depth texture wrappers should emit Depth sample types.');
        await t.expectFileContains('generate_result.hpp', 'layoutEntry[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment | GVM::RHI::ShaderStage::Compute', 'Unused bind groups should fall back to the non-tessellation all-stage visibility expression.');
      }
    }),
    createFixtureCase({
      id: 'render-set-basic',
      title: 'Regression: RenderSet component wiring stays explicit and stable',
      group: 'regressions',
      labels: ['regression', 'render-set', 'components', 'validation'],
      fixtureDir: 'render-set-basic',
      sourceFile: 'RenderSetBasic.hpp',
      description: 'Covers the RenderSet code path with one texture component plus explicit vertex/index buffer annotations so we keep a positive signal on component classification and handle assignment.',
      validates: [
        'RenderSet generation still succeeds for the supported `BufferComponent` and `TextureComponent` DSL surface.',
        'Vertex and index component names continue to come from the explicitly annotated fields.',
        'Generated component handle assignment stays aligned with the inferred component kinds.'
      ],
      watchouts: [
        'The RenderSet path has accumulated historical hard-coded assumptions, so this case protects the supported path while diagnostics are tightened.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RenderSet fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'createInfo.vertexComponentName = "vertices"', 'Vertex component name should come from the [[RenderSetVertexBuffer]] field.');
        await t.expectFileContains('generate_result.hpp', 'createInfo.indexComponentName = "indices"', 'Index component name should come from the [[RenderSetIndexBuffer]] field.');
        await t.expectFileContains(
          'generate_result.hpp',
          'GVM::Core::RenderComponentCreateInfo{.type = GVM::Core::RenderComponentType::BufferComponent, .dataElementStorageSize = sizeof(float3), .maxResourceCount = 1, .componentName = "vertices"}',
          'Generated designated initializers should follow the RenderComponentCreateInfo declaration order.'
        );
        await t.expectFileNotContains(
          'generate_result.hpp',
          'GVM::Core::RenderComponentCreateInfo{.componentName = "vertices"',
          'Generated RenderComponentCreateInfo initializers should not begin with the last-declared field.'
        );
        await t.expectFileContains('generate_result.hpp', 'RenderComponentType::BufferComponent', 'Buffer components should still be emitted as buffer component create infos.');
        await t.expectFileContains('generate_result.hpp', 'RenderComponentType::TextureComponent', 'Texture components should still be emitted as texture component create infos.');
        await t.expectFileContains('generate_result.hpp', '.maxResourceCount = 4', 'Texture component host create info should preserve the declared MaxResourceCount instead of silently collapsing to the per-entity slot stride.');
        await t.expectFileContains('generate_result.hpp', 'this->vertices = getBufferComponentByName("vertices")', 'Annotated vertex buffer fields should still bind as buffer components.');
        await t.expectFileContains('generate_result.hpp', 'this->albedo = getTextureComponentByName("albedo")', 'Texture components should still bind through the texture-component lookup path.');
      }
    }),
    createFixtureCase({
      id: 'render-set-shader-abi',
      title: 'Regression: RenderSet shader ABI stays aligned with host instance and 8-slot texture packing',
      group: 'regressions',
      labels: ['regression', 'render-set', 'shader', 'abi', 'hlsl', 'msl'],
      fixtureDir: 'render-set-shader-abi',
      sourceFile: 'RenderSetShaderABI.hpp',
      description: 'Exercises a render shader that reads a RenderSet texture component through get(entity, slot) and RenderEntity metadata so we keep the shader-side ABI aligned with the host-side 8-slot texture index contract.',
      validates: [
        'RenderSet texture components lower to a bindless Texture2D descriptor array on the shader side.',
        'Shader-side texture addressing uses entity * 8 + slot through the per-entity texture component-list buffer.',
        'Vulkan HLSL and Metal MSL both guard RenderSet texture fetches through the shared `RenderSetAccessBoundData` contract while decoding RenderEntity builtins through RenderEntityCMDParams.'
      ],
      watchouts: [
        'This fixture now locks both shader backends to the same `RenderSetAccessBoundData`-driven texture safety contract so helper refactors cannot quietly drift Vulkan and Metal apart again.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RenderSet shader ABI fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(1, 0)]] StructuredBuffer<uint> renderSet_albedoComponentList : register(t1, space0);', 'Vulkan HLSL should bind the texture component-list buffer before the bindless resource array.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(2, 0)]] Texture2D<float4> renderSet_albedo[] : register(t2, space0);', 'Vulkan HLSL should lower RenderSet texture pools as a bindless Texture2D descriptor array.');
        await t.expectFileContains('generate_result.hpp', 'uint renderSet_albedo_UGLResolveTextureIndexSafe(uint entity, uint slot)', 'Vulkan HLSL should emit a dedicated entity-slot texture resolve helper.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_raw_component_index = (entity * 8u) + __uglc_safe_slot;', 'Vulkan HLSL should use the fixed 8-slot per-entity texture ABI.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> renderSet_albedo_UGLGetSafe(uint entity, uint slot)', 'Vulkan HLSL should expose get(entity, slot) as a Texture2D resource helper.');
        await t.expectFileContains('generate_result.hpp', 'return renderSet_albedo[NonUniformResourceIndex(renderSet_albedo_UGLResolveTextureIndexSafe(entity, slot))];', 'Vulkan HLSL should use NonUniformResourceIndex for the bindless descriptor lookup.');
        await t.expectFileContains('generate_result.hpp', 'UGL_RenderEntityInfo_ renderSet_UGLLoadRenderEntityInfoSafe(uint entity)', 'HLSL RenderEntity metadata access should flow through the shared safe helper.');
        await t.expectFileContains('generate_result.hpp', 'const uint2 __uglc_header = renderSet_RenderSetAccessBoundData[0u];', 'HLSL RenderEntity metadata helpers should clamp against RenderSetAccessBoundData instead of hard-coded magic values.');
        await t.expectFileContains('generate_result.hpp', 'uint globalInstanceBase;', 'Generated RenderEntityInfo should expose the dynamic global instance base.');
        await t.expectFileContains('generate_result.hpp', 'uint cmdParamsOffset;', 'Generated RenderEntityInfo should expose the command-params offset separately from the global instance base.');
        await t.expectFileContains('generate_result.hpp', 'const uint2 __uglc_render_entity_cmd = renderSet_UGLLoadRenderEntityCMDParamsSafe(_UGLC_InstanceID);', 'HLSL RenderEntity builtins should decode through the RenderSet command-params buffer.');
        await t.expectFileContains('generate_result.hpp', 'const uint renderEntityID = __uglc_render_entity_cmd.x;', 'HLSL RenderEntityID should come from decoded command params.');
        await t.expectFileContains('generate_result.hpp', 'const uint renderEntityInstanceID = __uglc_render_entity_cmd.y;', 'HLSL RenderEntityInstanceID should come from decoded command params.');
        await t.expectFileContains('generate_result.hpp', 'device uint* albedoComponentList [[id(1)]];', 'Generated MSL should bind the per-entity texture component list.');
        await t.expectFileContains('generate_result.hpp', 'texture2d<half> albedoget(uint renderEntityID, uint renderEntityInstanceID) const', 'Generated MSL RenderSet wrappers should expose get(entity, slot).');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_raw_component_index = (renderEntityID*8u) + __uglc_safe_slot;', 'Generated MSL texture getters should use the fixed 8-slot per-entity texture ABI.');
        await t.expectFileContains('generate_result.hpp', 'return albedo[__uglc_safe_descriptor].texture;', 'Generated MSL texture getters should return the resolved bindless texture wrapper resource.');
        await t.expectFileContains('generate_result.hpp', 'const uint2 __uglc_bounds = RenderSetAccessBoundData[1u];', 'MSL RenderSet texture getters should read the shared per-field bound data entry.');
        await t.expectFileContains('generate_result.hpp', 'const uint2 __uglc_render_entity_cmd = renderSet->UGLLoadRenderEntityCMDParamsSafe(_Backup_InstanceID);', 'MSL RenderEntity builtins should decode through the RenderSet command-params buffer.');
        await t.expectFileContains('generate_result.hpp', 'const uint renderEntityID = __uglc_render_entity_cmd.x;', 'MSL RenderEntityID should come from decoded command params.');
        await t.expectFileContains('generate_result.hpp', 'const uint renderEntityInstanceID = __uglc_render_entity_cmd.y;', 'MSL RenderEntityInstanceID should come from decoded command params.');
        await t.expectFileNotContains('generate_result.hpp', '/ 4096u', 'Generated shader code should not decode RenderEntityID with a fixed 4096 division.');
        await t.expectFileNotContains('generate_result.hpp', '% 4096u', 'Generated shader code should not decode RenderEntityInstanceID with a fixed 4096 modulo.');
        await t.expectFileNotContains('generate_result.hpp', '/4096u', 'Generated MSL shader code should not decode RenderEntityID with a fixed 4096 division.');
        await t.expectFileNotContains('generate_result.hpp', '%4096u', 'Generated MSL shader code should not decode RenderEntityInstanceID with a fixed 4096 modulo.');
        await t.expectFileNotContains('generate_result.hpp', 'renderSet_albedo[0]', 'RenderSet texture lowering should not collapse every HLSL texture fetch to descriptor 0.');
        await t.expectFileNotContains('generate_result.hpp', 'Texture2DArray<float4> renderSet_albedo', 'RenderSet texture components should not use Texture2DArray lowering.');
        await t.expectFileNotContains('generate_result.hpp', 'renderSet_albedo_UGLGetSafe(uint textureHandle)', 'Generated HLSL should not expose the removed texture-handle getter helper.');
        await t.expectFileNotContains('generate_result.hpp', 'albedoget(uint textureHandle) const', 'Generated MSL should not expose texture-handle getters.');
        await t.expectFileNotContains('generate_result.hpp', '99999999u', 'RenderSet metadata helpers should no longer rely on legacy nine-digit magic bounds.');
        await t.expectFileNotContains('generate_result.hpp', '999999u', 'RenderSet metadata helpers should no longer clamp entity IDs through the legacy six-digit magic bound.');
      }
    }),
    createFixtureCase({
      id: 'render-entity-builtin-multiple-render-sets',
      title: 'Diagnostic: RenderEntity builtins require exactly one RenderSet binding',
      group: 'diagnostics',
      labels: ['diagnostic', 'render-set', 'render-entity', 'builtin', 'validation'],
      fixtureDir: 'render-entity-builtin-multiple-render-sets',
      sourceFile: 'RenderEntityBuiltinMultipleRenderSets.hpp',
      description: 'Confirms RenderEntityID/RenderEntityInstanceID builtins fail when a render class binds more than one RenderSet because InstanceID cannot be decoded unambiguously.',
      validates: [
        'The diagnostic is emitted before HLSL/MSL codegen picks an arbitrary RenderSet.',
        'The message explains the exactly-one-RenderSet requirement.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'RenderEntity builtins require exactly one UGL::RenderSet<T> binding', 'Diagnostic output should explain the unique RenderSet requirement.');
        await t.expectStepOutputContains('uglc', "InstanceID must be decoded through that RenderSet's RenderEntityCMDParams", 'Diagnostic output should explain why multiple RenderSets are ambiguous.');
      }
    }),
    createFixtureCase({
      id: 'render-set-large-texture-pool',
      title: 'Regression: RenderSet bindless texture pools keep fixed per-entity slot stride',
      group: 'regressions',
      labels: ['regression', 'render-set', 'texture', 'resource-pool', 'abi', 'hlsl', 'msl'],
      fixtureDir: 'render-set-large-texture-pool',
      sourceFile: 'RenderSetLargeTexturePool.hpp',
      description: 'Confirms `TextureComponent<T, MaxResourceCount>` keeps treating `MaxResourceCount` as global descriptor-pool capacity while shader-side entity lookups still use the fixed 8-slot RenderSet ABI stride.',
      validates: [
        'Large texture pools such as 256 descriptors generate successfully.',
        'Host-side descriptor-array capacity still follows `MaxResourceCount` exactly while Vulkan HLSL lowers the pool as a bindless Texture2D array.',
        'Texture reads use the fixed 8-slot component-list stride while both shader backends keep the shared safety helper path.'
      ],
      watchouts: [
        'This is the semantic split downstream integrations depend on: one component can own many textures globally while each entity still exposes eight material texture slots.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Large RenderSet texture-pool fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '.maxResourceCount = 256', 'Host create info should preserve the declared large texture-pool capacity.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(1, 0)]] StructuredBuffer<uint> renderSet_albedoComponentList : register(t1, space0);', 'Vulkan HLSL should bind the per-entity texture component-list buffer.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(2, 0)]] Texture2D<float4> renderSet_albedo[] : register(t2, space0);', 'Vulkan HLSL should lower large RenderSet texture pools as a bindless Texture2D descriptor array.');
        await t.expectFileContains('generate_result.hpp', '[[vk::binding(3, 0)]] StructuredBuffer<uint> renderSet_verticesComponentList : register(t3, space0);', 'Resources declared after the texture descriptor array should continue to the next base binding and register index.');
        await t.expectFileContains('generate_result.hpp', 'uint renderSet_albedo_UGLResolveTextureIndexSafe(uint entity, uint slot)', 'Large texture pools should still emit the shared HLSL entity-slot resolver.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> renderSet_albedo_UGLGetSafe(uint entity, uint slot)', 'Large texture pools should expose get(entity, slot) on HLSL.');
        await t.expectFileContains('generate_result.hpp', 'return albedo[__uglc_safe_descriptor].texture;', 'MSL large-pool texture getters should keep the shared invalid-to-zero fallback.');
        await t.expectFileNotContains('generate_result.hpp', 'Texture2DArray<float4> renderSet_albedo', 'Large RenderSet texture pools should not lower to Texture2DArray.');
      }
    }),
    createFixtureCase({
      id: 'render-set-buffer-clamp-regression',
      title: 'Regression: RenderSet buffer helpers clamp against RenderSetAccessBoundData',
      group: 'regressions',
      labels: ['regression', 'render-set', 'buffer', 'hlsl', 'msl', 'clamp', 'dxc'],
      fixtureDir: 'render-set-buffer-clamp-regression',
      sourceFile: 'RenderSetBufferClampRegression.hpp',
      description: 'Exercises the hardened HLSL and MSL RenderSet buffer-component helpers with a complex struct payload plus generic, vertex, and index component reads so both shader backends lock down the current RenderSetAccessBoundData-based anti-OOB behavior.',
      validates: [
        '`getRaw(index)` and `get(entity, subIndex)` clamp against per-component `RenderSetAccessBoundData` instead of descriptor `GetDimensions()` queries.',
        '`checkValid(entity)` and `get(entity, subIndex)` derive their entity and base safety from the component-list entry plus the published component bounds.',
        'Large `subIndex` inputs clamp against the remaining physical tail of the actual data buffer.',
        'Complex struct element types still keep concrete typed helper signatures in both Vulkan HLSL and Metal MSL.'
      ],
      watchouts: [
        'This is the crash-containment line for the macOS Vulkan/MoltenVK investigation: if these helpers regress back to raw indexing or hidden `GetDimensions()` logic drifts from the published bound-data ABI, the old GPU hang class can quietly come back.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile the generated RenderSet clamp regression header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RenderSet clamp regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact computeShaderArtifact', 'The compute shader artifact bundle should still be emitted for the RenderSet clamp regression fixture.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {', 'DXC-backed SPIR-V should still be embedded for the RenderSet clamp regression fixture.');
        await t.expectFileContains('generate_result.hpp', '0x07230203', 'The embedded SPIR-V blob should retain the SPIR-V magic number.');
        await t.expectFileContains('generate_result.hpp', 'RenderSetBufferClampRegressionPayload renderSet_payloads_UGLGetRawSafe(uint index)', 'Generated HLSL should emit a dedicated safe GetRaw helper for the complex payload component.');
        await t.expectFileContains('generate_result.hpp', 'bool renderSet_payloads_UGLCheckValid(uint entity)', 'Generated HLSL should emit a dedicated safe checkValid helper for the complex payload component.');
        await t.expectFileContains('generate_result.hpp', 'RenderSetBufferClampRegressionPayload renderSet_payloads_UGLGetSafe(uint entity, uint subIndex)', 'Generated HLSL should emit a dedicated safe get helper for the complex payload component.');
        await t.expectFileContains('generate_result.hpp', 'const uint2 __uglc_bounds = renderSet_RenderSetAccessBoundData[1u];', 'Clamp helpers should read their per-component published bounds from RenderSetAccessBoundData.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_component_count_nz = max(__uglc_bounds.x, 1u);', 'Entity guards should derive a non-zero component-list bound from the published component count.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_element_count_nz = max(__uglc_bounds.y, 1u);', 'Data-buffer guards should derive a non-zero element bound from the published resource count.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_safe_entity = min(entity, __uglc_component_count_nz - 1u);', 'Entity guards should clamp against the published component-list length.');
        await t.expectFileContains('generate_result.hpp', 'const bool __uglc_base_valid = entity < __uglc_bounds.x && __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y;', 'Base validation should rely on the published component-list and resource bounds.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_safe_base = __uglc_base_valid ? __uglc_raw_base : 0u;', 'Invalid component-list entries should fall back to base element 0.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_safe_base_clamped = min(__uglc_safe_base, __uglc_element_count_nz - 1u);', 'Base indices should clamp against the published resource tail.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_physical_remaining = (__uglc_element_count_nz - 1u) - __uglc_safe_base_clamped;', 'Large subIndex values should clamp against the remaining physical tail of the actual data buffer.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_safe_offset = min(subIndex, __uglc_physical_remaining);', 'The final payload offset should clamp directly against the physical remaining tail.');
        await t.expectFileContains('generate_result.hpp', 'const uint __uglc_safe_index = min(index, __uglc_element_count_nz - 1u);', 'GetRaw helpers should clamp against the published resource count instead of descriptor queries.');
        await t.expectFileContains('generate_result.hpp', 'RenderSetBufferClampRegressionPayload payloadsget(uint renderEntityID, uint renderEntityInstanceID) const', 'Metal MSL should emit a typed RenderSet buffer getter.');
        await t.expectFileContains('generate_result.hpp', 'bool payloadscheckValid(uint renderEntityID) const', 'Metal MSL should emit a bounds-hardened component validity check.');
        await t.expectFileContains('generate_result.hpp', 'const bool __uglc_base_valid = renderEntityID < __uglc_bounds.x && __uglc_raw_base != 4294967295u && __uglc_raw_base < __uglc_bounds.y;', 'Metal MSL should reject invalid entities, sentinels, and out-of-range component bases.');
        await t.expectFileContains('generate_result.hpp', 'return payloads[__uglc_safe_base_clamped + __uglc_safe_offset];', 'Metal MSL should clamp component reads to the published resource extent.');
        await t.expectFileNotContains('generate_result.hpp', '.GetDimensions(', 'Clamp helpers should no longer issue descriptor GetDimensions queries.');
        await t.expectFileNotContains('generate_result.hpp', '__uglc_entity_limit', 'Clamp helpers should no longer compute an entity limit from RenderEntityInfo lengths.');
        await t.expectFileNotContains('generate_result.hpp', '__uglc_safe_sub_index', 'Clamp helpers should no longer apply the removed entity-local count clamp path.');
      }
    }),
    createFixtureCase({
      id: 'render-set-helper-parameter-regression',
      title: 'Regression: HLSL RenderSet helper parameters rebind to global resources',
      group: 'regressions',
      labels: ['regression', 'render-set', 'helper-parameters', 'hlsl', 'msl', 'host-compile'],
      fixtureDir: 'render-set-helper-parameter-regression',
      sourceFile: 'RenderSetHelperParameterRegression.hpp',
      description: 'Covers namespace helpers that take RenderSet<T> [[IN]] parameters, forward them through nested helper calls, access RenderSet shader data packs, and coexist with host create methods that only bind RenderSet handles.',
      validates: [
        'HLSL emits global-resource helper variants and omits RenderSet parameters from generated helper signatures.',
        'Nested helper forwarding maps the forwarded RenderSet parameter back to the same global RenderSet alias.',
        'MSL keeps the real const RenderSet data-pack pointer parameter model.',
        'CPP host deletion is driven by RenderSet shader data-pack access, not by RenderSet parameters alone.'
      ],
      watchouts: [
        'RenderSet texture pools cannot use the BindGroup-style local handle struct route on Vulkan HLSL because DXC emits invalid SPIR-V when a struct stores opaque Texture2D descriptor arrays.',
        'HLSL intentionally supports only direct RenderSet variables, shader-class RenderSet fields, and already-mapped RenderSet helper parameters as arguments.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile generated RenderSet helper-parameter header',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        },
        {
          id: 'host-compile-call-deleted-renderset-helper',
          title: 'Reject host calls to deleted RenderSet helper declarations',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { eastl::intrusive_ptr<RenderSetHelperParameterRegressionSet> rs; auto value = RenderSetHelperParameterRegressionHelpers::loadPayload(rs, 0u); return (int)value.tag; }\n',
          expectedExitCode: 1
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RenderSet helper-parameter regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'RenderSetHelperParameterRegressionHelpers_loadPayload_UGLRenderSetSpecialized_', 'HLSL should synthesize a global-resource variant for the payload helper.');
        await t.expectFileContains('generate_result.hpp', 'RenderSetHelperParameterRegressionHelpers_forwardPayload_UGLRenderSetSpecialized_', 'HLSL should synthesize a global-resource variant for nested RenderSet forwarding.');
        await t.expectFileContains('generate_result.hpp', 'return renderSet_payloads_UGLGetSafe(entity, 0u);', 'The rebound payload helper should read the global RenderSet payload component helper.');
        await t.expectFileContains('generate_result.hpp', 'return renderSet_UGLLoadRenderEntityInfoSafe(entity).indexCount;', 'The rebound RenderEntity helper should read the global RenderEntity metadata helper.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> albedoTexture = renderSet_albedo_UGLGetSafe(entity, slot);', 'The rebound texture helper should resolve through the global RenderSet texture helper.');
        await t.expectFileContains('generate_result.hpp', 'return RenderSetHelperParameterRegressionHelpers::RenderSetHelperParameterRegressionHelpers_loadPayload_UGLRenderSetSpecialized_', 'Nested HLSL forwarding should call the rebound payload helper instead of passing a RenderSet value.');
        await t.expectFileContains('generate_result.hpp', 'const constant RenderSetHelperParameterRegressionSet* rs', 'MSL helper lowering should keep the explicit const RenderSet pointer parameter.');
        await t.expectFileContains('generate_result.hpp', 'return rs->payloadsget(entity, 0u);', 'MSL helper bodies should continue to access through the RenderSet pointer.');
        await t.expectFileContains('generate_result.hpp', 'Host declaration is deleted because the original DSL body depends on shader-only operations.', 'Host C++ should delete helpers only when their bodies perform shader-only data access.');
        await t.expectFileContains('generate_result.hpp', 'loadPayload(eastl::intrusive_ptr<RenderSetHelperParameterRegressionSet> rs, uint entity) = delete;', 'Host C++ should not emit the original RenderSet payload helper body.');
        await t.expectFileContains('generate_result.hpp', 'loadIndexCount(eastl::intrusive_ptr<RenderSetHelperParameterRegressionSet> rs, uint entity) = delete;', 'Host C++ should not emit RenderSetDataPack entity helper bodies that the host RenderSet object does not implement.');
        await t.expectFileContains('generate_result.hpp', 'loadAlbedo(eastl::intrusive_ptr<RenderSetHelperParameterRegressionSet> rs, uint entity, uint slot) = delete;', 'Host C++ should not emit the original RenderSet texture helper body.');
        await t.expectFileNotContains('generate_result.hpp', 'public: void create(eastl::intrusive_ptr<RenderSetHelperParameterRegressionSet> renderSet [[Slot0]]) = delete;', 'Host create methods that only bind RenderSet handles should stay callable.');
        await t.expectStepOutputContains('host-compile-call-deleted-renderset-helper', 'deleted function', 'Host-side misuse should fail at compile time when a RenderSet helper is called.');
        await t.expectFileNotContains('generate_result.hpp', 'rs_payloads_UGLGetSafe', 'HLSL should not invent per-parameter RenderSet resource helpers.');
        await t.expectFileNotContains('generate_result.hpp', 'StructuredBuffer<uint2> rs_RenderSetAccessBoundData', 'HLSL should not expand a RenderSet helper parameter into a long resource parameter list.');
        await t.expectFileNotContains('generate_result.hpp', 'return rs->payloads->get(entity, 0u);', 'Host C++ should not leak shader-side RenderSet component get() calls.');
      }
    }),
    createFixtureCase({
      id: 'texture2darray-gather-lowering',
      title: 'Regression: Texture2DArray gather lowering keeps layer and offset order stable',
      group: 'regressions',
      labels: ['regression', 'texture', 'array', 'gather', 'msl', 'hlsl'],
      fixtureDir: 'texture2darray-gather-lowering',
      sourceFile: 'Texture2DArrayGatherLowering.hpp',
      description: 'Adds a direct texture-array gather call so the HLSL and MSL lowerings keep the layer/offset schema aligned and future member-call refactors do not fall back to substring matching bugs.',
      validates: [
        'HLSL array gather lowering still combines UV and layer into the expected `float3` coordinate.',
        'MSL array gather lowering keeps the `sampler, uv, layer, offset, component` parameter order.',
        'The green-channel gather variant continues to lower to the correct channel selector.'
      ],
      watchouts: [
        'This protects the bug class that previously mixed up `getArg(2)` and `getArg(3)` in the Metal array gather path.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Texture2DArray gather fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup.textureArray.GatherGreen(bindGroup.sampler0, float3(inputValue.uv, 1u), int2(1, 2))', 'HLSL Texture2DArray gather lowering should keep layer packed into the third coordinate component.');
        await t.expectFileContains('generate_result.hpp', 'inputValue.uv, 1u, int2(1, 2), component::y', 'MSL Texture2DArray gather lowering should keep the layer argument ahead of the offset and target the green channel.');
        await t.expectFileNotContains('generate_result.hpp', 'inputValue.uv, int2(1, 2), 1u, component::y', 'MSL Texture2DArray gather lowering should not swap the layer and offset arguments.');
      }
    }),
    createFixtureCase({
      id: 'rwtexture2darray-write-lowering',
      title: 'Regression: RWTexture2DArray write lowering keeps value, coordinate, and layer order stable',
      group: 'regressions',
      labels: ['regression', 'texture', 'array', 'write', 'storage', 'msl', 'hlsl'],
      fixtureDir: 'rwtexture2darray-write-lowering',
      sourceFile: 'RWTexture2DArrayWriteLowering.hpp',
      description: 'Covers writable texture-array member calls so HLSL keeps the `uint3(coord, layer)` store form and Metal emits the native `write(value, coord, layer)` call.',
      validates: [
        'HLSL array write lowering still combines the 2D coordinate and layer into a uint3 storage index.',
        'MSL array write lowering emits Metal texture-array writes as value, coordinate, then layer.',
        'The old Metal argument order that treated the layer as the written value is rejected.'
      ],
      watchouts: [
        'This protects the Metal bug class that corrupted Nanite HZB array layers by lowering `write(coord, layer, value)` as `write(layer, coord, value)`.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RWTexture2DArray write fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup.targetArray[uint3(uint2(3u, 5u), 2u)] = half(0.75f)', 'HLSL RWTexture2DArray write lowering should keep the layer in the uint3 storage index.');
        await t.expectFileContains('generate_result.hpp', 'bindGroup->targetArray.write(half(0.75f), uint2(3u, 5u), 2u)', 'MSL RWTexture2DArray write lowering should emit value, coordinate, then layer.');
        await t.expectFileNotContains('generate_result.hpp', 'bindGroup->targetArray.write(2u, uint2(3u, 5u), half(0.75f))', 'MSL RWTexture2DArray write lowering should not emit the old layer, coordinate, value order.');
      }
    }),
    createFixtureCase({
      id: 'host-resource-record-filter',
      title: 'Regression: host-only resource records stay out of shader backends',
      group: 'regressions',
      labels: ['regression', 'host', 'shader', 'filtering', 'hlsl', 'msl'],
      fixtureDir: 'host-resource-record-filter',
      sourceFile: 'HostResourceRecordFilter.hpp',
      description: 'Covers a host helper struct that owns `UGL::Buffer`, `UGL::Texture`, `UGL::TextureView`, and `eastl::array<TextureView, N>` state alongside a valid compute shader, so shader codegen keeps host-only resource records out of emitted HLSL/MSL while preserving them in generated host code.',
      validates: [
        'UGLC still succeeds when a translation unit mixes host-only resource helper records with shader entry classes.',
        'Host-only structs that own `UGL::Buffer`, `UGL::TextureView`, and texture-view arrays no longer leak into HLSL/MSL source emission.',
        'The generated host header still preserves the helper record and its `UGL::Device` create method.'
      ],
      watchouts: [
        'This protects the regression where broadening `checkCXXRecord()` accidentally let host helpers poison shader compilation with `TextureView` and other CPU-side handles.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host resource filtering fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'HostOnlyBufferPack', 'Generated host output should still preserve the host-only helper record.');
        await t.expectFileContains('generate_result.hpp', 'create(GVM::Core::DeviceProxy device)', 'Generated host output should keep the host helper create method in the rewritten host ABI form.');
        await t.expectFileContains('generate_result.hpp', 'GVM::RHI::TextureView textureView;', 'Generated host output should keep the single host texture view field.');
        await t.expectFileContains('generate_result.hpp', 'eastl::array<GVM::RHI::TextureView, 2> textureViews;', 'Generated host output should keep the host texture view array field.');
        await t.expectFileContains('generate_result.hpp', 'const UGLC::Generated::ShaderArtifact computeShaderArtifact', 'Shader generation should still complete for the valid compute class in the same translation unit.');
        await t.expectFileNotContains('generate_result.hpp', 'TextureView<TextureFormat::RGBA8Unorm', 'Shader backend sources should not contain raw DSL TextureView declarations.');
      }
    }),
    createFixtureCase({
      id: 'host-only-record-in-unused-namespace',
      title: 'Regression: unused namespace host-only records do not poison shader artifacts',
      group: 'regressions',
      labels: ['regression', 'host', 'namespace', 'reachability', 'hlsl', 'msl'],
      fixtureDir: 'host-only-record-in-unused-namespace',
      sourceFile: 'HostOnlyRecordInUnusedNamespace.hpp',
      description: 'Covers an unused namespace that owns host-only `TextureView` fields and arrays while a separate compute pass remains valid, matching WVM-style shader library pollution.',
      validates: [
        'The generated host header may keep the host-only namespace record.',
        'HLSL/MSL shader sources do not emit unused host-only namespace records.',
        'Backend shader generation succeeds without requiring the user to delete unrelated host-only DSL declarations.'
      ],
      watchouts: [
        'This guards the artifact-boundary bug where namespace traversal treated a namespace as a package and emitted unrelated host-only declarations into shader source.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Unused namespace host-only fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'HostOnlyRecordInUnusedNamespaceHelpers', 'Generated host output should still preserve the namespace that owns the host-only record.');
        await t.expectFileContains('generate_result.hpp', 'eastl::array<GVM::RHI::TextureView, 2> textureViews;', 'Generated host output should keep the host-only texture-view array.');
        const { mslSource, hlslSource } = await readGeneratedShaderSources(t, 'generate_result.hpp');
        t.recordCheck('MSL shader source should be extractable for host-only namespace checks.', mslSource.length > 0, mslSource.length > 0 ? 'Extracted MSL shader source.' : 'Could not locate the MSL shader source raw string.');
        t.recordCheck('HLSL shader source should be extractable for host-only namespace checks.', hlslSource.length > 0, hlslSource.length > 0 ? 'Extracted HLSL shader source.' : 'Could not locate the HLSL shader source raw string.');
        t.recordCheck('MSL should not emit unused host-only namespace records.', !mslSource.includes('UnusedHostOnlyRecord') && !mslSource.includes('TextureView'), 'Unexpected host-only namespace record in MSL source.');
        t.recordCheck('HLSL should not emit unused host-only namespace records.', !hlslSource.includes('UnusedHostOnlyRecord') && !hlslSource.includes('TextureView'), 'Unexpected host-only namespace record in HLSL source.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-uniform-abi-guard',
      title: 'Regression: host headers no longer emit fake uniform-buffer ABI guards',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'uniform-buffer', 'abi'],
      fixtureDir: 'bindgroup-uniform-abi-guard',
      sourceFile: 'UniformAbiGuard.hpp',
      description: 'Locks in the new policy: layout mismatches are diagnosed during shader backend generation, so generated host headers no longer inject incomplete `static_assert` guesses for uniform buffers or storage buffers.',
      validates: [
        'Generated host headers no longer emit the old uniform-buffer size-based `static_assert`.',
        'Storage buffers and scalar element types also remain free of the earlier pseudo-ABI assertions.',
        'Valid bind groups still generate successfully without any fallback ABI guesswork in host code.'
      ],
      watchouts: [
        'A future regression here would reintroduce host-side checks that still cannot prove backend layout compatibility member-by-member.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Uniform ABI guard fixture should generate successfully.');
        await t.expectFileNotContains('generate_result.hpp', 'UniformBuffer element type UniformAbiGuardParams', 'Host code should no longer emit the old uniform-buffer size-based ABI guard.');
        await t.expectFileNotContains('generate_result.hpp', 'static_assert(sizeof(UniformAbiGuardParams)', 'Generated host headers should not contain the removed uniform-buffer size assertion.');
        await t.expectFileNotContains('generate_result.hpp', 'alignof(', 'Generated ABI guards should no longer claim safety through the earlier alignof-based expression.');
        await t.expectFileNotContains('generate_result.hpp', 'static_assert(sizeof(unsigned int)', 'Scalar storage-buffer element types should not emit the earlier pseudo-safety assertion.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-helper-parameter-uniform',
      title: 'Regression: BindGroup helper parameters keep a stable HLSL uniform wrapper type',
      group: 'regressions',
      labels: ['regression', 'bindgroup', 'helpers', 'uniform-buffer', 'hlsl'],
      fixtureDir: 'bindgroup-helper-parameter-uniform',
      sourceFile: 'BindGroupHelperParameterUniform.hpp',
      description: 'Covers helper functions that take a bind group with a `UniformBuffer<T>` field so the HLSL path can reuse one canonical wrapper type name across global resource declarations, helper signatures, and nested helper forwarding.',
      validates: [
        'Generated HLSL declares a stable canonical wrapper type for the bind-group uniform field.',
        'Helper signatures reference one generated bind-group handle type that owns that wrapper field.',
        'Nested helper calls forward the same bind-group handle consistently.'
      ],
      watchouts: [
        'If this regresses, uniform-buffer helpers can still parse as DSL but fail later because signatures, declarations, and `.value` access no longer agree on one wrapper type.'
      ],
      verify: async (t) => {
        const wrapperType = 'BindGroupHelperParameterUniformBindGroup_params_UniformValue';
        const hlslNamespaceTypeSignature = 'uint ExtractPackedZ(BindGroupHelperParameterUniformTypes_Params params)';
        const hlslHandleType = 'BindGroupHelperParameterUniformBindGroup_UGLBindGroupHandle';
        const hlslTopLevelSignature = `uint BindGroupHelperParameterUniformTopLevel(${hlslHandleType} bg)`;
        const hlslNamespaceSignature = `uint LoadPackedX(${hlslHandleType} bg)`;
        await t.expectArtifactExists('generate_result.hpp', 'Uniform bind-group helper-parameter fixture should generate successfully.');
        await t.expectFileNotContains('generate_result.hpp', `struct ${wrapperType};`, 'HLSL should not rely on an incomplete forward declaration for the canonical uniform wrapper type.');
        await t.expectFileOccurrenceCount('generate_result.hpp', `struct ${wrapperType}\n`, 1, 'HLSL should emit the canonical uniform wrapper definition exactly once.');
        await t.expectFileContainsInOrder('generate_result.hpp', [
          'struct BindGroupHelperParameterUniformTypes_Params',
          `struct ${wrapperType}\n`,
          `struct ${hlslHandleType}`,
          'namespace BindGroupHelperParameterUniformHelpers',
          hlslNamespaceTypeSignature,
          hlslNamespaceSignature
        ], 'HLSL should define namespace data types, then the uniform wrapper and bind-group handle before handle-dependent helpers.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHelperParameterUniformTypes_Params value;', 'HLSL uniform wrapper definitions should use the flattened namespaced uniform element type.');
        await t.expectFileContains('generate_result.hpp', hlslNamespaceTypeSignature, 'HLSL namespace helper declarations should be emitted only after the namespace data types they reference.');
        await t.expectFileContains('generate_result.hpp', hlslNamespaceSignature, 'HLSL helper signatures should use the bind-group handle type that owns the canonical uniform wrapper field.');
        await t.expectFileContains('generate_result.hpp', hlslTopLevelSignature, 'HLSL top-level helper signatures should also use the bind-group handle type.');
        await t.expectFileContains('generate_result.hpp', 'return uint(bg.params.value.packedValue.x);', 'HLSL uniform helper bodies should keep the expected handle `.params.value` access path.');
        await t.expectFileContains('generate_result.hpp', 'uint(bg.params.value.packedValue.y)', 'HLSL top-level helper bodies should also use the same handle `.params.value` access path.');
        await t.expectFileContains('generate_result.hpp', 'BindGroupHelperParameterUniformTypes::ExtractPackedZ(bg.params.value)', 'HLSL top-level helper bodies should pass uniform wrapper values to namespace helpers that take the original uniform element type.');
        await t.expectFileContains('generate_result.hpp', 'bg.values[0] = (LoadPackedX(bg) + BindGroupHelperParameterUniformTopLevel(bg));', 'Nested HLSL helper calls should use namespace-local lookup while forwarding the bind-group handle.');
        await t.expectFileContains('generate_result.hpp', 'LoadPackedX(const constant BindGroupHelperParameterUniformBindGroup* bg)', 'MSL helpers should still keep one argument-buffer pointer parameter for the same bind group.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-helper-parameter-member',
      title: 'Diagnostic: compute shader class rejects BindGroup member helpers',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'helpers', 'member-functions', 'shader-class', 'validation'],
      fixtureDir: 'bindgroup-helper-parameter-member',
      sourceFile: 'BindGroupHelperParameterMember.hpp',
      description: 'Confirms BindGroup helper logic must live outside IComputeClass instead of as an ordinary member method.',
      validates: [
        'IComputeClass allows constructor and compute only.',
        'Namespace helper coverage remains separate from shader-class member helper rejection.'
      ],
      watchouts: [
        'This fixture intentionally preserves the old unsupported shape so the compiler keeps rejecting it clearly.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ComputeClass "BindGroupHelperParameterMemberPass" cannot declare helper method "loadValue"', 'Diagnostic output should name the invalid BindGroup member helper.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into compute().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'bindgroup-helper-parameter-member-uniform',
      title: 'Diagnostic: compute shader class rejects uniform BindGroup member helpers',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'helpers', 'member-functions', 'uniform-buffer', 'shader-class', 'validation'],
      fixtureDir: 'bindgroup-helper-parameter-member-uniform',
      sourceFile: 'BindGroupHelperParameterMemberUniform.hpp',
      description: 'Confirms uniform BindGroup helper logic must not be declared as an ordinary IComputeClass member method.',
      validates: [
        'IComputeClass allows constructor and compute only.',
        'The diagnostic trips before HLSL/MSL helper ABI generation.'
      ],
      watchouts: [
        'This fixture intentionally preserves the old unsupported shape so the compiler keeps rejecting it clearly.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ComputeClass "BindGroupHelperParameterMemberUniformPass" cannot declare helper method "loadValue"', 'Diagnostic output should name the invalid uniform BindGroup member helper.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into compute().', 'Diagnostic output should explain the supported rewrite shape.');
      }
    }),
    createFixtureCase({
      id: 'framebuffer-depth-last-success',
      title: 'Regression: depth-last framebuffer stays valid',
      group: 'regressions',
      labels: ['regression', 'render', 'depth', 'framebuffer'],
      fixtureDir: 'framebuffer-depth-last-success',
      sourceFile: 'DepthLastSuccess.hpp',
      description: 'Verifies the now-explicit framebuffer contract in its valid form: a depth attachment is allowed, but only when it is the last field.',
      validates: [
        'Valid depth-last framebuffers still generate successfully.',
        'Generated pipeline depth state keeps the requested depth format.',
        'Explicit depth write patterns lower only to shader depth-output semantics.'
      ],
      watchouts: [
        'This protects the positive path while the negative diagnostic path is enforced separately.',
        'Depth compare now requires an explicit setDepthCompareFunction(...) call and must not be inferred from the pattern.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Depth-last fixture should generate a render header.');
        await t.expectFileContains('generate_result.hpp', 'fragmentState.targets.resize(1)', 'Depth attachment should not inflate the color target count.');
        await t.expectFileContains('generate_result.hpp', 'depthStencilState = {.format = GVM::RHI::TextureFormat::Depth32Float', 'Depth pipeline state should preserve the declared depth format.');
        await t.expectFileContains('generate_result.hpp', 'float depth : SV_DepthLessEqual;', 'Less depth write pattern should still lower to the HLSL depth-output semantic.');
        await t.expectFileContains('generate_result.hpp', 'float depth : SV_DepthGreaterEqual;', 'Greater depth write pattern should still lower to the HLSL depth-output semantic.');
        await t.expectFileContains('generate_result.hpp', 'float depth[[depth(less)]];', 'Less depth write pattern should still lower to the MSL depth-output attribute.');
        await t.expectFileContains('generate_result.hpp', 'float depth[[depth(greater)]];', 'Greater depth write pattern should still lower to the MSL depth-output attribute.');
        await t.expectFileNotContains('generate_result.hpp', '.depthCompare =', 'Depth write patterns must not generate implicit pipeline compare state.');
      }
    }),
    createFixtureCase({
      id: 'msl-namespace-regression',
      title: 'Regression: shader reference visitor keeps nested namespaces and template helpers',
      group: 'regressions',
      labels: ['regression', 'msl', 'namespace', 'shader-reference'],
      fixtureDir: 'msl-namespace-regression',
      sourceFile: 'NamespacedShader.hpp',
      description: 'Exercises nested namespace references and a templated helper used from shader code so we keep coverage on the ShaderReferenceVisitor fixes.',
      validates: [
        'Nested helper namespaces referenced by shader code are retained in generated shader text.',
        'Template helper references remain available after reference solving.',
        'Generated MSL does not fall back to the earlier broken namespace shorthand.'
      ],
      watchouts: [
        'If this breaks, generated shaders can miss helper symbols even though the DSL parses correctly.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Namespaced shader fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'namespace RefHelpers', 'Generated shader text should keep the outer helper namespace.');
        await t.expectFileContains('generate_result.hpp', 'namespace Inner', 'Generated shader text should keep the nested helper namespace.');
        await t.expectFileContains('generate_result.hpp', 'RefHelpers::Inner::LocalPayload payload;', 'Generated shader text should preserve fully qualified helper type references instead of flattening namespaces away.');
        await t.expectFileContains('generate_result.hpp', 'identityValue', 'Template helper should appear in generated shader text.');
        await t.expectFileNotContains('generate_result.hpp', 'namespace RefHelpers::NamespacedPass', 'Generated shader text should not use the broken namespace shorthand that mixed class scope into namespace syntax.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-namespace-const-regression',
      title: 'Regression: HLSL keeps namespace-scope constant references qualified',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'namespace', 'constants'],
      fixtureDir: 'hlsl-namespace-const-regression',
      sourceFile: 'NamespaceConstRegression.hpp',
      description: 'Covers the HLSL name-lookup edge case where shader code references a namespace-scope constant without an explicit qualifier from inside the same namespace.',
      validates: [
        'Namespace-scope constant references remain valid after HLSL namespace preservation was enabled.',
        'Generated shader text uses the canonical qualified name when lowering namespace-owned constant references for HLSL.'
      ],
      watchouts: [
        'If this regresses, DXC fails with an undeclared identifier even though the DSL is valid C++ and the constant exists in the same namespace.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Namespace constant regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'inputValue.pos.x * NamespaceConstPassNs::VertexScale', 'Generated shader text should qualify namespace-scope constant references instead of relying on local unqualified lookup.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-global-const-array-static-regression',
      title: 'Regression: Vulkan HLSL keeps namespace const arrays in static storage',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'vulkan', 'namespace', 'constants', 'arrays', 'spirv'],
      fixtureDir: 'hlsl-global-const-array-static-regression',
      sourceFile: 'GlobalConstArrayStaticRegression.hpp',
      description: 'Covers the Vulkan/SPIR-V lowering bug where a namespace-scope `const` aggregate array used from shader code could lose internal-linkage semantics in generated HLSL, causing DXC to synthesize a hidden `$Globals` descriptor binding that the host pipeline layout never declared.',
      validates: [
        'Generated HLSL emits namespace-scope immutable aggregate arrays as `static const` instead of plain `const`.',
        'Shader code can keep indexing those tables directly from vertex entry code without introducing extra bind-group declarations in source emission.'
      ],
      watchouts: [
        'If this regresses, Vulkan validation reports that the entry point requires an unexpected binding even though the DSL only declares the visible bind groups.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Global const-array regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'static const GlobalConstArrayStaticRegressionNs_TableEntry VertexTable[3] = {', 'Generated HLSL should preserve namespace-scope immutable arrays as static const storage.');
        await t.expectFileContains('generate_result.hpp', 'const GlobalConstArrayStaticRegressionNs_TableEntry entry = GlobalConstArrayStaticRegressionNs::VertexTable[vid];', 'Shader entry code should keep reading through the namespace-scoped const table.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-workgroup-size-namespace-const-regression',
      title: 'Regression: HLSL numthreads folds namespace workgroup constants to literals',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'compute', 'namespace', 'workgroup-size'],
      fixtureDir: 'hlsl-workgroup-size-namespace-const-regression',
      sourceFile: 'WorkgroupSizeNamespaceConstRegression.hpp',
      description: 'Covers compute classes whose [[LocalWorkGroupSize(...)]] attribute references a namespace-scope constant so HLSL [numthreads(...)] emission lowers that constant to a legal integer literal instead of leaving a non-constant symbolic reference behind.',
      validates: [
        'HLSL numthreads emission folds namespace constants from LocalWorkGroupSize attributes into integer literals.',
        'Explicitly qualified namespace constants remain legal after lowering to DXC/SPIR-V.'
      ],
      watchouts: [
        'If this regresses, DXC reports that numthreads requires an integer constant or that the synthesized symbolic name is undeclared.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Workgroup-size namespace constant regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '[numthreads(64u, 1, 1)]', 'Generated HLSL should fold namespace constants used by LocalWorkGroupSize attributes into integer literals.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-aggregate-call-arg-regression',
      title: 'Regression: HLSL lowers aggregate call arguments through helper constructors',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'compute', 'aggregate', 'call-arguments'],
      fixtureDir: 'hlsl-aggregate-call-arg-regression',
      sourceFile: 'AggregateCallArgRegression.hpp',
      description: 'Covers compute shaders that pass a braced aggregate directly into a function call so HLSL lowering does not emit an illegal bare `{ ... }` expression.',
      validates: [
        'Aggregate call arguments are lowered into helper-function construction expressions that DXC accepts.',
        'Generated HLSL no longer leaves generalized initializer lists in function-call argument position.'
      ],
      watchouts: [
        'If this regresses, DXC reports `expected expression` on a synthesized call such as `push({ value, 1 })`.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Aggregate call-argument regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '__uglc_make_AggregateCallArgRegression_StackFrame(', 'Generated HLSL should synthesize a helper constructor for aggregate call arguments.');
        await t.expectFileContains('generate_result.hpp', 'stack.push(__uglc_make_AggregateCallArgRegression_StackFrame(threadID.x + 1u, 1))', 'Aggregate call arguments should lower to a helper call instead of a bare braced initializer.');
        await t.expectFileNotContains('generate_result.hpp', 'push({threadID.x + 1u, 1})', 'Generated HLSL must not emit a generalized initializer list directly inside a function call.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-const-conditional-aggregate-regression',
      title: 'Regression: HLSL const aggregate ternary lowers through mutable branch assignment',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'msl', 'conditional', 'aggregate', 'const'],
      fixtureDir: 'hlsl-const-conditional-aggregate-regression',
      sourceFile: 'HLSLConstConditionalAggregateRegression.hpp',
      description: 'Covers helper-local conditional initialization so scalar ternaries stay native while HLSL aggregate ternaries lower to DXC-compatible branch assignment without assigning to a const local.',
      validates: [
        'Scalar conditional initialization remains a direct HLSL ternary.',
        'Struct conditional initialization lowers to a mutable local plus if/else assignment for HLSL/SPIR-V.',
        'MSL keeps the original struct ternary because Metal accepts that form.'
      ],
      watchouts: [
        'Pinned DXC still rejects struct and array conditional operator results, so only scalar/vector/matrix ternaries are emitted directly in HLSL.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Const conditional aggregate regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'const float scalarValue = (useFirst) ? (firstValue.pos.x) : (secondValue.pos.x);', 'HLSL scalar conditional initialization should remain a native ternary expression.');
        await t.expectFileContains('generate_result.hpp', 'HLSLConstConditionalAggregateRegressionVertexOutput chosen;', 'HLSL aggregate conditional lowering should declare a mutable local.');
        await t.expectFileContains('generate_result.hpp', 'chosen = firstValue;', 'HLSL aggregate conditional lowering should assign the true branch value.');
        await t.expectFileContains('generate_result.hpp', 'chosen = secondValue;', 'HLSL aggregate conditional lowering should assign the false branch value.');
        await t.expectFileContains('generate_result.hpp', 'const HLSLConstConditionalAggregateRegressionVertexOutput chosen = (useFirst) ? (firstValue) : (secondValue);', 'MSL should preserve the original struct ternary form.');
        await t.expectFileNotContains('generate_result.hpp', 'const HLSLConstConditionalAggregateRegressionVertexOutput chosen;', 'HLSL should not assign to a const aggregate local after lowering.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-vulkan11-wave-regression',
      title: 'Regression: HLSL/SPIR-V compute wave builtins compile against Vulkan 1.1',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'spirv', 'compute', 'wave', 'vulkan11'],
      fixtureDir: 'hlsl-vulkan11-wave-regression',
      sourceFile: 'HlslVulkan11WaveRegression.hpp',
      description: 'Covers the DXC/SPIR-V path for compute shaders that use WaveGetLaneIndex, WaveGetLaneCount, and WaveReadLaneAt so UGLC keeps targeting a Vulkan environment new enough for subgroup operations.',
      validates: [
        'DXC receives a Vulkan 1.1 SPIR-V target environment for compute shaders that use wave operations.',
        'UGLC can still emit and compile HLSL wave builtins into SPIR-V instead of failing during offline compilation.'
      ],
      watchouts: [
        'If this regresses, DXC reports that Vulkan 1.1 is required for wave intrinsics and asks for -fspv-target-env='
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Vulkan 1.1 wave regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'WaveGetLaneIndex()', 'Generated shader text should still contain WaveGetLaneIndex.');
        await t.expectFileContains('generate_result.hpp', 'WaveGetLaneCount()', 'Generated shader text should still contain WaveGetLaneCount.');
        await t.expectFileContains('generate_result.hpp', 'WaveReadLaneAt(threadID.x + laneIndex, selectedLane)', 'Generated shader text should still contain WaveReadLaneAt in the compute entry.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-renderclass-nested-type-regression',
      title: 'Regression: HLSL preserves nested shader types from render classes',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'render', 'nested-types'],
      fixtureDir: 'hlsl-renderclass-nested-type-regression',
      sourceFile: 'RenderClassNestedTypeRegression.hpp',
      description: 'Covers render-class nested shader structs such as VertexOutput so HLSL can still resolve qualified type names after the render class itself is omitted from shader source emission.',
      validates: [
        'Nested render-class shader structs remain addressable through the original qualified type name.',
        'Generated entry signatures and local declarations stay consistent with the emitted HLSL type scope.'
      ],
      watchouts: [
        'If this regresses, DXC reports `no member named <RenderClass>` in the surrounding namespace for entry return types and local variables.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Render-class nested type regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'class ScopedPass', 'Generated shader text should materialize a render-class scope for nested HLSL shader types.');
        await t.expectFileContains('generate_result.hpp', 'RenderClassNestedType::ScopedPass::VertexOutput vertexMain', 'Entry signatures should keep resolving through the render-class-qualified nested type name.');
        await t.expectFileContains('generate_result.hpp', 'RenderClassNestedType::ScopedPass::VertexOutput outputValue;', 'Local declarations should resolve against the same emitted nested type scope.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-groupshared-atomic-regression',
      title: 'Regression: HLSL groupshared atomics do not leak address-space qualifiers into local temporaries',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'compute', 'groupshared', 'atomic'],
      fixtureDir: 'hlsl-groupshared-atomic-regression',
      sourceFile: 'GroupSharedAtomicRegression.hpp',
      description: 'Covers compute shaders that call atomicStore/atomicOr on plain GroupShared<uint> values so HLSL lowering keeps the groupshared storage global while using a plain local scalar for ignored Interlocked output values.',
      validates: [
        'GroupShared atomics still compile through DXC after lowering to HLSL Interlocked operations.',
        'Ignored old-value temporaries are emitted as plain local scalars rather than illegal local groupshared variables.'
      ],
      watchouts: [
        'If this regresses, DXC reports that `groupshared` is not a valid modifier for a local variable on the synthesized atomic helper block.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Groupshared atomic regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'groupshared unsigned int __uglc_scratch_groupshared', 'Generated shader text should still materialize the group shared storage in global scope.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedExchange(__uglc_scratch_groupshared', 'atomicStore should lower to InterlockedExchange on the groupshared backing storage.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedOr(__uglc_scratch_groupshared', 'atomicOr should lower to InterlockedOr on the groupshared backing storage.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedAnd(__uglc_scalarCounter_groupshared', 'atomicAnd return values should lower to InterlockedAnd on the groupshared backing storage.');
        await t.expectFileContains('generate_result.hpp', 'InterlockedAnd(__uglc_scratch_groupshared', 'Ignored atomicAnd calls should lower to InterlockedAnd on the groupshared backing storage.');
        await t.expectFileNotContains('generate_result.hpp', 'groupshared unsigned int __uglc_atomic_ignored', 'Ignored Interlocked output temporaries must not inherit the groupshared qualifier.');
      }
    }),
    createFixtureCase({
      id: 'hlsl-structured-buffer-atomic-or-swizzle',
      title: 'Regression: HLSL atomicOr on structured-buffer vector-field swizzles uses HLSL scalar temporaries',
      group: 'regressions',
      labels: ['regression', 'hlsl', 'compute', 'atomic', 'structured-buffer'],
      fixtureDir: 'hlsl-structured-buffer-atomic-or-swizzle',
      sourceFile: 'StructuredBufferAtomicOrSwizzle.hpp',
      description: 'Covers ignored and consumed atomicOr return values on RWStructuredBuffer struct fields, including a uint4 swizzle lvalue that resolves to a DSL-side uint_t.',
      validates: [
        'Ignored atomicOr return-value temporaries are emitted as HLSL uint instead of DSL uint_t.',
        'Consumed atomicOr return values on nearby swizzle lvalues use a valid HLSL scalar type.',
        'InterlockedOr still targets the original structured-buffer field swizzle lvalue.'
      ],
      watchouts: [
        'If this regresses, DXC reports `unknown type name uint_t` for the synthesized atomic temporary.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Structured-buffer atomic swizzle regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', '.stateInfo.y, bit, __uglc_atomic_ignored', 'Ignored atomicOr should lower directly on the uint4 field swizzle.');
        await t.expectFileContains('generate_result.hpp', 'uint __uglc_atomic_ignored', 'Ignored atomicOr result temporary should use the native HLSL uint type.');
        await t.expectFileNotContains('generate_result.hpp', 'uint_t __uglc_atomic_ignored', 'Ignored atomicOr result temporary must not use the DSL uint_t alias.');
        await t.expectFileNotContains('generate_result.hpp', 'uint_t __uglc_previous_atomic_result', 'Consumed atomicOr result temporary must not use the DSL uint_t alias.');
      }
    }),
    createFixtureCase({
      id: 'shader-barrier-builtins',
      title: 'Regression: shader memory barrier builtins lower on MSL and HLSL',
      group: 'regressions',
      labels: ['regression', 'compute', 'barrier', 'msl', 'hlsl', 'spirv'],
      fixtureDir: 'shader-barrier-builtins',
      sourceFile: 'ShaderBarrierBuiltins.hpp',
      description: 'Covers the UGL shader memory barrier intrinsics through both generated shader backends so explicit `UGL::` calls do not leak into backend source.',
      validates: [
        'DeviceMemoryBarrierWithGroupSync lowers to a Metal device-memory threadgroup barrier.',
        'The HLSL/SPIR-V path keeps the native HLSL barrier intrinsic spelling for DXC.',
        'The generated backend source does not retain the C++ `UGL::` qualifier.'
      ],
      watchouts: [
        'Without this regression, explicit UGL-qualified barrier calls can survive into MSL as invalid namespace-qualified function calls.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Shader barrier fixture should generate successfully.');

        const content = await t.readArtifact('generate_result.hpp');
        const mslOpenMarker = '__UGL__Global__MSLHeader + R"(';
        const mslStart = content.indexOf(mslOpenMarker);
        const mslContentStart = mslStart === -1 ? -1 : mslStart + mslOpenMarker.length;
        const mslEnd = mslContentStart === -1 ? -1 : content.indexOf(')",', mslContentStart);
        const hlslStart = mslEnd === -1 ? -1 : content.indexOf('R"(', mslEnd);
        const hlslContentStart = hlslStart === -1 ? -1 : hlslStart + 'R"('.length;
        const hlslEnd = hlslContentStart === -1 ? -1 : content.indexOf(')",', hlslContentStart);
        const mslSource = mslContentStart === -1 || mslEnd === -1 ? '' : content.slice(mslContentStart, mslEnd);
        const hlslSource = hlslContentStart === -1 || hlslEnd === -1 ? '' : content.slice(hlslContentStart, hlslEnd);

        t.recordCheck('MSL shader source should be extractable from the generated artifact.', mslSource.length > 0, mslSource.length > 0 ? 'Extracted MSL shader source.' : 'Could not locate the MSL shader source raw string.');
        t.recordCheck('HLSL shader source should be extractable from the generated artifact.', hlslSource.length > 0, hlslSource.length > 0 ? 'Extracted HLSL shader source.' : 'Could not locate the HLSL shader source raw string.');
        t.recordCheck('MSL DeviceMemoryBarrierWithGroupSync should lower to a device-memory threadgroup barrier.', mslSource.includes('threadgroup_barrier(mem_flags::mem_device);'), 'Expected `threadgroup_barrier(mem_flags::mem_device);` in emitted MSL.');
        t.recordCheck('MSL AllMemoryBarrierWithGroupSync should include both device and threadgroup memory flags.', mslSource.includes('threadgroup_barrier(mem_flags::mem_device | mem_flags::mem_threadgroup);'), 'Expected combined Metal barrier flags in emitted MSL.');
        t.recordCheck('MSL GroupMemoryBarrierWithGroupSync should lower to a threadgroup-memory barrier.', mslSource.includes('threadgroup_barrier(mem_flags::mem_threadgroup);'), 'Expected threadgroup Metal barrier in emitted MSL.');
        t.recordCheck('MSL no-sync device barrier should lower to a device memory fence.', mslSource.includes('atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed);'), 'Expected device Metal fence in emitted MSL.');
        t.recordCheck('MSL no-sync group barrier should lower to a threadgroup memory fence.', mslSource.includes('atomic_thread_fence(mem_flags::mem_threadgroup, memory_order_seq_cst);'), 'Expected threadgroup Metal fence in emitted MSL.');
        t.recordCheck('MSL shader source should not retain UGL-qualified barrier calls.', !mslSource.includes('UGL::DeviceMemoryBarrierWithGroupSync'), 'MSL source must contain backend syntax, not the C++ UGL-qualified call.');
        t.recordCheck('HLSL shader source should keep the native DeviceMemoryBarrierWithGroupSync spelling.', hlslSource.includes('DeviceMemoryBarrierWithGroupSync();'), 'Expected native HLSL DeviceMemoryBarrierWithGroupSync call.');
        t.recordCheck('HLSL shader source should keep the native AllMemoryBarrierWithGroupSync spelling.', hlslSource.includes('AllMemoryBarrierWithGroupSync();'), 'Expected native HLSL AllMemoryBarrierWithGroupSync call.');
        t.recordCheck('HLSL shader source should not retain UGL-qualified barrier calls.', !hlslSource.includes('UGL::DeviceMemoryBarrierWithGroupSync'), 'HLSL source must contain native barrier calls, not the C++ UGL-qualified call.');
        await t.expectFileContains('generate_result.hpp', 'static constexpr uint32_t computeShaderArtifact_SpirvWords[] = {', 'Barrier fixture should compile through DXC and embed SPIR-V.');
      }
    }),
    createFixtureCase({
      id: 'msl-parameter-commas',
      title: 'Regression: MSL entry signature keeps commas without bind groups',
      group: 'regressions',
      labels: ['regression', 'msl', 'signature', 'parameters'],
      fixtureDir: 'msl-parameter-commas',
      sourceFile: 'ParameterCommaRegression.hpp',
      description: 'Covers the MSL entry generation edge case where there is no bind group but there are multiple non-bind-group parameters in the vertex entry.',
      validates: [
        'Generated vertex entry signature keeps comma separation when no bind-group parameters exist.',
        'Instance-id backup parameter remains visible in generated MSL.'
      ],
      watchouts: [
        'This is easy to regress because the separator logic depends on optional bind-group emission.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Comma regression fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'vertexMain(uint vid [[vertex_id]], ParamVertexInput inValue [[stage_in]], uint instanceId [[instance_id]])', 'Vertex entry signature should keep commas between non-bind-group parameters.');
      }
    }),
    createFixtureCase({
      id: 'wave-global-builtins',
      title: 'Regression: global wave builtins lower to collision-safe Metal simdgroup parameters',
      group: 'regressions',
      labels: ['regression', 'compute', 'wave', 'msl', 'builtins'],
      fixtureDir: 'wave-global-builtins',
      sourceFile: 'WaveGlobalBuiltins.hpp',
      description: 'Covers the new global `WaveGetLaneIndex()` / `WaveGetLaneCount()` path, including helper-function propagation and hidden-parameter collision avoidance when user code already declares the base reserved names.',
      validates: [
        'Compute entrypoints inject `thread_index_in_simdgroup` and `threads_per_simdgroup` as hidden Metal parameters.',
        'Referenced helper functions receive the same hidden parameters through their generated signatures and callsites.',
        'Hidden parameter names avoid collisions with user-declared locals that already use the preferred base names.'
      ],
      watchouts: [
        'This path is easy to break if call translation, function-signature generation, or hidden-name allocation drift apart.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Wave builtin fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'kernel void computeMain(uint3 threadID [[thread_position_in_grid]], uint __uglc_hidden_wave_lane_index_1 [[thread_index_in_simdgroup]], uint __uglc_hidden_wave_lane_count_1 [[threads_per_simdgroup]], const constant WaveGlobalBuiltinsBindGroup* bindGroup [[buffer(0)]])', 'Compute entry should inject collision-safe Metal simdgroup builtin parameters.');
        await t.expectFileContains('generate_result.hpp', 'AccumulateWaveInfo( uint baseValue, uint __uglc_hidden_wave_lane_index_1, uint __uglc_hidden_wave_lane_count_1)', 'Helper functions should receive propagated hidden wave builtin parameters.');
        await t.expectFileContains('generate_result.hpp', 'return (baseValue + __uglc_hidden_wave_lane_index_1) + __uglc_hidden_wave_lane_count_1;', 'Wave builtin calls should lower directly to the hidden Metal builtin variables.');
        await t.expectFileContains('generate_result.hpp', 'WaveHelpers::AccumulateWaveInfo(threadID.x, __uglc_hidden_wave_lane_index_1, __uglc_hidden_wave_lane_count_1)', 'Callsites should forward hidden wave builtin parameters through helper chains.');
      }
    }),
    createFixtureCase({
      id: 'wave-read-lane-at',
      title: 'Regression: WaveReadLaneAt lowers to a real Metal simdgroup shuffle',
      group: 'regressions',
      labels: ['regression', 'compute', 'wave', 'shuffle', 'msl'],
      fixtureDir: 'wave-read-lane-at',
      sourceFile: 'WaveReadLaneAt.hpp',
      description: 'Verifies the Metal backend no longer lowers `WaveReadLaneAt` to a quad operation and instead emits a real simdgroup shuffle helper for compute shaders.',
      validates: [
        'The generated Metal helper for `WaveReadLaneAt` uses `simd_shuffle` instead of `quad_broadcast`.',
        'Compute shaders can still call helper functions that use `WaveReadLaneAt` without disturbing the main codegen path.'
      ],
      watchouts: [
        'This is the core correctness fix for subgroup-wide reads on Metal compute.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'WaveReadLaneAt fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'inline T WaveReadLaneAt(T x, uint index)', 'Generated MSL header should expose the updated WaveReadLaneAt helper signature.');
        await t.expectFileContains('generate_result.hpp', 'return simd_shuffle(x, ushort(index));', 'WaveReadLaneAt should lower to a real Metal simdgroup shuffle.');
        await t.expectFileContains('generate_result.hpp', 'WaveLaneAtHelpers::BroadcastLaneZero(threadID.x)', 'Helper calls using WaveReadLaneAt should remain visible in generated shader code.');
      }
    }),
    createFixtureCase({
      id: 'wave-collectives',
      title: 'Regression: radix-sort wave collectives lower to Metal simdgroup operations',
      group: 'regressions',
      labels: ['regression', 'compute', 'wave', 'ballot', 'prefix', 'match'],
      fixtureDir: 'wave-collectives',
      sourceFile: 'WaveCollectives.hpp',
      description: 'Covers the new radix-sort-oriented wave collective surface so ballot, match, prefix-count, prefix-sum, and first-lane broadcast all keep their intended Metal lowering and helper propagation behavior.',
      validates: [
        'Wave ballots lower through a deterministic uint4 mask packing helper.',
        'Wave prefix-count and prefix-sum lower to Metal simdgroup prefix intrinsics.',
        'WaveMatch propagates hidden lane index/count metadata through helper chains.'
      ],
      watchouts: [
        'This is the wave feature block most likely to be used in radix sort and other subgroup-heavy compute algorithms, so drift here would be expensive to debug downstream.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Wave collectives fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'inline uint4 UGLC_WaveActiveBallot(bool predicate, uint laneCount)', 'Generated MSL header should expose the packed wave ballot helper.');
        await t.expectFileContains('generate_result.hpp', 'return UGLC_WaveMaskFromRaw((ulong)static_cast<simd_vote::vote_t>(simd_ballot(predicate)), laneCount);', 'WaveActiveBallot should lower through simd_ballot and explicit mask packing.');
        await t.expectFileContains('generate_result.hpp', 'inline uint WaveActiveCountBits(bool predicate)', 'Generated MSL header should expose WaveActiveCountBits.');
        await t.expectFileContains('generate_result.hpp', 'return simd_sum(predicate ? 1u : 0u);', 'WaveActiveCountBits should lower to a simdgroup sum of predicate bits.');
        await t.expectFileContains('generate_result.hpp', 'inline uint WavePrefixCountBits(bool predicate)', 'Generated MSL header should expose WavePrefixCountBits.');
        await t.expectFileContains('generate_result.hpp', 'return simd_prefix_exclusive_sum(predicate ? 1u : 0u);', 'WavePrefixCountBits should lower to an exclusive simdgroup prefix sum.');
        await t.expectFileContains('generate_result.hpp', 'inline T WavePrefixSum(T x)', 'Generated MSL header should expose WavePrefixSum.');
        await t.expectFileContains('generate_result.hpp', 'inline T WaveReadLaneFirst(T x)', 'Generated MSL header should expose WaveReadLaneFirst.');
        await t.expectFileContains('generate_result.hpp', 'return simd_broadcast_first(x);', 'WaveReadLaneFirst should lower to a first-lane simd broadcast.');
        await t.expectFileContains('generate_result.hpp', 'inline uint4 UGLC_WaveMatch(T value, uint laneIndex, uint laneCount)', 'Generated MSL header should expose the WaveMatch fallback helper.');
        await t.expectFileContains('generate_result.hpp', 'UGLC_WaveActiveBallot(isEven, __uglc_hidden_wave_lane_count)', 'Wave ballot calls should consume the propagated hidden wave lane count.');
        await t.expectFileContains('generate_result.hpp', 'UGLC_WaveMatch(value & 3u, __uglc_hidden_wave_lane_index, __uglc_hidden_wave_lane_count)', 'WaveMatch should consume the propagated hidden lane index and lane count.');
        await t.expectFileContains('generate_result.hpp', 'WaveCollectiveHelpers::ComputeWaveSummary(threadID.x, __uglc_hidden_wave_lane_index, __uglc_hidden_wave_lane_count)', 'Helper callsites should forward the hidden wave collective metadata.');
      }
    }),
    createFixtureCase({
      id: 'msl-rg8sint-framebuffer',
      title: 'Regression: RG8Sint framebuffer outputs preserve signed two-channel MSL type',
      group: 'regressions',
      labels: ['regression', 'msl', 'format', 'framebuffer'],
      fixtureDir: 'msl-rg8sint-framebuffer',
      sourceFile: 'RG8SintFrameBuffer.hpp',
      description: 'Exercises the MSL framebuffer return-type path with `TextureFormat::RG8Sint` so the generated fragment output struct keeps the declared signed two-channel attachment ABI.',
      validates: [
        'The generated MSL framebuffer struct emits `short2` for `RG8Sint` color attachments.',
        'The old scalar `int` fallback does not reappear in fragment output records.'
      ],
      watchouts: [
        'This path is separate from texture object lowering, so it needs its own regression coverage.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'RG8Sint framebuffer fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'short2 color[[color(0)]]', 'RG8Sint framebuffer outputs should lower to a signed two-channel MSL type.');
        await t.expectFileNotContains('generate_result.hpp', 'int color[[color(0)]]', 'RG8Sint framebuffer outputs should not collapse to a scalar int.');
      }
    }),
    createFixtureCase({
      id: 'host-shader-only-stubs',
      title: 'Regression: host-visible shader-only paths lower to explicit fail-fast stubs',
      group: 'regressions',
      labels: ['regression', 'host', 'shader-only', 'wave', 'textures'],
      fixtureDir: 'host-shader-only-stubs',
      sourceFile: 'HostShaderOnlyStubs.hpp',
      description: 'Protects the generated host C++ side against silent fake implementations. Namespace helper functions that reference texture sampling, texture reads/writes, or wave intrinsics should stay visible in generated code, but route through explicit host-only fail-fast stubs instead of returning fabricated values or empty bodies.',
      validates: [
        'Shader-only texture and wave helper functions remain visible in the generated host surface, but lower to deleted declarations instead of fake CPU implementations.',
        'Wave/quad helper stubs in the generated single header stay includable and compile-time-only by using deleted declarations.',
        'The generated header can be included by host code without errors until a deleted shader-only API is actually called.'
      ],
      watchouts: [
        'This is a trust-boundary regression: users should never mistake generated host code for a real CPU implementation of GPU-only operations.'
      ],
      hostCompileSteps: [
        {
          id: 'host-compile-include-only',
          title: 'Compile generated header without calling deleted shader-only APIs',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return 0; }\n',
          expectedExitCode: 0
        },
        {
          id: 'host-compile-call-deleted',
          title: 'Compile host code that incorrectly calls a deleted shader-only helper',
          sourceText: '#include "__GENERATED_HEADER__"\nint main() { return (int)HostShaderOnlyHelpers::previewWave(1u); }\n',
          expectedExitCode: 1
        }
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Host shader-only stub fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'T WaveReadLaneAt(T value, unsigned int laneIndex) = delete;', 'Generated host wave helper stubs should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'T WaveReadLaneFirst(T value) = delete;', 'Generated host first-lane wave helpers should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'T WavePrefixSum(T value) = delete;', 'Generated host wave prefix helpers should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'struct UGLC_HostWaveMask', 'Generated host wave helper stubs should declare a local placeholder mask type.');
        await t.expectFileContains('generate_result.hpp', 'inline UGLC_HostWaveMask WaveActiveBallot(bool predicate) = delete;', 'Generated host wave ballot helpers should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'UGLC_HostWaveMask WaveMatch(T value) = delete;', 'Generated host wave match helpers should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'inline unsigned int WaveGetLaneCount() = delete;', 'Generated host wave builtin accessors should use deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewSample(eastl::intrusive_ptr<HostShaderOnlyBindGroup> bindGroup, float2 uv) = delete;', 'Host-visible texture sampling helpers should be emitted as deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewGather(eastl::intrusive_ptr<HostShaderOnlyBindGroup> bindGroup, float2 uv) = delete;', 'Host-visible texture gather helpers should be emitted as deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewRead(eastl::intrusive_ptr<HostShaderOnlyBindGroup> bindGroup, uint2 coord) = delete;', 'Host-visible texture read helpers should be emitted as deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewWrite(eastl::intrusive_ptr<HostShaderOnlyBindGroup> bindGroup, uint2 coord, float4 value) = delete;', 'Host-visible texture write helpers should be emitted as deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewDimensions(eastl::intrusive_ptr<HostShaderOnlyBindGroup> bindGroup, uint width, uint height) = delete;', 'Host-visible texture dimension helpers should be emitted as deleted declarations.');
        await t.expectFileContains('generate_result.hpp', 'previewWave(uint value) = delete;', 'Host-visible wave helpers should be emitted as deleted declarations.');
        await t.expectStepOutputContains('host-compile-call-deleted', 'attempt to use a deleted function', 'Host-side misuse should fail at compile time when a deleted helper is called.');
        await t.expectStepOutputContains('host-compile-call-deleted', 'previewWave', 'The compile-time failure should point at the deleted shader-only helper name.');
        await t.expectFileNotContains('generate_result.hpp', '[]<class __UGLC_Dummy = void>()', 'Generated host methods should no longer contain immediately-invoked compile-fail lambdas.');
        await t.expectFileNotContains('generate_result.hpp', 'float4<float4>', 'Host shader-only stub lowering should not synthesize invalid vector template types.');
        await t.expectFileNotContains('generate_result.hpp', '#include <stdexcept>', 'Generated host stub support should not pull in unnecessary exception headers.');
        await t.expectFileNotContains('generate_result.hpp', 'throw std::runtime_error', 'Host shader-only stubs should not rely on runtime exceptions.');
      }
    }),
    createFixtureCase({
      id: 'quad-builtins-render',
      title: 'Regression: explicit QuadRead* intrinsics lower to quad shuffles',
      group: 'regressions',
      labels: ['regression', 'render', 'quad', 'msl', 'intrinsics'],
      fixtureDir: 'quad-builtins-render',
      sourceFile: 'QuadBuiltinsRender.hpp',
      description: 'Adds direct coverage for the new explicit quad API so render shaders can opt into quad semantics without borrowing the wave naming surface.',
      validates: [
        'The Metal header exposes `QuadReadLaneAt`, `QuadReadAcrossX`, `QuadReadAcrossY`, and `QuadReadAcrossDiagonal`.',
        'The generated helper implementations use Metal quad intrinsics instead of simdgroup shuffles.'
      ],
      watchouts: [
        'This locks in the semantic split between subgroup wave operations and quad-local render helpers.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Quad builtin fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'inline T QuadReadLaneAt(T x, uint index)', 'Generated MSL header should include the explicit QuadReadLaneAt helper.');
        await t.expectFileContains('generate_result.hpp', 'return quad_shuffle(x, ushort(index));', 'QuadReadLaneAt should lower to a quad shuffle.');
        await t.expectFileContains('generate_result.hpp', 'return quad_shuffle_xor(x,1);', 'QuadReadAcrossX should lower to the expected quad xor shuffle.');
        await t.expectFileContains('generate_result.hpp', 'return quad_shuffle_xor(x,2);', 'QuadReadAcrossY should lower to the expected quad xor shuffle.');
        await t.expectFileContains('generate_result.hpp', 'return quad_shuffle_xor(x,3);', 'QuadReadAcrossDiagonal should lower to the expected quad xor shuffle.');
      }
    }),
    createFixtureCase({
      id: 'legacy-wave-across-render',
      title: 'Regression: legacy WaveReadAcross* stays compatible for render quad code',
      group: 'regressions',
      labels: ['regression', 'render', 'quad', 'compatibility', 'wave'],
      fixtureDir: 'legacy-wave-across-render',
      sourceFile: 'LegacyWaveAcrossRender.hpp',
      description: 'Protects existing render code that still uses the old `WaveReadAcross*` names for quad-local behavior while the DSL migrates to the explicit `QuadRead*` API.',
      validates: [
        'Legacy WaveReadAcrossX/Y/Diagonal calls continue to generate successfully for render shaders.',
        'The generated Metal helpers route the legacy names through the new quad helpers instead of pretending they are wave operations.'
      ],
      watchouts: [
        'Some downstream integrations still use these legacy names, so this compatibility path needs explicit regression coverage.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Legacy render quad fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'return QuadReadAcrossX(x);', 'Legacy WaveReadAcrossX should route through the explicit quad helper.');
        await t.expectFileContains('generate_result.hpp', 'return QuadReadAcrossY(x);', 'Legacy WaveReadAcrossY should route through the explicit quad helper.');
        await t.expectFileContains('generate_result.hpp', 'return QuadReadAcrossDiagonal(x);', 'Legacy WaveReadAcrossDiagonal should route through the explicit quad helper.');
        await t.expectFileContains('generate_result.hpp', 'WaveReadAcrossDiagonal(WaveReadAcrossY(WaveReadAcrossX(inputValue.color)))', 'Render shader bodies should still accept legacy quad-shaped WaveReadAcross helper chains.');
      }
    }),
    createFixtureCase({
      id: 'invalid-depth-middle',
      title: 'Diagnostic: depth attachment in the middle is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'depth', 'validation'],
      fixtureDir: 'invalid-depth-middle',
      sourceFile: 'InvalidDepthMiddle.hpp',
      description: 'Confirms the compiler now emits a clear error when a framebuffer places a depth attachment before later color attachments.',
      validates: [
        'The command exits with a non-zero status for invalid depth placement.',
        'The diagnostic message explains that depth must be the last framebuffer field.'
      ],
      watchouts: [
        'This protects the DSL contract documented in the guide and avoids silently generating invalid render-target wiring.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires depth attachment to be the last field', 'Diagnostic output should explain the depth-last requirement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-read-write-same-field',
      title: 'Diagnostic: pixel-local same-phase read/write is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-read-write-same-field',
      sourceFile: 'InvalidPixelLocalReadWriteSameField.hpp',
      description: 'Confirms the compiler rejects a pixel-local entry that reads and writes the same attachment inside one phase.',
      validates: [
        'Pixel-local read/write hazards fail at UGLC codegen time.',
        'The diagnostic tells users to split the dependency with nextPixelLocalPass().'
      ],
      watchouts: [
        'This is the source-level guard for undefined same-phase pixel-local data dependencies.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'reads and writes the same color attachment in one phase', 'Diagnostic output should explain the pixel-local read/write hazard.');
        await t.expectStepOutputContains('uglc', 'nextPixelLocalPass()', 'Diagnostic output should suggest an explicit pixel-local phase boundary.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-missing-next-pass',
      title: 'Diagnostic: pixel-local cross-task read/write without boundary is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-missing-next-pass',
      sourceFile: 'InvalidPixelLocalMissingNextPass.hpp',
      description: 'Confirms the compiler rejects a pixel-local pass where one task reads attachments written by an earlier task without nextPixelLocalPass().',
      validates: [
        'Pixel-local renderPass phase access is checked across task boundaries during UGLC lowering.',
        'The diagnostic points at the missing nextPixelLocalPass() phase boundary.'
      ],
      watchouts: [
        'RHI still keeps a runtime guard, but this fixture protects the source-level compiler diagnostic.',
        'The invalid pixelLocalPass is stored in a local variable first, so validation must happen at pixelLocalPass construction time rather than only while translating queue->renderPass arguments.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'Pixel-local renderPass phase reads and writes the same color attachment across tasks', 'Diagnostic output should explain the cross-task pixel-local hazard.');
        await t.expectStepOutputContains('uglc', 'nextPixelLocalPass()', 'Diagnostic output should suggest an explicit pixel-local phase boundary.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-depth-read-metal',
      title: 'Diagnostic: native pixel-local depth reads are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'depth', 'portable'],
      fixtureDir: 'invalid-pixel-local-depth-read-metal',
      sourceFile: 'InvalidPixelLocalDepthReadMetal.hpp',
      description: 'Confirms PixelLocalDepthAttachment does not expose read(), preventing backend-specific native depth input code.',
      validates: [
        'Portable pixelLocal only exposes color/local-storage reads.',
        'Lighting depth must be authored as an explicit GBuffer PixelLocalColorAttachment.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', "no member named 'read'", 'Diagnostic output should reject native depth reads at the DSL type level.');
        await t.expectStepOutputContains('uglc', 'PixelLocalDepthAttachment', 'Diagnostic output should identify the depth pixel-local attachment type.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-depth-write',
      title: 'Diagnostic: pixel-only passes cannot write native depth',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'depth', 'portable'],
      fixtureDir: 'invalid-pixel-local-depth-write',
      sourceFile: 'InvalidPixelLocalDepthWrite.hpp',
      description: 'Confirms IPixelLocalRenderClass::pixel() cannot write PixelLocalDepthAttachment fields; native depth writes belong to raster fragment passes.',
      validates: [
        'Pixel-only passes remain color attachment operations.',
        'Portable native depth output is authored through IRenderClass::fragment().'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'IPixelLocalRenderClass::pixel() cannot write PixelLocalDepthAttachment', 'Diagnostic output should reject depth writes from pixel-only passes.');
        await t.expectStepOutputContains('uglc', 'IRenderClass::fragment()', 'Diagnostic output should point users to raster fragment depth writes.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-native-depth-write',
      title: 'Diagnostic: pixel-only passes cannot write ordinary depth attachments',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'depth', 'portable'],
      fixtureDir: 'invalid-pixel-local-native-depth-write',
      sourceFile: 'InvalidPixelLocalNativeDepthWrite.hpp',
      description: 'Confirms IPixelLocalRenderClass::pixel() rejects ordinary DepthStencilAttachment writes as well as PixelLocalDepthAttachment writes.',
      validates: [
        'Pixel-only passes cannot enable a native depth output path through ordinary depth attachments.',
        'Depth output remains a raster IRenderClass::fragment() responsibility.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'IPixelLocalRenderClass::pixel() cannot write depth attachment', 'Diagnostic output should reject ordinary depth writes from pixel-only passes.');
        await t.expectStepOutputContains('uglc', 'IRenderClass::fragment()', 'Diagnostic output should point users to raster fragment depth writes.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-copy-attachment',
      title: 'Diagnostic: pixel-local attachments cannot be copied out of input fields',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-copy-attachment',
      sourceFile: 'InvalidPixelLocalCopyAttachment.hpp',
      description: 'Confirms PixelLocalColorAttachment is not a value object users can copy before calling read().',
      validates: [
        'Pixel-local attachment source copies fail at the DSL type level.',
        'Users must call read() directly on a [[PixelLocalInput]] framebuffer field.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalColorAttachment', 'Diagnostic output should identify the copied pixel-local attachment type.');
        await t.expectStepOutputContains('uglc', 'deleted', 'Diagnostic output should explain that copying the attachment value is not allowed.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-move-attachment',
      title: 'Diagnostic: pixel-local attachments cannot be moved out of input fields',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-move-attachment',
      sourceFile: 'InvalidPixelLocalMoveAttachment.hpp',
      description: 'Confirms PixelLocalColorAttachment cannot be moved into a value object before calling read().',
      validates: [
        'Pixel-local attachment source moves fail at the DSL type level.',
        'Users must call read() directly on a [[PixelLocalInput]] framebuffer field.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalColorAttachment', 'Diagnostic output should identify the moved pixel-local attachment type.');
        await t.expectStepOutputContains('uglc', 'deleted', 'Diagnostic output should explain that moving the attachment value is not allowed.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-write-only-read',
      title: 'Diagnostic: WriteOnly pixel-local attachments cannot be read',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-write-only-read',
      sourceFile: 'InvalidPixelLocalWriteOnlyRead.hpp',
      description: 'Confirms PixelLocalAccess::WriteOnly is enforced from the enum declaration rather than hardcoded numeric values.',
      validates: [
        'WriteOnly pixel-local attachment reads fail during UGLC lowering.',
        'The diagnostic names the incompatible access policy.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalAccess::WriteOnly but is read', 'Diagnostic output should reject reading a WriteOnly pixel-local attachment.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-read-only-write',
      title: 'Diagnostic: ReadOnly pixel-local attachments cannot be written',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-read-only-write',
      sourceFile: 'InvalidPixelLocalReadOnlyWrite.hpp',
      description: 'Confirms PixelLocalAccess::ReadOnly is enforced from the enum declaration rather than hardcoded numeric values.',
      validates: [
        'ReadOnly pixel-local attachment writes fail during UGLC lowering.',
        'The diagnostic names the incompatible access policy.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalAccess::ReadOnly but is written', 'Diagnostic output should reject writing a ReadOnly pixel-local attachment.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-transient-persistent-policy',
      title: 'Diagnostic: transient pixel-local attachments cannot request persistent load/store',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-transient-persistent-policy',
      sourceFile: 'InvalidPixelLocalTransientPersistentPolicy.hpp',
      description: 'Confirms PixelLocalStorage::Transient rejects PixelLocalLoad::Load and PixelLocalStore::Store semantics.',
      validates: [
        'Transient pixel-local attachments cannot request persistent load/store behavior.',
        'The policy diagnostic is emitted while lowering framebuffer descriptors.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalStorage::Transient', 'Diagnostic output should identify the transient storage policy.');
        await t.expectStepOutputContains('uglc', 'persistent load/store semantics', 'Diagnostic output should reject persistent semantics on transient storage.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-output-read',
      title: 'Diagnostic: pixel-local output attachment reads are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-output-read',
      sourceFile: 'InvalidPixelLocalOutputRead.hpp',
      description: 'Confirms PixelLocalColorAttachment::read() is only valid on direct [[PixelLocalInput]] framebuffer fields.',
      validates: [
        'Pixel-local reads from output framebuffer fields fail during shader lowering.',
        'Generated metadata cannot drift from shader read calls.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalColorAttachment::read() must be called directly', 'Diagnostic output should explain the direct PixelLocalInput field rule.');
        await t.expectStepOutputContains('uglc', '[[PixelLocalInput]]', 'Diagnostic output should name the required input annotation.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-local-read',
      title: 'Diagnostic: local framebuffer pixel-local reads are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-local-read',
      sourceFile: 'InvalidPixelLocalLocalRead.hpp',
      description: 'Confirms PixelLocalColorAttachment::read() cannot be called on ordinary local framebuffer values.',
      validates: [
        'Pixel-local reads are tied to a direct [[PixelLocalInput]] parameter field.',
        'Local framebuffer values cannot invent pixel-local input metadata.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalColorAttachment::read() must be called directly', 'Diagnostic output should explain the direct PixelLocalInput field rule.');
        await t.expectStepOutputContains('uglc', '[[PixelLocalInput]]', 'Diagnostic output should name the required input annotation.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-raster-entry',
      title: 'Diagnostic: IPixelLocalRenderClass raster entries are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-raster-entry',
      sourceFile: 'InvalidPixelLocalRasterEntry.hpp',
      description: 'Confirms IPixelLocalRenderClass is reserved for pixel-only passes and raster GBuffer producers must derive from IRenderClass.',
      validates: [
        'Pixel-local classes declaring vertex()/fragment() fail at UGLC codegen time.',
        'The diagnostic points users to IRenderClass for raster producers inside pixelLocalPass().'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalRenderClass "InvalidPixelLocalRasterPass" cannot declare helper method "vertex"', 'Diagnostic output should identify the invalid pixel-local render class and method.');
        await t.expectStepOutputContains('uglc', 'Move helper logic outside the shader class or inline it into pixel().', 'Diagnostic output should explain the supported pixel-local entry boundary.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-input-in-render',
      title: 'Diagnostic: IRenderClass cannot declare PixelLocalInput parameters',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-input-in-render',
      sourceFile: 'InvalidPixelLocalInputInRender.hpp',
      description: 'Confirms [[PixelLocalInput]] is only valid on IPixelLocalRenderClass::pixel(...).',
      validates: [
        'Raster render classes cannot consume pixel-local input attachments directly.',
        'The diagnostic names the pixel-only entry point that owns PixelLocalInput.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', '[[PixelLocalInput]] parameters are only valid', 'Diagnostic output should reject PixelLocalInput outside pixel().');
        await t.expectStepOutputContains('uglc', 'IPixelLocalRenderClass::pixel', 'Diagnostic output should identify the allowed entry.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-read-in-render',
      title: 'Diagnostic: IRenderClass cannot read pixel-local attachment values',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-read-in-render',
      sourceFile: 'InvalidPixelLocalReadInRender.hpp',
      description: 'Confirms PixelLocalColorAttachment::read() is only valid on direct PixelLocalInput fields inside IPixelLocalRenderClass::pixel(...).',
      validates: [
        'Raster render classes cannot call PixelLocalColorAttachment::read().',
        'The diagnostic preserves the direct PixelLocalInput field rule.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'PixelLocalColorAttachment::read() must be called directly', 'Diagnostic output should reject read() outside pixel().');
        await t.expectStepOutputContains('uglc', 'IPixelLocalRenderClass::pixel', 'Diagnostic output should identify the allowed entry.');
      }
    }),
    createFixtureCase({
      id: 'invalid-pixel-local-run-with-extent',
      title: 'Diagnostic: IPixelLocalRenderClass tasks no longer accept pixel extents',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'pixel-local', 'validation'],
      fixtureDir: 'invalid-pixel-local-run-with-extent',
      sourceFile: 'InvalidPixelLocalRunWithExtent.hpp',
      description: 'Confirms pixel-local pixel-only tasks are full-attachment tasks and cannot be invoked with width/height.',
      validates: [
        'Pixel-local RenderClass operator() is parameterless.',
        'Old extent-style pixel-local task calls fail at source level.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'no matching function', 'Diagnostic output should reject width/height pixel-local task invocation.');
        await t.expectStepOutputContains('uglc', 'InvalidPixelLocalRunWithExtent', 'Diagnostic output should identify the invalid fixture.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-attribute',
      title: 'Diagnostic: missing vertex [[AttributeN]] is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'vertex', 'validation', 'layout'],
      fixtureDir: 'invalid-missing-attribute',
      sourceFile: 'InvalidMissingAttribute.hpp',
      description: 'Verifies the compiler reports a clear error when a vertex input field is missing an explicit `[[AttributeN]]` annotation.',
      validates: [
        'Invalid vertex layouts fail fast.',
        'The error message points to the missing explicit attribute requirement.'
      ],
      watchouts: [
        'This prevents silent shaderLocation drift between DSL struct layout and generated vertex descriptors.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires every field of vertex input', 'Diagnostic output should explain the explicit vertex attribute requirement.');
        await t.expectStepOutputContains('uglc', 'Field "color" is missing one', 'Diagnostic output should identify the missing field.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-varying-attribute',
      title: 'Diagnostic: missing render varying [[AttributeN]] is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'varying', 'validation'],
      fixtureDir: 'invalid-missing-varying-attribute',
      sourceFile: 'InvalidMissingVaryingAttribute.hpp',
      description: 'Verifies the compiler fails before HLSL emission when a non-system vertex output or fragment input field omits its explicit `[[AttributeN]]` annotation.',
      validates: [
        'Invalid render varying layouts fail fast before DXC sees malformed HLSL.',
        'The error message points to the missing explicit varying attribute requirement.'
      ],
      watchouts: [
        'Without this guard, render passes can slip through codegen and only fail later with opaque `semantic string missing` diagnostics from DXC.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires every non-system field of vertex output', 'Diagnostic output should explain the explicit varying attribute requirement.');
        await t.expectStepOutputContains('uglc', 'Field "color" is missing one', 'Diagnostic output should identify the missing varying field.');
      }
    }),
    createFixtureCase({
      id: 'invalid-varying-type-mismatch',
      title: 'Diagnostic: vertex/fragment varyings must agree on types',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'varying', 'validation', 'types'],
      fixtureDir: 'invalid-varying-type-mismatch',
      sourceFile: 'InvalidVaryingTypeMismatch.hpp',
      description: 'Verifies fragment inputs cannot reuse a vertex varying location with a different field type.',
      validates: [
        'Matching [[AttributeN]] locations must agree on field types across vertex output and fragment input.',
        'The diagnostic identifies both records and the conflicting field types.'
      ],
      watchouts: [
        'Without this, semantic/interface mismatches leak into backend shader compilation with much less actionable diagnostics.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'fragment input "InvalidVaryingTypeMismatchFragmentInput" field "color" declares [[Attribute0]] with type', 'Diagnostic output should identify the mismatched fragment varying field.');
        await t.expectStepOutputContains('uglc', 'vertex output "InvalidVaryingTypeMismatchVertexOutput" field "color" uses type', 'Diagnostic output should identify the conflicting vertex varying field.');
      }
    }),
    createFixtureCase({
      id: 'invalid-fragment-input-extra-varying',
      title: 'Diagnostic: fragment inputs cannot require missing vertex varyings',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'varying', 'validation', 'signature'],
      fixtureDir: 'invalid-fragment-input-extra-varying',
      sourceFile: 'InvalidFragmentInputExtraVarying.hpp',
      description: 'Verifies fragment inputs cannot declare extra [[AttributeN]] varyings that the vertex stage never produces.',
      validates: [
        'Fragment inputs must be a subset of the vertex output varying locations.',
        'The diagnostic identifies the missing vertex varying location and field.'
      ],
      watchouts: [
        'This prevents backend-only signature mismatches when fragment stages assume varyings the vertex stage does not export.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'fragment input "InvalidFragmentInputExtraVaryingFragmentInput" field "uv" declares [[Attribute1]]', 'Diagnostic output should identify the extra fragment varying field.');
        await t.expectStepOutputContains('uglc', 'vertex output "InvalidFragmentInputExtraVaryingVertexOutput" does not provide it', 'Diagnostic output should explain that the vertex stage is missing the varying.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-set-missing-vertex-buffer',
      title: 'Diagnostic: RenderSet missing [[RenderSetVertexBuffer]] is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render-set', 'validation', 'vertex-buffer'],
      fixtureDir: 'invalid-render-set-missing-vertex-buffer',
      sourceFile: 'InvalidRenderSetMissingVertexBuffer.hpp',
      description: 'Confirms the compiler now reports a clear DSL error instead of dereferencing null state when a RenderSet omits its required vertex-buffer marker.',
      validates: [
        'RenderSet declarations must define exactly one [[RenderSetVertexBuffer]] field.',
        'Missing special-buffer annotations fail fast with a clear diagnostic.'
      ],
      watchouts: [
        'Without this check, malformed RenderSets could crash the compiler instead of producing a user-facing error.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires exactly one [[RenderSetVertexBuffer]] field', 'Diagnostic output should explain the missing vertex-buffer marker.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-set-duplicate-vertex-buffer',
      title: 'Diagnostic: duplicate [[RenderSetVertexBuffer]] fields are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render-set', 'validation', 'vertex-buffer'],
      fixtureDir: 'invalid-render-set-duplicate-vertex-buffer',
      sourceFile: 'InvalidRenderSetDuplicateVertexBuffer.hpp',
      description: 'Checks that RenderSets with more than one vertex-buffer marker fail with a precise diagnostic that lists the conflicting fields.',
      validates: [
        'RenderSet declarations must not mark multiple fields as the vertex buffer.',
        'The diagnostic calls out the conflicting field names.'
      ],
      watchouts: [
        'This prevents accidental ambiguity from silently drifting into the generated render-set metadata.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires exactly one [[RenderSetVertexBuffer]] field', 'Diagnostic output should explain the single-vertex-buffer requirement.');
        await t.expectStepOutputContains('uglc', '"vertices0", "vertices1"', 'Diagnostic output should identify the duplicate vertex-buffer fields.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-set-unsupported-component',
      title: 'Diagnostic: unsupported RenderSet component types are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render-set', 'validation', 'components'],
      fixtureDir: 'invalid-render-set-unsupported-component',
      sourceFile: 'InvalidRenderSetUnsupportedComponent.hpp',
      description: 'Verifies that non-component fields no longer fall through the old buffer-or-texture split and instead fail with a precise unsupported-type diagnostic.',
      validates: [
        'Only `UGL::BufferComponent<T>` and `UGL::TextureComponent<T, MaxResourceCount>` are accepted as RenderSet component fields.',
        'Unsupported fields fail with a non-zero exit code and a clear message.'
      ],
      watchouts: [
        'This closes a historically brittle else-branch that used to misclassify arbitrary fields as texture components.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'contains unsupported component field "debugValue"', 'Diagnostic output should identify the unsupported RenderSet field.');
      }
    }),
    createFixtureCase({
      id: 'render-set-texture-get',
      title: 'Regression: RenderSet texture components expose get(entity, slot)',
      group: 'regressions',
      labels: ['regression', 'render-set', 'texture', 'api'],
      fixtureDir: 'invalid-render-set-texture-get',
      sourceFile: 'InvalidRenderSetTextureGet.hpp',
      description: 'Verifies TextureComponentDataPack exposes the restored `get(entity, slot)` API and returns a sampled Texture2D resource.',
      validates: [
        'Texture components return Texture2D resources through entity-slot lookup.',
        'The frontend rejects neither the restored API nor the Texture2D operations chained after it.'
      ],
      watchouts: [
        'This protects the bindless 8-slot RenderSet texture ABI from accidentally drifting back to handle-only texture operations.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Restored RenderSet texture get fixture should generate successfully.');
        await t.expectFileContains('generate_result.hpp', 'Texture2D<float4> renderSet_albedo_UGLGetSafe(uint entity, uint slot)', 'HLSL should expose the restored entity-slot getter.');
        await t.expectFileContains('generate_result.hpp', 'texture2d<half> albedoget(uint renderEntityID, uint renderEntityInstanceID) const', 'MSL should expose the restored entity-slot getter.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-local-workgroup',
      title: 'Diagnostic: missing [[LocalWorkGroupSize]] is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'validation', 'workgroup'],
      fixtureDir: 'invalid-missing-local-workgroup',
      sourceFile: 'InvalidMissingLocalWorkGroup.hpp',
      description: 'Checks that compute classes without the required local workgroup-size attribute fail with a precise error message.',
      validates: [
        'Compute classes must declare exactly one valid `[[LocalWorkGroupSize(x, y, z)]]` attribute.',
        'The error message clearly identifies the compute class.'
      ],
      watchouts: [
        'Without this check, generated compute pipelines can be built with garbage workgroup metadata.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'is missing required attribute [[LocalWorkGroupSize(x, y, z)]]', 'Diagnostic output should explain the missing workgroup attribute.');
      }
    }),
    createFixtureCase({
      id: 'invalid-local-workgroup-zero',
      title: 'Diagnostic: [[LocalWorkGroupSize]] rejects zero dimensions',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'workgroup', 'validation'],
      fixtureDir: 'invalid-local-workgroup-zero',
      sourceFile: 'InvalidLocalWorkGroupZero.hpp',
      description: 'Verifies compute classes now reject zero-valued LocalWorkGroupSize dimensions before backend entry generation.',
      validates: [
        'Each LocalWorkGroupSize dimension must be greater than zero.',
        'The diagnostic identifies the offending dimension and expression.'
      ],
      watchouts: [
        'A zero threadgroup dimension is never valid and should fail in the DSL frontend instead of leaking to backend shader compilation.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'InvalidLocalWorkGroupZero.hpp:', 'Diagnostic output should include the source file path in clang-style form.');
        await t.expectStepOutputContains('uglc', ': error:', 'Diagnostic output should use clang-style error formatting.');
        await t.expectStepOutputContains('uglc', 'argument x in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer', 'Diagnostic output should identify the invalid LocalWorkGroupSize dimension.');
        await t.expectStepOutputContains('uglc', 'expression "0" evaluates to 0', 'Diagnostic output should report the zero-valued expression result.');
      }
    }),
    createFixtureCase({
      id: 'invalid-local-workgroup-negative',
      title: 'Diagnostic: [[LocalWorkGroupSize]] rejects negative dimensions',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'workgroup', 'validation'],
      fixtureDir: 'invalid-local-workgroup-negative',
      sourceFile: 'InvalidLocalWorkGroupNegative.hpp',
      description: 'Verifies compute classes reject negative LocalWorkGroupSize dimensions before any backend sees an invalid numthreads/threadgroup contract.',
      validates: [
        'Each LocalWorkGroupSize dimension must be positive.',
        'The diagnostic reports the evaluated negative value.'
      ],
      watchouts: [
        'Without this, negative dimensions can degrade into backend-specific parse errors or undefined host pipeline metadata.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'argument x in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer', 'Diagnostic output should identify the invalid LocalWorkGroupSize dimension.');
        await t.expectStepOutputContains('uglc', 'expression "-1" evaluates to -1', 'Diagnostic output should report the negative expression result.');
      }
    }),
    createFixtureCase({
      id: 'invalid-local-workgroup-nonconst',
      title: 'Diagnostic: [[LocalWorkGroupSize]] requires compile-time constants',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'workgroup', 'constexpr'],
      fixtureDir: 'invalid-local-workgroup-nonconst',
      sourceFile: 'InvalidLocalWorkGroupNonconst.hpp',
      description: 'Verifies LocalWorkGroupSize dimensions cannot be sourced from non-constexpr variables, even if they have integral initializers.',
      validates: [
        'LocalWorkGroupSize dimensions must resolve from compile-time integer constants.',
        'The diagnostic names the non-constant expression.'
      ],
      watchouts: [
        'This prevents runtime-looking state from silently leaking into backend-only threadgroup metadata.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'argument x in [[LocalWorkGroupSize(x, y, z)]] must resolve to a compile-time positive integer', 'Diagnostic output should identify the invalid LocalWorkGroupSize dimension.');
        await t.expectStepOutputContains('uglc', 'expression "InvalidLocalWorkGroupNonconstSize" is not a compile-time integer constant', 'Diagnostic output should explain why the named dimension expression is rejected.');
      }
    }),
    createFixtureCase({
      id: 'invalid-explicit-this-pointer-access',
      title: 'Diagnostic: explicit this-pointer syntax is rejected in DSL shader code',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'this', 'pointer', 'validation'],
      fixtureDir: 'invalid-explicit-this-pointer-access',
      sourceFile: 'InvalidExplicitThisPointerAccess.hpp',
      description: 'Verifies that user-authored `this->...` remains rejected as explicit pointer syntax, while the new implicit-this recovery only applies to compiler-synthesized `CXXThisExpr` nodes.',
      validates: [
        'Explicit `this->` access fails with a DSL diagnostic.',
        'The diagnostic is emitted in clang-style file:line:column form.'
      ],
      watchouts: [
        'Without this guard, the implicit-this fix could accidentally make all explicit pointer-style member access legal too.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'InvalidExplicitThisPointerAccess.hpp:', 'Diagnostic output should include the source file path.');
        await t.expectStepOutputContains('uglc', ': error:', 'Diagnostic output should use clang-style formatting.');
        await t.expectStepOutputContains('uglc', 'does not allow explicit this-pointer access', 'Diagnostic output should explain that explicit this-pointer syntax is forbidden.');
      }
    }),
    createFixtureCase({
      id: 'invalid-legacy-wave-attributes',
      title: 'Diagnostic: removed [[WaveLaneIndex]] / [[WaveLaneCount]] parameters are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'wave', 'validation'],
      fixtureDir: 'invalid-legacy-wave-attributes',
      sourceFile: 'InvalidLegacyWaveAttributes.hpp',
      description: 'Locks in the DSL surface change from entry-parameter attributes to global wave builtins by requiring a clear diagnostic when old wave attributes are still present on shader parameters.',
      validates: [
        'Legacy wave builtin parameter attributes fail with a non-zero exit code.',
        'The diagnostic tells users to migrate to the new global functions.'
      ],
      watchouts: [
        'Without this, removed wave attributes can silently slip back into codegen through old attribute-mapping paths.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'still uses removed parameter attribute [[WaveLaneIndex]]', 'Diagnostic output should explicitly reject the removed WaveLaneIndex parameter attribute.');
        await t.expectStepOutputContains('uglc', 'Use WaveGetLaneIndex() inside shader code instead', 'Diagnostic output should point users to the new global wave builtin API.');
      }
    }),
    createFixtureCase({
      id: 'invalid-wave-builtins-in-render',
      title: 'Diagnostic: global wave builtins are rejected outside compute shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'wave', 'validation'],
      fixtureDir: 'invalid-wave-builtins-in-render',
      sourceFile: 'InvalidWaveBuiltinsInRender.hpp',
      description: 'Ensures the new global wave builtin API fails loudly when used from a render-stage shader entry, where the current Metal lowering cannot provide simdgroup builtins safely.',
      validates: [
        'Wave builtins used from non-compute shader entries fail with a non-zero exit code.',
        'The diagnostic explicitly states that the feature is compute-only today.'
      ],
      watchouts: [
        'This avoids silently generating invalid Metal for render-stage wave builtin calls.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'WaveGetLaneIndex() and WaveGetLaneCount() are currently only supported in compute shaders', 'Diagnostic output should explain the current compute-only support boundary.');
      }
    }),
    createFixtureCase({
      id: 'invalid-barrier-in-render',
      title: 'Diagnostic: shader memory barriers are rejected outside compute shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'barrier', 'validation'],
      fixtureDir: 'invalid-barrier-in-render',
      sourceFile: 'InvalidBarrierInRender.hpp',
      description: 'Ensures group/device memory barrier intrinsics fail before backend emission when used from render-stage shader entries.',
      validates: [
        'DeviceMemoryBarrierWithGroupSync used from a render-stage shader fails with a non-zero exit code.',
        'The diagnostic explains that the barrier intrinsic is compute-only.'
      ],
      watchouts: [
        'This keeps explicit UGL barrier calls from turning into invalid Metal threadgroup barriers in vertex or fragment functions.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'DeviceMemoryBarrierWithGroupSync() is only supported in compute shaders', 'Diagnostic output should explain the compute-only barrier support boundary.');
        await t.expectStepOutputContains('uglc', 'InvalidBarrierInRender.hpp:', 'Diagnostic output should include the source file path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-discard-in-vertex',
      title: 'Diagnostic: fragment discard builtin is rejected outside fragment-capable shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'fragment', 'discard', 'validation'],
      fixtureDir: 'invalid-discard-in-vertex',
      sourceFile: 'InvalidDiscardInVertex.hpp',
      description: 'Ensures `discard_fragment()` fails before backend emission when it appears in a vertex shader, where neither HLSL nor MSL fragment discard semantics apply.',
      validates: [
        'Vertex-stage use of the fragment discard builtin fails with a non-zero exit code.',
        'The diagnostic explicitly states that the builtin is limited to fragment and pixel-local shader entries.'
      ],
      watchouts: [
        'Without this guard, vertex shaders could emit invalid backend code or silently compile with the wrong control-flow semantics.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'discard_fragment() is only supported in fragment or pixel-local shaders', 'Diagnostic output should explain the fragment-capable discard support boundary.');
        await t.expectStepOutputContains('uglc', 'InvalidDiscardInVertex.hpp:', 'Diagnostic output should include the source file path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-clip-in-vertex',
      title: 'Diagnostic: clip builtin is rejected outside fragment-capable shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'fragment', 'clip', 'validation'],
      fixtureDir: 'invalid-clip-in-vertex',
      sourceFile: 'InvalidClipInVertex.hpp',
      description: 'Ensures `clip()` fails before backend emission when it appears in a vertex shader.',
      validates: [
        'Vertex-stage use of clip fails with a non-zero exit code.',
        'The diagnostic explicitly states that the builtin is limited to fragment and pixel-local shader entries.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'clip() is only supported in fragment or pixel-local shaders', 'Diagnostic output should explain the fragment-capable clip support boundary.');
        await t.expectStepOutputContains('uglc', 'InvalidClipInVertex.hpp:', 'Diagnostic output should include the source file path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-wave-read-lane-at-in-render',
      title: 'Diagnostic: WaveReadLaneAt is rejected outside compute shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'wave', 'validation', 'shuffle'],
      fixtureDir: 'invalid-wave-read-lane-at-in-render',
      sourceFile: 'InvalidWaveReadLaneAtInRender.hpp',
      description: 'Ensures the real wave lane-read intrinsic stays on the compute path where the Metal backend can model subgroup semantics safely.',
      validates: [
        'WaveReadLaneAt used from a render-stage shader fails with a non-zero exit code.',
        'The diagnostic points users to QuadReadLaneAt when quad-local behavior was intended.'
      ],
      watchouts: [
        'Without this boundary, render-stage code could accidentally rely on unsupported subgroup semantics.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'WaveReadLaneAt() is currently only supported in compute shaders', 'Diagnostic output should explain the current WaveReadLaneAt support boundary.');
        await t.expectStepOutputContains('uglc', 'Use QuadReadLaneAt() if quad semantics were intended.', 'Diagnostic output should provide the quad migration path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-wave-collectives-in-render',
      title: 'Diagnostic: wave collectives are rejected outside compute shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'wave', 'validation', 'collectives'],
      fixtureDir: 'invalid-wave-collectives-in-render',
      sourceFile: 'InvalidWaveCollectivesInRender.hpp',
      description: 'Ensures ballot-style and prefix-style wave collectives stay on the compute path where the current Metal backend can provide subgroup semantics safely.',
      validates: [
        'Wave collectives used from a render-stage shader fail with a non-zero exit code.',
        'The diagnostic explicitly names the offending collective and its current compute-only support boundary.'
      ],
      watchouts: [
        'Without this guard, render shaders could start depending on wave collectives that the current backend contract does not actually support.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'WaveActiveBallot() is currently only supported in compute shaders', 'Diagnostic output should explain the current compute-only support boundary for wave collectives.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-missing-binding',
      title: 'Diagnostic: bind-group resources must declare explicit [[BindingN]]',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'binding', 'validation'],
      fixtureDir: 'invalid-bindgroup-missing-binding',
      sourceFile: 'InvalidBindGroupMissingBinding.hpp',
      description: 'Confirms the stricter bind-group ABI contract needed for HLSL/SPIR-V: bind-group resources no longer fall back to declaration order and must declare an explicit `[[BindingN]]`.',
      validates: [
        'Bind-group resources without an explicit binding fail with a non-zero exit code.',
        'The diagnostic explains that declaration order is no longer the binding ABI.'
      ],
      watchouts: [
        'This is the key contract change for backend-stable descriptor binding and must not silently regress into field-order defaults.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires an explicit [[BindingN]] attribute', 'Diagnostic output should explain the explicit bind-group binding requirement.');
        await t.expectStepOutputContains('uglc', 'no longer default to declaration order', 'Diagnostic output should explain that field order is no longer the binding ABI.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-legacy-slot',
      title: 'Diagnostic: legacy [[SlotN]] on bind-group resources is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'binding', 'slot', 'validation'],
      fixtureDir: 'invalid-bindgroup-legacy-slot',
      sourceFile: 'InvalidBindGroupLegacySlot.hpp',
      description: 'Confirms that `[[SlotN]]` is now reserved for shader-class bind-group or render-set parameters and can no longer be used as a bind-group resource binding annotation.',
      validates: [
        'Bind-group resources that still use legacy Slot syntax fail with a non-zero exit code.',
        'The diagnostic explains the Slot-vs-Binding contract split.'
      ],
      watchouts: [
        'This prevents old Metal-only habits from leaking into the cross-backend binding ABI.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses legacy [[SlotN]] syntax', 'Diagnostic output should reject legacy Slot syntax on bind-group resources.');
        await t.expectStepOutputContains('uglc', 'must declare explicit [[BindingN]] attributes', 'Diagnostic output should point users to the required Binding syntax.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-duplicate-binding',
      title: 'Diagnostic: duplicate [[BindingN]] in one bind group is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'binding', 'validation'],
      fixtureDir: 'invalid-bindgroup-duplicate-binding',
      sourceFile: 'InvalidBindGroupDuplicateBinding.hpp',
      description: 'Checks that bind-group resources cannot alias the same binding index, which would otherwise produce backend-specific descriptor collisions.',
      validates: [
        'Duplicate bind-group resource bindings fail with a non-zero exit code.',
        'The diagnostic identifies the conflicting binding index and fields.'
      ],
      watchouts: [
        'This protects the explicit resource-binding ABI from silently collapsing multiple resources onto one descriptor binding.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'reuses [[Binding0]]', 'Diagnostic output should identify the duplicated binding index.');
        await t.expectStepOutputContains('uglc', 'albedoTexture', 'Diagnostic output should identify the first conflicting field.');
        await t.expectStepOutputContains('uglc', 'linearSampler', 'Diagnostic output should identify the second conflicting field.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-binding32',
      title: 'Diagnostic: bind-group resources beyond [[Binding31]] are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'binding', 'validation'],
      fixtureDir: 'invalid-bindgroup-binding32',
      sourceFile: 'InvalidBindGroupBinding32.hpp',
      description: 'Confirms the explicit bind-group resource ABI has a clear upper bound today, so out-of-range bindings fail before any backend sees a partially valid descriptor layout.',
      validates: [
        'Bind-group resources using `[[Binding32]]` fail with a non-zero exit code.',
        'The diagnostic explains the currently supported binding range.'
      ],
      watchouts: [
        'Without this, projects can accidentally depend on unbounded binding indices that different backends may lower inconsistently.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses [[Binding32]]', 'Diagnostic output should identify the out-of-range binding index.');
        await t.expectStepOutputContains('uglc', 'only supports [[Binding0]] through [[Binding31]]', 'Diagnostic output should explain the supported bind-group resource binding range.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-helper-parameter-out',
      title: 'Diagnostic: BindGroup helper parameters cannot use [[OUT]] / [[INOUT]]',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'helpers', 'abi', 'validation'],
      fixtureDir: 'invalid-bindgroup-helper-parameter-out',
      sourceFile: 'InvalidBindGroupHelperParameterOut.hpp',
      description: 'Locks in the helper-ABI rule that `BindGroup<T>` parameters are read-only handle inputs and cannot be declared as `[[OUT]]` or `[[INOUT]]` helper parameters.',
      validates: [
        'Helper functions that annotate a BindGroup parameter as output or inout fail with a non-zero exit code.',
        'The diagnostic explains that bind-group helper parameters must stay input-only.'
      ],
      watchouts: [
        'Without this guard, backend-specific signature generation could silently accept an impossible mutable bind-group ABI on one backend and fail later on another.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses UGL::BindGroup<T> with [[OUT]] or [[INOUT]]', 'Diagnostic output should explain that bind-group helper parameters cannot use output-style attributes.');
        await t.expectStepOutputContains('uglc', 'must be passed by input only', 'Diagnostic output should explain the input-only bind-group helper ABI requirement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-hlsl-texture-helper-parameter-out',
      title: 'Diagnostic: standalone sampled texture helper parameters cannot use [[OUT]] / [[INOUT]]',
      group: 'diagnostics',
      labels: ['diagnostic', 'hlsl', 'texture', 'sampler', 'helpers', 'abi', 'validation'],
      fixtureDir: 'invalid-hlsl-texture-helper-parameter-out',
      sourceFile: 'InvalidHLSLTextureHelperParameterOut.hpp',
      description: 'Locks in the HLSL helper-ABI rule that standalone sampled texture and sampler parameters are read-only input handles.',
      validates: [
        'Helper functions that annotate a standalone sampled texture parameter as output or inout fail with a non-zero exit code.',
        'The diagnostic explains that sampled texture and sampler helper parameters must stay input-only.'
      ],
      watchouts: [
        'This does not expand support to RWTexture, StructuredBuffer, or RWStructuredBuffer standalone helper parameters.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'sampled texture/sampler', 'Diagnostic output should identify standalone sampled texture or sampler helper parameters.');
        await t.expectStepOutputContains('uglc', '[[OUT]] or [[INOUT]]', 'Diagnostic output should explain that output-style attributes are invalid for sampled texture helper parameters.');
        await t.expectStepOutputContains('uglc', 'input parameters', 'Diagnostic output should explain the input-only sampled texture helper ABI requirement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-host-resource-handle-in-shader',
      title: 'Diagnostic: host-only resource handles cannot be used in shader code',
      group: 'diagnostics',
      labels: ['diagnostic', 'host', 'shader', 'resource', 'validation'],
      fixtureDir: 'invalid-host-resource-handle-in-shader',
      sourceFile: 'InvalidHostResourceHandleInShader.hpp',
      description: 'Confirms that host-only resource records may exist in a translation unit but fail with a DSL diagnostic when shader code declares or passes them.',
      validates: [
        'Shader code that declares a record containing `UGL::Texture` or `UGL::TextureView` fails before backend compiler parsing.',
        'The diagnostic explains that host-only resource handles cannot be used in shader code.'
      ],
      watchouts: [
        'Without this guard, skipped host-only records can still re-enter backend code through local declarations or helper calls and fail as unknown HLSL/MSL types.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'host-only resource handle', 'Diagnostic output should identify host-only resource handles.');
        await t.expectStepOutputContains('uglc', 'cannot be used in shader code', 'Diagnostic output should explain the shader-code restriction.');
        await t.expectStepOutputContains('uglc', 'InvalidHostResourceHandleInShader.hpp', 'Diagnostic output should point back to the invalid fixture source.');
      }
    }),
    createFixtureCase({
      id: 'entry-local-bindgroup-leak-negative',
      title: 'Diagnostic: shader resource handles cannot be namespace-scope variables',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'namespace', 'resource', 'validation'],
      fixtureDir: 'entry-local-bindgroup-leak-negative',
      sourceFile: 'EntryLocalBindGroupLeakNegative.hpp',
      description: 'Confirms a helper cannot reach shader resources through a namespace-scope BindGroup variable instead of receiving the handle from the shader entry or another helper parameter.',
      validates: [
        'Reachable namespace-scope `BindGroup<T>` variables fail during shared shader-reference collection.',
        'The diagnostic tells users to pass resource handles through entry or helper parameters.',
        'The error is emitted before HLSL/MSL backend code reports an undeclared resource.'
      ],
      watchouts: [
        'This keeps entry-local bind-group scope rules backend-independent after namespace reachability was tightened.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'shader resource handles cannot be stored as namespace-scope variables', 'Diagnostic output should identify the invalid namespace-scope resource handle.');
        await t.expectStepOutputContains('uglc', 'pass BindGroup<T>, RenderSet<T>, textures, samplers, or buffers through shader entry or helper parameters', 'Diagnostic output should explain the supported resource handle path.');
        await t.expectStepOutputContains('uglc', 'EntryLocalBindGroupLeakNegative.hpp', 'Diagnostic output should point back to the invalid fixture source.');
      }
    }),
    createFixtureCase({
      id: 'invalid-legacy-storage-buffer',
      title: 'Diagnostic: legacy StorageBuffer is rejected with a migration hint',
      group: 'diagnostics',
      labels: ['diagnostic', 'buffer', 'migration', 'validation'],
      fixtureDir: 'invalid-legacy-storage-buffer',
      sourceFile: 'InvalidLegacyStorageBuffer.hpp',
      description: 'Confirms that the removed `UGL::StorageBuffer<T>` spelling fails fast and points users to the new read-only vs read-write buffer split.',
      validates: [
        'Legacy `UGL::StorageBuffer<T>` fails with a non-zero exit code.',
        'The diagnostic tells users to choose between `UGL::StructuredBuffer<T>` and `UGL::RWStructuredBuffer<T>`.'
      ],
      watchouts: [
        'This keeps migration guidance stable after the API split and prevents the compiler from silently accepting the legacy alias again.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGL::StorageBuffer<T> has been removed', 'Diagnostic output should explicitly reject the legacy StorageBuffer spelling.');
        await t.expectStepOutputContains('uglc', 'UGL::StructuredBuffer<T>', 'Diagnostic output should recommend the read-only replacement.');
        await t.expectStepOutputContains('uglc', 'UGL::RWStructuredBuffer<T>', 'Diagnostic output should recommend the read-write replacement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-legacy-atomic-template',
      title: 'Diagnostic: legacy Atomic<T> resource spelling is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'atomic', 'migration', 'validation'],
      fixtureDir: 'invalid-legacy-atomic-template',
      sourceFile: 'InvalidLegacyAtomicTemplate.hpp',
      description: 'Confirms that user-authored DSL can no longer declare resources as `Atomic<T>` and must instead use plain integral storage with atomic builtins.',
      validates: [
        'Legacy `Atomic<T>` resource payloads fail with a non-zero exit code.',
        'The failure protects the HLSL-style DSL surface where atomic layout is inferred from use sites.'
      ],
      watchouts: [
        'This prevents Metal layout spelling from leaking back into user-authored DSL declarations.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'Atomic', 'Diagnostic output should identify the removed Atomic<T> spelling.');
      }
    }),
    createFixtureCase({
      id: 'invalid-structured-buffer-write',
      title: 'Diagnostic: StructuredBuffer rejects writes',
      group: 'diagnostics',
      labels: ['diagnostic', 'buffer', 'readonly', 'validation'],
      fixtureDir: 'invalid-structured-buffer-write',
      sourceFile: 'InvalidStructuredBufferWrite.hpp',
      description: 'Confirms that the new read-only `UGL::StructuredBuffer<T>` surface rejects write attempts before shader code generation continues.',
      validates: [
        'Writing through `UGL::StructuredBuffer<T>` fails with a non-zero exit code.',
        'The diagnostic explains that the indexed element is const-qualified.'
      ],
      watchouts: [
        'This protects the StructuredBuffer/RWStructuredBuffer split from regressing into an accidentally writable read-only buffer API.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'field "values" is declared as', 'Diagnostic output should identify the read-only StructuredBuffer field.');
        await t.expectStepOutputContains('uglc', 'which is read-only', 'Diagnostic output should explain that StructuredBuffer indexing is read-only.');
        await t.expectStepOutputContains('uglc', 'Use UGL::RWStructuredBuffer<T> for read-write access', 'Diagnostic output should point users to the writable replacement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-sampled-texture-type',
      title: 'Diagnostic: unsupported sampled texture element type is rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'validation'],
      fixtureDir: 'invalid-bindgroup-sampled-texture-type',
      sourceFile: 'InvalidBindGroupSampledTextureType.hpp',
      description: 'Confirms the compiler now fails loudly when a sampled texture bind-group field uses an element type that cannot be mapped to a valid RHI texture sample type.',
      validates: [
        'Unsupported sampled texture element types fail with a non-zero exit code.',
        'The diagnostic identifies the field and the unsupported element type.'
      ],
      watchouts: [
        'Without this, users can get syntactically generated output that is semantically wrong for the backend binding model.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses unsupported sampled texture type "bool"', 'Diagnostic output should name the unsupported sampled texture element type.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-rwtexture-nonformat',
      title: 'Diagnostic: storage textures require TextureFormat template arguments',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'storage-texture', 'validation'],
      fixtureDir: 'invalid-bindgroup-rwtexture-nonformat',
      sourceFile: 'InvalidBindGroupRWTextureNonformat.hpp',
      description: 'Confirms read-write storage textures are rejected early when users pass a sampled-type payload like `float4` instead of a `UGL::TextureFormat::*` format tag.',
      validates: [
        'RWTexture2D and RWTexture2DArray require a concrete TextureFormat template argument.',
        'The diagnostic identifies the offending field and the wrong non-format type.'
      ],
      watchouts: [
        'Without this front-end check, the same mistake used to fall through to backend-specific unknown-format failures with much worse context.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'read-write storage textures require a UGL::TextureFormat::* template argument', 'Diagnostic output should explain the storage-texture template contract.');
        await t.expectStepOutputContains('uglc', 'Found "UGL::float4"', 'Diagnostic output should name the non-format template argument.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-rwtexture3d-nonformat',
      title: 'Diagnostic: RWTexture3D requires a TextureFormat template argument',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'texture3d', 'storage-texture', 'validation'],
      fixtureDir: 'invalid-bindgroup-rwtexture3d-nonformat',
      sourceFile: 'InvalidBindGroupRWTexture3DNonformat.hpp',
      description: 'Confirms `RWTexture3D<T>` follows the same storage-texture contract as 2D storage textures and rejects sampled vector payload types.',
      validates: [
        'RWTexture3D requires a concrete TextureFormat template argument.',
        'The diagnostic identifies the offending non-format type.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'read-write storage textures require a UGL::TextureFormat::* template argument', 'Diagnostic output should explain the RWTexture3D template contract.');
        await t.expectStepOutputContains('uglc', 'Found "UGL::float4"', 'Diagnostic output should name the non-format template argument.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-rwtexture-depth-format',
      title: 'Diagnostic: depth formats are rejected for read-write storage textures',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'storage-texture', 'depth'],
      fixtureDir: 'invalid-bindgroup-rwtexture-depth-format',
      sourceFile: 'InvalidBindGroupRWTextureDepthFormat.hpp',
      description: 'Verifies depth formats fail fast on `RWTexture2D<T>` because depth textures are sampled-only in the current DSL/backend contract.',
      validates: [
        'Depth formats cannot be used as read-write storage textures.',
        'The diagnostic identifies both the format and the offending field.'
      ],
      watchouts: [
        'This prevents depth textures from drifting into generated HLSL/MSL storage-texture declarations that backends cannot legally compile.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses storage texture format "UGL::TextureFormat::Depth32Float"', 'Diagnostic output should identify the rejected depth format.');
        await t.expectStepOutputContains('uglc', 'depth formats are read-only sampled textures', 'Diagnostic output should explain why depth formats are invalid for RW storage textures.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-rwtexture3d-depth-format',
      title: 'Diagnostic: depth formats are rejected for RWTexture3D',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'texture3d', 'storage-texture', 'depth'],
      fixtureDir: 'invalid-bindgroup-rwtexture3d-depth-format',
      sourceFile: 'InvalidBindGroupRWTexture3DDepthFormat.hpp',
      description: 'Verifies depth formats fail fast on `RWTexture3D<T>` because depth textures are sampled-only in the current DSL/backend contract.',
      validates: [
        'Depth formats cannot be used as 3D read-write storage textures.',
        'The diagnostic identifies both the format and the offending field.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses storage texture format "UGL::TextureFormat::Depth32Float"', 'Diagnostic output should identify the rejected depth format.');
        await t.expectStepOutputContains('uglc', 'depth formats are read-only sampled textures', 'Diagnostic output should explain why depth formats are invalid for RWTexture3D.');
      }
    }),
    createFixtureCase({
      id: 'invalid-bindgroup-rwtexture3d-astc4x4',
      title: 'Diagnostic: ASTC4x4Unorm is rejected for RWTexture3D storage textures',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'texture', 'texture3d', 'storage-texture', 'astc'],
      fixtureDir: 'invalid-bindgroup-rwtexture3d-astc4x4',
      sourceFile: 'InvalidBindGroupRWTexture3DASTC4x4.hpp',
      description: 'Verifies ASTC4x4Unorm stays sampled-only and cannot be used as a read-write storage texture format.',
      validates: [
        'Compressed ASTC textures cannot lower to RWTexture3D storage declarations.',
        'The diagnostic identifies ASTC4x4Unorm as an unsupported storage texture format.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses unsupported storage texture format "UGL::TextureFormat::ASTC4x4Unorm"', 'Diagnostic output should identify the rejected compressed format.');
        await t.expectStepOutputContains('uglc', 'valid MSL lowering and an explicit Vulkan/HLSL image_format lowering', 'Diagnostic output should explain the storage texture lowering contract.');
      }
    }),
    createFixtureCase({
      id: 'invalid-uniform-buffer-resource-member',
      title: 'Diagnostic: UniformBuffer element types cannot hide nested shader resources',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'buffer', 'uniform-buffer', 'validation'],
      fixtureDir: 'invalid-uniform-buffer-resource-member',
      sourceFile: 'InvalidUniformBufferResourceMember.hpp',
      description: 'Confirms UniformBuffer payload validation now recursively rejects nested textures/samplers instead of letting illegal resource members hide inside user-defined structs.',
      validates: [
        'UniformBuffer payload types may only contain plain shader data.',
        'The diagnostic reports the nested member path that introduced the hidden resource.'
      ],
      watchouts: [
        'Without recursive payload validation, nested resource handles can silently slip into shader buffer layouts and only explode much later in backend codegen.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses UniformBuffer element type "InvalidUniformBufferResourceMemberPayload"', 'Diagnostic output should identify the invalid UniformBuffer payload type.');
        await t.expectStepOutputContains('uglc', 'member path "textures.albedo"', 'Diagnostic output should name the nested illegal resource field path.');
        await t.expectStepOutputContains('uglc', 'Nested textures, samplers, bind groups, render sets, and host resource handles are not allowed', 'Diagnostic output should explain the buffer-payload restriction.');
      }
    }),
    createFixtureCase({
      id: 'invalid-shader-resource-behavior-structured-buffer',
      title: 'Diagnostic: shader-resource behavior records cannot enter StructuredBuffer layouts',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'buffer', 'structured-buffer', 'resource', 'behavior', 'layout'],
      fixtureDir: 'invalid-shader-resource-behavior-structured-buffer',
      sourceFile: 'InvalidShaderResourceBehaviorStructuredBuffer.hpp',
      description: 'Confirms shader-only behavior records that store resource handles are rejected when used as ordinary StructuredBuffer element layouts.',
      validates: [
        'StructuredBuffer element types may only contain plain shader data.',
        'A behavior record with a Texture2D field cannot be treated as a value record just because it is reachable from shader code.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'uses StructuredBuffer element type "InvalidShaderResourceBehaviorStructuredBufferElement"', 'Diagnostic output should identify the invalid StructuredBuffer payload type.');
        await t.expectStepOutputContains('uglc', 'member path "texture"', 'Diagnostic output should name the resource field that entered the layout.');
        await t.expectStepOutputContains('uglc', 'Nested textures, samplers, bind groups, render sets, and host resource handles are not allowed', 'Diagnostic output should explain that buffer payloads must be plain data.');
      }
    }),
    createFixtureCase({
      id: 'invalid-hlsl-uniform-array-layout',
      title: 'Diagnostic: HLSL uniform-buffer scalar arrays must match constant-buffer layout',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'uniform-buffer', 'hlsl', 'layout'],
      fixtureDir: 'invalid-hlsl-uniform-array-layout',
      sourceFile: 'InvalidHLSLUniformArrayLayout.hpp',
      description: 'Verifies the HLSL backend now rejects UniformBuffer payloads whose C++ array stride does not match HLSL constant-buffer packing, instead of silently shifting later member offsets.',
      validates: [
        'Scalar arrays inside UniformBuffer payloads fail fast when HLSL constant-buffer stride becomes 16 bytes per element.',
        'The diagnostic names the offending member path and reports the array-stride mismatch directly.'
      ],
      watchouts: [
        'This locks down the exact `float pad[3]` class of bug that previously zeroed later Vulkan/HLSL cbuffer members without breaking unrelated objects.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'HLSL/Vulkan UniformBuffer buffer layout mismatch', 'Diagnostic output should identify the HLSL/Vulkan backend layout failure.');
        await t.expectStepOutputContains('uglc', 'member path "fogPad"', 'Diagnostic output should identify the array field that drifted.');
        await t.expectStepOutputContains('uglc', 'offset', 'Diagnostic output should report the shifted field offset explicitly.');
      }
    }),
    createFixtureCase({
      id: 'invalid-msl-uniform-float3-layout',
      title: 'Diagnostic: MSL uniform-buffer float3 layout must match host struct layout',
      group: 'diagnostics',
      labels: ['diagnostic', 'bindgroup', 'uniform-buffer', 'msl', 'layout'],
      fixtureDir: 'invalid-msl-uniform-float3-layout',
      sourceFile: 'InvalidMSLUniformFloat3Layout.hpp',
      description: 'Verifies the Metal backend now rejects UniformBuffer payloads whose `float3` member layout diverges from the host struct layout instead of relying on a generated assert or silently misreading later fields.',
      validates: [
        'UniformBuffer payloads with `float3` in a struct fail fast when Metal expands the member to a four-component slot.',
        'The diagnostic points to the concrete member path whose size/offset no longer matches the host layout.'
      ],
      watchouts: [
        'Without this, Metal could still appear to compile while reading a different byte layout than the host-side DSL struct.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'MSL/Metal UniformBuffer buffer layout mismatch', 'Diagnostic output should identify the Metal backend layout failure.');
        await t.expectStepOutputContains('uglc', 'member path "direction"', 'Diagnostic output should identify the float3 member that expanded.');
        await t.expectStepOutputContains('uglc', 'size', 'Diagnostic output should report the member size mismatch explicitly.');
      }
    }),
    createFixtureCase({
      id: 'invalid-compute-slot8',
      title: 'Diagnostic: compute bind groups beyond the Metal SlotN limit are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'bindgroup', 'slot', 'metal'],
      fixtureDir: 'invalid-compute-slot8',
      sourceFile: 'InvalidComputeSlot8.hpp',
      description: 'Confirms the compiler rejects a ninth compute bind-group parameter before code generation falls through to a later Metal shader-module failure.',
      validates: [
        'Compute classes fail fast when a bind-group parameter exceeds the supported Slot range.',
        'The diagnostic names the offending class and explains that only [[Slot0]] through [[Slot7]] are supported.',
        'The failure happens during UGLC code generation instead of at runtime shader compilation.'
      ],
      watchouts: [
        'This specifically locks down the renderer-team complaint where [[Slot8]] was accepted, but the generated Metal entry still referenced a bind group that the backend could not bind.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'InvalidComputeSlot8Pass', 'Diagnostic output should identify the offending compute class.');
        await t.expectStepOutputContains('uglc', '[[Slot8]]', 'Diagnostic output should name the overflowing Slot attribute.');
        await t.expectStepOutputContains('uglc', '[[Slot0]] through [[Slot7]]', 'Diagnostic output should explain the supported compute Slot range.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-slot7-buffer-limit',
      title: 'Diagnostic: render bind groups respect the Metal buffer limit after vertex-input offset',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'bindgroup', 'slot', 'metal'],
      fixtureDir: 'invalid-render-slot7-buffer-limit',
      sourceFile: 'InvalidRenderSlot7BufferLimit.hpp',
      description: 'Confirms render classes also fail fast when a high SlotN would overflow the Metal buffer space after the entry reserves buffer slots ahead of bind groups.',
      validates: [
        'Render classes fail fast when a bind group would map beyond Metal buffer(7).',
        'The diagnostic explains that an earlier Metal buffer slot is already reserved before bind groups are appended.'
      ],
      watchouts: [
        'This keeps the render path from silently accepting a slot that only becomes invalid after vertex-input buffer reservation is applied.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'InvalidRenderSlot7Pass', 'Diagnostic output should identify the offending render class.');
        await t.expectStepOutputContains('uglc', '[[Slot7]]', 'Diagnostic output should name the overflowing render Slot attribute.');
        await t.expectStepOutputContains('uglc', '[[buffer(8)]]', 'Diagnostic output should name the overflowing Metal buffer binding.');
        await t.expectStepOutputContains('uglc', 'already reserves 1 Metal buffer slot', 'Diagnostic output should explain the reserved-slot offset for render entry generation.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-missing-vertex',
      title: 'Diagnostic: render classes without a vertex entry fail with a clear error',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'entry', 'validation'],
      fixtureDir: 'invalid-render-missing-vertex',
      sourceFile: 'InvalidRenderMissingVertex.hpp',
      description: 'Confirms the compiler reports a readable DSL error when a render class reaches code generation without defining its required `vertex(...)` shader entry.',
      validates: [
        'Missing render shader entry points fail with a non-zero exit code.',
        'The diagnostic names the render class and the missing required method.'
      ],
      watchouts: [
        'Without this guard, downstream shader code generation could dereference null state and crash instead of producing a user-facing error.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'RenderClass "InvalidRenderMissingVertexPass" is missing required shader entry method "vertex(...)"', 'Diagnostic output should explain the missing render entry.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-missing-create',
      title: 'Diagnostic: render classes without constructor/create fail clearly',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'constructor', 'validation'],
      fixtureDir: 'invalid-render-missing-create',
      sourceFile: 'InvalidRenderMissingCreate.hpp',
      description: 'Confirms render classes now fail fast when the DSL class never defines its required constructor/create entry instead of crashing later while generating the host wrapper.',
      validates: [
        'Render classes must define a constructor/create method.',
        'The diagnostic names the offending render class and the missing method contract.'
      ],
      watchouts: [
        'Without this guard, later code paths could dereference a null create function while building the generated host pipeline wrapper.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'RenderClass "InvalidRenderMissingCreatePass" is missing required constructor method "create(...)"', 'Diagnostic output should explain the missing render constructor/create method.');
      }
    }),
    createFixtureCase({
      id: 'invalid-compute-missing-create',
      title: 'Diagnostic: compute classes without constructor/create fail clearly',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'constructor', 'validation'],
      fixtureDir: 'invalid-compute-missing-create',
      sourceFile: 'InvalidComputeMissingCreate.hpp',
      description: 'Confirms compute classes now fail fast when they omit the required constructor/create surface instead of crashing during wrapper generation.',
      validates: [
        'Compute classes must define a constructor/create method.',
        'The diagnostic names the offending compute class and the missing method contract.'
      ],
      watchouts: [
        'This protects the compute path from the same null-create crash class as render and bind-group generation.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ComputeClass "InvalidComputeMissingCreatePass" is missing required constructor method "create(...)"', 'Diagnostic output should explain the missing compute constructor/create method.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-duplicate-slot',
      title: 'Diagnostic: duplicate render [[SlotN]] parameters are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'bindgroup', 'slot', 'validation'],
      fixtureDir: 'invalid-render-duplicate-slot',
      sourceFile: 'InvalidRenderDuplicateSlot.hpp',
      description: 'Checks that two render bind-group parameters cannot alias the same Slot index, which would otherwise collapse distinct resources onto one backend bind-group slot.',
      validates: [
        'Duplicate render Slot indices fail with a non-zero exit code.',
        'The diagnostic identifies the duplicated Slot index and conflicting parameters.'
      ],
      watchouts: [
        'This prevents silent bind-group aliasing before pipeline layouts or shader entry signatures are emitted.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'reuses [[Slot0]] on both parameter "firstBindGroup" and parameter "secondBindGroup"', 'Diagnostic output should identify the duplicated Slot index and parameter names.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-set-texture-count-zero',
      title: 'Diagnostic: RenderSet texture components require MaxResourceCount > 0',
      group: 'diagnostics',
      labels: ['diagnostic', 'render-set', 'texture', 'validation'],
      fixtureDir: 'invalid-render-set-texture-count-zero',
      sourceFile: 'InvalidRenderSetTextureCountZero.hpp',
      description: 'Verifies that RenderSet texture components now reject zero-sized MaxResourceCount values before any backend tries to expand invalid bindless resource arrays.',
      validates: [
        'TextureComponent MaxResourceCount must be greater than zero.',
        'The diagnostic names the offending RenderSet field.'
      ],
      watchouts: [
        'A zero-sized bindless expansion is always invalid and used to slip into later backend code paths.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'texture component field "albedo" requires MaxResourceCount to be greater than 0', 'Diagnostic output should explain the zero-count RenderSet texture restriction.');
      }
    }),
    createFixtureCase({
      id: 'invalid-duplicate-varying-attribute',
      title: 'Diagnostic: duplicate render varying [[AttributeN]] are rejected',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'varying', 'validation'],
      fixtureDir: 'invalid-duplicate-varying-attribute',
      sourceFile: 'InvalidDuplicateVaryingAttribute.hpp',
      description: 'Verifies that render varying records cannot alias two fields onto the same explicit Attribute location.',
      validates: [
        'Duplicate varying Attribute locations fail before backend shader emission.',
        'The diagnostic names the duplicated Attribute index and both conflicting fields.'
      ],
      watchouts: [
        'This keeps vertex-to-fragment ABI mismatches from turning into opaque backend semantic collisions.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'reuses [[Attribute0]] on both field "color0" and field "color1"', 'Diagnostic output should identify the duplicated varying Attribute location.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-position',
      title: 'Diagnostic: vertex outputs must declare [[Position]] exactly once',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'vertex', 'validation'],
      fixtureDir: 'invalid-missing-position',
      sourceFile: 'InvalidMissingPosition.hpp',
      description: 'Checks that render vertex output records fail fast when they omit the required Position semantic entirely.',
      validates: [
        'Vertex outputs must declare exactly one [[Position]] field.',
        'The diagnostic names the offending vertex output record.'
      ],
      watchouts: [
        'Without this guard, Metal clip-space correction and backend ABI generation both become unreliable.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'requires vertex output "InvalidMissingPositionVertexOutput" to declare exactly one [[Position]] field', 'Diagnostic output should explain the missing vertex Position requirement.');
      }
    }),
    createFixtureCase({
      id: 'invalid-render-entity-in-fragment',
      title: 'Diagnostic: [[RenderEntityID]] is rejected outside vertex shaders',
      group: 'diagnostics',
      labels: ['diagnostic', 'render', 'builtin', 'validation'],
      fixtureDir: 'invalid-render-entity-in-fragment',
      sourceFile: 'InvalidRenderEntityInFragment.hpp',
      description: 'Ensures RenderEntity-derived entry parameters cannot silently leak into fragment shaders, where the current ABI contract does not define them.',
      validates: [
        'RenderEntity builtin parameters are restricted to vertex shaders.',
        'The diagnostic names the offending parameter and stage restriction.'
      ],
      watchouts: [
        'This prevents later backend entry-signature generation from fabricating invalid instance-derived semantics in fragment stages.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'parameter "renderEntityID" uses [[RenderEntityID]]', 'Diagnostic output should identify the invalid RenderEntity parameter.');
        await t.expectStepOutputContains('uglc', 'only valid in vertex shaders', 'Diagnostic output should explain the stage restriction.');
      }
    }),
    createFixtureCase({
      id: 'invalid-framebuffer-unsupported-field',
      title: 'Diagnostic: framebuffer fields must be attachment types',
      group: 'diagnostics',
      labels: ['diagnostic', 'framebuffer', 'render-target', 'validation'],
      fixtureDir: 'invalid-framebuffer-unsupported-field',
      sourceFile: 'InvalidFrameBufferUnsupportedField.hpp',
      description: 'Confirms framebuffer records now reject arbitrary plain fields instead of silently dropping them from generated render-pass descriptors.',
      validates: [
        'Framebuffer fields must be declared as attachment wrapper types.',
        'The diagnostic names the invalid field type.'
      ],
      watchouts: [
        'Without this, users can believe a fragment output field participates in the render pass even though codegen ignores it.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'must be declared as UGL::ColorAttachment<Format>, UGL::DepthStencilAttachment<Format>, UGL::PixelLocalColorAttachment<Format, ...>, or UGL::PixelLocalDepthAttachment<Format, ...>', 'Diagnostic output should explain the framebuffer attachment-type contract.');
        await t.expectStepOutputContains('uglc', 'float4', 'Diagnostic output should identify the unsupported framebuffer field type.');
      }
    }),
    createFixtureCase({
      id: 'invalid-framebuffer-astc4x4',
      title: 'Diagnostic: ASTC4x4Unorm is rejected as a framebuffer attachment',
      group: 'diagnostics',
      labels: ['diagnostic', 'framebuffer', 'render-target', 'texture', 'astc'],
      fixtureDir: 'invalid-framebuffer-astc4x4',
      sourceFile: 'InvalidFrameBufferASTC4x4.hpp',
      description: 'Verifies ASTC4x4Unorm remains a sampled compressed texture format and cannot be used as a color attachment.',
      validates: [
        'Compressed ASTC textures cannot lower to framebuffer attachment return types.',
        'The diagnostic explains that the format cannot be used as a framebuffer or color attachment format.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'ASTC4x4Unorm cannot be used as a framebuffer or color attachment format', 'Diagnostic output should reject ASTC attachments clearly.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-source-file',
      title: 'Diagnostic: missing source files fail before discovery starts',
      group: 'diagnostics',
      labels: ['diagnostic', 'frontend', 'cli', 'validation'],
      fixtureDir: 'invalid-missing-source-file',
      sourceFile: 'MissingSource.hpp',
      description: 'Verifies the CLI now rejects a missing `-s` source file up front instead of falling through into discovery/codegen setup.',
      validates: [
        'UGLC exits with a non-zero status when the requested source file does not exist.',
        'The diagnostic is emitted as a stable front-end configuration error.'
      ],
      watchouts: [
        'This keeps obvious invocation mistakes from surfacing later as empty discovery or unrelated parser failures.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGLC: error: Source file does not exist:', 'Diagnostic output should explain that the requested source path is missing.');
        await t.expectStepOutputContains('uglc', 'MissingSource.hpp', 'Diagnostic output should include the missing source file path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-missing-include-path',
      title: 'Diagnostic: missing include search paths fail before discovery starts',
      group: 'diagnostics',
      labels: ['diagnostic', 'frontend', 'cli', 'validation'],
      fixtureDir: 'invalid-missing-include-path',
      sourceFile: 'InvalidMissingIncludePath.hpp',
      extraIncludeDirs: [
        path.join(sourceDir, 'tests', 'uglc', 'fixtures', 'invalid-missing-include-path', 'MissingIncludeDir')
      ],
      description: 'Verifies the CLI now rejects nonexistent `-I` include directories before running the single-header discovery pass.',
      validates: [
        'UGLC exits with a non-zero status when any configured include search path does not exist.',
        'The diagnostic is emitted as a stable front-end configuration error.'
      ],
      watchouts: [
        'This keeps typos in include path wiring from surfacing later as missing local headers or empty discovery noise.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'UGLC: error: Include path does not exist:', 'Diagnostic output should explain that one of the requested include directories is missing.');
        await t.expectStepOutputContains('uglc', 'MissingIncludeDir', 'Diagnostic output should include the missing include directory path.');
      }
    }),
    createFixtureCase({
      id: 'invalid-local-include-missing',
      title: 'Diagnostic: missing quoted local includes fail clearly',
      group: 'diagnostics',
      labels: ['diagnostic', 'include', 'frontend', 'validation'],
      fixtureDir: 'invalid-local-include-missing',
      sourceFile: 'InvalidLocalIncludeMissing.hpp',
      description: 'Verifies the single-header discovery pass no longer treats missing quoted project includes like external system headers and instead fails with a direct configuration error.',
      validates: [
        'Missing quoted local includes fail with a non-zero exit code.',
        'The diagnostic names both the missing include and the source file that referenced it.'
      ],
      watchouts: [
        'This keeps include-path mistakes from being misdiagnosed later as missing DSL types or parser failures.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'Unable to resolve local include "MissingLocalHeader.hpp"', 'Diagnostic output should identify the missing quoted include.');
        await t.expectStepOutputContains('uglc', 'InvalidLocalIncludeMissing.hpp', 'Diagnostic output should identify the referencing source file.');
      }
    }),
    createFixtureCase({
      id: 'invalid-local-include-non-hpp',
      title: 'Diagnostic: quoted local .h includes are treated as project headers too',
      group: 'diagnostics',
      labels: ['diagnostic', 'include', 'frontend', 'validation'],
      fixtureDir: 'invalid-local-include-non-hpp',
      sourceFile: 'InvalidLocalIncludeNonHpp.hpp',
      description: 'Verifies quoted project includes no longer special-case `.hpp` and now fail fast even when the missing header uses a `.h` suffix.',
      validates: [
        'Missing quoted local `.h` includes fail with a non-zero exit code.',
        'The diagnostic keeps the same local-include wording and includes the include chain.'
      ],
      watchouts: [
        'Without this, a misspelled local `.h` include could be silently reclassified as an external header and only fail much later with misleading parser errors.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'Unable to resolve local include "MissingLocalHeader.h"', 'Diagnostic output should identify the missing quoted .h include.');
        await t.expectStepOutputContains('uglc', 'Include chain:', 'Diagnostic output should include the local include chain context.');
      }
    }),
    createFixtureCase({
      id: 'invalid-compute-unannotated-param',
      title: 'Diagnostic: compute entry parameters must declare explicit builtins',
      group: 'diagnostics',
      labels: ['diagnostic', 'compute', 'entry', 'validation'],
      fixtureDir: 'invalid-compute-unannotated-param',
      sourceFile: 'InvalidComputeUnannotatedParam.hpp',
      description: 'Confirms compute entries no longer auto-upgrade unannotated parameters into implicit stage inputs, which is invalid for Metal compute entry signatures.',
      validates: [
        'Compute entry parameters must use explicit builtin annotations.',
        'The diagnostic explains that implicit stage_in lowering is not available in compute.'
      ],
      watchouts: [
        'Without this, malformed compute signatures can make it all the way to backend entry generation before failing with much less readable errors.'
      ],
      expectedExitCode: 1,
      verify: async (t) => {
        await t.expectStepOutputContains('uglc', 'is missing an explicit compute builtin attribute', 'Diagnostic output should explain the missing compute builtin annotation.');
        await t.expectStepOutputContains('uglc', 'implicit [[stage_in]]', 'Diagnostic output should explain why compute cannot fall back to stage_in.');
      }
    }),
    createHarnessCase({
      id: 'code-writer-regressions',
      title: 'White-box: structured code writer keeps backend wrapper formatting stable',
      group: 'whitebox',
      labels: ['whitebox', 'codegen', 'writer', 'backend'],
      sourceFile: 'code_writer_regressions.cpp',
      extraSources: ['UGLC/Source/CodeGen/CodeWriter.cpp'],
      description: 'Exercises the new structured code-writer foundation so backend-facing wrapper code keeps stable line/statement formatting as we prepare for additional shader backends.',
      validates: [
        'Embedded shader-artifact wrappers keep a stable newline and terminator layout.',
        'Structured statement emission keeps indentation and semicolon placement stable.',
        'The backend-facing wrapper format stays stable as we expand beyond a single text-only shader artifact.'
      ],
      watchouts: [
        'This locks down the minimal abstraction introduced for future HLSL support before more code paths start depending on it.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('run', 'code_writer_formatting_ok', 'Harness should confirm the structured code writer keeps stable formatting.');
      }
    }),
    createHarnessCase({
      id: 'app-support-regressions',
      title: 'White-box: shared result aggregation and output failures stay observable',
      group: 'whitebox',
      labels: ['whitebox', 'output', 'aggregation', 'regression'],
      sourceFile: 'app_support_regressions.cpp',
      runArgs: ({ profile }) => [
        '--uglc',
        profile.uglcExecutable,
        '--ugl-headers',
        profile.uglHeadersDir
      ],
      description: 'Exercises the app-level helpers behind the multithreaded driver so duplicate generated keys overwrite with a warning and output-path failures produce a non-zero process result.',
      validates: [
        'Duplicate generated keys overwrite stale content instead of being silently ignored.',
        'Duplicate overwrites emit a readable warning for investigation.',
        'Output-directory and file-write failures stay observable through boolean helpers and a failing UGLC exit code.'
      ],
      watchouts: [
        'These are driver-level regressions; they are easy to miss if we only inspect generated shader text and never probe the app support layer directly.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('run', 'shared_results_overwrite_ok', 'Harness should confirm duplicate generated keys overwrite stale content.');
        await t.expectStepOutputContains('run', 'write_result_success_ok', 'Harness should confirm successful writes still work.');
        await t.expectStepOutputContains('run', 'write_result_failure_ok', 'Harness should confirm failing writes return false.');
        await t.expectStepOutputContains('run', 'uglc_output_failure_exit_ok', 'Harness should confirm UGLC exits non-zero when the output path is invalid.');
      }
    }),
    createHarnessCase({
      id: 'msl-safety-regressions',
      title: 'White-box: MSL format mappings stay stable',
      group: 'whitebox',
      labels: ['whitebox', 'msl', 'format'],
      sourceFile: 'msl_safety_regressions.cpp',
      description: 'Runs direct host-side checks against the MSL type convertor so confirmed format-mapping bugs cannot silently return.',
      validates: [
        '`RG8Sint` framebuffer output types stay mapped to `short2`.',
        'Integer texture scalar categories remain available for the texture-object path.',
        'Exact format-key lookup keeps sRGB and integer formats on stable explicit rows.'
      ],
      watchouts: [
        'This is the narrowest way to lock down the exact helper-layer contracts behind S1-15 without depending on full shader generation.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('run', 'msl_format_mapping_ok', 'Harness should confirm the MSL format mapping regression is fixed.');
      }
    }),
    createHarnessCase({
      id: 'shader-compiler-service-regressions',
      title: 'White-box: shader compiler service stays structured in both placeholder and DXC-enabled builds',
      group: 'whitebox',
      labels: ['whitebox', 'hlsl', 'dxc', 'compiler-service', 'regression'],
      sourceFile: 'shader_compiler_service_regressions.cpp',
      extraSources: ['UGLC/Source/CodeGen/ShaderCompilerRegistry.cpp'],
      description: 'Exercises the HLSL/SPIR-V compiler-service seam in both supported states: the default DXC-enabled build should return a real SPIR-V payload, while opt-out builds must still preserve the same diagnostics-shaped placeholder contract.',
      validates: [
        'Both DXC-enabled and explicitly disabled builds preserve backend/stage/entry/source metadata on the compiler-service diagnostics path.',
        'The default build exposes the real HLSL/SPIR-V compiler service and returns a non-empty SPIR-V module.',
        'Opt-out builds still expose a placeholder HLSL/SPIR-V compiler service instead of leaving the call site without an insertion point.',
        'DXC-enabled builds return a real non-empty SPIR-V module with the expected magic number.',
        'Shader line directives remain wired but disabled by default while other bugs are still being investigated.'
      ],
      watchouts: [
        'This is now the contract line for both the opt-out placeholder path and the default DXC backend. If it regresses, future HLSL work will break at the service boundary instead of inside emitters.'
      ],
      verify: async (t) => {
        const runStep = t.getStep('run');
        const output = `${runStep?.output ?? ''}`;
        const matchedPlaceholder = output.includes('shader_compiler_service_placeholder_ok');
        const matchedDxc = output.includes('shader_compiler_service_dxc_ok');
        t.recordCheck(
          'Harness should confirm either the default placeholder DXC compiler-service path or the enabled DXC compilation path.',
          matchedPlaceholder || matchedDxc,
          matchedPlaceholder
            ? 'Observed placeholder DXC compiler-service behavior.'
            : (matchedDxc ? 'Observed real DXC/SPIR-V compiler-service behavior.' : 'Harness output did not match either expected DXC compiler-service mode.')
        );
      }
    }),
    createHarnessCase({
      id: 'space-manager-regressions',
      title: 'White-box: SpaceManager indentation depth stays saturated at zero',
      group: 'whitebox',
      labels: ['whitebox', 'indentation', 'space-manager', 'regression'],
      sourceFile: 'space_manager_regressions.cpp',
      extraSources: ['UGLC/Source/CodeGen/SpaceManager.cpp'],
      description: 'Compiles and runs a tiny host-side harness that exercises repeated enter/quit calls so indentation depth cannot underflow and poison later code emission.',
      validates: [
        'Calling `quit()` at depth zero no longer produces a negative indentation state.',
        'A later `enter()` still restores the expected single indent after an underflow attempt.',
        'Balanced enter/quit sequences still return to zero indentation.'
      ],
      watchouts: [
        'This is a low-level emitter invariant; if it regresses, later generated code can become hard to inspect even when semantics are otherwise correct.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('run', 'space_manager_underflow_guard_ok', 'Harness should confirm SpaceManager no longer underflows below zero indentation.');
      }
    }),
    createHarnessCase({
      id: 'single-header-state-reset',
      title: 'White-box: SingleHeaderGenerator state resets between runs',
      group: 'whitebox',
      labels: ['whitebox', 'single-header', 'state', 'regression'],
      sourceFile: 'single_header_state_reset.cpp',
      description: 'Compiles and runs a tiny host-side harness that reuses one `SingleHeaderGenerator` instance across two different inputs to verify state is fully reset.',
      validates: [
        'The include-path list is reset on `init(...)`.',
        'In-memory file cache and raw/export state are cleared on `create(...)`.',
        'A second run does not leak content from the first run.',
        'Local includes after a shader-artifact raw string still get expanded.'
      ],
      watchouts: [
        'This is a pure host-side regression that the black-box UGLC executable path would not expose on its own.'
      ],
      verify: async (t) => {
        await t.expectStepOutputContains('run', 'single_header_cache_lookup_ok', 'Harness should confirm canonical cache lookups hit the in-memory file map.');
        await t.expectStepOutputContains('run', 'single_header_reuse_ok', 'Harness should report that state reset behaved correctly.');
        await t.expectStepOutputContains('run', 'single_header_raw_string_close_ok', 'Harness should confirm raw strings that close with a trailing comma do not stop later include expansion.');
      }
    }),
    createHarnessCase({
      id: 'single-header-include-paths',
      title: 'White-box: canonical include identity preserves ambiguity checks',
      group: 'whitebox',
      labels: ['whitebox', 'single-header', 'include-path', 'regression'],
      sourceFile: 'single_header_include_paths.cpp',
      description: 'Resolves relative, absolute and symbolic-link aliases of a dependency while rejecting genuinely different include candidates.',
      validates: [
        'Dot and parent components do not cause false include conflicts.',
        'Absolute paths and symbolic links use the same canonical file identity.',
        'Dependencies outside the declared DSL roots remain external includes.',
        'Repeated aliases expand a DSL dependency only once.',
        'Different files with the same include name still fail.'
      ],
      /** Checks each independent include-resolution scenario completed successfully. */
      verify: async (t) => {
        for (const marker of [
          'plain_include_ok', 'dot_include_ok', 'parent_dsl_include_ok',
          'external_include_preserved_ok', 'canonical_absolute_include_ok',
          'absolute_alias_include_ok', 'symlink_include_ok',
          'duplicate_alias_expanded_once_ok', 'distinct_files_rejected_ok'
        ]) {
          await t.expectStepOutputContains('run', marker, `Include-resolution scenario: ${marker}`);
        }
      }
    }),
    ...[
      createFixtureCase({
        id: 'experimental-uglir-excluded-hull-domain',
        title: 'Experimental UGLIR: excluded Hull and Domain stages are explicitly rejected',
        group: 'uglir', labels: ['uglir', 'uglir', 'diagnostic', 'stage'],
        fixtureDir: 'experimental-uglir-excluded-stages', sourceFile: 'ExcludedStages.hpp',
        uglcArgs: ['--shader-pipeline=uglir'], expectedExitCode: 1,
        description: 'Keeps the user-excluded stages out of the experimental path without falling back to Legacy.',
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'unsupported method "hull"', 'Hull must be rejected explicitly.');
          await t.expectStepOutputContains('uglc', 'unsupported method "domain"', 'Domain must be rejected explicitly.');
          await t.expectArtifactNotExists('generate_result.hpp', 'Excluded stages must not publish a fallback artifact.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-unoptimized-evaluation-semantics',
        title: 'Experimental UGLIR: explicitly deliver validated unoptimized SPIR-V',
        group: 'uglir', labels: ['uglir', 'uglir', 'evaluation', 'spirv', 'unoptimized'],
        fixtureDir: '../../rhi_cases', sourceFile: 'EvaluationSemantics.hpp',
        uglcArgs: ['--shader-pipeline=uglir', '--no-spirv-optimization'],
        description: 'Compiles the GPU evaluation corpus without SPIR-V optimization and checks the actual delivered words.',
        verify: async (t) => {
          const raw = await t.readArtifact('spv/EvaluationSemanticsPass.raw.spv.txt');
          const delivered = await t.readArtifact('spv/EvaluationSemanticsPass.spv.txt');
          t.recordCheck('The unoptimized final module must equal the validated raw writer output.', raw === delivered && raw.length > 0);
          const compilation = JSON.parse(await t.readArtifact('shader-compilation.json'));
          t.recordCheck('Unoptimized delivery must be recorded explicitly.', compilation.spirvOptimization === 'disabled');
          await t.expectArtifactNotExists('hlsl', 'Disabling optimization must not enable HLSL fallback.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-optimization-option-requires-entry',
        title: 'Experimental UGLIR: optimization options cannot implicitly select the pipeline',
        group: 'uglir', labels: ['uglir', 'uglir', 'entry', 'diagnostic'],
        fixtureDir: '../../rhi_cases', sourceFile: 'EvaluationSemantics.hpp',
        uglcArgs: ['--shader-pipeline=legacy', '--no-spirv-optimization'], expectedExitCode: 1,
        description: 'Rejects direct SPIR-V optimization options when the Legacy pipeline is selected.',
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'requires --shader-pipeline=uglir', 'A tuning option cannot change the default pipeline.');
          await t.expectArtifactNotExists('generate_result.hpp', 'Invalid entry configuration must not publish an artifact.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-buffer-layout-semantics',
        title: 'Experimental UGLIR: half storage, nested arrays, and non-square matrix layout',
        group: 'uglir', labels: ['uglir', 'uglir', 'layout', 'half', 'spirv'],
        fixtureDir: '../../rhi_cases', sourceFile: 'BufferLayoutSemantics.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        description: 'Compiles the accepted host-layout corpus shared with GPU readback tests.',
        verify: async (t) => {
          await t.expectArtifactExists('generate_result.hpp', 'Both direct emitters must accept the layout.');
          await t.expectFileContains('spv/BufferLayoutPass.spvasm', 'StorageBuffer16BitAccess', 'Half storage must declare its own capability.');
          await t.expectFileContains('spv/BufferLayoutPass.spvasm', 'UniformAndStorageBuffer16BitAccess', 'Half uniform storage must declare its own capability.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-failure-invalidates-stale-artifacts',
        title: 'Experimental UGLIR: failed compilation invalidates stale shader artifacts',
        group: 'uglir', labels: ['uglir', 'uglir', 'diagnostic', 'artifacts'],
        fixtureDir: 'experimental-uglir-invalid-raw-pointer', sourceFile: 'ExperimentalUGLIRInvalidRawPointer.hpp',
        uglcArgs: ['--shader-pipeline=uglir'], expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success', 'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.json': '{}', 'spv/stale.spvasm': 'stale binary',
          'msl/stale.msl': 'stale source', 'hlsl/stale.hlsl': 'stale fallback',
          'unrelated.txt': 'preserved'
        },
        description: 'Seeds previous output before a rejected source invocation to test failure publication behavior.',
        verify: async (t) => {
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'spv', 'msl', 'hlsl']) {
            await t.expectArtifactNotExists(artifact, 'A failed experiment must invalidate ' + artifact);
          }
          await t.expectFileContains('unrelated.txt', 'preserved', 'Unrelated files must survive artifact invalidation.');
          await t.expectStepOutputContains('uglc', 'UGLIR verifier:', 'The failure must identify the experimental source verifier.');
        }
      }),
      createHarnessCase({
        id: 'experimental-uglir-invariant-validation',
        title: 'Experimental UGLIR: malformed intermediate representations fail validation',
        group: 'uglir', labels: ['uglir', 'uglir', 'whitebox', 'validation'],
        sourceFile: 'uglir_verifier.cpp',
        extraSources: ['UGLC/Source/CodeGen/UGLIR/UGLIRVerifier.cpp', 'UGLC/Source/CodeGen/UGLIR/UGLIRTypeUtils.cpp'],
        description: 'Checks invalid operand counts, write destinations, control flow, types, entries, and resource bindings.',
        verify: async (t) => {
          await t.expectStepOutputContains('run', 'uglir_invariant_validation_ok', 'Malformed IR must be rejected before emission.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-evaluation-semantics',
        title: 'Experimental UGLIR: evaluation semantics reach both direct emitters',
        group: 'uglir', labels: ['uglir', 'uglir', 'evaluation', 'msl', 'spirv'],
        fixtureDir: '../../rhi_cases', sourceFile: 'EvaluationSemantics.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        description: 'Compiles the same dynamic evaluation corpus used by the Metal and Vulkan GPU readback tests.',
        verify: async (t) => {
          await t.expectArtifactExists('generate_result.hpp', 'Both direct emitters must succeed.');
          await t.expectArtifactNotExists('hlsl', 'The direct path must not use HLSL/DXC fallback.');
          await t.expectFileContains('uglir/EvaluationSemanticsPass.uglir.json', '"isPostfix": true', 'IR must preserve postfix evaluation.');
          await t.expectFileContains('spv/EvaluationSemanticsPass.raw.spvasm', 'OpSelectionMerge', 'Conditional operands must have structured control flow.');
          await t.expectArtifactExists('spv/EvaluationSemanticsPass.spvasm', 'The validated final SPIR-V must be available.');
          const compilation = JSON.parse(await t.readArtifact('shader-compilation.json'));
          t.recordCheck('Provenance must identify the UGLIR path and toolchains.', !("experimental" in compilation) &&
            compilation.pipeline === 'UGLIR' && compilation.defaultPipeline === 'UGLIR' && compilation.fallbackUsed === false &&
            compilation.clangVersion.length > 0 && compilation.spirvToolsVersion.length > 0 && compilation.targetTriple.length > 0 &&
            compilation.spirvOptimization === 'runtime' && /^[a-f0-9]{64}$/u.test(compilation.mergedDslSha256));
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'msl/EvaluationSemanticsPass.msl', 'spv/EvaluationSemanticsPass.spv.txt']) {
            const digest = createHash('sha256').update(await t.readArtifact(artifact)).digest('hex');
            t.recordCheck('Provenance must hash the delivered ' + artifact, compilation.artifacts[artifact] === digest);
          }
        }
      }),
      createHarnessCase({
        id: 'uglir-core-dump',
        title: 'Experimental white-box: UGLIR core dumps stay deterministic',
        group: 'uglir',
        labels: ['uglir', 'whitebox', 'uglir', 'dump'],
        sourceFile: 'uglir_core_dump.cpp',
        extraSources: [
          'UGLC/Source/CodeGen/UGLIR/UGLIRDump.cpp',
          'UGLC/Source/CodeGen/UGLIR/UGLIRTypeUtils.cpp'
        ],
        description: 'Constructs a minimal compute UGLIR module by hand and verifies stable text and JSON dumps without touching real shader lowering.',
        validates: [
          'The phase-1 UGLIR core data model can represent source locations, types, functions, statements, expressions, and reflection.',
          'Text dumps match an exact golden string.',
          'JSON dumps are parseable through nlohmann/json and deterministic across repeated calls.'
        ],
        watchouts: [
          'This case is registered only for build profiles that compile UGLC with UGLIR enabled by default.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('run', 'uglir_text_dump_ok', 'Harness should confirm the UGLIR text dump matches the phase-1 golden output.');
          await t.expectStepOutputContains('run', 'uglir_json_dump_ok', 'Harness should confirm the UGLIR JSON dump parses and preserves required fields.');
          await t.expectStepOutputContains('run', 'uglir_dump_determinism_ok', 'Harness should confirm repeated UGLIR dumps are byte-identical.');
        }
      }),
      createExperimentalUglirComputeBasicLoweringCase(),
      createFixtureCase({
        id: 'experimental-uglir-explicit-precision-construct-info',
        title: 'Experimental UGLIR shader pipeline: explicit precision constructors carry normalized construct metadata',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'half', 'conversion', 'construct'],
        fixtureDir: 'shader-explicit-precision-conversion',
        sourceFile: 'ShaderExplicitPrecisionConversion.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 0,
        description: 'Confirms explicit scalar, vector splat, and vector-from-vector precision constructors are normalized in UGLIR before backend emission.',
        validates: [
          'UGLIR records scalar conversion metadata for explicit half construction.',
          'UGLIR records vector splat metadata for explicit half vector splats.',
          'UGLIR records vector component mapping for explicit float4 to half4 construction.'
        ],
        verify: async (t) => {
          await t.expectArtifactExists('uglir/ShaderExplicitPrecisionConversionPass.uglir.txt', 'Explicit precision fixture should write a UGLIR text dump.');
          await t.expectArtifactExists('uglir/ShaderExplicitPrecisionConversionPass.uglir.json', 'Explicit precision fixture should write a UGLIR JSON dump.');
          await t.expectFileContains('uglir/ShaderExplicitPrecisionConversionPass.uglir.txt', 'construct_kind scalar_convert', 'Scalar half construction should be normalized as a scalar conversion.');
          await t.expectFileContains('uglir/ShaderExplicitPrecisionConversionPass.uglir.txt', 'construct_kind vector_splat', 'half4(1.0f) should be normalized as a vector splat.');
          await t.expectFileContains('uglir/ShaderExplicitPrecisionConversionPass.uglir.txt', 'construct_kind vector_from_components construct_scalar_kind "half" construct_components 4', 'half4(float4Value) should be normalized as four half components.');
          await t.expectFileContains('uglir/ShaderExplicitPrecisionConversionPass.uglir.txt', 'construct_component operand 0 source_component 0 source_scalar_kind "float" target_scalar_kind "half" source_is_vector true', 'The vector construct should explicitly map source x from float to half.');
          const parsed = JSON.parse(await t.readArtifact('uglir/ShaderExplicitPrecisionConversionPass.uglir.json'));
          const text = JSON.stringify(parsed);
          t.recordCheck('UGLIR JSON should expose normalized construct metadata for backend-independent constructor lowering.', (
            text.includes('"constructKind":"scalar_convert"') &&
            text.includes('"constructKind":"vector_splat"') &&
            text.includes('"constructKind":"vector_from_components"') &&
            text.includes('"sourceScalarKind":"float"') &&
            text.includes('"targetScalarKind":"half"')
          ), text);
        }
      }),
      createExperimentalUglirSymbolicValueTypesCase(),
      createExperimentalUglirSymbolicResourcesCase(),
      createExperimentalUglirStageIOSemanticsCase(),
      createExperimentalUglirNestedStageOutputCase(),
      createExperimentalUglirIntrinsicCallKindsComputeCase(),
      createExperimentalUglirIntrinsicCallKindsFragmentCase(),
      createExperimentalUglirControlFlowLoweringCase(),
      createExperimentalUglirRenderBasicCase(),
      createExperimentalUglirMatrixVectorMulVulkanCase(),
      createFixtureCase({
        id: 'experimental-uglir-invalid-non-square-vector-matrix-mul',
        title: 'Experimental UGLIR verifier: non-square vector-matrix multiplication is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'matrix'],
        fixtureDir: 'experimental-uglir-invalid-non-square-vector-matrix-mul',
        sourceFile: 'InvalidNonSquareVectorMatrixMul.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Rejects a non-square vector-matrix overload at the shared UGLIR validation boundary because Metal and SPIR-V cannot express the same operand shape.',
        validates: [
          'The shared UGLIR verifier diagnoses the unsupported non-square vector-matrix shape before either backend emits code.',
          'A verifier failure removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR validation:', 'The rejection must come from the shared UGLIR verifier.');
          await t.expectStepOutputContains('uglc', 'non-square vector-matrix multiplication is unsupported by the cross-backend shader ABI', 'The diagnostic must identify the unsupported operand shape.');
          await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'The experimental path must not fall back to DXC.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected non-square vector-matrix operation must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-non-square-matrix-matrix-mul',
        title: 'Experimental UGLIR verifier: non-square matrix-matrix multiplication is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'matrix'],
        fixtureDir: 'experimental-uglir-invalid-non-square-matrix-matrix-mul',
        sourceFile: 'InvalidNonSquareMatrixMatrixMul.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Rejects a non-square matrix-matrix overload at the shared UGLIR validation boundary because the experimental Metal and SPIR-V paths expose incompatible matrix shapes.',
        validates: [
          'The shared UGLIR verifier diagnoses the unsupported non-square matrix-matrix shape before either backend emits code.',
          'A verifier failure removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR validation:', 'The rejection must come from the shared UGLIR verifier.');
          await t.expectStepOutputContains('uglc', 'non-square or mismatched matrix-matrix multiplication is unsupported by the cross-backend shader ABI', 'The diagnostic must identify the unsupported operand shape.');
          await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'The experimental path must not fall back to DXC.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected non-square matrix-matrix operation must remove ' + artifact + '.');
          }
        }
      }),
      createExperimentalUglirTexture2DArrayGatherCase(),
      createExperimentalUglirStorageTextureComputeCase(),
      createExperimentalUglirRenderSetShaderABICase(),
      createExperimentalUglirStaticVariantRenderCase(),
      createExperimentalUglirPixelLocalDeferredScreenCase(),
      createExperimentalUglirNativeHalfSPIRVCase(),
      createExperimentalUglirWVMRegressionCase(),
      createFixtureCase({
        id: 'experimental-uglir-if-constexpr-std-bool',
        title: 'Experimental UGLIR: dependent if constexpr with std::is_same_v<bool>',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'compute', 'template', 'constexpr'],
        fixtureDir: 'experimental-uglir-if-constexpr-std-bool',
        sourceFile: 'ExperimentalUGLIRIfConstexprStdBool.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 0,
        description: 'Compiles a shader helper whose if constexpr condition is std::is_same_v<T, bool> after explicit bool specialization.',
        hostCompileSteps: [
          {
            id: 'host-compile-if-constexpr-bool-specialization',
            title: 'Compile and instantiate the generated bool if constexpr helper',
            sourceText: '#include "__GENERATED_HEADER__"\nint main() { return ExperimentalUGLIRIfConstexprStdBoolHelpers::selectValue<bool>(true) == 1u ? 0 : 1; }\n',
            expectedExitCode: 0
          }
        ],
        validates: [
          'The experimental path compiles the selected bool branch without Legacy ShaderReferenceVisitor analysis.',
          'The discarded branch is not emitted into the UGLIR-derived shader artifact.',
          'The generated host header retains a compilable selectValue<bool>(true) specialization that is instantiated in a host translation unit.'
        ],
        verify: async (t) => {
          await t.expectArtifactExists('generate_result.hpp', 'The dependent if constexpr fixture should generate the host artifact.');
          await t.expectArtifactExists('uglir/ExperimentalUGLIRIfConstexprStdBoolPass.uglir.txt', 'The fixture should generate a UGLIR text dump.');
          await t.expectArtifactExists('spv/ExperimentalUGLIRIfConstexprStdBoolPass.spvasm', 'The fixture should generate direct SPIR-V.');
          await t.expectFileNotContains('uglir/ExperimentalUGLIRIfConstexprStdBoolPass.uglir.txt', 'std::is_same_v', 'The dependent type trait should be resolved before UGLIR emission.');
          await t.expectFileContains('msl/ExperimentalUGLIRIfConstexprStdBoolPass.msl', '1u', 'The selected bool branch should remain observable in MSL.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-dsl-reserved-vertex-local',
        title: 'Experimental UGLIR verifier: shader local vertex is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'reserved-identifier'],
        fixtureDir: 'invalid-dsl-reserved-vertex-local',
        sourceFile: 'InvalidDSLReservedVertexLocal.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Reuses the existing reserved vertex local fixture to cover reachable local-name validation after the legacy reference visitor is skipped.',
        validates: [
          'The experimental verifier preserves the reserved-name diagnostic for a reachable local declaration.',
          'A verifier failure removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'The experimental verifier should preserve the reserved-name diagnostic.');
          await t.expectStepOutputContains('uglc', 'Rename local variable "vertex"', 'The diagnostic should identify the local role.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A reserved local-name failure must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-dsl-reserved-vertex-parameter',
        title: 'Experimental UGLIR verifier: shader parameter vertex is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'reserved-identifier'],
        fixtureDir: 'invalid-dsl-reserved-vertex-parameter',
        sourceFile: 'InvalidDSLReservedVertexParameter.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Reuses the existing reserved vertex parameter fixture to cover reachable helper-parameter validation.',
        validates: [
          'The experimental verifier preserves the reserved-name diagnostic for a reachable helper parameter.',
          'A verifier failure removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'The experimental verifier should preserve the reserved-name diagnostic.');
          await t.expectStepOutputContains('uglc', 'Rename parameter "vertex"', 'The diagnostic should identify the parameter role.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A reserved parameter-name failure must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-dsl-reserved-vertex-field',
        title: 'Experimental UGLIR verifier: shader field vertex is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'reserved-identifier'],
        fixtureDir: 'invalid-dsl-reserved-vertex-field',
        sourceFile: 'InvalidDSLReservedVertexField.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Reuses the existing reserved vertex field fixture to cover shader-visible record-field validation.',
        validates: [
          'The experimental verifier preserves the reserved-name diagnostic for a shader-visible record field.',
          'A verifier failure removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGL DSL reserves variable identifier "vertex" for shader stage names.', 'The experimental verifier should preserve the reserved-name diagnostic.');
          await t.expectStepOutputContains('uglc', 'Rename field "vertex"', 'The diagnostic should identify the field role.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A reserved field-name failure must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-global-mutable-read',
        title: 'Experimental UGLIR verifier: mutable global reads are rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'storage'],
        fixtureDir: 'experimental-uglir-invalid-global-mutable-read',
        sourceFile: 'InvalidGlobalMutableRead.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Rejects a shader read from mutable namespace storage before either experimental emitter can publish artifacts.',
        validates: [
          'The source verifier identifies the mutable global storage reference.',
          'A rejected global-storage read removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR verifier:', 'The rejection must come from the experimental source verifier.');
          await t.expectStepOutputContains('uglc', 'shader-reachable global variables must be immutable compile-time constants', 'The diagnostic must explain why mutable global storage is unavailable.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected mutable global read must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-static-mutable-local',
        title: 'Experimental UGLIR verifier: mutable function statics are rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'storage'],
        fixtureDir: 'experimental-uglir-invalid-static-mutable-local',
        sourceFile: 'InvalidStaticMutableLocal.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Rejects a shader-reachable mutable function-local static instead of treating it as an ordinary local variable.',
        validates: [
          'The source verifier identifies mutable static local storage.',
          'A rejected static-storage use removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR verifier:', 'The rejection must come from the experimental source verifier.');
          await t.expectStepOutputContains('uglc', 'shader-reachable static locals must have immutable compile-time constant initialization', 'The diagnostic must explain why mutable static locals are unavailable.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected mutable static local must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-dynamic-constant-storage',
        title: 'Experimental UGLIR verifier: dynamic const global and static initialization is rejected',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'verifier', 'diagnostic', 'storage', 'constants'],
        fixtureDir: 'experimental-uglir-invalid-dynamic-constant-storage',
        sourceFile: 'InvalidDynamicConstantStorage.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        initialArtifacts: {
          'generate_result.hpp': 'stale success',
          'exports.hpp': 'stale exports',
          'shader-compilation.json': '{"stale":true}',
          'experimental-compilation.json': '{"stale":true}',
          'uglir/stale.uglir.txt': 'stale uglir',
          'msl/stale.msl': 'stale msl',
          'hlsl/stale.hlsl': 'stale hlsl',
          'spv/stale.spvasm': 'stale spv'
        },
        description: 'Rejects const-qualified global and static storage whose initializers are not proven compile-time constants.',
        validates: [
          'Const qualification does not permit a dynamic global initializer in shader-reachable code.',
          'Const qualification does not permit a dynamic static-local initializer in shader-reachable code.',
          'A rejected dynamic constant removes all compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR verifier:', 'The rejection must come from the experimental source verifier.');
          await t.expectStepOutputContains('uglc', 'shader-reachable global variables must be immutable compile-time constants', 'The diagnostic must identify the dynamic global constant.');
          await t.expectStepOutputContains('uglc', 'shader-reachable static locals must have immutable compile-time constant initialization', 'The diagnostic must identify the dynamic static constant.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected dynamic constant must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-mutable-reference-non-memory-object',
        title: 'Experimental UGLIR SPIR-V emitter: ordinary mutable resource references are diagnosed',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'spirv', 'diagnostic', 'reference'],
        fixtureDir: 'experimental-uglir-invalid-mutable-reference-non-memory-object',
        sourceFile: 'InvalidMutableReferenceNonMemoryObject.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        description: 'Rejects an ordinary mutable C++ reference when a storage-buffer subobject would require an alias-breaking temporary.',
        validates: [
          'The direct SPIR-V emitter identifies the unsupported mutable-reference temporary path.',
          'Emitter failure does not publish compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR SPIR-V emitter:', 'The diagnostic must come from the direct SPIR-V emitter.');
          await t.expectStepOutputContains('uglc', 'ordinary mutable reference argument requires a function-local memory object', 'The emitter must identify the unsupported temporary path.');
          await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'The experimental path must not fall back to DXC.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected mutable reference must remove ' + artifact + '.');
          }
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-value-result-reference-address-space',
        title: 'Experimental UGLIR SPIR-V emitter: OUT and INOUT address-space crossings are diagnosed',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'spirv', 'diagnostic', 'reference'],
        fixtureDir: 'experimental-uglir-invalid-value-result-reference-address-space',
        sourceFile: 'InvalidValueResultReferenceAddressSpace.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 1,
        description: 'Rejects OUT and INOUT value-result calls whose storage-buffer lvalues cannot be represented in Function storage.',
        validates: [
          'The direct SPIR-V emitter identifies unsupported OUT and INOUT address-space crossings.',
          'Emitter failure does not publish compiler-owned shader artifacts.'
        ],
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR SPIR-V emitter:', 'The diagnostic must come from the direct SPIR-V emitter.');
          await t.expectStepOutputContains('uglc', 'OUT/INOUT argument cannot cross a non-function SPIR-V storage class', 'The emitter must identify the unsupported address-space crossing.');
          await t.expectStepOutputNotContains('uglc', 'DXC compilation failed', 'The experimental path must not fall back to DXC.');
          for (const artifact of ['generate_result.hpp', 'exports.hpp', 'shader-compilation.json', 'experimental-compilation.json', 'uglir', 'msl', 'hlsl', 'spv']) {
            await t.expectArtifactNotExists(artifact, 'A rejected OUT/INOUT crossing must remove ' + artifact + '.');
          }
        }
      }),
      createExperimentalUglirComputeBasicShadowCase(),
      createExperimentalUglirStaticVariantComputeTemplateCase(),
      createExperimentalUglirTemplatePolicyClassCallbackCase(),
      createExperimentalUglirConceptsRequiresPolicyCase(),
      createExperimentalUglirLambdaComputeCase(),
      createExperimentalUglirSPIRVEmitterDiagnosticCase(),
      createExperimentalUglirUniformBufferComputeCase(),
      createExperimentalUglirInvalidUniformScalarCase(),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-destructor',
        title: 'Experimental UGLIR verifier: non-trivial destructors are rejected',
        fixtureDir: 'experimental-uglir-invalid-destructor',
        sourceFile: 'InvalidDestructor.hpp',
        feature: 'non-trivial destructor'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-lambda-reference-capture',
        title: 'Experimental UGLIR verifier: lambda reference capture is rejected',
        fixtureDir: 'experimental-uglir-invalid-lambda-reference-capture',
        sourceFile: 'ExperimentalUGLIRInvalidLambdaReferenceCapture.hpp',
        feature: 'lambda reference capture'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-lambda-host-pointer-capture',
        title: 'Experimental UGLIR verifier: lambda raw pointer capture is rejected',
        fixtureDir: 'experimental-uglir-invalid-lambda-host-pointer-capture',
        sourceFile: 'ExperimentalUGLIRInvalidLambdaHostPointerCapture.hpp',
        feature: 'raw pointer'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-escaping-lambda',
        title: 'Experimental UGLIR verifier: escaping lambda is rejected',
        fixtureDir: 'experimental-uglir-invalid-escaping-lambda',
        sourceFile: 'ExperimentalUGLIRInvalidEscapingLambda.hpp',
        feature: 'escaping lambda'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-generic-lambda',
        title: 'Experimental UGLIR verifier: generic lambda is rejected',
        fixtureDir: 'experimental-uglir-invalid-generic-lambda',
        sourceFile: 'ExperimentalUGLIRInvalidGenericLambda.hpp',
        feature: 'generic lambda'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-raw-pointer',
        title: 'Experimental UGLIR verifier: raw pointers are rejected',
        fixtureDir: 'experimental-uglir-invalid-raw-pointer',
        sourceFile: 'ExperimentalUGLIRInvalidRawPointer.hpp',
        feature: 'raw pointer'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-address-of',
        title: 'Experimental UGLIR verifier: address-of is rejected',
        fixtureDir: 'experimental-uglir-invalid-address-of',
        sourceFile: 'ExperimentalUGLIRInvalidAddressOf.hpp',
        feature: 'address-of'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-nullptr',
        title: 'Experimental UGLIR verifier: nullptr flow is rejected',
        fixtureDir: 'experimental-uglir-invalid-nullptr',
        sourceFile: 'ExperimentalUGLIRInvalidNullptr.hpp',
        feature: 'nullptr'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-function-pointer',
        title: 'Experimental UGLIR verifier: function pointers are rejected',
        fixtureDir: 'experimental-uglir-invalid-function-pointer',
        sourceFile: 'ExperimentalUGLIRInvalidFunctionPointer.hpp',
        feature: 'function pointer'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-member-pointer',
        title: 'Experimental UGLIR verifier: member pointers are rejected',
        fixtureDir: 'experimental-uglir-invalid-member-pointer',
        sourceFile: 'ExperimentalUGLIRInvalidMemberPointer.hpp',
        feature: 'member pointer'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-exception',
        title: 'Experimental UGLIR verifier: exceptions are rejected',
        fixtureDir: 'experimental-uglir-invalid-exception',
        sourceFile: 'ExperimentalUGLIRInvalidException.hpp',
        feature: 'exception'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-rtti',
        title: 'Experimental UGLIR verifier: RTTI is rejected',
        fixtureDir: 'experimental-uglir-invalid-rtti',
        sourceFile: 'ExperimentalUGLIRInvalidRTTI.hpp',
        feature: 'RTTI'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-virtual-dispatch',
        title: 'Experimental UGLIR verifier: virtual dispatch is rejected',
        fixtureDir: 'experimental-uglir-invalid-virtual-dispatch',
        sourceFile: 'ExperimentalUGLIRInvalidVirtualDispatch.hpp',
        feature: 'virtual dispatch'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-new-delete',
        title: 'Experimental UGLIR verifier: new/delete is rejected',
        fixtureDir: 'experimental-uglir-invalid-new-delete',
        sourceFile: 'ExperimentalUGLIRInvalidNewDelete.hpp',
        feature: 'new/delete'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-coroutine',
        title: 'Experimental UGLIR verifier: coroutines are rejected',
        fixtureDir: 'experimental-uglir-invalid-coroutine',
        sourceFile: 'ExperimentalUGLIRInvalidCoroutine.hpp',
        feature: 'coroutine'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-thread',
        title: 'Experimental UGLIR verifier: CPU thread primitives are rejected',
        fixtureDir: 'experimental-uglir-invalid-thread',
        sourceFile: 'ExperimentalUGLIRInvalidThread.hpp',
        feature: 'thread'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-stl-container',
        title: 'Experimental UGLIR verifier: complex STL containers are rejected',
        fixtureDir: 'experimental-uglir-invalid-stl-container',
        sourceFile: 'ExperimentalUGLIRInvalidSTLContainer.hpp',
        feature: 'complex STL container'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-direct-recursion',
        title: 'Experimental UGLIR verifier: direct recursion is rejected',
        fixtureDir: 'experimental-uglir-invalid-direct-recursion',
        sourceFile: 'ExperimentalUGLIRInvalidDirectRecursion.hpp',
        feature: 'recursion'
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-mutual-recursion',
        title: 'Experimental UGLIR verifier: mutual recursion is rejected',
        fixtureDir: 'experimental-uglir-invalid-mutual-recursion',
        sourceFile: 'ExperimentalUGLIRInvalidMutualRecursion.hpp',
        feature: 'recursion'
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-integer-width',
        title: 'Experimental UGLIR: unsupported integer widths cannot silently narrow',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'integer', 'diagnostic'],
        fixtureDir: 'experimental-uglir-invalid-integer-width',
        sourceFile: 'InvalidIntegerWidth.hpp',
        expectedExitCode: 1,
        uglcArgs: ['--shader-pipeline=uglir'],
        description: 'Rejects runtime 64-bit values until exact-width integer lowering is implemented.',
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'implicit narrowing', 'Unsupported integer widths must fail with a precise diagnostic.');
          await t.expectArtifactNotExists('generate_result.hpp', 'Failure must not publish a success artifact.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-undefined-function',
        title: 'Experimental UGLIR: ordinary calls require a matching function definition',
        group: 'uglir', labels: ['uglir', 'uglir', 'function', 'diagnostic'],
        fixtureDir: 'experimental-uglir-undefined-function', sourceFile: 'UndefinedFunction.hpp',
        expectedExitCode: 1, uglcArgs: ['--shader-pipeline=uglir'],
        description: 'Rejects a declared but undefined shader function at the common IR validation boundary.',
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'UGLIR validation: function call has no definition: unavailableValue', 'Both backends require a resolved ordinary call.');
          await t.expectArtifactNotExists('generate_result.hpp', 'An unresolved call must not publish a success artifact.');
          await t.expectArtifactNotExists('shader-compilation.json', 'An unresolved call must not leave a success manifest.');
        }
      }),
      createFixtureCase({
        id: 'experimental-uglir-invalid-floating-width',
        title: 'Experimental UGLIR: unsupported floating-point widths cannot silently narrow',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'floating-point', 'diagnostic'],
        fixtureDir: 'experimental-uglir-invalid-floating-width',
        sourceFile: 'InvalidFloatingWidth.hpp',
        expectedExitCode: 1,
        uglcArgs: ['--shader-pipeline=uglir'],
        description: 'Rejects dynamic binary64 arithmetic until exact-width floating-point lowering is implemented.',
        verify: async (t) => {
          await t.expectStepOutputContains('uglc', 'implicit narrowing of type "double"', 'Unsupported floating-point widths must fail with a source diagnostic.');
          await t.expectArtifactNotExists('generate_result.hpp', 'Failure must not publish a success artifact.');
        }
      }),
      createExperimentalUglirDiagnosticCase({
        id: 'experimental-uglir-invalid-stage-method',
        title: 'Experimental UGLIR verifier: invalid stage methods are rejected',
        fixtureDir: 'experimental-uglir-invalid-stage-method',
        sourceFile: 'ExperimentalUGLIRInvalidStageMethod.hpp',
        feature: 'stage interface'
      })
    ],
    createFixtureCase({
      id: 'rgba32float-framebuffer',
      title: 'Regression: RGBA32Float framebuffer output accepts and emits float4',
      group: 'regressions',
      labels: ['regression', 'render', 'format', 'framebuffer', 'precision'],
      fixtureDir: 'rgba32float-framebuffer',
      sourceFile: 'RGBA32FloatFrameBuffer.hpp',
      description: 'Compiles a float4 assignment to an RGBA32Float color attachment and checks both legacy shader artifacts keep the declared 32-bit four-channel ABI.',
      validates: [
        'The public RGBA32Float format wrapper accepts a float4 framebuffer payload.',
        'Legacy MSL and HLSL outputs remain float4 and the host pipeline uses RGBA32Float.'
      ],
      verify: async (t) => {
        await t.expectArtifactExists('generate_result.hpp', 'Legacy RGBA32Float fixture should generate successfully.');
        await t.expectArtifactExists('dsl_single_header.hpp', 'Legacy RGBA32Float fixture should preserve its DSL source artifact.');
        await t.expectFileContains('dsl_single_header.hpp', 'frameBuffer.color = float4(', 'The DSL source should compile with a direct float4 framebuffer assignment.');
        await t.expectFileContains('generate_result.hpp', 'float4 color[[color(0)]]', 'Legacy MSL should emit an RGBA32Float color output as float4.');
        await t.expectFileNotContains('generate_result.hpp', 'half4 color[[color(0)]]', 'Legacy MSL must not narrow RGBA32Float to half4.');
        await t.expectFileContains('generate_result.hpp', 'float4 color : SV_Target0;', 'Legacy HLSL should emit an RGBA32Float color output as float4.');
        await t.expectFileContains('generate_result.hpp', 'fragmentState.targets[0].format = GVM::RHI::TextureFormat::RGBA32Float', 'The generated host pipeline should retain the RGBA32Float target format.');
      }
    }),
    ...[
      createFixtureCase({
        id: 'experimental-uglir-rgba32float-framebuffer',
        title: 'Experimental UGLIR: RGBA32Float framebuffer output remains float4',
        group: 'uglir',
        labels: ['uglir', 'uglir', 'render', 'format', 'msl', 'spirv'],
        fixtureDir: 'rgba32float-framebuffer',
        sourceFile: 'RGBA32FloatFrameBuffer.hpp',
        uglcArgs: ['--shader-pipeline=uglir'],
        expectedExitCode: 0,
        description: 'Runs a float4 RGBA32Float fragment output through UGLIR, MSL, and direct SPIR-V to prevent the format from regressing to half4.',
        validates: [
          'UGLIR reflects the RGBA32Float attachment as a four-component 32-bit float output.',
          'Experimental MSL emits a float4 color attachment and direct SPIR-V emits a 32-bit float vector output.'
        ],
        verify: async (t) => {
          await t.expectArtifactExists('generate_result.hpp', 'Experimental RGBA32Float fixture should write the generated host header.');
          await t.expectArtifactExists('uglir/RGBA32FloatRenderPass__fragment.uglir.txt', 'Experimental RGBA32Float fixture should write fragment UGLIR.');
          await t.expectArtifactExists('msl/RGBA32FloatRenderPass__fragment.msl', 'Experimental RGBA32Float fixture should write fragment MSL.');
          await t.expectArtifactExists('spv/RGBA32FloatRenderPass__fragment.spvasm', 'Experimental RGBA32Float fixture should write fragment SPIR-V disassembly.');
          await t.expectFileContains('uglir/RGBA32FloatRenderPass__fragment.uglir.txt', 'field "color" type "float4" semantic "Color"', 'UGLIR should reflect the framebuffer color field as float4.');
          await t.expectFileContains('uglir/RGBA32FloatRenderPass__fragment.uglir.txt', 'type "float4" kind vector scalar_kind float bit_width 32', 'UGLIR should preserve four 32-bit floating-point components.');
          await t.expectFileContains('msl/RGBA32FloatRenderPass__fragment.msl', 'float4 color [[color(0)]]', 'Experimental MSL should emit an RGBA32Float color output as float4.');
          await t.expectFileNotContains('msl/RGBA32FloatRenderPass__fragment.msl', 'half4 color [[color(0)]]', 'Experimental MSL must not narrow RGBA32Float to half4.');
          await t.expectFileContains('spv/RGBA32FloatRenderPass__fragment.spvasm', 'OpTypeFloat 32', 'Direct SPIR-V should declare a 32-bit float scalar.');
          await t.expectFileContains('spv/RGBA32FloatRenderPass__fragment.spvasm', 'OpTypeVector %float 4', 'Direct SPIR-V should declare a four-component float output vector.');
          await t.expectFileContains('spv/RGBA32FloatRenderPass__fragment.raw.spvasm', 'OpStore %color', 'Direct SPIR-V should store the float4 payload to the color output.');
          await t.expectFileContains('generate_result.hpp', 'fragmentState.targets[0].format = GVM::RHI::TextureFormat::RGBA32Float', 'The generated host pipeline should retain the RGBA32Float target format.');
        }
      })
    ]
  ].map((entry) => {
    const requiresLegacy = entry.kind === 'uglc-shadow-fixture' ||
      (!entry.id.startsWith('shader-pipeline-') && entry.uglcArgs?.includes('--shader-pipeline=legacy')) ||
      entry.id === 'shader-pipeline-legacy-artifact';
    entry = { ...entry, requiresLegacy,
      skipReason: requiresLegacy && !profile.legacyEnabled ? 'Not applicable: UGLC_ENABLE_LEGACY=OFF.' : '' };
    if (entry.kind === 'uglc-fixture' || entry.kind === 'uglc-shadow-fixture') {
      return {
        ...entry,
        absoluteSourcePath: buildFixturePath(sourceDir, entry.fixtureDir, entry.sourceFile),
        uglHeadersDir: profile.uglHeadersDir
      };
    }
    return {
      ...entry,
      absoluteSourcePath: buildHarnessPath(sourceDir, entry.sourceFile),
      extraSourcesAbsolutePaths: (entry.extraSources ?? []).map((relativePath) => path.join(sourceDir, relativePath))
    };
  });
}

export function getTestSelection(registry, group = 'all', caseId = '') {
  const allCases = [...registry];
  if (caseId) {
    return allCases.filter((entry) => entry.id === caseId);
  }

  if (group === 'all') {
    const groupOrder = new Map([
      ['smoke', 0],
      ['render-feature', 1],
      ['regressions', 2],
      ['diagnostics', 3],
      ['whitebox', 4],
      ['uglir', 5]
    ]);
    return allCases.sort((left, right) => {
      const leftOrder = groupOrder.get(left.group) ?? 99;
      const rightOrder = groupOrder.get(right.group) ?? 99;
      if (leftOrder !== rightOrder) {
        return leftOrder - rightOrder;
      }
      return left.id.localeCompare(right.id);
    });
  }

  const directMatches = allCases.filter((entry) => entry.group === group);
  if (directMatches.length > 0) {
    return directMatches;
  }
  return allCases.filter((entry) => entry.labels.includes(group));
}
