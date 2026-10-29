include(CMakeParseArguments)

function(gvm_require_uglc_compiler CALLER)
    if(NOT GVM_UGLC_COMPILER_AVAILABLE)
        message(FATAL_ERROR "${CALLER} requires a valid GVM_UGLC_COMPILER target or executable path.")
    endif()
endfunction()

function(gvm_add_gtest TARGET)
    set(options RUN_SERIAL NO_GTEST_MAIN)
    set(oneValueArgs TIMEOUT)
    set(multiValueArgs SOURCES LABELS INCLUDE_DIRS LINK_LIBS ENVIRONMENT)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "gvm_add_gtest(${TARGET}) requires at least one source file.")
    endif()

    add_executable(${TARGET} ${ARG_SOURCES})
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    if(ARG_NO_GTEST_MAIN)
        target_link_libraries(${TARGET} PRIVATE GTest::gtest GVMTestCommon ${ARG_LINK_LIBS})
    else()
        target_link_libraries(${TARGET} PRIVATE GTest::gtest_main GVMTestCommon ${ARG_LINK_LIBS})
    endif()
    set_target_properties(${TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${GVM_TEST_BINARY_ROOT}"
    )

    if(ARG_INCLUDE_DIRS)
        target_include_directories(${TARGET} PRIVATE ${ARG_INCLUDE_DIRS})
    endif()
endfunction()

function(gvm_add_dsl_codegen_test TARGET DSL_SOURCE)
    gvm_require_uglc_compiler("gvm_add_dsl_codegen_test(${TARGET})")

    set(options RUN_SERIAL)
    set(oneValueArgs TIMEOUT)
    set(multiValueArgs SOURCES LABELS INCLUDE_DIRS LINK_LIBS UGL_INCLUDE_DIRS UGL_DSL_DIRS UGLC_ARGS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(_generated_dir "${GVM_TEST_GENERATED_ROOT}/${TARGET}")
    get_filename_component(_dsl_source_dir "${DSL_SOURCE}" DIRECTORY)
    set(_ugl_include_dirs ${ARG_UGL_INCLUDE_DIRS} "${_dsl_source_dir}")
    set(_ugl_dsl_dirs ${ARG_UGL_DSL_DIRS} "${_dsl_source_dir}")
    list(REMOVE_DUPLICATES _ugl_include_dirs)
    list(REMOVE_DUPLICATES _ugl_dsl_dirs)
    set(_optional_args "")
    if(ARG_RUN_SERIAL)
        list(APPEND _optional_args RUN_SERIAL)
    endif()
    if(ARG_TIMEOUT)
        list(APPEND _optional_args TIMEOUT "${ARG_TIMEOUT}")
    endif()

    gvm_add_gtest(${TARGET}
        SOURCES ${ARG_SOURCES}
        LABELS ${ARG_LABELS}
        INCLUDE_DIRS ${ARG_INCLUDE_DIRS}
        LINK_LIBS ${ARG_LINK_LIBS}
        ENVIRONMENT
            "GVM_TEST_DSL_GENERATED_DIR=${_generated_dir}"
            "GVM_TEST_DSL_SOURCE=${DSL_SOURCE}"
        ${_optional_args}
    )

    # Consume the generated host header as emitted.
    UGLCOMPILE(${TARGET} ${DSL_SOURCE} ${_generated_dir} ${GVM_UGLC_COMPILER}
        INCLUDE_DIRS ${_ugl_include_dirs}
        DSL_DIRS ${_ugl_dsl_dirs}
        UGLC_ARGS ${ARG_UGLC_ARGS}
    )
endfunction()

function(gvm_add_dsl_runtime_gtest TARGET DSL_SOURCE)
    gvm_require_uglc_compiler("gvm_add_dsl_runtime_gtest(${TARGET})")

    set(options RUN_SERIAL NO_GTEST_MAIN)
    set(oneValueArgs TIMEOUT)
    set(multiValueArgs SOURCES LABELS INCLUDE_DIRS LINK_LIBS UGL_INCLUDE_DIRS UGL_DSL_DIRS UGLC_ARGS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(_generated_dir "${GVM_TEST_GENERATED_ROOT}/${TARGET}")
    get_filename_component(_dsl_source_dir "${DSL_SOURCE}" DIRECTORY)
    set(_ugl_include_dirs ${ARG_UGL_INCLUDE_DIRS} "${_dsl_source_dir}")
    set(_ugl_dsl_dirs ${ARG_UGL_DSL_DIRS} "${_dsl_source_dir}")
    list(REMOVE_DUPLICATES _ugl_include_dirs)
    list(REMOVE_DUPLICATES _ugl_dsl_dirs)
    set(_include_dirs ${ARG_INCLUDE_DIRS} "${_generated_dir}")
    set(_optional_args "")
    if(ARG_RUN_SERIAL)
        list(APPEND _optional_args RUN_SERIAL)
    endif()
    if(ARG_NO_GTEST_MAIN)
        list(APPEND _optional_args NO_GTEST_MAIN)
    endif()
    if(ARG_TIMEOUT)
        list(APPEND _optional_args TIMEOUT "${ARG_TIMEOUT}")
    endif()

    gvm_add_gtest(${TARGET}
        SOURCES ${ARG_SOURCES}
        LABELS ${ARG_LABELS}
        INCLUDE_DIRS ${_include_dirs}
        LINK_LIBS ${ARG_LINK_LIBS}
        ENVIRONMENT
            "GVM_TEST_DSL_GENERATED_DIR=${_generated_dir}"
            "GVM_TEST_DSL_SOURCE=${DSL_SOURCE}"
        ${_optional_args}
    )

    UGLCOMPILE(${TARGET} ${DSL_SOURCE} ${_generated_dir} ${GVM_UGLC_COMPILER}
        INCLUDE_DIRS ${_ugl_include_dirs}
        DSL_DIRS ${_ugl_dsl_dirs}
        UGLC_ARGS ${ARG_UGLC_ARGS}
    )
endfunction()
