#pragma once

// GVM's shared EASTL ABI is independent of the application's Debug/Release mode.
// All EASTL sources, GVM binaries and consumers must use this configuration and
// GVM's paired allocator implementation. Rebuild the SDK after changing it.
#if (defined(EASTL_DEBUG) && EASTL_DEBUG != 0) || \
    (defined(EASTL_NAME_ENABLED) && EASTL_NAME_ENABLED != 0) || \
    (defined(EASTL_DEBUGPARAMS_LEVEL) && EASTL_DEBUGPARAMS_LEVEL != 0) || \
    (defined(EASTL_FIXED_SIZE_TRACKING_ENABLED) && EASTL_FIXED_SIZE_TRACKING_ENABLED != 0) || \
    (defined(EASTL_USER_DEFINED_ALLOCATOR) && EASTL_USER_DEFINED_ALLOCATOR != 1) || \
    (defined(EASTL_OPENSOURCE) && EASTL_OPENSOURCE != 1) || \
    (defined(EASTL_EASTDC_VSNPRINTF) && EASTL_EASTDC_VSNPRINTF != 0)
#error "GVM EASTL configuration conflict: use GVM::EASTL and the SDK's fixed layout and allocator settings."
#endif

#define EASTL_DEBUG 0
#define EASTL_NAME_ENABLED 0
#define EASTL_DEBUGPARAMS_LEVEL 0
#define EASTL_FIXED_SIZE_TRACKING_ENABLED 0
#define EASTL_USER_DEFINED_ALLOCATOR 1
#define EASTL_OPENSOURCE 1
#define EASTL_EASTDC_VSNPRINTF 0
