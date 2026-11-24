cmake_minimum_required(VERSION 3.22.0)
set(CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/EASTL_Helper ${CMAKE_CURRENT_BINARY_DIR}/EASTL_Helper)
add_library(GVMCore SHARED)
add_library(GVM::Core ALIAS GVMCore)
target_compile_features(GVMCore PUBLIC cxx_std_20)
target_sources(
    GVMCore
    PRIVATE
        Private/GDeviceProxy.cpp
        Private/GGPUVector.cpp
        Private/GHelperFunction.cpp
        Private/GObject.cpp
        Private/GProgressiveData.cpp
        Private/GQueue.cpp
        Private/GRenderBufferComponent.cpp
        Private/GRenderComponent.cpp
        Private/GRenderComponentBatchedCopyCommand.cpp
        Private/GRenderEntity.cpp
        Private/GRenderSet.cpp
        Private/GRenderSetCommandEncoder.cpp
        Private/GRenderSetInternalCommand.cpp
        Private/GRenderTextureComponent.cpp
        Private/GResource.cpp
        Private/GShader.cpp
        Private/GStagingLinearAllocator.cpp
        Private/GUnifiedMemoryGPUVector.cpp
        Public/GAbstractRenderSetCommandEncoder.cpp
        Public/GAbstractRenderer.cpp
        Public/GRenderSetCommand.cpp
        Public/ExecutorTask.cpp
        Public/ITaskExecutor.cpp
        Public/ThreadExecutor.cpp
        Public/Task.cpp
        Public/TaskGraph.cpp
        Public/TaskRuntime.cpp
        Public/Execution.cpp
        Private/TaskGraphDefinition.cpp
        Private/TaskExecutionState.cpp
        Private/TaskRuntimeState.cpp
        Private/PassResourceAccess.cpp
        Private/PassSchedule.cpp
        Private/PassWeightProfiler.cpp
        Private/GraphDevice.cpp
        Private/GraphResourceAllocation.cpp
        Private/GpuGraphExecution.cpp
        Public/RenderNode.cpp
)
set_target_properties(GVMCore PROPERTIES
    EXPORT_NAME Core
)
if(NOT ANDROID AND NOT OHOS)
    set_target_properties(GVMCore PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()
if(APPLE)
    set_target_properties(GVMCore PROPERTIES INSTALL_RPATH "@loader_path")
elseif(WIN32)
    set_target_properties(GVMCore PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

target_include_directories(GVMCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/third_party>"
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(GVMCore PRIVATE EASTL Threads::Threads)
target_link_libraries(GVMCore PUBLIC EASTL_Helper GVMLoggingSupport GVMRHI)

if(TARGET marl::marl OR TARGET marl)
    add_library(GVMMarlExecutor INTERFACE)
    add_library(GVM::MarlExecutor ALIAS GVMMarlExecutor)
    target_compile_features(GVMMarlExecutor INTERFACE cxx_std_20)
    if(TARGET marl::marl)
        target_link_libraries(GVMMarlExecutor INTERFACE GVMCore marl::marl)
    else()
        target_link_libraries(GVMMarlExecutor INTERFACE GVMCore marl)
    endif()
endif()
