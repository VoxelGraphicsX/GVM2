#ifndef UGLC_TEST_QUAD_BUILTINS_RENDER_HPP
#define UGLC_TEST_QUAD_BUILTINS_RENDER_HPP

#include "UGL.h"

using namespace UGL;

struct QuadBuiltinsVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct QuadBuiltinsFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class QuadBuiltinsRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    QuadBuiltinsVertexOutput vertex(uint vid [[VertexID]])
    {
        QuadBuiltinsVertexOutput outputValue;
        outputValue.pos = float4(float(vid) * 0.5f - 0.5f, 0.0f, 0.0f, 1.0f);
        outputValue.color = float4(0.25f, 0.5f, 0.75f, 1.0f);
        return outputValue;
    }

    QuadBuiltinsFrameBuffer fragment(QuadBuiltinsVertexOutput inputValue)
    {
        QuadBuiltinsFrameBuffer frameBuffer;
        const float4 lane0 = QuadReadLaneAt(inputValue.color, 0u);
        const float4 valueX = QuadReadAcrossX(inputValue.color);
        const float4 valueY = QuadReadAcrossY(inputValue.color);
        const float4 valueDiagonal = QuadReadAcrossDiagonal(inputValue.color);
        frameBuffer.color = half4(lane0 + valueX + valueY + valueDiagonal);
        return frameBuffer;
    }
};

#endif
