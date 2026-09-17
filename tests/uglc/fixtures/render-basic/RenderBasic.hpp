#ifndef UGLC_TEST_RENDER_BASIC_HPP
#define UGLC_TEST_RENDER_BASIC_HPP

#include "UGL.h"

using namespace UGL;

struct RenderBasicVertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct RenderBasicVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct RenderBasicBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding1]], Sampler sampler0 [[Binding0]])
    {
    }
};

struct RenderBasicFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class RenderBasicPass final : public IRenderClass
{
public:
    constructor(BindGroup<RenderBasicBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    RenderBasicVertexOutput vertex(uint vid [[VertexID]], RenderBasicVertexInput inputValue [[VertexInput0]])
    {
        RenderBasicVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    RenderBasicFrameBuffer fragment(RenderBasicVertexOutput inputValue)
    {
        float4 sampled = bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv);
        RenderBasicFrameBuffer frameBuffer;
        frameBuffer.color = half4(sampled);
        return frameBuffer;
    }
};

#endif
