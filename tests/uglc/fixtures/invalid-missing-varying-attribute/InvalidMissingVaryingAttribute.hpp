#ifndef UGLC_TEST_INVALID_MISSING_VARYING_ATTRIBUTE_HPP
#define UGLC_TEST_INVALID_MISSING_VARYING_ATTRIBUTE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidMissingVaryingFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct InvalidMissingVaryingVertexOutput
{
    float4 pos [[Position]];
    float4 color;
};

class InvalidMissingVaryingPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidMissingVaryingVertexOutput vertex(uint vid [[VertexID]])
    {
        InvalidMissingVaryingVertexOutput outputValue = {};
        if (vid == 0)
        {
            outputValue.pos = float4(-1.0, -1.0, 0.0, 1.0);
            outputValue.color = float4(1.0, 0.0, 0.0, 1.0);
        }
        else if (vid == 1)
        {
            outputValue.pos = float4(0.0, 1.0, 0.0, 1.0);
            outputValue.color = float4(0.0, 1.0, 0.0, 1.0);
        }
        else
        {
            outputValue.pos = float4(1.0, -1.0, 0.0, 1.0);
            outputValue.color = float4(0.0, 0.0, 1.0, 1.0);
        }
        return outputValue;
    }

    InvalidMissingVaryingFrameBuffer fragment(InvalidMissingVaryingVertexOutput inputValue)
    {
        InvalidMissingVaryingFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
