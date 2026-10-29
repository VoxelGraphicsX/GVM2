# Configures one template directory into an IDE-openable mobile host project.
function(gvm_configure_template_tree TEMPLATE_DIR OUTPUT_DIR)
  file(GLOB_RECURSE GVM_TEMPLATE_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false "${TEMPLATE_DIR}/*")
  foreach(GVM_TEMPLATE_FILE IN LISTS GVM_TEMPLATE_FILES)
    file(RELATIVE_PATH GVM_TEMPLATE_RELATIVE_PATH "${TEMPLATE_DIR}" "${GVM_TEMPLATE_FILE}")
    get_filename_component(GVM_TEMPLATE_RELATIVE_DIR "${GVM_TEMPLATE_RELATIVE_PATH}" DIRECTORY)
    if(GVM_TEMPLATE_RELATIVE_DIR)
      file(MAKE_DIRECTORY "${OUTPUT_DIR}/${GVM_TEMPLATE_RELATIVE_DIR}")
    endif()

    if(GVM_TEMPLATE_RELATIVE_PATH MATCHES "\\.in$")
      string(REGEX REPLACE "\\.in$" "" GVM_OUTPUT_RELATIVE_PATH "${GVM_TEMPLATE_RELATIVE_PATH}")
      configure_file("${GVM_TEMPLATE_FILE}" "${OUTPUT_DIR}/${GVM_OUTPUT_RELATIVE_PATH}" @ONLY)
    else()
      configure_file("${GVM_TEMPLATE_FILE}" "${OUTPUT_DIR}/${GVM_TEMPLATE_RELATIVE_PATH}" COPYONLY)
    endif()
  endforeach()
endfunction()

# Generates Android Studio and DevEco Studio host projects for the current mobile sample.
function(gvm_configure_mobile_sample_hosts)
  set(GVM_ANDROID_HOST_NDK_VERSION "29.0.14206865" CACHE STRING "Android NDK version used by generated GVM Android sample hosts.")
  set(GVM_ANDROID_HOST_COMPILE_SDK "32" CACHE STRING "Android compile SDK used by generated GVM Android sample hosts.")
  set(GVM_ANDROID_HOST_MIN_SDK "26" CACHE STRING "Android min SDK used by generated GVM Android sample hosts.")
  set(GVM_ANDROID_HOST_TARGET_SDK "32" CACHE STRING "Android target SDK used by generated GVM Android sample hosts.")
  set(GVM_OHOS_HOST_SDK_VERSION "23" CACHE STRING "OpenHarmony SDK API version used by generated GVM OHOS sample hosts.")

  set(GVM_SOURCE_DIR "${GVM_REPOSITORY_ROOT}")
  set(GVM_MOBILE_SAMPLE_ID "Test01Triangle")
  set(GVM_MOBILE_SAMPLE_LIBRARY_NAME "gvm_test01_triangle")
  set(GVM_MOBILE_SAMPLE_APP_LABEL "GVM Test01")

  set(GVM_MOBILE_UGLC_EXECUTABLE "")
  if(GVM_UGLC_COMPILER AND TARGET "${GVM_UGLC_COMPILER}" AND NOT CMAKE_CONFIGURATION_TYPES)
    set(GVM_MOBILE_UGLC_EXECUTABLE "${CMAKE_BINARY_DIR}/UGLC${CMAKE_EXECUTABLE_SUFFIX}")
  elseif(GVM_UGLC_COMPILER AND TARGET "${GVM_UGLC_COMPILER}")
    message(WARNING
      "Generated mobile host projects need a host-runnable UGLC path. "
      "The current generator is multi-config, so set GVM_UGLC_EXECUTABLE explicitly before using the generated mobile host projects."
    )
  elseif(GVM_UGLC_COMPILER AND EXISTS "${GVM_UGLC_COMPILER}")
    set(GVM_MOBILE_UGLC_EXECUTABLE "${GVM_UGLC_COMPILER}")
  elseif(GVM_UGLC_EXECUTABLE AND EXISTS "${GVM_UGLC_EXECUTABLE}")
    set(GVM_MOBILE_UGLC_EXECUTABLE "${GVM_UGLC_EXECUTABLE}")
  endif()

  set(GVM_MOBILE_TEMPLATE_ROOT "${GVM_REPOSITORY_ROOT}/GVMRuntime_Samples/SampleWindowManager/MobileHostTemplates")
  set(GVM_ANDROID_HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/mobile/android/${GVM_MOBILE_SAMPLE_ID}")
  set(GVM_OHOS_HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/mobile/ohos/${GVM_MOBILE_SAMPLE_ID}")

  gvm_configure_template_tree(
    "${GVM_MOBILE_TEMPLATE_ROOT}/AndroidNativeHost"
    "${GVM_ANDROID_HOST_OUTPUT_DIR}")
  gvm_configure_template_tree(
    "${GVM_MOBILE_TEMPLATE_ROOT}/OhosXComponentHost"
    "${GVM_OHOS_HOST_OUTPUT_DIR}")

  add_custom_target(GVMGenerateAndroidTest01Host
    COMMAND ${CMAKE_COMMAND} -E echo "Generated Android host: ${GVM_ANDROID_HOST_OUTPUT_DIR}"
    VERBATIM)
  add_custom_target(GVMGenerateOhosTest01Host
    COMMAND ${CMAKE_COMMAND} -E echo "Generated OHOS host: ${GVM_OHOS_HOST_OUTPUT_DIR}"
    VERBATIM)
  add_custom_target(GVMGenerateMobileSampleHosts
    DEPENDS GVMGenerateAndroidTest01Host GVMGenerateOhosTest01Host)
endfunction()
