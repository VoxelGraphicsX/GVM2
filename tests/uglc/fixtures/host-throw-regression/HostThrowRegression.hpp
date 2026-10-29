#ifndef UGLC_TEST_HOST_THROW_REGRESSION_HPP
#define UGLC_TEST_HOST_THROW_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct HostThrowRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

struct HostThrowRegressionVertexOutput
{
    float4 pos [[Position]];
};

struct HostThrowRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class HostThrowRegressionPass final : public IRenderClass
{
public:
    constructor()
    {
        uint elapsedMs = 0u;
        if (elapsedMs == 0u)
        {
            throw;
        }
    }

private:
    HostThrowRegressionVertexOutput vertex(uint vid [[VertexID]],
                                           HostThrowRegressionVertexInput inputValue [[VertexInput0]])
    {
        HostThrowRegressionVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    HostThrowRegressionFrameBuffer fragment(HostThrowRegressionVertexOutput inputValue)
    {
        HostThrowRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
