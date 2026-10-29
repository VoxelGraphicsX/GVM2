#ifndef UGLC_TEST_INVALID_RENDER_SET_DUPLICATE_VERTEX_BUFFER_HPP
#define UGLC_TEST_INVALID_RENDER_SET_DUPLICATE_VERTEX_BUFFER_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSetDuplicateVertexBuffer final : public IRenderSet
{
    constructor(BufferComponent<float3> vertices0 [[RenderSetVertexBuffer]],
                BufferComponent<float3> vertices1 [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

#endif
