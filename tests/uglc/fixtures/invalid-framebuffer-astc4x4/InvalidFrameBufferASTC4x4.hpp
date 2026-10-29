#ifndef UGLC_TEST_INVALID_FRAMEBUFFER_ASTC4X4_HPP
#define UGLC_TEST_INVALID_FRAMEBUFFER_ASTC4X4_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidFrameBufferASTC4x4 final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::ASTC4x4Unorm> color;
};

struct InvalidFrameBufferASTC4x4VertexOutput
{
    float4 pos [[Position]];
};

class InvalidFrameBufferASTC4x4Pass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidFrameBufferASTC4x4VertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidFrameBufferASTC4x4VertexOutput outputValue = {};
        outputValue.pos = float4(vertexID == 0u ? -1.0f : 1.0f, 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidFrameBufferASTC4x4 fragment(InvalidFrameBufferASTC4x4VertexOutput inputValue)
    {
        InvalidFrameBufferASTC4x4 frameBuffer;
        frameBuffer.color = inputValue.pos;
        return frameBuffer;
    }
};

#endif
