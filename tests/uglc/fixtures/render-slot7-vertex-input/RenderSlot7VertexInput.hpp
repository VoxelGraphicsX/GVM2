#ifndef UGLC_TEST_RENDER_SLOT7_VERTEX_INPUT_HPP
#define UGLC_TEST_RENDER_SLOT7_VERTEX_INPUT_HPP

#include "UGL.h"

using namespace UGL;

struct RenderSlot7VertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct RenderSlot7VertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct RenderSlot7BindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct RenderSlot7FrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class RenderSlot7Pass final : public IRenderClass
{
public:
    constructor(BindGroup<RenderSlot7BindGroup> bindGroup [[Slot7]])
    {
    }

private:
    RenderSlot7VertexOutput vertex(uint vid [[VertexID]], RenderSlot7VertexInput inputValue [[VertexInput0]])
    {
        RenderSlot7VertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    RenderSlot7FrameBuffer fragment(RenderSlot7VertexOutput inputValue)
    {
        RenderSlot7FrameBuffer frameBuffer;
        frameBuffer.color = half4(bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv));
        return frameBuffer;
    }
};

#endif
