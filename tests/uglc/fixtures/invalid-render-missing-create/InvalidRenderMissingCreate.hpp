#ifndef UGLC_TEST_INVALID_RENDER_MISSING_CREATE_HPP
#define UGLC_TEST_INVALID_RENDER_MISSING_CREATE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderMissingCreateFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidRenderMissingCreateVertexOutput
{
    float4 pos [[Position]];
};

class InvalidRenderMissingCreatePass final : public IRenderClass
{
private:
    InvalidRenderMissingCreateVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidRenderMissingCreateVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidRenderMissingCreateFrameBuffer fragment(InvalidRenderMissingCreateVertexOutput inputValue)
    {
        InvalidRenderMissingCreateFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
