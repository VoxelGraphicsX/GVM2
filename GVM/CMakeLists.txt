cmake_minimum_required(VERSION 3.22.0)
project(GVM VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)

add_library(GVMUGLHeaders INTERFACE)
add_library(GVM::UGLHeaders ALIAS GVMUGLHeaders)
target_compile_features(GVMUGLHeaders INTERFACE cxx_std_20)
target_compile_options(GVMUGLHeaders
    INTERFACE
        "$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>"
        "$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:preprocessor>"
)
target_include_directories(GVMUGLHeaders
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/UGLHeaders>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/UGLHeaders>"
)
target_link_libraries(GVMUGLHeaders INTERFACE GVMDeps::glm EASTL_Helper)
set_target_properties(GVMUGLHeaders PROPERTIES EXPORT_NAME UGLHeaders)

add_library(GVM SHARED GVM.cpp)
add_library(GVM::GVM ALIAS GVM)

add_subdirectory(GVMRHI)
add_subdirectory(GVMCore)

set_target_properties(GVM PROPERTIES
    EXPORT_NAME GVM
)
if(NOT ANDROID AND NOT OHOS)
    set_target_properties(GVM PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
endif()

if(APPLE)
    set_target_properties(GVM PROPERTIES INSTALL_RPATH "@loader_path")
elseif(WIN32)
    set_target_properties(GVM PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

set(GVM_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(GVM PUBLIC GVMRHI GVMCore GVMUGLHeaders EASTL_Helper)
target_include_directories(GVM PRIVATE ${GVM_INCLUDE_DIRS})
