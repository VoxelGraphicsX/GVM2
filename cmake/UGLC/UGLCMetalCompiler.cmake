include_guard(GLOBAL)

option(GVM_UGLC_EMBED_METALLIB "Embed an offline-compiled Metal library in generated C++ for every runtime backend." ON)
set(GVM_METAL_COMPILER "" CACHE FILEPATH "Apple Metal compiler executable (also supported by Apple's Windows Metal tools).")
set(GVM_METAL_TARGET "air64-apple-macosx15.0.0" CACHE STRING "Target of the embedded Metal library, independent of the build host.")

if(GVM_UGLC_EMBED_METALLIB AND NOT GVM_METAL_COMPILER)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        execute_process(COMMAND xcrun --sdk macosx --find metal
            OUTPUT_VARIABLE _metal_compiler OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _metal_result ERROR_QUIET)
        if(_metal_result EQUAL 0)
            set(GVM_METAL_COMPILER "${_metal_compiler}" CACHE FILEPATH "Apple Metal compiler executable." FORCE)
        endif()
    else()
        find_program(_metal_compiler NAMES metal metal.exe)
        if(_metal_compiler)
            set(GVM_METAL_COMPILER "${_metal_compiler}" CACHE FILEPATH "Apple Metal compiler executable." FORCE)
        endif()
    endif()
endif()

