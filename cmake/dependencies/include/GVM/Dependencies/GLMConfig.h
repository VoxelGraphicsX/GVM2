#pragma once

// Load before GLM, using GVM::GLM, to keep generated host types and application
// math on the same packed, left-handed, zero-to-one-depth configuration.
#if defined(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES) || defined(GLM_FORCE_ALIGNED) || \
    defined(GLM_FORCE_SIZE_T_LENGTH) || defined(GLM_FORCE_XYZW_ONLY) || \
    defined(GLM_FORCE_CTOR_INIT)
#error "GVM GLM configuration conflict: packed types, int lengths and default constructors are required."
#endif

#ifndef GLM_FORCE_CXX20
#define GLM_FORCE_CXX20
#endif
#ifndef GLM_FORCE_SWIZZLE
#define GLM_FORCE_SWIZZLE
#endif
#ifndef GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_LEFT_HANDED
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
// Preserve the existing ARM host implementation without selecting ARM on x86.
#if defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
#ifndef GLM_FORCE_NEON
#define GLM_FORCE_NEON
#endif
#endif

#include <glm/detail/setup.hpp>

#if GLM_VERSION_MAJOR != 1 || GLM_VERSION_MINOR != 0 || GLM_VERSION_PATCH != 1
#error "GVM requires its supplied GLM 1.0.1 headers."
#endif
#if GLM_CONFIG_CLIP_CONTROL != GLM_CLIP_CONTROL_LH_ZO || \
    GLM_CONFIG_SWIZZLE == GLM_SWIZZLE_DISABLED || \
    GLM_CONFIG_LENGTH_TYPE != GLM_LENGTH_INT || \
    GLM_CONFIG_CTOR_INIT != GLM_CTOR_INIT_DISABLE
#error "GVM GLM configuration conflict: GLM was configured before GVM. Link GVM::GLM to configure it before the first include."
#endif
