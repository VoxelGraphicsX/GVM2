cmake_minimum_required(VERSION 3.22.0)
project(GVM VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)
find_package(Python3 COMPONENTS Interpreter QUIET)

get_filename_component(GVM_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

# 【关键修改】检测如果是苹果平台，开启 ObjC 支持
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

# =========================================================
# 1. 定义智能开关 (Smart Option)
# =========================================================
# 判断当前项目是否是“根项目”（即用户是直接打开的这个文件夹，而不是通过 add_subdirectory 引入的）
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(GVM_IS_TOP_LEVEL ON)
else()
    set(GVM_IS_TOP_LEVEL OFF)
endif()

# 定义选项：GVM_BUILD_SAMPLES
# 如果是顶层项目，默认值(第三个参数)为 ON，否则为 OFF
option(GVM_BUILD_SAMPLES "Build GVM samples and tests dependencies" ${GVM_IS_TOP_LEVEL})
option(GVM_BUILD_TESTS "Build GVM automated tests" ${GVM_IS_TOP_LEVEL})
option(GVM_BUILD_TESTS_UNIT "Build GVM white-box unit tests" ON)
option(GVM_BUILD_TESTS_RHI "Build GVM RHI integration tests" ON)
option(GVM_BUILD_TESTS_DSL "Build GVM DSL/codegen integration tests" ON)
option(GVM_BUILD_TESTS_RENDER "Build GVM windowed DSL render feature tests" ON)
option(GVM_BUILD_UGLC "Build the in-tree UGLC compiler used by GVM DSL codegen" ON)
option(GVM_INSTALL "Install the GVM runtime CMake package" ${GVM_IS_TOP_LEVEL})
option(GVM_BUILD_ADVANCED_SAMPLES "Build closed-source advanced GVM runtime samples." OFF)
if(GVM_BUILD_SAMPLES AND GVM_BUILD_ADVANCED_SAMPLES AND
   NOT IS_DIRECTORY "${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples")
    message(FATAL_ERROR "GVM_BUILD_ADVANCED_SAMPLES requires the private GVMRuntime_AdvancedSamples directory. Use -DGVM_BUILD_ADVANCED_SAMPLES=OFF for the public source distribution.")
endif()
option(GVM_BUILD_THREE_SAMPLES "Build the Three.js r185 DSL compatibility sample matrix." OFF)
option(GVM_RHI_ENABLE_VULKAN "Build the Vulkan backend for GVMRHI" ON)
option(GVM_RHI_ENABLE_LOGGING "Build instance-level RHI logging capability" OFF)
option(GVM_RHI_ENABLE_CPU_PROBES "Build CPU performance probe macros on top of the logging system" OFF)
option(GVM_RHI_USE_SPDLOG_LOGGER "Build and auto-register the built-in spdlog logger provider" OFF)
option(GVM_USE_REPOSITORY_DEPENDENCY_CACHE "Use the repository-local .cache directory for downloaded dependency sources and toolchains." ON)
if(NOT DEFINED GVM_DEPENDENCY_CACHE_ROOT)
    set(GVM_DEPENDENCY_CACHE_ROOT "${GVM_REPOSITORY_ROOT}/.cache" CACHE PATH "Repository-local cache root for downloaded dependency sources and toolchains.")
endif()

if(GVM_USE_REPOSITORY_DEPENDENCY_CACHE)
    if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
        set(CPM_SOURCE_CACHE "${GVM_DEPENDENCY_CACHE_ROOT}/cpm" CACHE PATH "Directory used by CPM to cache downloaded dependency sources.")
    endif()
    if(NOT DEFINED UGLC_LLVM_CACHE_DIR)
        set(UGLC_LLVM_CACHE_DIR "${GVM_DEPENDENCY_CACHE_ROOT}/uglc/llvm" CACHE PATH "Cache directory used for downloaded UGLC LLVM bundles.")
    endif()
    set(_gvm_dependency_build_key_input "${CMAKE_BINARY_DIR}|${CMAKE_GENERATOR}|${CMAKE_SYSTEM_NAME}|${CMAKE_OSX_ARCHITECTURES}|${CMAKE_BUILD_TYPE}")
    string(SHA1 _gvm_dependency_build_key_hash "${_gvm_dependency_build_key_input}")
    string(SUBSTRING "${_gvm_dependency_build_key_hash}" 0 12 _gvm_dependency_build_key)
    set(_gvm_default_fetchcontent_base_dir "${GVM_DEPENDENCY_CACHE_ROOT}/cpm-build/${_gvm_dependency_build_key}")
    set(_gvm_legacy_fetchcontent_base_dir "${GVM_DEPENDENCY_CACHE_ROOT}/cpm-build")
    if(NOT DEFINED ENV{FETCHCONTENT_BASE_DIR})
        if(NOT DEFINED FETCHCONTENT_BASE_DIR)
            set(FETCHCONTENT_BASE_DIR "${_gvm_default_fetchcontent_base_dir}" CACHE PATH "Per-build-tree directory for CPM/FetchContent dependency build artifacts.")
        elseif(FETCHCONTENT_BASE_DIR STREQUAL "${_gvm_legacy_fetchcontent_base_dir}")
            set(FETCHCONTENT_BASE_DIR "${_gvm_default_fetchcontent_base_dir}" CACHE PATH "Per-build-tree directory for CPM/FetchContent dependency build artifacts." FORCE)
        endif()
    endif()
    unset(_gvm_legacy_fetchcontent_base_dir)
    unset(_gvm_default_fetchcontent_base_dir)
    unset(_gvm_dependency_build_key)
    unset(_gvm_dependency_build_key_hash)
    unset(_gvm_dependency_build_key_input)
endif()
if(ANDROID OR OHOS)
    set(GVM_BUILD_SAMPLE_DESKTOP_BINARIES_DEFAULT OFF)
else()
    set(GVM_BUILD_SAMPLE_DESKTOP_BINARIES_DEFAULT ON)
endif()
option(GVM_BUILD_SAMPLE_DESKTOP_BINARIES "Build desktop executable sample binaries that own their own window." ${GVM_BUILD_SAMPLE_DESKTOP_BINARIES_DEFAULT})
option(GVM_ANDROID_BUILD_TEST01_NATIVE "Build the Android NativeActivity shared library for Test01 Triangle." OFF)
option(GVM_OHOS_BUILD_TEST01_XCOMPONENT "Build the OHOS XComponent shared library for Test01 Triangle." OFF)

if(NOT GVM_RHI_ENABLE_LOGGING AND GVM_RHI_USE_SPDLOG_LOGGER)
    message(WARNING "GVM_RHI_USE_SPDLOG_LOGGER is ignored because GVM_RHI_ENABLE_LOGGING=OFF.")
    set(GVM_RHI_USE_SPDLOG_LOGGER OFF CACHE BOOL "Build and auto-register the built-in spdlog logger provider" FORCE)
endif()

if(NOT GVM_RHI_ENABLE_LOGGING AND GVM_RHI_ENABLE_CPU_PROBES)
    message(WARNING "GVM_RHI_ENABLE_CPU_PROBES is ignored because GVM_RHI_ENABLE_LOGGING=OFF.")
    set(GVM_RHI_ENABLE_CPU_PROBES OFF CACHE BOOL "Build CPU performance probe macros on top of the logging system" FORCE)
endif()

if(NOT DEFINED GVM_UGLC_EXECUTABLE)
    set(GVM_UGLC_EXECUTABLE "" CACHE FILEPATH "Optional external UGLC executable override used only when GVM_BUILD_UGLC is OFF.")
endif()

set(GVM_TEST_OUTPUT_ROOT "${CMAKE_BINARY_DIR}/gvm_tests" CACHE PATH "Root directory for all generated GVM test outputs")
if(NOT DEFINED GVM_TEST_RUNS_ROOT)
    set(GVM_TEST_RUNS_ROOT "${GVM_TEST_OUTPUT_ROOT}/runs" CACHE PATH "Root directory for per-run timestamped test data")
endif()

if(NOT DEFINED GVM_TEST_BINARY_ROOT)
    set(GVM_TEST_BINARY_ROOT "${GVM_TEST_OUTPUT_ROOT}/bin" CACHE PATH "Directory for built GVM test executables")
endif()
if(NOT DEFINED GVM_TEST_GENERATED_ROOT)
    set(GVM_TEST_GENERATED_ROOT "${GVM_TEST_OUTPUT_ROOT}/generated" CACHE PATH "Root directory for generated DSL code used by tests")
endif()
set(GVM_SAMPLE_GENERATED_ROOT "${CMAKE_BINARY_DIR}/gvm_samples/generated" CACHE PATH "Root directory for generated DSL code used by samples")
set(GVM_ADVANCED_SAMPLE_GENERATED_ROOT "${CMAKE_BINARY_DIR}/gvm_advanced_samples/generated" CACHE PATH "Root directory for generated DSL code used by advanced samples")
set(GVM_THREE_SAMPLE_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated/three-r185" CACHE PATH "Root directory for dual-pipeline Three.js sample DSL output")
set(GVM_THREE_SAMPLE_BINARY_ROOT "${CMAKE_BINARY_DIR}/gvm_three_samples/bin" CACHE PATH "Directory for Three.js compatibility sample hosts")
set(GVM_THREE_ORACLE_ROOT "" CACHE PATH "Explicit immutable Three.js r185 Oracle pack used by optional sample fixtures")
set(GVM_THREE_ASSET_ROOT "" CACHE PATH "Explicit immutable Three.js r185 asset pack used by optional sample fixtures")

set(GVM_UGL_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/GVM/UGLHeaders")

message(STATUS "GVM Build Samples: ${GVM_BUILD_SAMPLES}")
message(STATUS "GVM Build Tests: ${GVM_BUILD_TESTS}")
message(STATUS "GVM Build UGLC: ${GVM_BUILD_UGLC}")
message(STATUS "GVM Install Runtime Package: ${GVM_INSTALL}")
message(STATUS "GVM Vulkan Backend: ${GVM_RHI_ENABLE_VULKAN}")
message(STATUS "GVM RHI Logging: ${GVM_RHI_ENABLE_LOGGING}")
message(STATUS "GVM RHI CPU Probes: ${GVM_RHI_ENABLE_CPU_PROBES}")
message(STATUS "GVM RHI Built-in spdlog Provider: ${GVM_RHI_USE_SPDLOG_LOGGER}")
message(STATUS "GVM Build Desktop Sample Binaries: ${GVM_BUILD_SAMPLE_DESKTOP_BINARIES}")
message(STATUS "GVM Build Advanced Samples: ${GVM_BUILD_ADVANCED_SAMPLES}")
message(STATUS "GVM Build Three.js r185 Samples: ${GVM_BUILD_THREE_SAMPLES}")
message(STATUS "GVM Android Test01 Native: ${GVM_ANDROID_BUILD_TEST01_NATIVE}")
message(STATUS "GVM OHOS Test01 XComponent: ${GVM_OHOS_BUILD_TEST01_XCOMPONENT}")
message(STATUS "GVM Repository Dependency Cache: ${GVM_USE_REPOSITORY_DEPENDENCY_CACHE}")
message(STATUS "GVM Dependency Cache Root: ${GVM_DEPENDENCY_CACHE_ROOT}")
if(DEFINED CPM_SOURCE_CACHE)
    message(STATUS "GVM CPM Source Cache: ${CPM_SOURCE_CACHE}")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    message(STATUS "GVM CPM Source Cache: $ENV{CPM_SOURCE_CACHE} (environment)")
else()
    message(STATUS "GVM CPM Source Cache: OFF")
endif()
if(DEFINED UGLC_LLVM_CACHE_DIR)
    message(STATUS "GVM UGLC LLVM Cache: ${UGLC_LLVM_CACHE_DIR}")
endif()
if(GVM_BUILD_UGLC AND GVM_UGLC_EXECUTABLE)
    message(STATUS "GVM External UGLC Executable Override: ${GVM_UGLC_EXECUTABLE} (ignored because GVM_BUILD_UGLC=ON)")
else()
    message(STATUS "GVM External UGLC Executable Override: ${GVM_UGLC_EXECUTABLE}")
endif()

# =========================================================
# 2. 引入依赖 (传递开关状态)
# =========================================================
# 将 cmake 目录加入路径 (可选，用于 include 其他工具脚本)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 执行依赖配置
include(cmake/Dependencies.cmake)
include(cmake/GVMMobileHostTemplates.cmake)
include(cmake/UGLC/UGLCTargets.cmake)

set(UGLC_COMMON_INCLUDE_DIRS
    "${GVM_UGL_HEADERS_DIR}"
    "${EASTL_SOURCE_DIR}/include"
    "${EABase_SOURCE_DIR}/include/Common"
)

set(GVM_UGLC_COMPILER_AVAILABLE OFF)
if(GVM_UGLC_COMPILER)
  if(TARGET "${GVM_UGLC_COMPILER}" OR EXISTS "${GVM_UGLC_COMPILER}")
    set(GVM_UGLC_COMPILER_AVAILABLE ON)
  endif()
endif()
message(STATUS "GVM UGLC Compiler: ${GVM_UGLC_COMPILER}")


#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# =========================================================
# 3. 添加核心库 (Core/LibA/LibB) - 这些总是需要编译的
# =========================================================

add_subdirectory(GVM)

if(GVM_INSTALL)
  include(cmake/GVMInstall.cmake)
endif()

if(GVM_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(GVM_BUILD_SAMPLES)

  add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

  set(GVM_INCLUDE_DIRS ${GVM_REPOSITORY_ROOT}/GVM ${GVM_REPOSITORY_ROOT}/GVM/third_party)

  if(GVM_BUILD_SAMPLE_DESKTOP_BINARIES)
    set(GVM_LINK_LIBS SDL2::SDL2main SDL2::SDL2-static GVM)
    if(APPLE)
      set(APP_NAME MetalTest)
      add_executable(${APP_NAME} main.cpp)
      target_link_libraries(${APP_NAME} ${GVM_LINK_LIBS})
      target_include_directories(${APP_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
    endif()
  else()
    set(GVM_LINK_LIBS GVM)
  endif()

	  include(cmake/UGLC/UGLCompile.cmake)
	  set(UGL_COMPILER "${GVM_UGLC_COMPILER}")
	  if(NOT GVM_UGLC_COMPILER_AVAILABLE)
	    message(FATAL_ERROR "GVM samples require a valid in-tree UGLC target or explicit GVM_UGLC_EXECUTABLE because UGLBin headers are generated into the build tree.")
	  endif()
	  if(NOT DEFINED imgui_SOURCE_DIR OR NOT EXISTS "${imgui_SOURCE_DIR}/imgui.cpp")
	    message(FATAL_ERROR "GVM samples require Dear ImGui when GVM_BUILD_SAMPLES=ON.")
	  endif()
	  add_library(GVMImGuiSampleSupport STATIC
	    ${imgui_SOURCE_DIR}/imgui.cpp
	    ${imgui_SOURCE_DIR}/imgui_draw.cpp
	    ${imgui_SOURCE_DIR}/imgui_tables.cpp
	    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
	    ${GVM_REPOSITORY_ROOT}/GVM/GVMPlugin/imgui_impl_GVM.cpp)
	  target_link_libraries(GVMImGuiSampleSupport PUBLIC GVM)
	  target_include_directories(GVMImGuiSampleSupport PUBLIC
	    ${imgui_SOURCE_DIR}
	    ${GVM_REPOSITORY_ROOT}/GVM/GVMPlugin
	    ${GVM_REPOSITORY_ROOT}/GVM
	    ${GVM_REPOSITORY_ROOT}/GVM/third_party)
	  target_compile_definitions(GVMImGuiSampleSupport PUBLIC
	    IMGUI_IMPL_WEBGPU_BACKEND_GVM
	    IMGUI_USER_CONFIG=\"imconfig_GVM.h\")

	  add_library(GVMSampleWindowManager STATIC
	    GVMRuntime_Samples/SampleWindowManager/SampleApplication.cpp
	    GVMRuntime_Samples/SampleWindowManager/SampleWindowManager.cpp)
	  target_link_libraries(GVMSampleWindowManager PUBLIC GVM GVMImGuiSampleSupport)
	  if(TARGET SDL2::SDL2-static)
	    target_link_libraries(GVMSampleWindowManager PUBLIC SDL2::SDL2-static)
	  endif()
	  target_include_directories(GVMSampleWindowManager PUBLIC
	    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager
	    ${GVM_REPOSITORY_ROOT}/GVM
	    ${GVM_REPOSITORY_ROOT}/GVM/third_party)

      if(GVM_BUILD_THREE_SAMPLES)
        add_subdirectory(GVMRuntime_ThreeSamples)
      endif()

	  if(GVM_BUILD_SAMPLE_DESKTOP_BINARIES AND APPLE)
	    set(GVMRHI_SAMPLES Test_Triangle Test_TriangleWithBuffer Test_TriangleWithTexture Test_WriteBuffer)
	    foreach(SAMPLE IN LISTS GVMRHI_SAMPLES)
	      set(SAMPLE_NAME GVMRHI_SAMPLES_${SAMPLE})
	      add_executable(${SAMPLE_NAME} GVMRHI_Samples/${SAMPLE}.cpp)
	      target_sources(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/third_party/ddsktx/ddsktx.cpp)
	      target_link_libraries(${SAMPLE_NAME} ${GVM_LINK_LIBS})
	      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
	    endforeach()
	  endif()


  set(GVMRUNTIMES_PORTABLE_WINDOWED_SAMPLES Test_01_Triangle Test_02_TriangleWithBuffer Test_03_TriangleWithTexture)
  if(WIN32)
    set(GVMRUNTIMES_SAMPLES ${GVMRUNTIMES_PORTABLE_WINDOWED_SAMPLES})
  else()
  set(GVMRUNTIMES_SAMPLES Test_01_Triangle Test_02_TriangleWithBuffer Test_03_TriangleWithTexture Test_04_Cube Test_05_3DGS_PLY_Compute Test_06_3DGS_PLY_ComputeSort_Graphics ReplayAttachmentHazard)
  endif()
  set(GVM_3DGS_RUNTIME_SAMPLES Test_05_3DGS_PLY_Compute Test_06_3DGS_PLY_ComputeSort_Graphics)
  set(GVMRUNTIME_ADVANCED_SAMPLES Test_01_3DGS_PLY_LodSort_Graphics Test_02_3DGS_PLY_Cluster16_Graphics Test_03_3DGS_PLY_RainyGS_Graphics Test_04_Nanite_Runtime_Graphics Test_05_3DGS_PLY_GardenRain_Graphics Test_06_Procedural_Clipmap_SDF_Graphics Test_07_Splat_Virtual_Geometry_Graphics Test_08_Sparse_Dual_Contouring_Graphics Test_09_Sparse_Hash_Clipmap_SDF_Graphics Test_10_Sparse_Clipmap_SDF_Mesh_Graphics Test_11_Sparse_Marching_Cubes_Graphics)
  #set(GVMRUNTIMES_SAMPLES  Test_TriangleWithTexture)
  add_library(GVMRuntimeSampleTest01TriangleApp STATIC
    GVMRuntime_Samples/Test_01_Triangle/Test01TriangleApp.cpp)
  target_link_libraries(GVMRuntimeSampleTest01TriangleApp PUBLIC GVMSampleWindowManager glm::glm)
  target_include_directories(GVMRuntimeSampleTest01TriangleApp PUBLIC
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
    ${GVM_REPOSITORY_ROOT}/GVM
    ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
    ${GVM_REPOSITORY_ROOT}/GVM/third_party)
  UGLCOMPILE(GVMRuntimeSampleTest01TriangleApp
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle/Test_01_Triangle.hpp
    ${GVM_SAMPLE_GENERATED_ROOT}/Test_01_Triangle/UGLBin
    ${UGL_COMPILER}
    INCLUDE_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVM/third_party
      ${EASTL_SOURCE_DIR}/include
      ${EABase_SOURCE_DIR}/include/Common
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle
    DSL_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_01_Triangle)
  set(GVM_RUNTIME_SAMPLE_APP_TARGET_Test_01_Triangle GVMRuntimeSampleTest01TriangleApp)

  add_library(GVMRuntimeSampleTest02TriangleWithBufferApp STATIC
    GVMRuntime_Samples/Test_02_TriangleWithBuffer/Test02TriangleWithBufferApp.cpp)
  target_link_libraries(GVMRuntimeSampleTest02TriangleWithBufferApp PUBLIC GVMSampleWindowManager glm::glm)
  target_include_directories(GVMRuntimeSampleTest02TriangleWithBufferApp PUBLIC
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
    ${GVM_REPOSITORY_ROOT}/GVM
    ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
    ${GVM_REPOSITORY_ROOT}/GVM/third_party)
  UGLCOMPILE(GVMRuntimeSampleTest02TriangleWithBufferApp
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer/Test_02_TriangleWithBuffer.hpp
    ${GVM_SAMPLE_GENERATED_ROOT}/Test_02_TriangleWithBuffer/UGLBin
    ${UGL_COMPILER}
    INCLUDE_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVM/third_party
      ${EASTL_SOURCE_DIR}/include
      ${EABase_SOURCE_DIR}/include/Common
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer
    DSL_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_02_TriangleWithBuffer)
  set(GVM_RUNTIME_SAMPLE_APP_TARGET_Test_02_TriangleWithBuffer GVMRuntimeSampleTest02TriangleWithBufferApp)

  add_library(GVMRuntimeSampleTest03TriangleWithTextureApp STATIC
    GVMRuntime_Samples/Test_03_TriangleWithTexture/Test03TriangleWithTextureApp.cpp)
  target_link_libraries(GVMRuntimeSampleTest03TriangleWithTextureApp PUBLIC GVMSampleWindowManager glm::glm)
  target_include_directories(GVMRuntimeSampleTest03TriangleWithTextureApp PUBLIC
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
    ${GVM_REPOSITORY_ROOT}/GVM
    ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders
    ${GVM_REPOSITORY_ROOT}/GVM/third_party)
  UGLCOMPILE(GVMRuntimeSampleTest03TriangleWithTextureApp
    ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture/Test_03_TriangleWithTexture.hpp
    ${GVM_SAMPLE_GENERATED_ROOT}/Test_03_TriangleWithTexture/UGLBin
    ${UGL_COMPILER}
    INCLUDE_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVM/third_party
      ${EASTL_SOURCE_DIR}/include
      ${EABase_SOURCE_DIR}/include/Common
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture
    DSL_DIRS
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
      ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_03_TriangleWithTexture)
  set(GVM_RUNTIME_SAMPLE_APP_TARGET_Test_03_TriangleWithTexture GVMRuntimeSampleTest03TriangleWithTextureApp)

  if(GVM_BUILD_SAMPLE_DESKTOP_BINARIES)
    foreach(SAMPLE IN LISTS GVMRUNTIMES_SAMPLES)
      set(SAMPLE_NAME GVMRUNTIMES_SAMPLES_${SAMPLE})
      UGLCOMPILE_LINTER(${SAMPLE_NAME} ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE}/${SAMPLE}.hpp ${GVM_SAMPLE_GENERATED_ROOT}/${SAMPLE}/UGLBin)
      target_link_libraries(${SAMPLE_NAME} ${GVM_LINK_LIBS} glm::glm)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${EASTL_SOURCE_DIR}/include)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${EABase_SOURCE_DIR}/include/Common)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE}/)
      list(FIND GVM_3DGS_RUNTIME_SAMPLES "${SAMPLE}" GVM_3DGS_SAMPLE_INDEX)
      if(GVM_3DGS_SAMPLE_INDEX GREATER -1)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
      endif()
    endforeach()

    if(GVM_BUILD_ADVANCED_SAMPLES)
      foreach(SAMPLE IN LISTS GVMRUNTIME_ADVANCED_SAMPLES)
        set(SAMPLE_NAME GVMRUNTIME_ADVANCED_SAMPLES_${SAMPLE})
        UGLCOMPILE_LINTER(${SAMPLE_NAME} ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE}/${SAMPLE}.hpp ${GVM_ADVANCED_SAMPLE_GENERATED_ROOT}/${SAMPLE}/UGLBin)
        target_link_libraries(${SAMPLE_NAME} ${GVM_LINK_LIBS} glm::glm)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${EASTL_SOURCE_DIR}/include)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${EABase_SOURCE_DIR}/include/Common)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/GaussianSplattingAdvancedShared)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE}/)
        if(SAMPLE STREQUAL "Test_04_Nanite_Runtime_Graphics")
          target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/NaniteShared)
        endif()
      endforeach()
    endif()
  endif()

  if(GVM_BUILD_SAMPLE_DESKTOP_BINARIES AND GVM_UGLC_COMPILER_AVAILABLE)
    foreach(SAMPLE IN LISTS GVMRUNTIMES_SAMPLES)
      set(SAMPLE_NAME GVMRUNTIMES_SAMPLES_${SAMPLE}_bin)

      add_executable(${SAMPLE_NAME} GVMRuntime_Samples/${SAMPLE}/main.cpp)

      set(GVM_RUNTIME_SAMPLE_UGL_INCLUDE_DIRS
          ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
          ${GVM_REPOSITORY_ROOT}/GVM/third_party
          ${EASTL_SOURCE_DIR}/include
          ${EABase_SOURCE_DIR}/include/Common
          ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE})
      set(GVM_RUNTIME_SAMPLE_UGL_DSL_DIRS
          ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
          ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE})
      list(FIND GVM_3DGS_RUNTIME_SAMPLES "${SAMPLE}" GVM_3DGS_SAMPLE_INDEX)
      if(GVM_3DGS_SAMPLE_INDEX GREATER -1)
        list(APPEND GVM_RUNTIME_SAMPLE_UGL_INCLUDE_DIRS
             ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
        list(APPEND GVM_RUNTIME_SAMPLE_UGL_DSL_DIRS
             ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
      endif()

        set(GVM_RUNTIME_SAMPLE_APP_TARGET_VAR "GVM_RUNTIME_SAMPLE_APP_TARGET_${SAMPLE}")
	      if(NOT DEFINED ${GVM_RUNTIME_SAMPLE_APP_TARGET_VAR})
	        UGLCOMPILE(${SAMPLE_NAME} ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE}/${SAMPLE}.hpp ${GVM_SAMPLE_GENERATED_ROOT}/${SAMPLE}/UGLBin ${UGL_COMPILER}
	          INCLUDE_DIRS
	            ${GVM_RUNTIME_SAMPLE_UGL_INCLUDE_DIRS}
	          DSL_DIRS
	            ${GVM_RUNTIME_SAMPLE_UGL_DSL_DIRS}
	        )
	      endif()

	      target_link_libraries(${SAMPLE_NAME} ${GVM_LINK_LIBS} glm::glm)
        if(SAMPLE STREQUAL "ReplayAttachmentHazard")
          target_link_libraries(${SAMPLE_NAME} GVMSampleWindowManager)
        endif()
	      if(DEFINED ${GVM_RUNTIME_SAMPLE_APP_TARGET_VAR})
	        target_link_libraries(${SAMPLE_NAME} ${${GVM_RUNTIME_SAMPLE_APP_TARGET_VAR}})
	      endif()
	      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/third_party)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS} GVMRuntime_Samples/TestUtils)
      target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/${SAMPLE}/)
      if(GVM_3DGS_SAMPLE_INDEX GREATER -1)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
      endif()
    endforeach()

    if(GVM_BUILD_ADVANCED_SAMPLES)
      foreach(SAMPLE IN LISTS GVMRUNTIME_ADVANCED_SAMPLES)
        set(SAMPLE_NAME GVMRUNTIME_ADVANCED_SAMPLES_${SAMPLE}_bin)

        add_executable(${SAMPLE_NAME} GVMRuntime_AdvancedSamples/${SAMPLE}/main.cpp)

        if(SAMPLE STREQUAL "Test_01_3DGS_PLY_LodSort_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE GVMRuntime_AdvancedSamples/${SAMPLE}/Test01LodRuntime.cpp)
        endif()
        if(SAMPLE STREQUAL "Test_02_3DGS_PLY_Cluster16_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE GVMRuntime_AdvancedSamples/${SAMPLE}/Test02ClusterRuntime.cpp)
        endif()
        if(SAMPLE STREQUAL "Test_03_3DGS_PLY_RainyGS_Graphics" OR SAMPLE STREQUAL "Test_05_3DGS_PLY_GardenRain_Graphics" OR SAMPLE STREQUAL "Test_06_Procedural_Clipmap_SDF_Graphics" OR SAMPLE STREQUAL "Test_07_Splat_Virtual_Geometry_Graphics" OR SAMPLE STREQUAL "Test_08_Sparse_Dual_Contouring_Graphics" OR SAMPLE STREQUAL "Test_09_Sparse_Hash_Clipmap_SDF_Graphics" OR SAMPLE STREQUAL "Test_11_Sparse_Marching_Cubes_Graphics")
          target_link_libraries(${SAMPLE_NAME} GVMImGuiSampleSupport)
        endif()
        if(SAMPLE STREQUAL "Test_10_Sparse_Clipmap_SDF_Mesh_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE
            GVMRuntime_AdvancedSamples/${SAMPLE}/Test10CpuStreamingAllocator.cpp)
          target_link_libraries(${SAMPLE_NAME} GVMSampleWindowManager)
        endif()
        if(SAMPLE STREQUAL "Test_04_Nanite_Runtime_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE
            GVMRuntime_AdvancedSamples/NaniteShared/NaniteClusterBuilder.cpp
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR}/tiny_obj_loader.cc)
          target_link_libraries(${SAMPLE_NAME} meshoptimizer)
          target_include_directories(${SAMPLE_NAME} PRIVATE
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR}
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/NaniteShared)
          target_compile_definitions(${SAMPLE_NAME} PRIVATE
            TEST04_NANITE_ASSET_DIR="${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/models")
          if(Python3_Interpreter_FOUND)
            set(TEST04_NANITE_MODEL_DIR "${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/models")
            set(TEST04_NANITE_MODEL_CACHE_DIR "${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/.cache")
            set(TEST04_NANITE_ASSET_TARGET GVMRUNTIME_ADVANCED_SAMPLES_Test_04_Nanite_Runtime_Graphics_assets)
            if(NOT TARGET ${TEST04_NANITE_ASSET_TARGET})
              add_custom_target(${TEST04_NANITE_ASSET_TARGET}
                COMMAND ${Python3_EXECUTABLE}
                  ${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/download_nanite_models.py
                  --models-dir ${TEST04_NANITE_MODEL_DIR}
                  --cache-dir ${TEST04_NANITE_MODEL_CACHE_DIR}
                COMMENT "Best-effort downloading optional Nanite Test04 model assets"
                VERBATIM)
            endif()
            message(STATUS "Optional Test04 Nanite model asset target: ${TEST04_NANITE_ASSET_TARGET}")
          else()
            message(WARNING "Python3 not found; optional Test04 Nanite model assets will not be downloaded during build.")
          endif()
        endif()
        if(SAMPLE STREQUAL "Test_07_Splat_Virtual_Geometry_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE
            GVMRuntime_AdvancedSamples/${SAMPLE}/SplatVirtualGeometryBuilder.cpp
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR}/tiny_obj_loader.cc)
          target_include_directories(${SAMPLE_NAME} PRIVATE
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR})
          target_compile_definitions(${SAMPLE_NAME} PRIVATE
            TEST07_SPLAT_VG_ASSET_DIR="${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/models")
        endif()
        if(SAMPLE STREQUAL "Test_08_Sparse_Dual_Contouring_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE
            GVMRuntime_AdvancedSamples/${SAMPLE}/SparseDualContouringBuilder.cpp
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR}/tiny_obj_loader.cc)
          target_include_directories(${SAMPLE_NAME} PRIVATE
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR})
          target_compile_definitions(${SAMPLE_NAME} PRIVATE
            TEST08_SPARSE_DC_ASSET_DIR="${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/models")
        endif()
        if(SAMPLE STREQUAL "Test_11_Sparse_Marching_Cubes_Graphics")
          target_sources(${SAMPLE_NAME} PRIVATE
            GVMRuntime_AdvancedSamples/${SAMPLE}/SparseMarchingCubesBuilder.cpp
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR}/tiny_obj_loader.cc)
          target_include_directories(${SAMPLE_NAME} PRIVATE
            ${CPM_PACKAGE_tinyobjloader_SOURCE_DIR})
          target_compile_definitions(${SAMPLE_NAME} PRIVATE
            TEST11_SPARSE_MC_ASSET_DIR="${GVM_REPOSITORY_ROOT}/GVMRuntime_SampleAssets/models")
        endif()

        set(GVM_ADVANCED_SAMPLE_UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVM/third_party
            ${EASTL_SOURCE_DIR}/include
            ${EABase_SOURCE_DIR}/include/Common
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/GaussianSplattingAdvancedShared
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE})
        if(SAMPLE STREQUAL "Test_04_Nanite_Runtime_Graphics")
          list(APPEND GVM_ADVANCED_SAMPLE_UGL_INCLUDE_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/NaniteShared)
        endif()

        set(GVM_ADVANCED_SAMPLE_UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/GaussianSplattingAdvancedShared
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE})
        if(SAMPLE STREQUAL "Test_04_Nanite_Runtime_Graphics")
          list(APPEND GVM_ADVANCED_SAMPLE_UGL_DSL_DIRS
            ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/NaniteShared)
        endif()

        UGLCOMPILE(${SAMPLE_NAME} ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE}/${SAMPLE}.hpp ${GVM_ADVANCED_SAMPLE_GENERATED_ROOT}/${SAMPLE}/UGLBin ${UGL_COMPILER}
          INCLUDE_DIRS
            ${GVM_ADVANCED_SAMPLE_UGL_INCLUDE_DIRS}
          DSL_DIRS
            ${GVM_ADVANCED_SAMPLE_UGL_DSL_DIRS}
        )

        target_link_libraries(${SAMPLE_NAME} ${GVM_LINK_LIBS} glm::glm)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/third_party)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/GaussianSplattingShared)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/GaussianSplattingAdvancedShared)
        target_include_directories(${SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_AdvancedSamples/${SAMPLE}/)
      endforeach()
    endif()

    set(GVM_RENDER_DEBUG_SAMPLE_NAME GVM_RENDER_FEATURE_DEBUG_bin)
    add_executable(${GVM_RENDER_DEBUG_SAMPLE_NAME} tests/render_cases/RenderFeatureDebugHost/main.cpp)
    UGLCOMPILE(
      ${GVM_RENDER_DEBUG_SAMPLE_NAME}
      ${GVM_REPOSITORY_ROOT}/tests/render_cases/RenderFeatureSuite.hpp
      ${GVM_SAMPLE_GENERATED_ROOT}/RenderFeatureDebugHost/UGLBin
      ${UGL_COMPILER}
      INCLUDE_DIRS
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
        ${GVM_REPOSITORY_ROOT}/GVM/third_party
        ${GVM_REPOSITORY_ROOT}/tests/render_cases
      DSL_DIRS
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils
        ${GVM_REPOSITORY_ROOT}/tests/render_cases
    )
    target_link_libraries(${GVM_RENDER_DEBUG_SAMPLE_NAME} ${GVM_LINK_LIBS} glm::glm)
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/third_party)
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_INCLUDE_DIRS})
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVM/UGLHeaders)
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/tests/render_cases/RenderFeatureDebugHost)
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/Test_04_Cube)
    target_include_directories(${GVM_RENDER_DEBUG_SAMPLE_NAME} PRIVATE ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/TestUtils)
  elseif(GVM_BUILD_SAMPLE_DESKTOP_BINARIES)
    message(WARNING "UGLC executable was not found, skipping DSL runtime sample binaries.")
  endif()

  if(ANDROID AND GVM_ANDROID_BUILD_TEST01_NATIVE)
    add_library(GVMRuntimeSampleTest01TriangleAndroid SHARED
      GVMRuntime_Samples/SampleWindowManager/AndroidNativeActivity.cpp
      ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_link_libraries(GVMRuntimeSampleTest01TriangleAndroid
      PRIVATE
        GVMRuntimeSampleTest01TriangleApp
        android
        log)
    target_include_directories(GVMRuntimeSampleTest01TriangleAndroid
      PRIVATE
        ${ANDROID_NDK}/sources/android/native_app_glue
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager)
    set_target_properties(GVMRuntimeSampleTest01TriangleAndroid
      PROPERTIES
        OUTPUT_NAME gvm_test01_triangle)
  endif()

  if(OHOS AND GVM_OHOS_BUILD_TEST01_XCOMPONENT)
    add_library(GVMRuntimeSampleTest01TriangleOhos SHARED
      GVMRuntime_Samples/SampleWindowManager/OhosXComponentHost.cpp)
    target_link_libraries(GVMRuntimeSampleTest01TriangleOhos
      PRIVATE
        GVMRuntimeSampleTest01TriangleApp
        libace_napi.z.so
        libace_ndk.z.so
        libhilog_ndk.z.so
        libnative_window.so)
    target_include_directories(GVMRuntimeSampleTest01TriangleOhos
      PRIVATE
        ${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager)
    set_target_properties(GVMRuntimeSampleTest01TriangleOhos
      PROPERTIES
        OUTPUT_NAME gvm_test01_triangle)
  endif()

  gvm_configure_mobile_sample_hosts()

  #add_executable(GVMTest GVMRuntime_Samples/Test_Triangle.cpp)

endif()
