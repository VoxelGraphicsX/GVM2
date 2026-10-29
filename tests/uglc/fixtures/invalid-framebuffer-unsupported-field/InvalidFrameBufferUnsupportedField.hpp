#ifndef UGLC_TEST_INVALID_FRAMEBUFFER_UNSUPPORTED_FIELD_HPP
#define UGLC_TEST_INVALID_FRAMEBUFFER_UNSUPPORTED_FIELD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidFrameBufferUnsupportedField final : public IFrameBuffer
{
    float4 color;
};

struct InvalidFrameBufferUnsupportedFieldVertexOutput
{
    float4 pos [[Position]];
};

class InvalidFrameBufferUnsupportedFieldPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidFrameBufferUnsupportedFieldVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidFrameBufferUnsupportedFieldVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidFrameBufferUnsupportedField fragment(InvalidFrameBufferUnsupportedFieldVertexOutput inputValue)
    {
        InvalidFrameBufferUnsupportedField frameBuffer;
        frameBuffer.color = inputValue.pos;
        return frameBuffer;
    }
};

#endif
