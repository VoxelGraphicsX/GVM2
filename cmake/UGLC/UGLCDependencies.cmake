include_guard(GLOBAL)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../CPM.cmake")

option(UGLC_ENABLE_SHADER_LINE_DIRECTIVES "Enable #line directives inside shader emitters." OFF)

set(UGLC_DXC_GIT_REPOSITORY "https://github.com/microsoft/DirectXShaderCompiler.git" CACHE STRING "DirectXShaderCompiler git repository used when no local source checkout is provided.")
set(UGLC_DXC_GIT_TAG "v1.8.2505.1" CACHE STRING "Pinned DirectXShaderCompiler git tag used when no local source checkout is provided.")
set(UGLC_DXC_VERSION "1.8.2505.1" CACHE STRING "Pinned DirectXShaderCompiler package version used by CPM metadata.")
set(UGLC_DXC_PREBUILT_LIBRARY "" CACHE FILEPATH "Optional prebuilt dxcompiler shared library used by UGLC instead of building DirectXShaderCompiler in this build tree.")
set(UGLC_DXC_PREBUILT_INCLUDE_DIR "" CACHE PATH "Optional DirectXShaderCompiler include directory paired with UGLC_DXC_PREBUILT_LIBRARY.")
set(UGLC_DXC_LLVM_TARGETS_TO_BUILD "None" CACHE STRING "LLVM targets requested when configuring DirectXShaderCompiler through CPM. Use None for the trimmed DXC toolchain.")
set_property(CACHE UGLC_DXC_LLVM_TARGETS_TO_BUILD PROPERTY STRINGS None)
set(UGLC_DXC_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type used for the external DXC host toolchain that UGLC vendors through CPM.")
set_property(CACHE UGLC_DXC_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
option(UGLC_DXC_ENABLE_ASSERTIONS "Enable LLVM/DXC assertions inside the external DXC build used by UGLC." OFF)

function(_uglc_configure_dxc_prebuilt prebuilt_library prebuilt_include_dir)
    if(NOT EXISTS "${prebuilt_library}")
        message(FATAL_ERROR "UGLC_DXC_PREBUILT_LIBRARY does not exist: ${prebuilt_library}")
    endif()
    if(NOT EXISTS "${prebuilt_include_dir}/dxc/dxcapi.h")
        message(FATAL_ERROR "UGLC_DXC_PREBUILT_INCLUDE_DIR must contain dxc/dxcapi.h: ${prebuilt_include_dir}")
    endif()

    if(NOT TARGET UGLCDxcBinary)
        add_library(UGLCDxcBinary SHARED IMPORTED GLOBAL)
        set_target_properties(UGLCDxcBinary PROPERTIES
            IMPORTED_LOCATION "${prebuilt_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${prebuilt_include_dir}"
        )
    endif()

    if(NOT TARGET UGLCDxcBridge)
        add_library(UGLCDxcBridge INTERFACE)
        target_include_directories(UGLCDxcBridge INTERFACE "${prebuilt_include_dir}")
        target_link_libraries(UGLCDxcBridge INTERFACE UGLCDxcBinary)
    endif()

    get_filename_component(_uglc_dxc_runtime_dir "${prebuilt_library}" DIRECTORY)
    get_filename_component(_uglc_dxc_install_root "${_uglc_dxc_runtime_dir}" DIRECTORY)

    set(UGLC_DXC_BUILD_TARGET "" PARENT_SCOPE)
    set(UGLC_DXC_SOURCE_ROOT "" PARENT_SCOPE)
    set(UGLC_DXC_INSTALL_ROOT "${_uglc_dxc_install_root}" PARENT_SCOPE)
    set(UGLC_DXC_RUNTIME_DIR "${_uglc_dxc_runtime_dir}" PARENT_SCOPE)
    set(UGLC_DXC_TARGET UGLCDxcBridge PARENT_SCOPE)
endfunction()

function(_uglc_configure_dxc_external_project dxc_source_dir)
    set(_uglc_dxc_build_type "${UGLC_DXC_BUILD_TYPE}")
    if(_uglc_dxc_build_type STREQUAL "")
        set(_uglc_dxc_build_type "RelWithDebInfo")
    endif()

    set(_uglc_dxc_llvm_targets "${UGLC_DXC_LLVM_TARGETS_TO_BUILD}")
    string(STRIP "${_uglc_dxc_llvm_targets}" _uglc_dxc_llvm_targets)
    if(_uglc_dxc_llvm_targets STREQUAL "")
        set(_uglc_dxc_llvm_targets "None")
    endif()
    if(NOT _uglc_dxc_llvm_targets STREQUAL "None")
        message(WARNING
            "UGLC external DXC uses the pinned HLSL DXC fork with backend target directories removed; "
            "overriding UGLC_DXC_LLVM_TARGETS_TO_BUILD='${UGLC_DXC_LLVM_TARGETS_TO_BUILD}' with 'None' "
            "to avoid llvm-build invalid target errors."
        )
        set(_uglc_dxc_llvm_targets "None")
    endif()

    set(_uglc_dxc_build_dir "${FETCHCONTENT_BASE_DIR}/dxcompiler-ext-build")
    set(_uglc_dxc_install_dir "${FETCHCONTENT_BASE_DIR}/dxcompiler-ext-install")

    if(WIN32)
        set(_uglc_dxc_library_path "${_uglc_dxc_install_dir}/bin/dxcompiler.dll")
        set(_uglc_dxc_import_library_path "${_uglc_dxc_install_dir}/lib/dxcompiler.lib")
        set(_uglc_dxc_runtime_dir "${_uglc_dxc_install_dir}/bin")
    else()
        set(_uglc_dxc_library_path "${_uglc_dxc_install_dir}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}dxcompiler${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(_uglc_dxc_import_library_path "")
        set(_uglc_dxc_runtime_dir "${_uglc_dxc_install_dir}/lib")
    endif()

    set(_uglc_dxc_cmake_args
        -DCMAKE_INSTALL_PREFIX:PATH=${_uglc_dxc_install_dir}
        -DCMAKE_BUILD_TYPE:STRING=${_uglc_dxc_build_type}
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
        -DLLVM_DEFAULT_TARGET_TRIPLE:STRING=dxil-ms-dx
        -DLLVM_ENABLE_EH:BOOL=ON
        -DLLVM_ENABLE_RTTI:BOOL=ON
        -DLLVM_ENABLE_ASSERTIONS:BOOL=${UGLC_DXC_ENABLE_ASSERTIONS}
        -DLLVM_OPTIMIZED_TABLEGEN:BOOL=OFF
        -DLLVM_TARGETS_TO_BUILD:STRING=${_uglc_dxc_llvm_targets}
        -DENABLE_SPIRV_CODEGEN:BOOL=ON
        -DSPIRV_BUILD_TESTS:BOOL=OFF
        -DLLVM_INCLUDE_DOCS:BOOL=OFF
        -DLLVM_INCLUDE_EXAMPLES:BOOL=OFF
        -DLLVM_INCLUDE_TESTS:BOOL=OFF
        -DLLVM_BUILD_TESTS:BOOL=OFF
        -DLIBCLANG_BUILD_STATIC:BOOL=ON
        -DCLANG_BUILD_EXAMPLES:BOOL=OFF
        -DCLANG_CL:BOOL=OFF
        -DCLANG_ENABLE_ARCMT:BOOL=OFF
        -DCLANG_ENABLE_STATIC_ANALYZER:BOOL=OFF
        -DCLANG_INCLUDE_TESTS:BOOL=OFF
        -DHLSL_INCLUDE_TESTS:BOOL=OFF
        -DHLSL_BUILD_DXILCONV:BOOL=OFF
        -DHLSL_DISABLE_SOURCE_GENERATION:BOOL=ON
        -DHLSL_SUPPORT_QUERY_GIT_COMMIT_INFO:BOOL=OFF
        -DLLVM_ENABLE_TERMINFO:BOOL=OFF
        -DLLVM_APPEND_VC_REV:BOOL=OFF
    )

    set(_uglc_dxc_cxx_flags "${CMAKE_CXX_FLAGS}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-Werror=invalid-specialization" UGLC_HAS_INVALID_SPECIALIZATION_WARNING)
        if(UGLC_HAS_INVALID_SPECIALIZATION_WARNING)
            string(APPEND _uglc_dxc_cxx_flags " -Wno-error=invalid-specialization")
        endif()
    endif()
    if(NOT _uglc_dxc_cxx_flags STREQUAL "")
        list(APPEND _uglc_dxc_cmake_args
            -DCMAKE_CXX_FLAGS:STRING=${_uglc_dxc_cxx_flags}
        )
    endif()

    if(CMAKE_MAKE_PROGRAM)
        list(APPEND _uglc_dxc_cmake_args
            -DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}
        )
    endif()

    if(APPLE)
        set(_uglc_dxc_osx_sysroot "${CMAKE_OSX_SYSROOT}")
        if(_uglc_dxc_osx_sysroot STREQUAL "" OR NOT EXISTS "${_uglc_dxc_osx_sysroot}")
            execute_process(
                COMMAND xcrun --sdk macosx --show-sdk-path
                OUTPUT_VARIABLE _uglc_detected_osx_sysroot
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _uglc_detected_osx_sysroot_result
            )
            if(_uglc_detected_osx_sysroot_result EQUAL 0 AND EXISTS "${_uglc_detected_osx_sysroot}")
                set(_uglc_dxc_osx_sysroot "${_uglc_detected_osx_sysroot}")
            endif()
        endif()
        if(CMAKE_OSX_ARCHITECTURES)
            list(APPEND _uglc_dxc_cmake_args
                -DCMAKE_OSX_ARCHITECTURES:STRING=${CMAKE_OSX_ARCHITECTURES}
            )
        endif()
        if(CMAKE_OSX_DEPLOYMENT_TARGET)
            list(APPEND _uglc_dxc_cmake_args
                -DCMAKE_OSX_DEPLOYMENT_TARGET:STRING=${CMAKE_OSX_DEPLOYMENT_TARGET}
            )
        endif()
        if(_uglc_dxc_osx_sysroot)
            list(APPEND _uglc_dxc_cmake_args
                -DCMAKE_OSX_SYSROOT:STRING=${_uglc_dxc_osx_sysroot}
            )
        endif()
        unset(_uglc_dxc_osx_sysroot)
        unset(_uglc_detected_osx_sysroot)
        unset(_uglc_detected_osx_sysroot_result)
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        set(_uglc_dxc_build_command
            ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target dxcompiler
        )
        set(_uglc_dxc_install_command
            ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target install-dxcompiler
        )
    else()
        set(_uglc_dxc_build_command
            ${CMAKE_COMMAND} --build <BINARY_DIR> --target dxcompiler
        )
        set(_uglc_dxc_install_command
            ${CMAKE_COMMAND} --build <BINARY_DIR> --target install-dxcompiler
        )
    endif()

    ExternalProject_Add(UGLCDxcExternal
        SOURCE_DIR "${dxc_source_dir}"
        BINARY_DIR "${_uglc_dxc_build_dir}"
        INSTALL_DIR "${_uglc_dxc_install_dir}"
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        TEST_COMMAND ""
        CMAKE_ARGS ${_uglc_dxc_cmake_args}
        BUILD_COMMAND ${_uglc_dxc_build_command}
        INSTALL_COMMAND ${_uglc_dxc_install_command}
        BUILD_BYPRODUCTS "${_uglc_dxc_library_path}"
        USES_TERMINAL_CONFIGURE TRUE
        USES_TERMINAL_BUILD TRUE
        USES_TERMINAL_INSTALL TRUE
    )

    add_library(UGLCDxcBinary SHARED IMPORTED GLOBAL)
    if(WIN32)
        set_target_properties(UGLCDxcBinary PROPERTIES
            IMPORTED_LOCATION "${_uglc_dxc_library_path}"
            IMPORTED_IMPLIB "${_uglc_dxc_import_library_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${dxc_source_dir}/include"
        )
    else()
        set_target_properties(UGLCDxcBinary PROPERTIES
            IMPORTED_LOCATION "${_uglc_dxc_library_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${dxc_source_dir}/include"
        )
    endif()

    add_library(UGLCDxcBridge INTERFACE)
    target_include_directories(UGLCDxcBridge INTERFACE "${dxc_source_dir}/include")
    target_link_libraries(UGLCDxcBridge INTERFACE UGLCDxcBinary)
    add_dependencies(UGLCDxcBridge UGLCDxcExternal)

    set(UGLC_DXC_BUILD_TARGET UGLCDxcExternal PARENT_SCOPE)
    set(UGLC_DXC_SOURCE_ROOT "${dxc_source_dir}" PARENT_SCOPE)
    set(UGLC_DXC_INSTALL_ROOT "${_uglc_dxc_install_dir}" PARENT_SCOPE)
    set(UGLC_DXC_RUNTIME_DIR "${_uglc_dxc_runtime_dir}" PARENT_SCOPE)
    set(UGLC_DXC_TARGET UGLCDxcBridge PARENT_SCOPE)
endfunction()

function(uglc_configure_dependencies)
    message(STATUS "UGLC HLSL backend: ${UGLC_ENABLE_LEGACY}")
    message(STATUS "UGLC DXC compiler service: ${UGLC_ENABLE_LEGACY}")
    message(STATUS "UGLC shader line directives: ${UGLC_ENABLE_SHADER_LINE_DIRECTIVES}")
    message(STATUS "UGLC external DXC build type: ${UGLC_DXC_BUILD_TYPE}")
    message(STATUS "UGLC external DXC assertions: ${UGLC_DXC_ENABLE_ASSERTIONS}")
    if(UGLC_DXC_PREBUILT_LIBRARY)
        message(STATUS "UGLC prebuilt DXC library: ${UGLC_DXC_PREBUILT_LIBRARY}")
        message(STATUS "UGLC prebuilt DXC includes: ${UGLC_DXC_PREBUILT_INCLUDE_DIR}")
    endif()

    CPMAddPackage(
        NAME cxxopts
        URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.3.1.tar.gz
        URL_HASH SHA256=3bfc70542c521d4b55a46429d808178916a579b28d048bd8c727ee76c39e2072
        OPTIONS
            "CXXOPTS_BUILD_EXAMPLES OFF"
            "CXXOPTS_BUILD_TESTS OFF"
            "CXXOPTS_ENABLE_INSTALL OFF"
    )

    if(TARGET cxxopts::cxxopts)
        set(_uglc_cxxopts_target cxxopts::cxxopts)
    elseif(TARGET cxxopts)
        set(_uglc_cxxopts_target cxxopts)
    else()
        message(FATAL_ERROR "UGLC expected a cxxopts target after resolving cxxopts v3.3.1 through CPM.")
    endif()

    set(_uglc_nlohmann_json_target "")
    set(_uglc_spirv_headers_target "")
    set(_uglc_spirv_tools_target "")
    set(_uglc_spirv_tools_opt_target "")
        message(STATUS "UGLC UGLIR JSON dump dependency: nlohmann/json v3.12.0")
        CPMAddPackage(
            NAME nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
            URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
            OPTIONS
                "JSON_BuildTests OFF"
                "JSON_Install OFF"
        )

        if(TARGET nlohmann_json::nlohmann_json)
            set(_uglc_nlohmann_json_target nlohmann_json::nlohmann_json)
        elseif(TARGET nlohmann_json)
            set(_uglc_nlohmann_json_target nlohmann_json)
        else()
            message(FATAL_ERROR "UGLC expected a nlohmann_json target after resolving nlohmann/json v3.12.0 through CPM.")
        endif()

        message(STATUS "UGLC UGLIR SPIR-V dependency: SPIRV-Headers vulkan-sdk-1.4.304.0")
        CPMAddPackage(
            NAME SPIRV-Headers
            URL https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.304.0.tar.gz
            URL_HASH SHA256=162b864ebaf339d66953fc2c4ad974bc4f453e0f04155cd3755a85e33f408eee
            OPTIONS
                "SPIRV_HEADERS_ENABLE_TESTS OFF"
                "SPIRV_HEADERS_ENABLE_INSTALL OFF"
        )

        if(TARGET SPIRV-Headers::SPIRV-Headers)
            set(_uglc_spirv_headers_target SPIRV-Headers::SPIRV-Headers)
        elseif(TARGET SPIRV-Headers)
            set(_uglc_spirv_headers_target SPIRV-Headers)
        else()
            message(FATAL_ERROR "UGLC expected a SPIRV-Headers target after resolving SPIRV-Headers vulkan-sdk-1.4.304.0 through CPM.")
        endif()

        if(NOT DEFINED SPIRV-Headers_SOURCE_DIR OR SPIRV-Headers_SOURCE_DIR STREQUAL "")
            message(FATAL_ERROR "UGLC expected CPM to expose SPIRV-Headers_SOURCE_DIR.")
        endif()
        set(SPIRV_HEADER_INCLUDE_DIR "${SPIRV-Headers_SOURCE_DIR}/include" CACHE PATH "SPIRV-Tools grammar and header include directory." FORCE)

        message(STATUS "UGLC UGLIR SPIR-V dependency: SPIRV-Tools vulkan-sdk-1.4.304.0")
        CPMAddPackage(
            NAME SPIRV-Tools
            URL https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-1.4.304.0.tar.gz
            URL_HASH SHA256=ad6e8922538c498e7131bcd82a8d6d9f9863b8d7431c5bfa27dd98e26435be07
            OPTIONS
                "SPIRV_SKIP_EXECUTABLES ON"
                "SPIRV_SKIP_TESTS ON"
                "SPIRV_TOOLS_BUILD_STATIC ON"
                "SKIP_SPIRV_TOOLS_INSTALL ON"
                "SPIRV_WERROR OFF"
                "SPIRV_WARN_EVERYTHING OFF"
        )

        if(TARGET SPIRV-Tools-static)
            set(_uglc_spirv_tools_target SPIRV-Tools-static)
        elseif(TARGET SPIRV-Tools)
            set(_uglc_spirv_tools_target SPIRV-Tools)
        else()
            message(FATAL_ERROR "UGLC expected a SPIRV-Tools target after resolving SPIRV-Tools vulkan-sdk-1.4.304.0 through CPM.")
        endif()

        if(TARGET SPIRV-Tools-opt)
            set(_uglc_spirv_tools_opt_target SPIRV-Tools-opt)
        else()
            message(FATAL_ERROR "UGLC expected a SPIRV-Tools-opt target after resolving SPIRV-Tools vulkan-sdk-1.4.304.0 through CPM.")
        endif()



    if(UGLC_ENABLE_LEGACY)
        if(UGLC_DXC_PREBUILT_LIBRARY)
            _uglc_configure_dxc_prebuilt("${UGLC_DXC_PREBUILT_LIBRARY}" "${UGLC_DXC_PREBUILT_INCLUDE_DIR}")
            set(UGLC_DXC_BUILD_TARGET "${UGLC_DXC_BUILD_TARGET}" PARENT_SCOPE)
            set(UGLC_DXC_SOURCE_ROOT "${UGLC_DXC_SOURCE_ROOT}" PARENT_SCOPE)
            set(UGLC_DXC_INSTALL_ROOT "${UGLC_DXC_INSTALL_ROOT}" PARENT_SCOPE)
            set(UGLC_DXC_RUNTIME_DIR "${UGLC_DXC_RUNTIME_DIR}" PARENT_SCOPE)
            set(UGLC_DXC_TARGET "${UGLC_DXC_TARGET}" PARENT_SCOPE)
            set(UGLC_CXXOPTS_TARGET "${_uglc_cxxopts_target}" PARENT_SCOPE)
            set(UGLC_NLOHMANN_JSON_TARGET "${_uglc_nlohmann_json_target}" PARENT_SCOPE)
            set(UGLC_SPIRV_HEADERS_TARGET "${_uglc_spirv_headers_target}" PARENT_SCOPE)
            set(UGLC_SPIRV_TOOLS_TARGET "${_uglc_spirv_tools_target}" PARENT_SCOPE)
            set(UGLC_SPIRV_TOOLS_OPT_TARGET "${_uglc_spirv_tools_opt_target}" PARENT_SCOPE)
            return()
        endif()

        CPMAddPackage(
            NAME dxcompiler
            GIT_REPOSITORY "${UGLC_DXC_GIT_REPOSITORY}"
            GIT_TAG "${UGLC_DXC_GIT_TAG}"
            VERSION "${UGLC_DXC_VERSION}"
            GIT_SHALLOW TRUE
            GIT_SUBMODULES
                external/DirectX-Headers
                external/SPIRV-Headers
                external/SPIRV-Tools
            GIT_SUBMODULES_RECURSE FALSE
            DOWNLOAD_ONLY YES
        )

        if(NOT DEFINED dxcompiler_SOURCE_DIR OR dxcompiler_SOURCE_DIR STREQUAL "")
            message(FATAL_ERROR "UGLC expected CPM to resolve a DirectXShaderCompiler source directory.")
        endif()

        _uglc_configure_dxc_external_project("${dxcompiler_SOURCE_DIR}")
        set(UGLC_DXC_BUILD_TARGET "${UGLC_DXC_BUILD_TARGET}" PARENT_SCOPE)
        set(UGLC_DXC_SOURCE_ROOT "${UGLC_DXC_SOURCE_ROOT}" PARENT_SCOPE)
        set(UGLC_DXC_INSTALL_ROOT "${UGLC_DXC_INSTALL_ROOT}" PARENT_SCOPE)
        set(UGLC_DXC_RUNTIME_DIR "${UGLC_DXC_RUNTIME_DIR}" PARENT_SCOPE)
        set(UGLC_DXC_TARGET "${UGLC_DXC_TARGET}" PARENT_SCOPE)
    else()
        set(UGLC_DXC_BUILD_TARGET "" PARENT_SCOPE)
        set(UGLC_DXC_SOURCE_ROOT "" PARENT_SCOPE)
        set(UGLC_DXC_INSTALL_ROOT "" PARENT_SCOPE)
        set(UGLC_DXC_RUNTIME_DIR "" PARENT_SCOPE)
        set(UGLC_DXC_TARGET "" PARENT_SCOPE)
    endif()

    set(UGLC_CXXOPTS_TARGET "${_uglc_cxxopts_target}" PARENT_SCOPE)
    set(UGLC_NLOHMANN_JSON_TARGET "${_uglc_nlohmann_json_target}" PARENT_SCOPE)
    set(UGLC_SPIRV_HEADERS_TARGET "${_uglc_spirv_headers_target}" PARENT_SCOPE)
    set(UGLC_SPIRV_TOOLS_TARGET "${_uglc_spirv_tools_target}" PARENT_SCOPE)
    set(UGLC_SPIRV_TOOLS_OPT_TARGET "${_uglc_spirv_tools_opt_target}" PARENT_SCOPE)
endfunction()
