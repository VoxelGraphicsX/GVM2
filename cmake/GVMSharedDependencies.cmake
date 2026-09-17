include_guard(GLOBAL)

if(NOT COMMAND CPMAddPackage)
    include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")
endif()

# Provides GVM's fixed shared dependencies for both source and installed consumers.
# Repeated requests reuse this provider; independently supplied targets are unsupported.
function(gvm_provide_shared_dependencies)
    get_property(_gvm_provider GLOBAL PROPERTY GVM_SHARED_DEPENDENCY_PROVIDER)
    if(_gvm_provider AND NOT _gvm_provider STREQUAL CMAKE_CURRENT_FUNCTION_LIST_DIR)
        message(FATAL_ERROR "GVM dependencies were already provided by another GVM source tree or SDK. Use one GVM distribution throughout the build.")
    endif()
    if(NOT _gvm_provider)
        foreach(_target EABase EASTL glm glm::glm glm-header-only glm::glm-header-only GVM::GLM GVMDeps::glm GVMDeps::EASTL)
            if(TARGET "${_target}")
                message(FATAL_ERROR "GVM requires its own fixed GLM/EASTL dependencies, but target '${_target}' already exists. Add or find GVM first, then link GVM::GLM and GVM::EASTL instead of providing another copy.")
            endif()
        endforeach()
        foreach(_package EABase EASTL glm)
            if(_package IN_LIST CPM_PACKAGES)
                message(FATAL_ERROR "GVM requires its own fixed GLM/EASTL dependencies; CPM package '${_package}' was already registered. Add or find GVM before requesting these dependencies.")
            endif()
        endforeach()

        set(_gvm_eabase_options "EABASE_BUILD_TESTS OFF")
        # EABase declares CMake 3.1 compatibility, which CMake 4 no longer supports.
        # CPM scopes OPTIONS to this dependency; preserve a stricter caller policy.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
            set(_gvm_eabase_policy_version 3.5)
            if(CMAKE_POLICY_VERSION_MINIMUM VERSION_GREATER _gvm_eabase_policy_version)
                set(_gvm_eabase_policy_version "${CMAKE_POLICY_VERSION_MINIMUM}")
            endif()
            list(APPEND _gvm_eabase_options
                "CMAKE_POLICY_VERSION_MINIMUM ${_gvm_eabase_policy_version}")
        endif()
        CPMAddPackage(
            NAME EABase
            URL https://github.com/electronicarts/EABase/archive/0699a15efdfd20b6cecf02153bfa5663decb653c.tar.gz
            URL_HASH SHA256=d33041d12765412080473568fec1e7cc17cde7255b6eb914085ccdf3c00911ad
            FORCE YES
            EXCLUDE_FROM_ALL YES
            OPTIONS ${_gvm_eabase_options}
        )
        CPMAddPackage(
            NAME EASTL
            URL https://github.com/electronicarts/EASTL/archive/refs/tags/3.27.01.tar.gz
            URL_HASH SHA256=fce43bf443f5569b00a8deae735394ea0b16f6c3f96867a17ded50775ffcdd12
            FORCE YES
            EXCLUDE_FROM_ALL YES
            OPTIONS "EASTL_BUILD_TESTS OFF" "EASTL_BUILD_BENCHMARK OFF" "BUILD_TESTING OFF"
        )
        CPMAddPackage(
            NAME glm
            URL https://github.com/g-truc/glm/archive/refs/tags/1.0.1.tar.gz
            URL_HASH SHA256=9f3174561fd26904b23f0db5e560971cbf9b3cbda0b280f04d5c379d03bf234c
            FORCE YES
            EXCLUDE_FROM_ALL YES
            OPTIONS "GLM_BUILD_LIBRARY OFF" "GLM_BUILD_TESTS OFF" "GLM_BUILD_INSTALL OFF"
        )

        set(_gvm_config_include "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dependencies/include")
        target_include_directories(EASTL PUBLIC "${_gvm_config_include}")
        target_compile_definitions(EASTL PUBLIC
            EASTL_USER_CONFIG_HEADER="GVM/Dependencies/EASTLConfig.h"
            EASTL_USER_DEFINED_ALLOCATOR=1)
        if(ANDROID)
            set_property(TARGET EASTL APPEND PROPERTY INTERFACE_LINK_LIBRARIES log)
        endif()

        target_include_directories(glm-header-only INTERFACE "${_gvm_config_include}")
        target_compile_features(glm-header-only INTERFACE cxx_std_20)
        # GLM has no user-config hook. Load the contract before any user includes,
        # including consumers that include GLM before GVM's generated host types.
        if(MSVC)
            target_compile_options(glm-header-only INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX>:/FIGVM/Dependencies/GLMConfig.h>")
        else()
            target_compile_options(glm-header-only INTERFACE
                "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:SHELL:-include GVM/Dependencies/GLMConfig.h>")
        endif()
        add_library(GVM::GLM ALIAS glm)
        add_library(GVMDeps::glm INTERFACE IMPORTED GLOBAL)
        target_link_libraries(GVMDeps::glm INTERFACE glm)
        add_library(GVMDeps::EASTL INTERFACE IMPORTED GLOBAL)
        target_link_libraries(GVMDeps::EASTL INTERFACE EASTL)
        set_property(GLOBAL PROPERTY GVM_SHARED_DEPENDENCY_PROVIDER "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
        foreach(_package EABase EASTL glm)
            set_property(GLOBAL PROPERTY "GVM_${_package}_SOURCE_DIR" "${${_package}_SOURCE_DIR}")
        endforeach()
    endif()
    # UGLC also needs the pinned include roots when parsing DSL inputs directly.
    foreach(_package EABase EASTL glm)
        get_property(_source GLOBAL PROPERTY "GVM_${_package}_SOURCE_DIR")
        set("${_package}_SOURCE_DIR" "${_source}" PARENT_SCOPE)
    endforeach()
endfunction()
