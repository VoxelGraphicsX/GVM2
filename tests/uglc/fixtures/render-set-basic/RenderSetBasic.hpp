#ifndef UGLC_TEST_RENDER_SET_BASIC_HPP
#define UGLC_TEST_RENDER_SET_BASIC_HPP

#include "UGL.h"

using namespace UGL;

static constexpr uint RenderSetBasicMaxTextures = 4;

struct RenderSetBasic final : public IRenderSet
{
    constructor(BufferComponent<float4> transforms,
                (TextureComponent<half4, RenderSetBasicMaxTextures> albedo),
                BufferComponent<float3> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

#endif
