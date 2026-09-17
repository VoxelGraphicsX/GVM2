include(CMakePackageConfigHelpers)

get_filename_component(GVM_INSTALL_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(GVM_INSTALL_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/GVM")

install(
    TARGETS
        GVM
        GVMRHI
        GVMCore
        GVMUGLHeaders
        EASTL_Helper
        GVMLoggingSupport
    EXPORT GVMTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    FILES
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMRHI/GVMRHI.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMRHI/GVMLogging.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMRHI/GVMCpuProbe.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/GVMRHI"
)

install(
    FILES
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/GVMCore.Public.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/GVMCore"
)

install(
    DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Public/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/GVMCore/Public"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

# Generated host code depends on these runtime definitions and their header closure.
install(
    FILES
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GDeviceProxy.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GGPUVector.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GHelperFunction.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GMath.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GObject.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GProgressiveData.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GQueue.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderBufferComponent.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderComponent.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderComponentBatchedCopyCommand.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderEntity.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderSet.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderSetCommandEncoder.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderSetInternalCommand.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GRenderTextureComponent.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GResource.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GShader.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GShaderHalf.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GStagingLinearAllocator.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GSync.hpp"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/GVMCore/Private/GVMCore.Private.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/GVMCore/Private"
)

install(
    DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/GVM/UGLHeaders/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/UGLHeaders"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/GVM/Utils/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Utils"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/GVM/third_party/EASTL_Helper/EASTL/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/third_party/EASTL_Helper/EASTL"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    DIRECTORY
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/third_party/GVMSTL"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/third_party/defer"
        "${GVM_INSTALL_PROJECT_ROOT}/GVM/third_party/xGEFoundation"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/third_party"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

install(
    EXPORT GVMTargets
    NAMESPACE GVM::
    DESTINATION "${GVM_INSTALL_CMAKE_DIR}"
)

configure_package_config_file(
    "${GVM_INSTALL_PROJECT_ROOT}/cmake/GVMConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GVMConfig.cmake"
    INSTALL_DESTINATION "${GVM_INSTALL_CMAKE_DIR}"
)

set(GVM_PACKAGE_FMT_DEPENDENCY_BLOCK "")
if(GVM_RHI_ENABLE_LOGGING)
    set(GVM_PACKAGE_FMT_DEPENDENCY_BLOCK [=[
if(NOT TARGET fmt::fmt)
    CPMAddPackage(
        NAME fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz
        URL_HASH SHA256=ac366b7b4c2e9f0dde63a59b3feb5ee59b67974b14ee5dc9ea8ad78aa2c1ee1e
        OPTIONS
            "FMT_TEST OFF"
            "FMT_DOC OFF"
            "FMT_INSTALL OFF"
    )
endif()

if(NOT TARGET GVMDeps::fmt)
    add_library(GVMDeps::fmt INTERFACE IMPORTED)
    if(TARGET fmt::fmt)
        target_link_libraries(GVMDeps::fmt INTERFACE fmt::fmt)
    elseif(TARGET fmt)
        target_link_libraries(GVMDeps::fmt INTERFACE fmt)
    endif()
endif()
]=])
endif()

configure_file(
    "${GVM_INSTALL_PROJECT_ROOT}/cmake/GVMPackageDependencies.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GVMPackageDependencies.cmake"
    @ONLY
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/GVMConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/GVMConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/GVMConfigVersion.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/GVMPackageDependencies.cmake"
        "${GVM_INSTALL_PROJECT_ROOT}/cmake/CPM.cmake"
        "${GVM_INSTALL_PROJECT_ROOT}/cmake/GVMSharedDependencies.cmake"
        "${GVM_INSTALL_PROJECT_ROOT}/cmake/UGLC/UGLCompile.cmake"
    DESTINATION "${GVM_INSTALL_CMAKE_DIR}"
)

# Ship the required attribution alongside both source and binary distributions.
install(
    FILES
        "${GVM_INSTALL_PROJECT_ROOT}/LICENSE"
        "${GVM_INSTALL_PROJECT_ROOT}/NOTICE"
        "${GVM_INSTALL_PROJECT_ROOT}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/GVM"
)
install(
    DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/licenses/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/GVM/licenses"
)

# Shared dependency configuration must travel with the installed provider.
install(DIRECTORY "${GVM_INSTALL_PROJECT_ROOT}/cmake/dependencies/"
    DESTINATION "${GVM_INSTALL_CMAKE_DIR}/dependencies")
