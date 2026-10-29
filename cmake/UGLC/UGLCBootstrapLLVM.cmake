include_guard(GLOBAL)

set(UGLC_LLVM_VERSION "20.1.7" CACHE STRING "Pinned prebuilt LLVM release version used by UGLC.")
set(UGLC_LLVM_ROOT "" CACHE PATH "Explicit LLVM toolchain root used by UGLC.")
set(UGLC_LLVM_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/llvm_bundles.json" CACHE FILEPATH "Manifest describing supported prebuilt LLVM bundles.")
if(DEFINED GVM_DEPENDENCY_CACHE_ROOT AND NOT GVM_DEPENDENCY_CACHE_ROOT STREQUAL "")
    set(_UGLC_LLVM_CACHE_DIR_DEFAULT "${GVM_DEPENDENCY_CACHE_ROOT}/uglc/llvm")
else()
    set(_UGLC_LLVM_CACHE_DIR_DEFAULT "${CMAKE_BINARY_DIR}/_deps/llvm")
endif()
set(UGLC_LLVM_CACHE_DIR "${_UGLC_LLVM_CACHE_DIR_DEFAULT}" CACHE PATH "Cache directory used for downloaded LLVM bundles.")
unset(_UGLC_LLVM_CACHE_DIR_DEFAULT)
set(UGLC_BOOTSTRAP_PYTHON "" CACHE FILEPATH "Python interpreter used to bootstrap LLVM downloads.")
set(UGLC_BOOTSTRAP_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/scripts/bootstrap_llvm.py" CACHE FILEPATH "Script used to bootstrap LLVM downloads.")
option(UGLC_LLVM_AUTO_DOWNLOAD "Automatically download a matching prebuilt LLVM toolchain when no local bundle is available." ON)

function(_uglc_validate_llvm_root llvm_root out_valid)
    set(_uglc_valid TRUE)

    if(NOT IS_DIRECTORY "${llvm_root}")
        set(_uglc_valid FALSE)
    endif()

    foreach(_uglc_required_path
        "lib/cmake/llvm/LLVMConfig.cmake"
        "lib/cmake/clang/ClangConfig.cmake"
    )
        if(_uglc_valid AND NOT EXISTS "${llvm_root}/${_uglc_required_path}")
            set(_uglc_valid FALSE)
        endif()
    endforeach()

    if(_uglc_valid)
        if(WIN32)
            if(EXISTS "${llvm_root}/bin/clang-cl.exe")
                set(_uglc_compiler_ok TRUE)
            elseif(EXISTS "${llvm_root}/bin/clang.exe" AND EXISTS "${llvm_root}/bin/clang++.exe")
                set(_uglc_compiler_ok TRUE)
            else()
                set(_uglc_compiler_ok FALSE)
            endif()
        else()
            set(_uglc_compiler_ok TRUE)
            foreach(_uglc_required_compiler "bin/clang" "bin/clang++")
                if(NOT EXISTS "${llvm_root}/${_uglc_required_compiler}")
                    set(_uglc_compiler_ok FALSE)
                endif()
            endforeach()
        endif()

        if(NOT _uglc_compiler_ok)
            set(_uglc_valid FALSE)
        endif()
    endif()

    set(${out_valid} "${_uglc_valid}" PARENT_SCOPE)
endfunction()

function(_uglc_select_llvm_compilers llvm_root out_c_compiler out_cxx_compiler)
    if(WIN32)
        if(EXISTS "${llvm_root}/bin/clang-cl.exe")
            set(_uglc_c_compiler "${llvm_root}/bin/clang-cl.exe")
            set(_uglc_cxx_compiler "${llvm_root}/bin/clang-cl.exe")
        elseif(EXISTS "${llvm_root}/bin/clang.exe" AND EXISTS "${llvm_root}/bin/clang++.exe")
            set(_uglc_c_compiler "${llvm_root}/bin/clang.exe")
            set(_uglc_cxx_compiler "${llvm_root}/bin/clang++.exe")
        else()
            message(FATAL_ERROR "UGLC could not determine a usable Windows compiler pair inside LLVM root: ${llvm_root}")
        endif()
    else()
        set(_uglc_c_compiler "${llvm_root}/bin/clang")
        set(_uglc_cxx_compiler "${llvm_root}/bin/clang++")
    endif()

    set(${out_c_compiler} "${_uglc_c_compiler}" PARENT_SCOPE)
    set(${out_cxx_compiler} "${_uglc_cxx_compiler}" PARENT_SCOPE)
endfunction()

function(_uglc_find_bootstrap_python out_var)
    if(NOT UGLC_BOOTSTRAP_PYTHON STREQUAL "")
        set(${out_var} "${UGLC_BOOTSTRAP_PYTHON}" PARENT_SCOPE)
        return()
    endif()

    if(DEFINED Python3_EXECUTABLE AND NOT Python3_EXECUTABLE STREQUAL "")
        set(${out_var} "${Python3_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    find_program(_uglc_python_executable NAMES python3 python)
    if(_uglc_python_executable)
        set(${out_var} "${_uglc_python_executable}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(uglc_resolve_llvm_bundle)
    set(_uglc_resolved_root "")
    set(_uglc_resolved_source "")
    set(_uglc_bootstrap_python "")
    set(_uglc_cached_root "")
    set(_uglc_cached_root_valid FALSE)

    if(NOT UGLC_LLVM_ROOT STREQUAL "")
        _uglc_validate_llvm_root("${UGLC_LLVM_ROOT}" _uglc_explicit_root_valid)
        if(NOT _uglc_explicit_root_valid)
            message(FATAL_ERROR
                "UGLC_LLVM_ROOT points to an invalid LLVM bundle: ${UGLC_LLVM_ROOT}\n"
                "Expected bin/clang[++] (or clang-cl on Windows) plus lib/cmake/{llvm,clang}."
            )
        endif()
        set(_uglc_resolved_root "${UGLC_LLVM_ROOT}")
        set(_uglc_resolved_source "explicit UGLC_LLVM_ROOT")
    else()
        if(UGLC_LLVM_AUTO_DOWNLOAD AND EXISTS "${UGLC_LLVM_MANIFEST}")
            _uglc_find_bootstrap_python(_uglc_bootstrap_python)
            if(NOT _uglc_bootstrap_python STREQUAL "")
                execute_process(
                    COMMAND
                        "${_uglc_bootstrap_python}"
                        "${UGLC_BOOTSTRAP_SCRIPT}"
                        --manifest "${UGLC_LLVM_MANIFEST}"
                        --version "${UGLC_LLVM_VERSION}"
                        --cache-dir "${UGLC_LLVM_CACHE_DIR}"
                        --dry-run
                        --print-root
                    WORKING_DIRECTORY "${GVM_REPOSITORY_ROOT}"
                    RESULT_VARIABLE _uglc_cached_root_result
                    OUTPUT_VARIABLE _uglc_cached_root_output
                    ERROR_QUIET
                )
                if(_uglc_cached_root_result EQUAL 0)
                    string(STRIP "${_uglc_cached_root_output}" _uglc_cached_root)
                    if(NOT _uglc_cached_root STREQUAL "")
                        _uglc_validate_llvm_root("${_uglc_cached_root}" _uglc_cached_root_valid)
                    endif()
                endif()
            endif()
        endif()

        if(_uglc_cached_root_valid)
            set(_uglc_resolved_root "${_uglc_cached_root}")
            set(_uglc_resolved_source "cached downloaded bundle")
        elseif(UGLC_LLVM_AUTO_DOWNLOAD)
            if(NOT EXISTS "${UGLC_LLVM_MANIFEST}")
                message(FATAL_ERROR "UGLC LLVM manifest not found: ${UGLC_LLVM_MANIFEST}")
            endif()
            if(NOT EXISTS "${UGLC_BOOTSTRAP_SCRIPT}")
                message(FATAL_ERROR "UGLC LLVM bootstrap script not found: ${UGLC_BOOTSTRAP_SCRIPT}")
            endif()

            if(_uglc_bootstrap_python STREQUAL "")
                message(FATAL_ERROR
                    "UGLC could not find a Python interpreter to bootstrap LLVM downloads.\n"
                    "Set UGLC_BOOTSTRAP_PYTHON or provide UGLC_LLVM_ROOT manually."
                )
            endif()

            execute_process(
                COMMAND
                    "${_uglc_bootstrap_python}"
                    "${UGLC_BOOTSTRAP_SCRIPT}"
                    --manifest "${UGLC_LLVM_MANIFEST}"
                    --version "${UGLC_LLVM_VERSION}"
                    --cache-dir "${UGLC_LLVM_CACHE_DIR}"
                    --print-root
                WORKING_DIRECTORY "${GVM_REPOSITORY_ROOT}"
                RESULT_VARIABLE _uglc_bootstrap_result
                OUTPUT_VARIABLE _uglc_bootstrap_output
                ERROR_VARIABLE _uglc_bootstrap_error
            )

            if(NOT _uglc_bootstrap_result EQUAL 0)
                message(FATAL_ERROR
                    "UGLC failed to resolve/download LLVM ${UGLC_LLVM_VERSION}.\n"
                    "${_uglc_bootstrap_error}"
                )
            endif()

            string(STRIP "${_uglc_bootstrap_error}" _uglc_bootstrap_error)
            if(NOT _uglc_bootstrap_error STREQUAL "")
                string(REPLACE "\n" "\n-- " _uglc_bootstrap_log "${_uglc_bootstrap_error}")
                message(STATUS "UGLC LLVM bootstrap log:\n-- ${_uglc_bootstrap_log}")
            endif()

            string(STRIP "${_uglc_bootstrap_output}" _uglc_bootstrap_output)
            if(_uglc_bootstrap_output STREQUAL "")
                message(FATAL_ERROR "UGLC LLVM bootstrap finished without returning an install root.")
            endif()

            _uglc_validate_llvm_root("${_uglc_bootstrap_output}" _uglc_downloaded_root_valid)
            if(NOT _uglc_downloaded_root_valid)
                message(FATAL_ERROR
                    "UGLC downloaded an invalid LLVM bundle: ${_uglc_bootstrap_output}\n"
                    "Expected bin/clang[++] (or clang-cl on Windows) plus lib/cmake/{llvm,clang}."
                )
            endif()

            set(_uglc_resolved_root "${_uglc_bootstrap_output}")
            set(_uglc_resolved_source "downloaded cache")
        else()
            message(FATAL_ERROR
                "UGLC could not locate a usable LLVM bundle.\n"
                "Provide UGLC_LLVM_ROOT or enable UGLC_LLVM_AUTO_DOWNLOAD."
            )
        endif()
    endif()

    _uglc_select_llvm_compilers("${_uglc_resolved_root}" _uglc_c_compiler _uglc_cxx_compiler)

    set(UGLC_RESOLVED_LLVM_ROOT "${_uglc_resolved_root}" PARENT_SCOPE)
    set(UGLC_RESOLVED_LLVM_SOURCE "${_uglc_resolved_source}" PARENT_SCOPE)
    set(UGLC_RESOLVED_LLVM_C_COMPILER "${_uglc_c_compiler}" PARENT_SCOPE)
    set(UGLC_RESOLVED_LLVM_CXX_COMPILER "${_uglc_cxx_compiler}" PARENT_SCOPE)
endfunction()
