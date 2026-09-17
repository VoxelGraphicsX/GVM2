cmake_minimum_required(VERSION 3.22.0)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GVMTestFunctions.cmake)

if(GVM_UGLC_COMPILER_AVAILABLE)
    include(${GVM_REPOSITORY_ROOT}/cmake/UGLC/UGLCompile.cmake)
endif()

file(MAKE_DIRECTORY "${GVM_TEST_OUTPUT_ROOT}")
file(MAKE_DIRECTORY "${GVM_TEST_RUNS_ROOT}")
file(MAKE_DIRECTORY "${GVM_TEST_BINARY_ROOT}")
file(MAKE_DIRECTORY "${GVM_TEST_GENERATED_ROOT}")

add_library(GVMTestCommon INTERFACE)
target_link_libraries(GVMTestCommon INTERFACE GVM)
target_include_directories(GVMTestCommon INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${GVM_REPOSITORY_ROOT}/GVM
    ${GVM_REPOSITORY_ROOT}/GVM/third_party
)

if(GVM_BUILD_TESTS_UNIT)
    gvm_add_gtest(gvm_unit_rhi_values
        SOURCES
            unit/test_multiple_elements.cpp
            unit/test_buffer_range.cpp
            unit/test_pixel_local_pass_access.cpp
            unit/test_metal_indirect_command_shader.cpp
            unit/test_shader_half.cpp
        LABELS unit whitebox rhi
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/Private
    )

    gvm_add_gtest(gvm_unit_core_values
        SOURCES
            unit/test_resource_pool.cpp
            unit/test_progressive_data.cpp
            unit/test_render_components.cpp
            unit/test_queue_timestamp.cpp
            unit/test_staging_linear_allocator.cpp
            unit/test_helper_function.cpp
            unit/test_eastl_allocator.cpp
        LABELS unit whitebox core
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
    )

    if(GVM_RHI_ENABLE_VULKAN)
        target_sources(gvm_unit_rhi_values PRIVATE unit/test_vulkan_shader_requirements.cpp)
        target_include_directories(gvm_unit_rhi_values PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Vulkan)
        target_link_libraries(gvm_unit_rhi_values PRIVATE Vulkan::Headers volk::volk GPUOpen::VulkanMemoryAllocator)
    endif()
    if(GVM_BUILD_UGLC)
        gvm_add_gtest(gvm_unit_uglc_spirv_validation
            SOURCES unit/test_uglc_spirv_validation.cpp
                ${GVM_UGLC_SOURCE_DIR}/CodeGen/SPIRVEmitter/SPIRVValidation.cpp
            LABELS unit whitebox uglir uglir spirv
            INCLUDE_DIRS ${GVM_UGLC_SOURCE_DIR}
            LINK_LIBS ${UGLC_SPIRV_TOOLS_TARGET} ${UGLC_SPIRV_TOOLS_OPT_TARGET}
        )
    endif()

    gvm_add_gtest(gvm_three_compat_tests
        SOURCES
            unit/test_three_compat.cpp
            unit/test_three_scene_render_plan.cpp
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat/Math.cpp
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat/Object3D.cpp
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat/Camera.cpp
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat/DeterministicRandom.cpp
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat/SceneRenderPlan.cpp
        LABELS unit three compatibility
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_ThreeSamples/ThreeCompat
        LINK_LIBS
            glm::glm
    )
endif()

