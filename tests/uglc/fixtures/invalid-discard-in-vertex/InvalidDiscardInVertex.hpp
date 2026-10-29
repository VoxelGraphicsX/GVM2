#ifndef UGLC_TEST_INVALID_DISCARD_IN_VERTEX_HPP
#define UGLC_TEST_INVALID_DISCARD_IN_VERTEX_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidDiscardVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidDiscardFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidDiscardInVertexPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidDiscardVertexOutput vertex(uint vid [[VertexID]])
    {
        UGL::discard_fragment();

        InvalidDiscardVertexOutput outputValue;
        outputValue.pos = float4(float(vid), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidDiscardFrameBuffer fragment(InvalidDiscardVertexOutput inputValue)
    {
        InvalidDiscardFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.x, 0.0f, 0.0f, 1.0f);
        return frameBuffer;
    }
};

#endif
