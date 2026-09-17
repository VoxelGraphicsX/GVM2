include_guard(GLOBAL)

set(GVM_UGLC_SOURCE_DIR "${GVM_REPOSITORY_ROOT}/UGLC/Source" CACHE PATH "UGLC production source directory.")

if(NOT DEFINED GVM_UGLC_EXECUTABLE)
    set(GVM_UGLC_EXECUTABLE "" CACHE FILEPATH "Optional external UGLC executable override used only when GVM_BUILD_UGLC is OFF.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/UGLCBootstrapLLVM.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/UGLCDependencies.cmake")

option(UGLC_STRICT_BUILD "Promote UGLC warnings to errors." OFF)
option(UGLC_ENABLE_LEGACY "Build the optional Legacy AST shader pipeline and DXC compiler." OFF)
message(STATUS "UGLC Legacy shader pipeline: ${UGLC_ENABLE_LEGACY}")

include("${CMAKE_CURRENT_LIST_DIR}/UGLCSources.cmake")

if(GVM_BUILD_UGLC)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_uglc_effective_osx_sysroot "${CMAKE_OSX_SYSROOT}")
        if(_uglc_effective_osx_sysroot STREQUAL "" OR NOT EXISTS "${_uglc_effective_osx_sysroot}")
            execute_process(
                COMMAND xcrun --sdk macosx --show-sdk-path
                OUTPUT_VARIABLE _uglc_detected_osx_sysroot
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _uglc_detected_osx_sysroot_result
            )
            if(_uglc_detected_osx_sysroot_result EQUAL 0 AND EXISTS "${_uglc_detected_osx_sysroot}")
                set(CMAKE_OSX_SYSROOT "${_uglc_detected_osx_sysroot}" CACHE STRING "macOS SDK used for GVM and UGLC builds." FORCE)
                message(STATUS "UGLC resolved macOS SDK root: ${CMAKE_OSX_SYSROOT}")
            endif()
        endif()
        unset(_uglc_effective_osx_sysroot)
        unset(_uglc_detected_osx_sysroot)
        unset(_uglc_detected_osx_sysroot_result)
    endif()

    uglc_resolve_llvm_bundle()
    set(LOCAL_LLVM_DIR "${UGLC_RESOLVED_LLVM_ROOT}")
    message(STATUS "UGLC LLVM bundle source: ${UGLC_RESOLVED_LLVM_SOURCE}")
    message(STATUS "UGLC LLVM root: ${LOCAL_LLVM_DIR}")

    set(LLVM_DIR "${LOCAL_LLVM_DIR}/lib/cmake/llvm")
    set(Clang_DIR "${LOCAL_LLVM_DIR}/lib/cmake/clang")

    find_package(LLVM 20.1 REQUIRED CONFIG)
    find_package(Clang 20.1 REQUIRED CONFIG)

    string(REGEX MATCH "^[0-9]+" _uglc_llvm_major "${LLVM_PACKAGE_VERSION}")
    set(GVM_UGLC_RESOURCE_DIR "${LOCAL_LLVM_DIR}/lib/clang/${_uglc_llvm_major}" CACHE PATH "Clang resource directory passed to UGLC invocations." FORCE)
    if(NOT EXISTS "${GVM_UGLC_RESOURCE_DIR}/include")
        message(FATAL_ERROR "UGLC Clang resource directory is invalid: ${GVM_UGLC_RESOURCE_DIR}")
    endif()

    find_library(UGLC_LLVM_CLANG_CPP_LIBRARY
        NAMES clang-cpp libclang-cpp
        PATHS "${LOCAL_LLVM_DIR}/lib"
        REQUIRED
        NO_DEFAULT_PATH
    )

    set(UGLC_LIBRARY_LIST "${UGLC_LLVM_CLANG_CPP_LIBRARY}")
    if(NOT WIN32)
        if(APPLE)
            find_library(UGLC_PLATFORM_LIBCXX
                NAMES c++ libc++
                REQUIRED
            )
            find_library(UGLC_PLATFORM_LIBCXXABI
                NAMES c++abi libc++abi
                REQUIRED
            )
            list(APPEND UGLC_LIBRARY_LIST "${UGLC_PLATFORM_LIBCXX}" "${UGLC_PLATFORM_LIBCXXABI}")
        else()
            find_library(UGLC_LLVM_LIBCXX
                NAMES c++ libc++
                PATHS "${LOCAL_LLVM_DIR}/lib"
                REQUIRED
                NO_DEFAULT_PATH
            )
            find_library(UGLC_LLVM_LIBCXXABI
                NAMES c++abi libc++abi
                PATHS "${LOCAL_LLVM_DIR}/lib"
                REQUIRED
                NO_DEFAULT_PATH
            )
            list(APPEND UGLC_LIBRARY_LIST "${UGLC_LLVM_LIBCXX}" "${UGLC_LLVM_LIBCXXABI}")
        endif()
    endif()

    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake from: ${LLVM_DIR}")

    uglc_configure_dependencies()

    set(_uglc_codegen_source_files "")
    foreach(_uglc_source IN LISTS UGLC_CODEGEN_SOURCES)
        list(APPEND _uglc_codegen_source_files "${GVM_UGLC_SOURCE_DIR}/${_uglc_source}")
    endforeach()

    add_executable(UGLC
        "${GVM_UGLC_SOURCE_DIR}/main.cpp"
        ${_uglc_codegen_source_files}
    )
    target_compile_features(UGLC PRIVATE cxx_std_20)
    target_include_directories(UGLC PRIVATE "${GVM_UGLC_SOURCE_DIR}")
    target_include_directories(UGLC SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(UGLC PRIVATE
        ${LLVM_DEFINITIONS}
        UGLC_ENABLE_LEGACY=$<BOOL:${UGLC_ENABLE_LEGACY}>
        UGLC_ENABLE_SHADER_LINE_DIRECTIVES=$<BOOL:${UGLC_ENABLE_SHADER_LINE_DIRECTIVES}>
        CXXOPTS_NO_RTTI=1
    )

    if(MSVC)
        target_compile_options(UGLC PRIVATE /W4)
        if(UGLC_STRICT_BUILD)
            target_compile_options(UGLC PRIVATE /WX)
        endif()
    else()
        target_compile_options(UGLC PRIVATE -fno-rtti -Wall -Wextra -Wpedantic)
        if(UGLC_STRICT_BUILD)
            target_compile_options(UGLC PRIVATE -Werror)
        endif()
    endif()

    target_link_directories(UGLC PRIVATE ${LLVM_LIBRARY_DIRS})
    target_link_libraries(UGLC PRIVATE
        ${UGLC_LIBRARY_LIST}
        ${UGLC_CXXOPTS_TARGET}
        ${UGLC_DXC_TARGET}
        ${UGLC_NLOHMANN_JSON_TARGET}
        ${UGLC_SPIRV_HEADERS_TARGET}
        ${UGLC_SPIRV_TOOLS_TARGET}
        ${UGLC_SPIRV_TOOLS_OPT_TARGET}
    )

    if(UGLC_DXC_BUILD_TARGET)
        add_dependencies(UGLC ${UGLC_DXC_BUILD_TARGET})
    endif()

    if(UGLC_DXC_RUNTIME_DIR AND NOT WIN32)
        set_property(TARGET UGLC APPEND PROPERTY BUILD_RPATH "${UGLC_DXC_RUNTIME_DIR}")
        set_property(TARGET UGLC APPEND PROPERTY INSTALL_RPATH "${UGLC_DXC_RUNTIME_DIR}")
    endif()

    set(GVM_UGLC_COMPILER UGLC CACHE STRING "UGLC compiler target or executable path used by GVM DSL codegen." FORCE)
else()
    if(GVM_UGLC_EXECUTABLE AND EXISTS "${GVM_UGLC_EXECUTABLE}")
        set(GVM_UGLC_COMPILER "${GVM_UGLC_EXECUTABLE}" CACHE STRING "UGLC compiler target or executable path used by GVM DSL codegen." FORCE)
    else()
        set(GVM_UGLC_COMPILER "" CACHE STRING "UGLC compiler target or executable path used by GVM DSL codegen." FORCE)
    endif()
endif()
