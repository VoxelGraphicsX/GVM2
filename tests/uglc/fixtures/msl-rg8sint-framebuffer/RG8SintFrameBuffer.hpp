#ifndef UGLC_TEST_MSL_RG8SINT_FRAMEBUFFER_HPP
#define UGLC_TEST_MSL_RG8SINT_FRAMEBUFFER_HPP

#include "UGL.h"

using namespace UGL;

struct RG8SintVertexInput final
{
    float4 pos [[Attribute0]];
};

struct RG8SintVertexOutput final
{
    float4 pos [[Position]];
};

struct RG8SintFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RG8Sint> color;
};

class RG8SintRenderPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    RG8SintVertexOutput vertex(RG8SintVertexInput inputValue [[VertexInput0]])
    {
        RG8SintVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    RG8SintFrameBuffer fragment(RG8SintVertexOutput inputValue)
    {
        (void)inputValue;
        RG8SintFrameBuffer frameBuffer;
        return frameBuffer;
    }
};

#endif
