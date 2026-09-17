import path from 'node:path';

function normalizeBackendName(value) {
  const normalized = String(value ?? '').trim().toLowerCase();
  if (normalized === 'metal' || normalized === 'vulkan') {
    return normalized;
  }
  return '';
}

/** Creates one UGLIR runner target entry with the generated DSL source environment wired explicitly. */
function makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, target, sourceRelativePath, labels, timeoutMs = 120000, extraEnvironment = {}) {
  return {
    target,
    group: 'uglir',
    labels: [...labels, 'uglir', 'uglir'],
    timeoutMs,
    args: backendEnvironment.GVM_TEST_RHI_BACKEND ? ['--backend', backendEnvironment.GVM_TEST_RHI_BACKEND] : [],
    environment: {
      ...backendEnvironment,
      GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, target),
      GVM_TEST_DSL_SOURCE: path.join(sourceDir, sourceRelativePath),
      ...extraEnvironment
    }
  };
}

export function getTestRegistry(sourceDir, binaryDir, options = {}) {
  const generatedRoot = path.join(binaryDir, 'gvm_tests', 'generated');
  const requestedBackend = normalizeBackendName(options.backend);
  const includeLegacy = options.includeLegacy === true;
  const backendEnvironment = requestedBackend
    ? { GVM_TEST_RHI_BACKEND: requestedBackend }
    : {};
  const renderEnvironment = {
    ...backendEnvironment,
    GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_render_feature_tests'),
    GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'render_cases', 'RenderFeatureSuite.hpp')
  };

  const uglirTargets = [
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_legacy_buffer_layout_semantics_tests', 'tests/rhi_cases/BufferLayoutSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'legacy', 'layout']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_buffer_layout_semantics_tests', 'tests/rhi_cases/BufferLayoutSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'layout']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_unoptimized_buffer_layout_semantics_tests', 'tests/rhi_cases/BufferLayoutSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'layout', 'unoptimized']),
    { target: 'gvm_unit_uglc_spirv_validation', group: 'uglir', labels: ['unit', 'whitebox', 'uglir', 'uglir', 'spirv'], timeoutMs: 120000 },
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_legacy_evaluation_semantics_tests', 'tests/rhi_cases/EvaluationSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'legacy', 'evaluation']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_evaluation_semantics_tests', 'tests/rhi_cases/EvaluationSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'evaluation']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_unoptimized_evaluation_semantics_tests', 'tests/rhi_cases/EvaluationSemantics.hpp', ['rhi', 'metal', 'vulkan', 'compute', 'readback', 'evaluation', 'unoptimized']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_codegen_smoke_tests', 'GVMRuntime_Samples/Test_01_Triangle/Test_01_Triangle.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'smoke'], 90000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_storage_codegen_tests', 'GVMRuntime_Samples/Test_02_TriangleWithBuffer/Test_02_TriangleWithBuffer.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'storage'], 90000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_texture_codegen_tests', 'GVMRuntime_Samples/Test_03_TriangleWithTexture/Test_03_TriangleWithTexture.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'texture'], 90000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_cube_codegen_tests', 'GVMRuntime_Samples/Test_04_Cube/Test_04_Cube.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'cube'], 120000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_render_feature_codegen_tests', 'tests/render_cases/RenderFeatureSuite.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'render'], 120000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_atomic_codegen_tests', 'tests/uglc/fixtures/hlsl-groupshared-atomic-regression/GroupSharedAtomicRegression.hpp', ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'atomic'], 90000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_uniform_buffer_tests', 'tests/rhi_cases/ExperimentalUGLIRUniformBufferCompute.hpp', ['integration', 'blackbox', 'rhi', 'vulkan', 'compute', 'uniform-buffer']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_uniform_render_tests', 'tests/rhi_cases/ExperimentalUGLIRUniformBufferRender.hpp', ['integration', 'blackbox', 'rhi', 'vulkan', 'render', 'uniform-buffer']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_wvm_regression_tests', 'tests/rhi_cases/ExperimentalUGLIRWVMRegressionSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'render', 'texture', 'wvm-regression']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_headless_readback_tests', 'tests/rhi_cases/ExperimentalUGLIRHeadlessReadbackSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'readback', 'headless']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_native_half_tests', 'tests/rhi_cases/NativeHalfCompute.hpp', ['integration', 'blackbox', 'rhi', 'vulkan', 'compute', 'half']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_spd_hiz_readback_tests', 'tests/rhi_cases/SpdHiZReadback.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'readback', 'spd', 'hiz']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_texture3d_readwrite_tests', 'tests/rhi_cases/Texture3DReadWriteSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'texture3d', 'readback']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_texture2darray_readwrite_tests', 'tests/rhi_cases/Texture2DArrayReadWriteSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'texture2darray', 'readback']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_vulkan_texture_role_transition_tests', 'tests/rhi_cases/VulkanTextureRoleTransitionSuite.hpp', ['integration', 'blackbox', 'rhi', 'vulkan', 'compute', 'render', 'texture']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_spark_lod_traversal_tests', 'tests/rhi_cases/SparkLodTraversalSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'lod', 'cluster', 'spark', 'headless']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_cluster16_traversal_tests', 'tests/rhi_cases/Cluster16TraversalSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'cluster', 'lod', 'test16', 'headless']),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_rhi_uglir_gpu_radix_sort_tests', 'tests/sort_cases/OneSweepRadixSortSuite.hpp', ['integration', 'blackbox', 'rhi', 'metal', 'compute', 'radix', 'sort', 'headless'], 300000),
    makeUglirTarget(sourceDir, generatedRoot, backendEnvironment, 'gvm_dsl_uglir_render_feature_tests', 'tests/render_cases/RenderFeatureSuite.hpp', ['integration', 'blackbox', 'render', 'dsl', 'window', 'swapchain', 'feature'], 240000)
  ];

  const registry = [
    ...uglirTargets.filter(entry => includeLegacy || !entry.labels.includes('legacy')),
    {
      target: 'gvm_unit_rhi_values',
      group: 'unit',
      labels: ['unit', 'whitebox', 'rhi'],
      timeoutMs: 60000,
      environment: {}
    },
    {
      target: 'gvm_unit_core_values',
      group: 'unit',
      labels: ['unit', 'whitebox', 'core'],
      timeoutMs: 60000,
      environment: {}
    },
    {
      target: 'gvm_three_compat_tests',
      group: 'unit',
      labels: ['unit', 'three', 'compatibility'],
      timeoutMs: 60000,
      environment: {}
    },
    {
      target: 'gvm_rhi_smoke_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'smoke'],
      timeoutMs: 90000,
      environment: backendEnvironment
    },
    {
      target: 'gvm_rhi_readback_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'readback'],
      timeoutMs: 120000,
      environment: backendEnvironment
    },
    {
      target: 'gvm_rhi_native_half_tests',
      args: requestedBackend ? ['--backend', requestedBackend] : [],
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'vulkan', 'compute', 'half', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_native_half_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'NativeHalfCompute.hpp')
      }
    },
    {
      target: 'gvm_rhi_spd_hiz_readback_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'readback', 'spd', 'hiz', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_spd_hiz_readback_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'SpdHiZReadback.hpp')
      }
    },
    {
      target: 'gvm_rhi_texture3d_readwrite_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'texture3d', 'readback', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_texture3d_readwrite_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'Texture3DReadWriteSuite.hpp')
      }
    },
    {
      target: 'gvm_rhi_texture2darray_readwrite_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'texture', 'texture2darray', 'readback', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_texture2darray_readwrite_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'Texture2DArrayReadWriteSuite.hpp')
      }
    },
    {
      target: 'gvm_rhi_vulkan_texture_role_transition_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'vulkan', 'compute', 'render', 'texture', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_vulkan_texture_role_transition_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'VulkanTextureRoleTransitionSuite.hpp')
      }
    },
    {
      target: 'gvm_rhi_spark_lod_traversal_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'lod', 'cluster', 'spark', 'headless', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_spark_lod_traversal_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'SparkLodTraversalSuite.hpp'),
        GVM_TEST_SPARK_LOD_CLUSTER_MEDIUM_SOURCE_SPLATS: '1000000',
        GVM_TEST_SPARK_LOD_CLUSTER_MEDIUM_THRESHOLD_PX: '1',
        GVM_TEST_SPARK_LOD_CLUSTER_MEDIUM_MEMORY_BUDGET_MB: '512',
        GVM_TEST_SPARK_LOD_CLUSTER_BICYCLE_SOURCE_SPLATS: '6131954',
        GVM_TEST_SPARK_LOD_CLUSTER_BICYCLE_THRESHOLD_PX: '1',
        GVM_TEST_SPARK_LOD_CLUSTER_BICYCLE_MEMORY_BUDGET_MB: '1024'
      }
    },
    {
      target: 'gvm_rhi_cluster16_traversal_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'vulkan', 'compute', 'cluster', 'lod', 'test16', 'headless', 'dsl'],
      timeoutMs: 120000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_rhi_cluster16_traversal_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'rhi_cases', 'Cluster16TraversalSuite.hpp'),
        GVM_TEST16_CLUSTER_MEDIUM_SOURCE_SPLATS: '1000000',
        GVM_TEST16_CLUSTER_MEDIUM_THRESHOLD_PX: '1',
        GVM_TEST16_CLUSTER_MEDIUM_MEMORY_BUDGET_MB: '512',
        GVM_TEST16_CLUSTER_BICYCLE_SOURCE_SPLATS: '6131954',
        GVM_TEST16_CLUSTER_BICYCLE_THRESHOLD_PX: '1',
        GVM_TEST16_CLUSTER_BICYCLE_MEMORY_BUDGET_MB: '1024'
      }
    },
    {
      target: 'gvm_rhi_gpu_radix_sort_tests',
      group: 'rhi',
      labels: ['integration', 'blackbox', 'rhi', 'metal', 'compute', 'radix', 'sort', 'onesweep', 'headless'],
      timeoutMs: 300000,
      environment: {
        ...backendEnvironment,
        GVM_TEST_GPU_RADIX_SORT_COUNT: '262144',
        GVM_TEST_GPU_RADIX_SORT_BENCHMARK_COUNT: '1048576',
        GVM_TEST_GPU_RADIX_SORT_BENCHMARK_ITERATIONS: '3'
      }
    },
    {
      target: 'gvm_dsl_codegen_smoke_tests',
      group: 'dsl',
      labels: ['integration', 'blackbox', 'dsl', 'codegen', 'smoke'],
      timeoutMs: 90000,
      environment: {
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_codegen_smoke_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'GVMRuntime_Samples', 'Test_01_Triangle', 'Test_01_Triangle.hpp')
      }
    },
    {
      target: 'gvm_dsl_storage_codegen_tests',
      group: 'dsl',
      labels: ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'storage'],
      timeoutMs: 90000,
      environment: {
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_storage_codegen_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'GVMRuntime_Samples', 'Test_02_TriangleWithBuffer', 'Test_02_TriangleWithBuffer.hpp')
      }
    },
    {
      target: 'gvm_dsl_texture_codegen_tests',
      group: 'dsl',
      labels: ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'texture'],
      timeoutMs: 90000,
      environment: {
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_texture_codegen_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'GVMRuntime_Samples', 'Test_03_TriangleWithTexture', 'Test_03_TriangleWithTexture.hpp')
      }
    },
    {
      target: 'gvm_dsl_cube_codegen_tests',
      group: 'dsl',
      labels: ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'cube'],
      timeoutMs: 120000,
      environment: {
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_cube_codegen_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'GVMRuntime_Samples', 'Test_04_Cube', 'Test_04_Cube.hpp')
      }
    },
    {
      target: 'gvm_dsl_render_feature_codegen_tests',
      group: 'dsl',
      labels: ['integration', 'blackbox', 'dsl', 'codegen', 'contract', 'render'],
      timeoutMs: 120000,
      environment: {
        GVM_TEST_DSL_GENERATED_DIR: path.join(generatedRoot, 'gvm_dsl_render_feature_codegen_tests'),
        GVM_TEST_DSL_SOURCE: path.join(sourceDir, 'tests', 'render_cases', 'RenderFeatureSuite.hpp')
      }
    },
    {
      target: 'gvm_dsl_render_feature_tests',
      group: 'render',
      labels: ['integration', 'blackbox', 'render', 'dsl', 'window', 'swapchain', 'feature'],
      timeoutMs: 240000,
      interactiveWindow: true,
      environment: renderEnvironment
    }
  ];
  for (const entry of registry) {
    if (entry.target === 'gvm_dsl_render_feature_tests' || entry.target === 'gvm_dsl_uglir_render_feature_tests') {
      entry.args = [...(entry.args ?? []), '--render-simple-frames', '180', '--render-complex-frames', '240',
        '--render-frame-delay-ms', '16', '--render-warmup-ms', '450', '--render-case-pause-ms', '350'];
    }
    if (requestedBackend && entry.environment?.GVM_TEST_RHI_BACKEND) {
      entry.args = [...(entry.args ?? []), '--gvm-backend', requestedBackend];
    }
  }
  return registry;
}

export function getTestSelection(registry, group = 'all') {
  if (group === 'all') {
    const groupOrder = new Map([
      ['render', 0],
      ['unit', 1],
      ['rhi', 2],
      ['dsl', 3],
      ['uglir', 4]
    ]);
    return [...registry].sort((a, b) => {
      const aOrder = groupOrder.get(a.group) ?? 99;
      const bOrder = groupOrder.get(b.group) ?? 99;
      if (aOrder !== bOrder) {
        return aOrder - bOrder;
      }
      return a.target.localeCompare(b.target);
    });
  }
  const directMatches = registry.filter((entry) => entry.group === group);
  if (directMatches.length > 0) {
    return directMatches;
  }
  return registry.filter((entry) => entry.labels.includes(group));
}
