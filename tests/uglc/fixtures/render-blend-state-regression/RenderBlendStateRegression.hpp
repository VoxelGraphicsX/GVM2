#ifndef UGLC_TEST_RENDER_BLEND_STATE_REGRESSION_HPP
#define UGLC_TEST_RENDER_BLEND_STATE_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

struct RenderBlendStateRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

struct RenderBlendStateRegressionVertexOutput
{
    float4 pos [[Position]];
};

struct RenderBlendStateRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class RenderBlendStateRegressionPass final : public IRenderClass
{
public:
    constructor()
    {
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::SrcAlpha;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::Back);
    }

private:
    RenderBlendStateRegressionVertexOutput vertex(uint vid [[VertexID]],
                                                  RenderBlendStateRegressionVertexInput inputValue [[VertexInput0]])
    {
        RenderBlendStateRegressionVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    RenderBlendStateRegressionFrameBuffer fragment(RenderBlendStateRegressionVertexOutput inputValue)
    {
        RenderBlendStateRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
