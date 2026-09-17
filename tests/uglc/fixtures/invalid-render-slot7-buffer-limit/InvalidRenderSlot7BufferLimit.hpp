#ifndef UGLC_TEST_INVALID_RENDER_SLOT7_BUFFER_LIMIT_HPP
#define UGLC_TEST_INVALID_RENDER_SLOT7_BUFFER_LIMIT_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderSlot7VertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct InvalidRenderSlot7VertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct InvalidRenderSlot7BindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct InvalidRenderSlot7FrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidRenderSlot7Pass final : public IRenderClass
{
public:
    constructor(BindGroup<InvalidRenderSlot7BindGroup> bindGroup [[Slot7]])
    {
    }

private:
    InvalidRenderSlot7VertexOutput vertex(uint vid [[VertexID]], InvalidRenderSlot7VertexInput inputValue [[VertexInput0]])
    {
        InvalidRenderSlot7VertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    InvalidRenderSlot7FrameBuffer fragment(InvalidRenderSlot7VertexOutput inputValue)
    {
        InvalidRenderSlot7FrameBuffer frameBuffer;
        frameBuffer.color = half4(bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv));
        return frameBuffer;
    }
};

#endif
