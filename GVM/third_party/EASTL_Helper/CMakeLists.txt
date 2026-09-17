
include(GNUInstallDirs)

# 包含胶水代码
add_library(EASTL_Helper STATIC
    EastlCompat.cpp
    # 这里也可以放其他基础代码，比如日志系统、断言宏等
)
add_library(GVM::EASTLHelper ALIAS EASTL_Helper)
add_library(GVM::EASTL ALIAS EASTL_Helper)
set_target_properties(EASTL_Helper PROPERTIES EXPORT_NAME EASTLHelper)

# The provider publishes the same pinned EASTL headers and configuration everywhere.
target_link_libraries(EASTL_Helper PUBLIC GVMDeps::EASTL)
target_include_directories(EASTL_Helper
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/third_party/EASTL_Helper>"
)
