cmake_minimum_required(VERSION 3.22)
project(GVMFmtConsumer LANGUAGES C CXX)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(GVM_TEST_FMT_FROM_PACKAGE "Use an installed parent fmt package" OFF)
if(GVM_TEST_FMT_FROM_PACKAGE)
    find_package(fmt 11.1 CONFIG REQUIRED)
else()
    # Provide a real parent target without registering fmt with GVM's CPM.
    include(FetchContent)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(gvm_test_fmt
        URL https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz
        URL_HASH SHA256=ac366b7b4c2e9f0dde63a59b3feb5ee59b67974b14ee5dc9ea8ad78aa2c1ee1e
    )
    FetchContent_MakeAvailable(gvm_test_fmt)
endif()

if(GVM_TEST_SOURCE_DIR)
    set(GVM_RHI_ENABLE_LOGGING ON CACHE BOOL "" FORCE)
    set(GVM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GVM_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(GVM_BUILD_UGLC OFF CACHE BOOL "" FORCE)
    add_subdirectory("${GVM_TEST_SOURCE_DIR}" gvm)
else()
    find_package(GVM CONFIG REQUIRED)
endif()

enable_testing()
add_executable(gvm_fmt_consumer main.cpp)
# The public logging API must inherit fmt's include paths, definitions and library.
target_link_libraries(gvm_fmt_consumer PRIVATE GVM::RHI)
add_test(NAME fmt_parent_logging COMMAND gvm_fmt_consumer)
