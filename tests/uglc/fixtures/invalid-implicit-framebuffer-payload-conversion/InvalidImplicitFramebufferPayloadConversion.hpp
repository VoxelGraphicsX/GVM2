#ifndef UGLC_TEST_INVALID_IMPLICIT_FRAMEBUFFER_PAYLOAD_CONVERSION_HPP
#define UGLC_TEST_INVALID_IMPLICIT_FRAMEBUFFER_PAYLOAD_CONVERSION_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidImplicitFramebufferPayloadVertexInput
{
    float4 position [[Attribute0]];
    float2 uv [[Attribute1]];
};

struct InvalidImplicitFramebufferPayloadVertexOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

struct InvalidImplicitFramebufferPayloadBindGroup final : public IBindGroup
{
    constructor(Texture2D<float4> texture0 [[Binding0]], Sampler sampler0 [[Binding1]])
    {
    }
};

struct InvalidImplicitFramebufferPayloadFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidImplicitFramebufferPayloadPass final : public IRenderClass
{
public:
    constructor(BindGroup<InvalidImplicitFramebufferPayloadBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    InvalidImplicitFramebufferPayloadVertexOutput vertex(uint vertexID [[VertexID]], InvalidImplicitFramebufferPayloadVertexInput inputValue [[VertexInput0]])
    {
        InvalidImplicitFramebufferPayloadVertexOutput outputValue;
        outputValue.position = inputValue.position + float4(float(vertexID), 0.0f, 0.0f, 0.0f);
        outputValue.uv = inputValue.uv;
        return outputValue;
    }

    InvalidImplicitFramebufferPayloadFrameBuffer fragment(InvalidImplicitFramebufferPayloadVertexOutput inputValue)
    {
        float4 sampled = bindGroup->texture0->sample(bindGroup->sampler0, inputValue.uv);
        InvalidImplicitFramebufferPayloadFrameBuffer frameBuffer;
        frameBuffer.color = sampled;
        return frameBuffer;
    }
};

#endif
