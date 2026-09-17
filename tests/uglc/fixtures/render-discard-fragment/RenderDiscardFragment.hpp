#ifndef UGLC_TEST_RENDER_DISCARD_FRAGMENT_HPP
#define UGLC_TEST_RENDER_DISCARD_FRAGMENT_HPP

#include "UGL.h"

using namespace UGL;

struct RenderDiscardVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct RenderDiscardFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class RenderDiscardFragmentPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    RenderDiscardVertexOutput vertex(uint vid [[VertexID]])
    {
        RenderDiscardVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        outputValue.uv = float2(float(vid), 0.25f);
        return outputValue;
    }

    RenderDiscardFrameBuffer fragment(RenderDiscardVertexOutput inputValue)
    {
        if (inputValue.uv.x < 1.0f)
        {
            UGL::discard_fragment();
        }

        if (inputValue.uv.y < 0.5f)
        {
            discard_fragment();
        }

        RenderDiscardFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.uv.x, inputValue.uv.y, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
