#ifndef UGLC_TEST_INVALID_RENDER_SET_MISSING_VERTEX_BUFFER_HPP
#define UGLC_TEST_INVALID_RENDER_SET_MISSING_VERTEX_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSetMissingVertexBuffer final : public IRenderSet
{
    constructor(BufferComponent<float4> transforms,
                BufferComponent<float3> vertices,
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

#endif
