#ifndef UGLC_TEST_INVALID_VARYING_TYPE_MISMATCH_HPP
#define UGLC_TEST_INVALID_VARYING_TYPE_MISMATCH_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidVaryingTypeMismatchVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct InvalidVaryingTypeMismatchFragmentInput
{
    float4 pos [[Position]];
    uint4 color [[Attribute0]];
};

struct InvalidVaryingTypeMismatchFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidVaryingTypeMismatchPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidVaryingTypeMismatchVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidVaryingTypeMismatchVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        outputValue.color = float4(1.0, 0.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidVaryingTypeMismatchFrameBuffer fragment(InvalidVaryingTypeMismatchFragmentInput inputValue)
    {
        InvalidVaryingTypeMismatchFrameBuffer frameBuffer = {};
        frameBuffer.color = half4(1.0, 0.0, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
