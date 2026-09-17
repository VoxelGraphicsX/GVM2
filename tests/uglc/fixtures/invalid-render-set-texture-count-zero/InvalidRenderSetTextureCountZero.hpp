#ifndef UGLC_TEST_INVALID_RENDER_SET_TEXTURE_COUNT_ZERO_HPP
#define UGLC_TEST_INVALID_RENDER_SET_TEXTURE_COUNT_ZERO_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSetTextureCountZero final : public IRenderSet
{
    constructor((TextureComponent<half4, 0> albedo),
                BufferComponent<float3> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

#endif
