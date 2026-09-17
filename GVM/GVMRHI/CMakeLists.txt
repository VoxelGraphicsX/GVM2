cmake_minimum_required(VERSION 3.22.0)
set(CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)

if(APPLE)
    enable_language(OBJCXX)
    find_library(APPLE_FWK_FOUNDATION Foundation REQUIRED)
    find_library(APPLE_FWK_QUARTZ_CORE QuartzCore REQUIRED)
    find_library(APPLE_FWK_METAL Metal REQUIRED)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/EASTL_Helper ${CMAKE_CURRENT_BINARY_DIR}/EASTL_Helper)
add_library(GVMLoggingSupport INTERFACE)
add_library(GVM::LoggingSupport ALIAS GVMLoggingSupport)
target_compile_definitions(GVMLoggingSupport INTERFACE GVM_LOGGING_ENABLED=$<IF:$<BOOL:${GVM_RHI_ENABLE_LOGGING}>,1,0>)
target_compile_definitions(GVMLoggingSupport INTERFACE GVM_CPU_PROBE_ENABLED=$<IF:$<BOOL:${GVM_RHI_ENABLE_CPU_PROBES}>,1,0>)
if(GVM_RHI_ENABLE_LOGGING)
    target_link_libraries(GVMLoggingSupport
        INTERFACE
            "$<BUILD_INTERFACE:fmt::fmt>"
            "$<INSTALL_INTERFACE:GVMDeps::fmt>"
    )
endif()
set_target_properties(GVMLoggingSupport PROPERTIES EXPORT_NAME LoggingSupport)
add_library(GVMRHI SHARED)
add_library(GVM::RHI ALIAS GVMRHI)
target_compile_features(GVMRHI PUBLIC cxx_std_20)
set_target_properties(GVMRHI PROPERTIES
    EXPORT_NAME RHI
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
if(APPLE)
    set_target_properties(GVMRHI PROPERTIES INSTALL_RPATH "@loader_path")
endif()

set(GVM_RHI_FACTORY_HAS_METAL 0)
if(APPLE)
    set(GVM_RHI_FACTORY_HAS_METAL 1)
endif()

set(GVM_RHI_FACTORY_HAS_VULKAN 0)
if(GVM_RHI_ENABLE_VULKAN)
    set(GVM_RHI_FACTORY_HAS_VULKAN 1)
endif()

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Generated)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/GBackendAvailability.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/Generated/GBackendAvailability.hpp
    @ONLY
)

target_include_directories(GVMRHI
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/Generated
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
        ${CMAKE_CURRENT_SOURCE_DIR}/../
)
add_subdirectory(Private)
if(APPLE)
    add_subdirectory(GVMRHI.Metal)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/GVMRHI.Metal/MInstance.cpp
        PROPERTIES
        COMPILE_DEFINITIONS "createInstance=GVM_RHI_METAL_INTERNAL_createInstance;destroyInstance=GVM_RHI_METAL_INTERNAL_destroyInstance"
    )
    target_link_libraries(GVMRHI PRIVATE ${APPLE_FWK_FOUNDATION} ${APPLE_FWK_QUARTZ_CORE} ${APPLE_FWK_METAL})

endif()

if(GVM_RHI_ENABLE_VULKAN)
    add_subdirectory(GVMRHI.Vulkan)
    target_link_libraries(
        GVMRHI
        PRIVATE
        Vulkan::Headers
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        spirv-reflect-static
    )
endif()

if(GVM_RHI_ENABLE_LOGGING AND GVM_RHI_USE_SPDLOG_LOGGER)
    target_link_libraries(GVMRHI PRIVATE spdlog::spdlog)
endif()

target_link_libraries(GVMRHI PRIVATE EASTL)
target_link_libraries(GVMRHI PUBLIC EASTL_Helper GVMLoggingSupport)
