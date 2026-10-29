include_guard(GLOBAL)

include(CMakeParseArguments)

# Adds one Three.js compatibility shard with isolated Legacy and Experimental UGLC outputs.
function(gvm_add_three_sample_shard)
    set(options)
    set(oneValueArgs
        NAME
        DSL_SOURCE
        RENDERER_TYPE
        RENDERER_FACTORY
        RUNTIME_ADAPTER_HEADER
        RUNTIME_ADAPTER_TYPE)
    set(multiValueArgs HOST_SOURCES INCLUDE_DIRS DSL_DIRS)
    cmake_parse_arguments(GVM_THREE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    foreach(_required_argument
            NAME
            DSL_SOURCE
            RENDERER_TYPE
            RENDERER_FACTORY
            RUNTIME_ADAPTER_HEADER
            RUNTIME_ADAPTER_TYPE)
        if(NOT GVM_THREE_${_required_argument})
            message(FATAL_ERROR
                "gvm_add_three_sample_shard requires ${_required_argument}.")
        endif()
    endforeach()

    if(GVM_THREE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "gvm_add_three_sample_shard received unknown arguments: ${GVM_THREE_UNPARSED_ARGUMENTS}")
    endif()

    if(NOT IS_ABSOLUTE "${GVM_THREE_DSL_SOURCE}")
        get_filename_component(
            GVM_THREE_DSL_SOURCE
            "${GVM_THREE_DSL_SOURCE}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${GVM_THREE_DSL_SOURCE}")
        message(FATAL_ERROR
            "Three.js compatibility shard '${GVM_THREE_NAME}' DSL source does not exist: "
            "${GVM_THREE_DSL_SOURCE}")
    endif()

    string(MAKE_C_IDENTIFIER "${GVM_THREE_NAME}" _gvm_three_shard_identifier)
    set(_gvm_three_target_prefix "GVMThree_${_gvm_three_shard_identifier}")
    get_filename_component(_gvm_three_dsl_directory "${GVM_THREE_DSL_SOURCE}" DIRECTORY)

    set(_gvm_three_host_sources
        "${GVM_THREE_SAMPLE_SOURCE_ROOT}/Host/ThreeGeneratedApplication.cpp")
    list(APPEND _gvm_three_host_sources ${GVM_THREE_HOST_SOURCES})

    foreach(_gvm_three_pipeline legacy experimental)
        if(_gvm_three_pipeline STREQUAL "legacy")
            set(_gvm_three_pipeline_label Legacy)
            set(_gvm_three_uglc_arguments --shader-pipeline=legacy)
        else()
            set(_gvm_three_pipeline_label Experimental)
            set(_gvm_three_uglc_arguments --shader-pipeline=uglir)
        endif()

        set(_gvm_three_app_target
            "${_gvm_three_target_prefix}_${_gvm_three_pipeline_label}App")
        set(_gvm_three_host_target
            "${_gvm_three_target_prefix}_${_gvm_three_pipeline_label}")
        set(_gvm_three_generated_directory
            "${GVM_THREE_SAMPLE_GENERATED_ROOT}/${_gvm_three_pipeline}/${GVM_THREE_NAME}/UGLBin")

        add_library(${_gvm_three_app_target} STATIC ${_gvm_three_host_sources})
        target_link_libraries(${_gvm_three_app_target}
            PUBLIC
                GVMThreeHostSupport
                GVMSampleWindowManager)
        target_include_directories(${_gvm_three_app_target}
            PRIVATE
                "${GVM_THREE_SAMPLE_SOURCE_ROOT}/Host"
                "${GVM_THREE_SAMPLE_SOURCE_ROOT}"
                ${GVM_THREE_DSL_DIRS}
                ${GVM_THREE_INCLUDE_DIRS})
        target_compile_definitions(${_gvm_three_app_target}
            PRIVATE
                "GVM_THREE_RENDERER_TYPE=${GVM_THREE_RENDERER_TYPE}"
                "GVM_THREE_RENDERER_FACTORY=${GVM_THREE_RENDERER_FACTORY}"
                "GVM_THREE_RUNTIME_ADAPTER_HEADER=\"${GVM_THREE_RUNTIME_ADAPTER_HEADER}\""
                "GVM_THREE_RUNTIME_ADAPTER_TYPE=${GVM_THREE_RUNTIME_ADAPTER_TYPE}"
                "GVM_THREE_SAMPLE_NAME=\"${GVM_THREE_NAME}\"")

        UGLCOMPILE(
            ${_gvm_three_app_target}
            "${GVM_THREE_DSL_SOURCE}"
            "${_gvm_three_generated_directory}"
            "${UGL_COMPILER}"
            INCLUDE_DIRS
                "${GVM_THREE_SAMPLE_SOURCE_ROOT}"
                "${_gvm_three_dsl_directory}"
                ${GVM_THREE_INCLUDE_DIRS}
            DSL_DIRS
                "${_gvm_three_dsl_directory}"
                ${GVM_THREE_DSL_DIRS}
            UGLC_ARGS
                ${_gvm_three_uglc_arguments})

        add_executable(
            ${_gvm_three_host_target}
            "${GVM_THREE_SAMPLE_SOURCE_ROOT}/Host/main.cpp")
        target_link_libraries(${_gvm_three_host_target}
            PRIVATE
                ${_gvm_three_app_target}
                GVMThreeHostSupport)
        target_include_directories(${_gvm_three_host_target}
            PRIVATE
                "${GVM_THREE_SAMPLE_SOURCE_ROOT}/Host")
        target_compile_definitions(${_gvm_three_host_target}
            PRIVATE
                "GVM_THREE_SAMPLE_NAME=\"${GVM_THREE_NAME}\""
                "GVM_THREE_PIPELINE_NAME=\"${_gvm_three_pipeline}\"")
        set_target_properties(${_gvm_three_host_target}
            PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${GVM_THREE_SAMPLE_BINARY_ROOT}"
                OUTPUT_NAME "${GVM_THREE_NAME}-${_gvm_three_pipeline}"
                GVM_THREE_PIPELINE "${_gvm_three_pipeline}"
                GVM_THREE_SHARD "${GVM_THREE_NAME}"
                GVM_THREE_GENERATED_DIRECTORY "${_gvm_three_generated_directory}")

        if(_gvm_three_pipeline STREQUAL "legacy")
            add_dependencies(GVMThreeSamplesLegacy ${_gvm_three_host_target})
        else()
            add_dependencies(GVMThreeSamplesExperimental ${_gvm_three_host_target})
        endif()
        add_dependencies(GVMThreeSamplesAll ${_gvm_three_host_target})
    endforeach()
endfunction()
