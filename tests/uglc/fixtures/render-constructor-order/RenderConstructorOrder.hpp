#ifndef UGLC_TEST_RENDER_CONSTRUCTOR_ORDER_HPP
#define UGLC_TEST_RENDER_CONSTRUCTOR_ORDER_HPP

#include "UGL.h"

using namespace UGL;

struct RenderConstructorOrderVertexInput
{
    float4 pos [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct RenderConstructorOrderVertexOutput
{
    float4 pos [[Position]];
    float2 uv [[Attribute0]];
};

struct RenderConstructorOrderBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct RenderConstructorOrderFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class RenderConstructorOrderPass final : public IRenderClass
{
public:
    uint constructorMarker = 0u;

    constructor(BindGroup<RenderConstructorOrderBindGroup> bindGroup [[Slot0]])
    {
        this->constructorMarker = 11u;
    }

private:
    RenderConstructorOrderVertexOutput vertex(uint vid [[VertexID]], RenderConstructorOrderVertexInput inputValue [[VertexInput0]])
    {
        RenderConstructorOrderVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    RenderConstructorOrderFrameBuffer fragment(RenderConstructorOrderVertexOutput inputValue)
    {
        RenderConstructorOrderFrameBuffer frameBuffer;
        frameBuffer.color = half4(bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv));
        return frameBuffer;
    }
};

#endif