if(APPLE AND GVM_BUILD_TESTS_RHI)
    gvm_add_gtest(gvm_rhi_smoke_tests
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_smoke.cpp
            integration/rhi/test_rhi_pipeline.cpp
        LABELS integration blackbox rhi metal smoke
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Metal
        NO_GTEST_MAIN
        RUN_SERIAL
    )

    gvm_add_gtest(gvm_rhi_readback_tests
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_readback.cpp
        LABELS integration blackbox rhi metal readback
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Metal
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )

    gvm_add_dsl_runtime_gtest(gvm_rhi_native_half_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/NativeHalfCompute.hpp
        SOURCES
            common/GVMShaderReadbackMain.cpp
            integration/rhi/test_rhi_native_half.cpp
        LABELS integration blackbox rhi vulkan compute half dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Vulkan
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_native_half_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        set(_evaluation_pipelines uglir uglir_unoptimized)
        if(UGLC_ENABLE_LEGACY)
            list(PREPEND _evaluation_pipelines legacy)
        endif()
        foreach(_evaluation_pipeline IN LISTS _evaluation_pipelines)
            set(_evaluation_uglc_args)
            if(_evaluation_pipeline STREQUAL "legacy")
                list(APPEND _evaluation_uglc_args --shader-pipeline=legacy)
            else()
                list(APPEND _evaluation_uglc_args --shader-pipeline=uglir)
            endif()
            if(_evaluation_pipeline STREQUAL "uglir_unoptimized")
                list(APPEND _evaluation_uglc_args --no-spirv-optimization)
            endif()
            gvm_add_dsl_runtime_gtest(gvm_rhi_${_evaluation_pipeline}_evaluation_semantics_tests
                ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/EvaluationSemantics.hpp
                SOURCES common/GVMShaderReadbackMain.cpp integration/rhi/test_rhi_evaluation_semantics.cpp
                LABELS integration blackbox rhi metal vulkan compute readback uglir evaluation
                INCLUDE_DIRS ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                UGL_INCLUDE_DIRS ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders ${GVM_REPOSITORY_ROOT}/GVM/third_party
                UGLC_ARGS ${_evaluation_uglc_args}
                NO_GTEST_MAIN RUN_SERIAL TIMEOUT 120
            )
            target_compile_definitions(gvm_rhi_${_evaluation_pipeline}_evaluation_semantics_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)
            gvm_add_dsl_runtime_gtest(gvm_rhi_${_evaluation_pipeline}_buffer_layout_semantics_tests
                ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/BufferLayoutSemantics.hpp
                SOURCES common/GVMShaderReadbackMain.cpp integration/rhi/test_rhi_buffer_layout_semantics.cpp
                LABELS integration blackbox rhi metal vulkan compute readback uglir layout
                INCLUDE_DIRS ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                UGL_INCLUDE_DIRS ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders ${GVM_REPOSITORY_ROOT}/GVM/third_party
                UGLC_ARGS ${_evaluation_uglc_args}
                NO_GTEST_MAIN RUN_SERIAL TIMEOUT 120
            )
            target_compile_definitions(gvm_rhi_${_evaluation_pipeline}_buffer_layout_semantics_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)
        endforeach()

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_uniform_buffer_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/ExperimentalUGLIRUniformBufferCompute.hpp
            SOURCES
                common/GVMShaderReadbackMain.cpp
                integration/rhi/test_rhi_uglir_uniform_buffer.cpp
            LABELS integration blackbox rhi vulkan compute uniform-buffer uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Vulkan
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_uniform_render_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/ExperimentalUGLIRUniformBufferRender.hpp
            SOURCES
                common/GVMShaderReadbackMain.cpp
                integration/rhi/test_rhi_uglir_uniform_render.cpp
            LABELS integration blackbox rhi vulkan render uniform-buffer uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Vulkan
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_wvm_regression_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/ExperimentalUGLIRWVMRegressionSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_uglir_wvm_regressions.cpp
            LABELS integration blackbox rhi metal vulkan compute render texture uglir uglir wvm-regression
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_wvm_regression_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_headless_readback_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/ExperimentalUGLIRHeadlessReadbackSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_uglir_headless_readback.cpp
            LABELS integration blackbox rhi metal vulkan compute readback uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_headless_readback_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_native_half_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/NativeHalfCompute.hpp
            SOURCES
                common/GVMShaderReadbackMain.cpp
                integration/rhi/test_rhi_native_half.cpp
            LABELS integration blackbox rhi vulkan compute half uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI/GVMRHI.Vulkan
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_native_half_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_spd_hiz_readback_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/SpdHiZReadback.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_spd_hiz_readback.cpp
            LABELS integration blackbox rhi metal vulkan compute texture readback spd hiz dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_spd_hiz_readback_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_texture3d_readwrite_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Texture3DReadWriteSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_texture3d_readwrite.cpp
            LABELS integration blackbox rhi metal vulkan compute texture texture3d readback dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_texture3d_readwrite_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_texture2darray_readwrite_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Texture2DArrayReadWriteSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_texture2darray_readwrite.cpp
            LABELS integration blackbox rhi metal vulkan compute texture texture2darray readback dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_texture2darray_readwrite_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_vulkan_texture_role_transition_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/VulkanTextureRoleTransitionSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_vulkan_texture_role_transitions.cpp
            LABELS integration blackbox rhi vulkan compute render texture dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_vulkan_texture_role_transition_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_spark_lod_traversal_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/SparkLodTraversalSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_spark_lod_traversal.cpp
            LABELS integration blackbox rhi metal vulkan compute lod cluster spark headless dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_spark_lod_traversal_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_cluster16_traversal_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Cluster16TraversalSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_cluster16_traversal.cpp
            LABELS integration blackbox rhi metal vulkan compute cluster lod test16 headless dsl uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                ${CMAKE_BINARY_DIR}/_deps/glm-src
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
            LINK_LIBS
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 120
        )
        target_compile_definitions(gvm_rhi_uglir_cluster16_traversal_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

        gvm_add_dsl_runtime_gtest(gvm_rhi_uglir_gpu_radix_sort_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/sort_cases/OneSweepRadixSortSuite.hpp
            SOURCES
                common/GVMHeadlessGTestMain.cpp
                integration/rhi/test_rhi_gpu_radix_sort.cpp
            LABELS integration blackbox rhi metal compute radix sort headless uglir experimental
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${CMAKE_BINARY_DIR}/_deps/glm-src
            LINK_LIBS
                glm::glm
                "-framework Foundation"
                "-framework Metal"
                "-framework QuartzCore"
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGLC_ARGS
                --shader-pipeline=uglir
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 300
        )
        target_compile_definitions(gvm_rhi_uglir_gpu_radix_sort_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_spd_hiz_readback_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/SpdHiZReadback.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_spd_hiz_readback.cpp
        LABELS integration blackbox rhi metal vulkan compute texture readback spd hiz dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_spd_hiz_readback_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_texture3d_readwrite_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Texture3DReadWriteSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_texture3d_readwrite.cpp
        LABELS integration blackbox rhi metal vulkan compute texture texture3d readback dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_texture3d_readwrite_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_texture2darray_readwrite_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Texture2DArrayReadWriteSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_texture2darray_readwrite.cpp
        LABELS integration blackbox rhi metal vulkan compute texture texture2darray readback dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_texture2darray_readwrite_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_vulkan_texture_role_transition_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/VulkanTextureRoleTransitionSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_vulkan_texture_role_transitions.cpp
        LABELS integration blackbox rhi vulkan compute render texture dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_vulkan_texture_role_transition_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_spark_lod_traversal_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/SparkLodTraversalSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_spark_lod_traversal.cpp
        LABELS integration blackbox rhi metal vulkan compute lod cluster spark headless dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_spark_lod_traversal_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_cluster16_traversal_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/rhi_cases/Cluster16TraversalSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_cluster16_traversal.cpp
        LABELS integration blackbox rhi metal vulkan compute cluster lod test16 headless dsl
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            ${CMAKE_BINARY_DIR}/_deps/glm-src
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
        LINK_LIBS
            glm::glm
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 120
    )
    target_compile_definitions(gvm_rhi_cluster16_traversal_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    gvm_add_dsl_runtime_gtest(gvm_rhi_gpu_radix_sort_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/sort_cases/OneSweepRadixSortSuite.hpp
        SOURCES
            common/GVMHeadlessGTestMain.cpp
            integration/rhi/test_rhi_gpu_radix_sort.cpp
        LABELS integration blackbox rhi metal compute radix sort headless
        INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/GVMRHI
            ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
            ${CMAKE_BINARY_DIR}/_deps/glm-src
        LINK_LIBS
            glm::glm
            "-framework Foundation"
            "-framework Metal"
            "-framework QuartzCore"
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        NO_GTEST_MAIN
        RUN_SERIAL
        TIMEOUT 300
    )
    target_compile_definitions(gvm_rhi_gpu_radix_sort_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)
elseif(GVM_BUILD_TESTS_RHI)
    message(WARNING "GVM_BUILD_TESTS_RHI is enabled, but runtime RHI tests are only configured on Apple in this source tree.")
endif()

if(GVM_BUILD_TESTS_DSL AND GVM_UGLC_COMPILER_AVAILABLE)
    gvm_add_dsl_codegen_test(gvm_dsl_codegen_smoke_tests
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle/Test_01_Triangle.hpp
        SOURCES
            integration/dsl/test_dsl_codegen.cpp
        LABELS integration blackbox dsl codegen smoke
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle
        RUN_SERIAL
    )

    gvm_add_dsl_codegen_test(gvm_dsl_storage_codegen_tests
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer/Test_02_TriangleWithBuffer.hpp
        SOURCES
            integration/dsl/test_dsl_storage_codegen.cpp
        LABELS integration blackbox dsl codegen contract storage
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer
        RUN_SERIAL
    )

    gvm_add_dsl_codegen_test(gvm_dsl_texture_codegen_tests
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture/Test_03_TriangleWithTexture.hpp
        SOURCES
            integration/dsl/test_dsl_texture_codegen.cpp
        LABELS integration blackbox dsl codegen contract texture
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture
        RUN_SERIAL
    )

    gvm_add_dsl_codegen_test(gvm_dsl_cube_codegen_tests
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube/Test_04_Cube.hpp
        SOURCES
            integration/dsl/test_dsl_cube_codegen.cpp
        LABELS integration blackbox dsl codegen contract cube
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
        RUN_SERIAL
    )

    gvm_add_dsl_codegen_test(gvm_dsl_render_feature_codegen_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/render_cases/RenderFeatureSuite.hpp
        SOURCES
            integration/dsl/test_dsl_render_feature_codegen.cpp
        LABELS integration blackbox dsl codegen contract render
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/render_cases
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
        RUN_SERIAL
    )

    gvm_add_dsl_codegen_test(gvm_dsl_atomic_codegen_tests
        ${CMAKE_CURRENT_SOURCE_DIR}/uglc/fixtures/hlsl-groupshared-atomic-regression/GroupSharedAtomicRegression.hpp
        SOURCES
            integration/dsl/test_dsl_atomic_codegen.cpp
        LABELS integration blackbox dsl codegen contract atomic
        UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
        UGL_DSL_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/uglc/fixtures/hlsl-groupshared-atomic-regression
        RUN_SERIAL
    )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_codegen_smoke_tests
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle/Test_01_Triangle.hpp
            SOURCES
                integration/dsl/test_dsl_codegen.cpp
            LABELS integration blackbox dsl codegen smoke uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_storage_codegen_tests
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer/Test_02_TriangleWithBuffer.hpp
            SOURCES
                integration/dsl/test_dsl_storage_codegen.cpp
            LABELS integration blackbox dsl codegen contract storage uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_texture_codegen_tests
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture/Test_03_TriangleWithTexture.hpp
            SOURCES
                integration/dsl/test_dsl_texture_codegen.cpp
            LABELS integration blackbox dsl codegen contract texture uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_cube_codegen_tests
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube/Test_04_Cube.hpp
            SOURCES
                integration/dsl/test_dsl_cube_codegen.cpp
            LABELS integration blackbox dsl codegen contract cube uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_render_feature_codegen_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/render_cases/RenderFeatureSuite.hpp
            SOURCES
                integration/dsl/test_dsl_render_feature_codegen.cpp
            LABELS integration blackbox dsl codegen contract render uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${CMAKE_CURRENT_SOURCE_DIR}/render_cases
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

        gvm_add_dsl_codegen_test(gvm_dsl_uglir_atomic_codegen_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/uglc/fixtures/hlsl-groupshared-atomic-regression/GroupSharedAtomicRegression.hpp
            SOURCES
                integration/dsl/test_dsl_atomic_codegen.cpp
            LABELS integration blackbox dsl codegen contract atomic uglir experimental
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${CMAKE_CURRENT_SOURCE_DIR}/uglc/fixtures/hlsl-groupshared-atomic-regression
            UGLC_ARGS
                --shader-pipeline=uglir
            RUN_SERIAL
        )

    if(APPLE AND GVM_BUILD_TESTS_RENDER)
        set(_render_baseline_uglc_args --shader-pipeline=uglir)
        if(UGLC_ENABLE_LEGACY)
            set(_render_baseline_uglc_args --shader-pipeline=legacy)
        endif()
        gvm_add_dsl_runtime_gtest(gvm_dsl_render_feature_tests
            ${CMAKE_CURRENT_SOURCE_DIR}/render_cases/RenderFeatureSuite.hpp
            SOURCES
                common/GVMSDLGTestMain.cpp
                integration/render/test_render_feature_suite.cpp
            LABELS integration blackbox render dsl window swapchain feature
            INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
            LINK_LIBS
                SDL2::SDL2main
                SDL2::SDL2-static
                glm::glm
            UGL_INCLUDE_DIRS
                ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                ${GVM_REPOSITORY_ROOT}/GVM/third_party
            UGL_DSL_DIRS
                ${CMAKE_CURRENT_SOURCE_DIR}/render_cases
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            UGLC_ARGS ${_render_baseline_uglc_args}
            NO_GTEST_MAIN
            RUN_SERIAL
            TIMEOUT 240
        )
        target_compile_definitions(gvm_dsl_render_feature_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

            gvm_add_dsl_runtime_gtest(gvm_dsl_uglir_render_feature_tests
                ${CMAKE_CURRENT_SOURCE_DIR}/render_cases/RenderFeatureSuite.hpp
                SOURCES
                    common/GVMSDLGTestMain.cpp
                    integration/render/test_render_feature_suite.cpp
                LABELS integration blackbox render dsl window swapchain feature uglir experimental
                INCLUDE_DIRS
                    ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Private
                    ${GVM_REPOSITORY_ROOT}/GVM/GVMCore/Public
                LINK_LIBS
                    SDL2::SDL2main
                    SDL2::SDL2-static
                    glm::glm
                UGL_INCLUDE_DIRS
                    ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
                    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                    ${GVM_REPOSITORY_ROOT}/GVM/third_party
                UGL_DSL_DIRS
                    ${CMAKE_CURRENT_SOURCE_DIR}/render_cases
                    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
                    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
                UGLC_ARGS
                    --shader-pipeline=uglir
                NO_GTEST_MAIN
                RUN_SERIAL
                TIMEOUT 240
            )
            target_compile_definitions(gvm_dsl_uglir_render_feature_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)
    elseif(GVM_BUILD_TESTS_RENDER)
        message(WARNING "GVM_BUILD_TESTS_RENDER is enabled, but render runtime tests are only configured on Apple in this source tree.")
    endif()
elseif(GVM_BUILD_TESTS_DSL)
    message(WARNING "GVM_BUILD_TESTS_DSL is ON but GVM_UGLC_COMPILER is unavailable. DSL tests will be skipped.")
endif()
