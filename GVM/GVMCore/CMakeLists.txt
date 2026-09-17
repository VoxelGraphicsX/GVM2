cmake_minimum_required(VERSION 3.22.0)
set(CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)

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
        Private/GSync.cpp
        Private/GUnifiedMemoryGPUVector.cpp
        Public/GAbstractRenderSetCommandEncoder.cpp
        Public/GAbstractRenderer.cpp
        Public/GRenderSetCommand.cpp
)
set_target_properties(GVMCore PROPERTIES
    EXPORT_NAME Core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
if(APPLE)
    set_target_properties(GVMCore PROPERTIES INSTALL_RPATH "@loader_path")
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

target_link_libraries(GVMCore PRIVATE EASTL)
target_link_libraries(GVMCore PUBLIC EASTL_Helper GVMLoggingSupport GVMRHI)
