#ifndef UGLC_TEST_INVALID_RENDER_SET_UNSUPPORTED_COMPONENT_HPP
#define UGLC_TEST_INVALID_RENDER_SET_UNSUPPORTED_COMPONENT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSetUnsupportedComponent final : public IRenderSet
{
    constructor(uint debugValue,
                BufferComponent<float3> vertices [[RenderSetVertexBuffer]],
                BufferComponent<uint> indices [[RenderSetIndexBuffer]])
    {
    }
};

#endif
