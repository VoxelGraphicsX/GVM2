include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/UGLCMetalCompiler.cmake")

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
    if(MSVC)
        if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
            set(_uglc_saved_has_msvc_runtime ON)
            set(_uglc_saved_msvc_runtime "${CMAKE_MSVC_RUNTIME_LIBRARY}")
        else()
            set(_uglc_saved_has_msvc_runtime OFF)
        endif()
        # Official prebuilt Windows LLVM/Clang static archives use the static release CRT (/MT).
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
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

    if(WIN32 AND TARGET LLVMDebugInfoPDB)
        # Prebuilt Windows LLVM archives embed the release builder's Visual Studio 2019 DIA SDK path in LLVMDebugInfoPDB.
        get_target_property(_uglc_pdb_link_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        if(_uglc_pdb_link_libs)
            set(_uglc_dia_search_dirs "")
            if(DEFINED ENV{VSINSTALLDIR} AND NOT "$ENV{VSINSTALLDIR}" STREQUAL "")
                file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" _uglc_env_vs_dir)
                list(APPEND _uglc_dia_search_dirs "${_uglc_env_vs_dir}/DIA SDK/lib/amd64")
                unset(_uglc_env_vs_dir)
            endif()
            if(CMAKE_CXX_COMPILER)
                set(_uglc_probe_dir "${CMAKE_CXX_COMPILER}")
                foreach(_uglc_step RANGE 1 8)
                    get_filename_component(_uglc_probe_dir "${_uglc_probe_dir}" DIRECTORY)
                    if(EXISTS "${_uglc_probe_dir}/DIA SDK/lib/amd64")
                        list(APPEND _uglc_dia_search_dirs "${_uglc_probe_dir}/DIA SDK/lib/amd64")
                    endif()
                    if(EXISTS "${_uglc_probe_dir}/lib/onecore/x64")
                        list(APPEND _uglc_dia_search_dirs "${_uglc_probe_dir}/lib/onecore/x64")
                    endif()
                endforeach()
                unset(_uglc_probe_dir)
                unset(_uglc_step)
            endif()
            set(_uglc_fixed_pdb_link_libs "")
            foreach(_uglc_lib IN LISTS _uglc_pdb_link_libs)
                if(_uglc_lib MATCHES "[/\\]diaguids\\.lib$" AND NOT EXISTS "${_uglc_lib}")
                    unset(_uglc_diaguids_library CACHE)
                    find_library(_uglc_diaguids_library NAMES diaguids PATHS ${_uglc_dia_search_dirs})
                    if(_uglc_diaguids_library)
                        list(APPEND _uglc_fixed_pdb_link_libs "${_uglc_diaguids_library}")
                    endif()
                else()
                    list(APPEND _uglc_fixed_pdb_link_libs "${_uglc_lib}")
                endif()
            endforeach()
            set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${_uglc_fixed_pdb_link_libs}")
            unset(_uglc_dia_search_dirs)
            unset(_uglc_fixed_pdb_link_libs)
            unset(_uglc_lib)
        endif()
        unset(_uglc_pdb_link_libs)
    endif()

    string(REGEX MATCH "^[0-9]+" _uglc_llvm_major "${LLVM_PACKAGE_VERSION}")
    set(GVM_UGLC_RESOURCE_DIR "${LOCAL_LLVM_DIR}/lib/clang/${_uglc_llvm_major}" CACHE PATH "Clang resource directory passed to UGLC invocations." FORCE)
    if(NOT EXISTS "${GVM_UGLC_RESOURCE_DIR}/include")
        message(FATAL_ERROR "UGLC Clang resource directory is invalid: ${GVM_UGLC_RESOURCE_DIR}")
    endif()

    if(WIN32)
        llvm_map_components_to_libnames(_uglc_llvm_libs
            core
            option
            support
            targetparser
        )
        set(UGLC_LIBRARY_LIST
            clangTooling
            clangFrontend
            clangDriver
            clangSerialization
            clangParse
            clangSema
            clangAnalysis
            clangEdit
            clangAST
            clangASTMatchers
            clangRewrite
            clangLex
            clangBasic
            ${_uglc_llvm_libs}
        )
        unset(_uglc_llvm_libs)
    else()
        find_library(UGLC_LLVM_CLANG_CPP_LIBRARY
            NAMES clang-cpp libclang-cpp
            PATHS "${LOCAL_LLVM_DIR}/lib"
            REQUIRED
            NO_DEFAULT_PATH
        )

        set(UGLC_LIBRARY_LIST "${UGLC_LLVM_CLANG_CPP_LIBRARY}")
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
    file(TO_CMAKE_PATH "${GVM_METAL_COMPILER}" _uglc_metal_compiler_path)
    target_compile_definitions(UGLC PRIVATE
        ${LLVM_DEFINITIONS}
        UGLC_ENABLE_LEGACY=$<BOOL:${UGLC_ENABLE_LEGACY}>
        UGLC_ENABLE_SHADER_LINE_DIRECTIVES=$<BOOL:${UGLC_ENABLE_SHADER_LINE_DIRECTIVES}>
        UGLC_DEFAULT_METAL_COMPILER="${_uglc_metal_compiler_path}"
        CXXOPTS_NO_RTTI=1
    )

    if(MSVC)
        set_target_properties(UGLC PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
        target_compile_options(UGLC PRIVATE /W4 /external:W0 /GR- /utf-8 /Zc:preprocessor /bigobj)
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
    if(MSVC)
        if(_uglc_saved_has_msvc_runtime)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "${_uglc_saved_msvc_runtime}")
        else()
            unset(CMAKE_MSVC_RUNTIME_LIBRARY)
        endif()
        unset(_uglc_saved_has_msvc_runtime)
        unset(_uglc_saved_msvc_runtime)
    endif()
else()
    if(GVM_UGLC_EXECUTABLE AND EXISTS "${GVM_UGLC_EXECUTABLE}")
        set(GVM_UGLC_COMPILER "${GVM_UGLC_EXECUTABLE}" CACHE STRING "UGLC compiler target or executable path used by GVM DSL codegen." FORCE)
        if(NOT DEFINED GVM_UGLC_RESOURCE_DIR OR GVM_UGLC_RESOURCE_DIR STREQUAL "" OR NOT EXISTS "${GVM_UGLC_RESOURCE_DIR}/include")
            uglc_resolve_llvm_bundle()
            string(REGEX MATCH "^[0-9]+" _uglc_llvm_major "${UGLC_LLVM_VERSION}")
            if(EXISTS "${UGLC_RESOLVED_LLVM_ROOT}/lib/clang/${_uglc_llvm_major}/include")
                set(GVM_UGLC_RESOURCE_DIR "${UGLC_RESOLVED_LLVM_ROOT}/lib/clang/${_uglc_llvm_major}" CACHE PATH "Clang resource directory passed to UGLC invocations." FORCE)
            endif()
            unset(_uglc_llvm_major)
        endif()
    else()
        set(GVM_UGLC_COMPILER "" CACHE STRING "UGLC compiler target or executable path used by GVM DSL codegen." FORCE)
    endif()
endif()
