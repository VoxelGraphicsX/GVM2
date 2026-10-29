#ifndef UGLC_TEST_INVALID_RENDER_ENTITY_IN_FRAGMENT_HPP
#define UGLC_TEST_INVALID_RENDER_ENTITY_IN_FRAGMENT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderEntityInFragmentFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidRenderEntityInFragmentVertexOutput
{
    float4 pos [[Position]];
};

class InvalidRenderEntityInFragmentPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidRenderEntityInFragmentVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidRenderEntityInFragmentVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidRenderEntityInFragmentFrameBuffer fragment(uint renderEntityID [[RenderEntityID]])
    {
        InvalidRenderEntityInFragmentFrameBuffer frameBuffer;
        frameBuffer.color = half4(renderEntityID > 0u ? 1.0 : 0.0, 0.0, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
