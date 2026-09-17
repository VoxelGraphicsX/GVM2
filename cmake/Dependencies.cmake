# cmake/Dependencies.cmake
set(FETCHCONTENT_QUIET FALSE CACHE BOOL "Allow FetchContent output" FORCE)
# 1. 加载本地的 CPM.cmake
# CMAKE_CURRENT_LIST_DIR 指向当前文件(Dependencies.cmake)所在的目录
include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

include("${CMAKE_CURRENT_LIST_DIR}/GVMSharedDependencies.cmake")
gvm_provide_shared_dependencies()

if(GVM_BUILD_TESTS)
    CPMAddPackage(
        NAME googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        OPTIONS
            "BUILD_GMOCK OFF"
            "INSTALL_GTEST OFF"
    )

    if(TARGET gtest AND NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
    endif()
    if(TARGET gtest_main AND NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
endif()

if(GVM_RHI_ENABLE_LOGGING AND NOT TARGET fmt::fmt)
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

if(GVM_RHI_ENABLE_LOGGING AND GVM_RHI_USE_SPDLOG_LOGGER)
    CPMAddPackage(
        NAME spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
        URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67
        OPTIONS
            "SPDLOG_BUILD_SHARED OFF"
            "SPDLOG_BUILD_EXAMPLE OFF"
            "SPDLOG_BUILD_EXAMPLE_HO OFF"
            "SPDLOG_BUILD_TESTS OFF"
            "SPDLOG_BUILD_TESTS_HO OFF"
            "SPDLOG_BUILD_BENCH OFF"
            "SPDLOG_BUILD_WARNINGS OFF"
            "SPDLOG_INSTALL OFF"
            "SPDLOG_FMT_EXTERNAL ON"
    )
endif()

if(GVM_RHI_ENABLE_VULKAN)
    CPMAddPackage(
        NAME VulkanHeaders
        URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.3.280.0.tar.gz
        URL_HASH SHA256=14caa991988be6451755ad1c81df112f4b6f2bea05f0cf2888a52d4d0f0910f6
        OPTIONS
            "BUILD_TESTS OFF"
            "UPDATE_DEPS OFF"
    )

    CPMAddPackage(
        NAME volk
        URL https://github.com/zeux/volk/archive/refs/tags/1.4.304.tar.gz
        URL_HASH SHA256=ab3d4a8ccaeb32652259cdd008399504a41792675b0421d90b67729ee274746f
        OPTIONS
            "VOLK_PULL_IN_VULKAN ON"
            "VOLK_INSTALL OFF"
            "VULKAN_HEADERS_INSTALL_DIR ${VulkanHeaders_SOURCE_DIR}"
    )

    CPMAddPackage(
        NAME VulkanMemoryAllocator
        URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.1.0.tar.gz
        URL_HASH SHA256=ae134ecc37c55634f108e926f85d5d887b670360e77cd107affaf3a9539595f2
        OPTIONS
            "VMA_BUILD_SAMPLES OFF"
            "VMA_BUILD_DOCUMENTATION OFF"
            "VMA_BUILD_HLSL OFF"
    )

    CPMAddPackage(
        NAME SPIRVReflect
        URL https://github.com/KhronosGroup/SPIRV-Reflect/archive/10b4f09a24d7ac1603071e767c089551dc6a3949.tar.gz
        URL_HASH SHA256=4a54b6d705df35223ee7fefc567998936d11315d1f897160196f9f97303bbf03
        OPTIONS
            "SPIRV_REFLECT_EXECUTABLE OFF"
            "SPIRV_REFLECT_STATIC_LIB ON"
            "SPIRV_REFLECT_BUILD_TESTS OFF"
            "SPIRV_REFLECT_INSTALL OFF"
    )
endif()

if((GVM_BUILD_SAMPLES OR (GVM_BUILD_TESTS AND GVM_BUILD_TESTS_RENDER)) AND NOT ANDROID AND NOT OHOS)

    # =========================================================
    # 4. SDL2 (窗口与输入)
    # =========================================================
    CPMAddPackage(
        NAME SDL2
        URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.tar.gz
        URL_HASH SHA256=03f9d7c191a837525c9cda6406af2f2e48be02b5e7eb03d949cc9f1e9ca41c8b
        OPTIONS
            "SDL_SHARED OFF"      # 【推荐】构建为静态库，避免处理 DLL 拷贝问题
            "SDL_STATIC ON"       # 强制开启静态库
            "SDL_TEST OFF"        # 关闭测试代码
            "SDL2_DISABLE_INSTALL ON" # 不安装到系统目录
            "BUILD_TESTING OFF"
    )

endif()

if(GVM_BUILD_SAMPLES)
    CPMAddPackage(
        NAME imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.4.tar.gz
        URL_HASH SHA256=0e175d4d941112532549b418ced0bd546abe9024ecb9b5f431f8a67a2197b0ba
        DOWNLOAD_ONLY YES
    )
endif()

if(GVM_BUILD_SAMPLES AND GVM_BUILD_ADVANCED_SAMPLES)
    CPMAddPackage(
        NAME meshoptimizer
        URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v1.1.1.tar.gz
        URL_HASH SHA256=30cd4d28fe71bf58c614c23c87fed385bac223acbb2dfaf343d20ffc3584a083
        OPTIONS
            "MESHOPT_BUILD_DEMO OFF"
            "MESHOPT_BUILD_GLTFPACK OFF"
    )
    CPMAddPackage(
        NAME tinyobjloader
        URL https://github.com/tinyobjloader/tinyobjloader/archive/refs/tags/v1.0.7.tar.gz
        URL_HASH SHA256=b9d08b675ba54b9cb00ffc99eaba7616d0f7e6f6b8947a7e118474e97d942129
        DOWNLOAD_ONLY YES
    )
endif()
