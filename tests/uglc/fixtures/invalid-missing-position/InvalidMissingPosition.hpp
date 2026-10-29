#ifndef UGLC_TEST_INVALID_MISSING_POSITION_HPP
#define UGLC_TEST_INVALID_MISSING_POSITION_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidMissingPositionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidMissingPositionVertexOutput
{
    float4 color [[Attribute0]];
};

class InvalidMissingPositionPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidMissingPositionVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidMissingPositionVertexOutput outputValue = {};
        outputValue.color = vid == 0u ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidMissingPositionFrameBuffer fragment(InvalidMissingPositionVertexOutput inputValue)
    {
        InvalidMissingPositionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
