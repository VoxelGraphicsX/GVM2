cmake_minimum_required(VERSION 3.22.0)
project(GVM VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
include(GNUInstallDirs)

add_library(GVMUGLHeaders INTERFACE)
add_library(GVM::UGLHeaders ALIAS GVMUGLHeaders)
target_compile_features(GVMUGLHeaders INTERFACE cxx_std_20)
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
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(APPLE)
    set_target_properties(GVM PROPERTIES INSTALL_RPATH "@loader_path")
endif()

set(GVM_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(GVM PUBLIC GVMRHI GVMCore GVMUGLHeaders EASTL_Helper)
target_include_directories(GVM PRIVATE ${GVM_INCLUDE_DIRS})
