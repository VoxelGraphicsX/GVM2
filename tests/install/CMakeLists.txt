cmake_minimum_required(VERSION 3.22)
project(GVMInstalledConsumer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(GVM CONFIG REQUIRED)

set(GVM_UGLC_EXECUTABLE "" CACHE FILEPATH "Host UGLC executable used to compile the installed SDK fixture.")
set(GVM_UGLC_RESOURCE_DIR "" CACHE PATH "Optional Clang resource directory for the host UGLC executable.")
if(NOT EXISTS "${GVM_UGLC_EXECUTABLE}")
    message(FATAL_ERROR "Set GVM_UGLC_EXECUTABLE to a built host UGLC executable.")
endif()
set(UGLC_COMMON_INCLUDE_DIRS)
foreach(_target GVM::UGLHeaders EASTL EABase)
    get_target_property(_include_dirs ${_target} INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND UGLC_COMMON_INCLUDE_DIRS ${_include_dirs})
endforeach()
add_executable(gvm_installed_consumer main.cpp)
target_link_libraries(gvm_installed_consumer PRIVATE GVM::GVM)
UGLCOMPILE(gvm_installed_consumer
    "${CMAKE_CURRENT_SOURCE_DIR}/InstalledCompute.hpp"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${GVM_UGLC_EXECUTABLE}"
    UGLC_ARGS --shader-pipeline=uglir)

enable_testing()
add_test(NAME installed_generated_compute COMMAND gvm_installed_consumer)
add_executable(gvm_installed_ugl_headers ugl_headers.cpp)
target_link_libraries(gvm_installed_ugl_headers PRIVATE GVM::UGLHeaders)
add_test(NAME installed_ugl_headers COMMAND gvm_installed_ugl_headers)
foreach(_backend STD EASTL)
    string(TOLOWER "${_backend}" _backend_name)
    set(_target "gvm_installed_unordered_set_${_backend_name}")
    add_executable(${_target} unordered_set.cpp)
    target_compile_definitions(${_target} PRIVATE GVM_USE_${_backend}=1)
    target_link_libraries(${_target} PRIVATE GVM::GVM)
    add_test(NAME installed_unordered_set_${_backend_name} COMMAND ${_target})
endforeach()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../dependencies" dependency-contract)
