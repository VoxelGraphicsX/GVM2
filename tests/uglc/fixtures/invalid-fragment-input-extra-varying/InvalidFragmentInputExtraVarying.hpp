#ifndef UGLC_TEST_INVALID_FRAGMENT_INPUT_EXTRA_VARYING_HPP
#define UGLC_TEST_INVALID_FRAGMENT_INPUT_EXTRA_VARYING_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidFragmentInputExtraVaryingVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct InvalidFragmentInputExtraVaryingFragmentInput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct InvalidFragmentInputExtraVaryingFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidFragmentInputExtraVaryingPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidFragmentInputExtraVaryingVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidFragmentInputExtraVaryingVertexOutput outputValue = {};
        outputValue.pos = float4(vid == 0u ? -1.0 : 1.0, 0.0, 0.0, 1.0);
        outputValue.color = float4(0.0, 1.0, 0.0, 1.0);
        return outputValue;
    }

    InvalidFragmentInputExtraVaryingFrameBuffer fragment(InvalidFragmentInputExtraVaryingFragmentInput inputValue)
    {
        InvalidFragmentInputExtraVaryingFrameBuffer frameBuffer = {};
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
