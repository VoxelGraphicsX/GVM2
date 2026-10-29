#ifndef UGLC_TEST_INVALID_DEPTH_MIDDLE_HPP
#define UGLC_TEST_INVALID_DEPTH_MIDDLE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidDepthVertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
};

struct InvalidDepthVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct InvalidDepthFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color0;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
    ColorAttachment<TextureFormat::RGBA8Unorm> color1;
};

class InvalidDepthPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    InvalidDepthVertexOutput vertex(uint vid [[VertexID]], InvalidDepthVertexInput inputValue [[VertexInput0]])
    {
        InvalidDepthVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    InvalidDepthFrameBuffer fragment(InvalidDepthVertexOutput inputValue)
    {
        InvalidDepthFrameBuffer frameBuffer;
        frameBuffer.color0 = half4(inputValue.color);
        frameBuffer.color1 = half4(inputValue.color);
        return frameBuffer;
    }
};

#endif
