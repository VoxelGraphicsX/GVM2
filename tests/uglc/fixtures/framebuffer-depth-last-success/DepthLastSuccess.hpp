#ifndef UGLC_TEST_DEPTH_LAST_SUCCESS_HPP
#define UGLC_TEST_DEPTH_LAST_SUCCESS_HPP

#include "UGL.h"

using namespace UGL;

struct DepthLastVertexInput
{
    float4 pos [[Attribute0]];
    float4 color [[Attribute1]];
};

struct DepthLastVertexOutput
{
    float4 pos [[Position]];
    float4 color [[Attribute0]];
};

struct DepthLastFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

struct DepthLastLessFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Less> depth;
};

struct DepthLastGreaterFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
    DepthStencilAttachment<TextureFormat::Depth32Float, DepthStencilAttachmentWritePattern::Greater> depth;
};

class DepthLastPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    DepthLastVertexOutput vertex(uint vid [[VertexID]], DepthLastVertexInput inputValue [[VertexInput0]])
    {
        DepthLastVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    DepthLastFrameBuffer fragment(DepthLastVertexOutput inputValue)
    {
        DepthLastFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        return frameBuffer;
    }
};

class DepthLastLessPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    DepthLastVertexOutput vertex(uint vid [[VertexID]], DepthLastVertexInput inputValue [[VertexInput0]])
    {
        DepthLastVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    DepthLastLessFrameBuffer fragment(DepthLastVertexOutput inputValue)
    {
        DepthLastLessFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        frameBuffer.depth = float(inputValue.pos.z);
        return frameBuffer;
    }
};

class DepthLastGreaterPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    DepthLastVertexOutput vertex(uint vid [[VertexID]], DepthLastVertexInput inputValue [[VertexInput0]])
    {
        DepthLastVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        outputValue.color = inputValue.color;
        return outputValue;
    }

    DepthLastGreaterFrameBuffer fragment(DepthLastVertexOutput inputValue)
    {
        DepthLastGreaterFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.color);
        frameBuffer.depth = float(inputValue.pos.z);
        return frameBuffer;
    }
};

#endif
