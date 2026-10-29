#pragma once
#include "UGL.Types.h"
#ifndef UGL_MAKE_TEXTURE_FORMAT
#define UGL_MAKE_TEXTURE_FORMAT(x, y)            \
    struct x final                               \
    {                                            \
    private:                                     \
        y data;                                  \
                                                 \
    public:                                      \
        using TrueType = y;                      \
        [[nodiscard]] x &operator=(const y &rhs) \
        {                                        \
            this->data = rhs;                    \
            return *this;                        \
        }                                        \
        void setData(y data)                     \
        {                                        \
            this->data = data;                   \
        }                                        \
        y getData() const                        \
        {                                        \
            return data;                         \
        }                                        \
    };
#endif
namespace UGL
{
    namespace TextureFormat
    {
        UGL_MAKE_TEXTURE_FORMAT(R8Unorm, half)
        UGL_MAKE_TEXTURE_FORMAT(R8Snorm, half)
        UGL_MAKE_TEXTURE_FORMAT(R8Uint, uint8_t)
        UGL_MAKE_TEXTURE_FORMAT(R8Sint, int8_t)
        UGL_MAKE_TEXTURE_FORMAT(R16Uint, uint16_t)
        UGL_MAKE_TEXTURE_FORMAT(R16Sint, int16_t)
        UGL_MAKE_TEXTURE_FORMAT(R16Float, half)
        UGL_MAKE_TEXTURE_FORMAT(RG8Unorm, half2)
        UGL_MAKE_TEXTURE_FORMAT(RG8Snorm, half2)
        UGL_MAKE_TEXTURE_FORMAT(RG8Uint, uint2)
        UGL_MAKE_TEXTURE_FORMAT(RG8Sint, int2)
        UGL_MAKE_TEXTURE_FORMAT(R32Float, float)
        UGL_MAKE_TEXTURE_FORMAT(R32Uint, uint)
        UGL_MAKE_TEXTURE_FORMAT(R32Sint, int)
        UGL_MAKE_TEXTURE_FORMAT(RG16Uint, uint2)
        UGL_MAKE_TEXTURE_FORMAT(RG16Sint, int2)
        UGL_MAKE_TEXTURE_FORMAT(RG16Float, half2)
        UGL_MAKE_TEXTURE_FORMAT(RGBA8Unorm, half4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA8UnormSrgb, half4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA8Snorm, half4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA8Uint, uint4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA8Sint, int4)
        UGL_MAKE_TEXTURE_FORMAT(BGRA8Unorm, half4)
        UGL_MAKE_TEXTURE_FORMAT(BGRA8UnormSrgb, half4)
        UGL_MAKE_TEXTURE_FORMAT(PreferredSwapchain, half4)
        UGL_MAKE_TEXTURE_FORMAT(RGB10A2Uint, uint2)
        UGL_MAKE_TEXTURE_FORMAT(RGB10A2Unorm, half4)
        UGL_MAKE_TEXTURE_FORMAT(RG11B10Ufloat, half3)
        UGL_MAKE_TEXTURE_FORMAT(RGB9E5Ufloat, half4)
        UGL_MAKE_TEXTURE_FORMAT(RG32Float, float2)
        UGL_MAKE_TEXTURE_FORMAT(RG32Uint, uint2)
        UGL_MAKE_TEXTURE_FORMAT(RG32Sint, int2)
        UGL_MAKE_TEXTURE_FORMAT(RGBA16Uint, uint4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA16Sint, int4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA16Float, half4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA32Float, float4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA32Uint, uint4)
        UGL_MAKE_TEXTURE_FORMAT(RGBA32Sint, int4)
        UGL_MAKE_TEXTURE_FORMAT(ASTC4x4Unorm, half4)
        UGL_MAKE_TEXTURE_FORMAT(Depth32Float, float)
        UGL_MAKE_TEXTURE_FORMAT(Depth16Unorm, half)
    } // namespace TextureFormat
} // namespace UGL
