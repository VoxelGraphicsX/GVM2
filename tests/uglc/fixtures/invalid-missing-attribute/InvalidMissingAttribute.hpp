#ifndef UGLC_TEST_INVALID_MISSING_ATTRIBUTE_HPP
#define UGLC_TEST_INVALID_MISSING_ATTRIBUTE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidAttributeVertexInput
{
    float4 pos [[Attribute0]];
    float4 color;
};

struct InvalidAttributeVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct InvalidAttributeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidAttributePass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidAttributeVertexOutput vertex(uint vid [[VertexID]], InvalidAttributeVertexInput inputValue [[VertexInput0]])
    {
        InvalidAttributeVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    InvalidAttributeFrameBuffer fragment(InvalidAttributeVertexOutput inputValue)
    {
        InvalidAttributeFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
