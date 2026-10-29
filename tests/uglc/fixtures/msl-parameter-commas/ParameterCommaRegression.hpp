#ifndef UGLC_TEST_PARAMETER_COMMA_REGRESSION_HPP
#define UGLC_TEST_PARAMETER_COMMA_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct ParamVertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
};

struct ParamVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct ParamFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class ParameterCommaPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    ParamVertexOutput vertex(uint vid [[VertexID]], ParamVertexInput inValue [[VertexInput0]], uint instanceId [[InstanceID]])
    {
        ParamVertexOutput outputValue;
        outputValue.pos = inValue.pos;
        outputValue.color = inValue.color;
        return outputValue;
    }

    ParamFrameBuffer fragment(ParamVertexOutput inputValue)
    {
        ParamFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
