#ifndef UGLC_TEST_INVALID_DUPLICATE_VARYING_ATTRIBUTE_HPP
#define UGLC_TEST_INVALID_DUPLICATE_VARYING_ATTRIBUTE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidDuplicateVaryingAttributeFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidDuplicateVaryingAttributeVertexOutput
{
    float4 pos [[Position]];
    float2 color0 [[Attribute0]];
    float2 color1 [[Attribute0]];
};

class InvalidDuplicateVaryingAttributePass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidDuplicateVaryingAttributeVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidDuplicateVaryingAttributeVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        outputValue.color0 = float2(1.0, 0.0);
        outputValue.color1 = float2(0.0, 1.0);
        return outputValue;
    }

    InvalidDuplicateVaryingAttributeFrameBuffer fragment(InvalidDuplicateVaryingAttributeVertexOutput inputValue)
    {
        InvalidDuplicateVaryingAttributeFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color0, inputValue.color1);
        return frameBuffer;
    }
};

#endif
