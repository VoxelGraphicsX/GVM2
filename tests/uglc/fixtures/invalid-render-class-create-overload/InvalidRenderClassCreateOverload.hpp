#ifndef UGLC_TEST_INVALID_RENDER_CLASS_CREATE_OVERLOAD_HPP
#define UGLC_TEST_INVALID_RENDER_CLASS_CREATE_OVERLOAD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderClassCreateOverloadVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidRenderClassCreateOverloadFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidRenderClassCreateOverloadPass final : public IRenderClass
{
public:
    constructor()
    {
    }

    void create(uint value)
    {
        (void)value;
    }

private:
    InvalidRenderClassCreateOverloadVertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidRenderClassCreateOverloadVertexOutput outputValue;
        outputValue.pos = float4(float(vertexID), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidRenderClassCreateOverloadFrameBuffer fragment(InvalidRenderClassCreateOverloadVertexOutput inputValue)
    {
        InvalidRenderClassCreateOverloadFrameBuffer outputValue;
        outputValue.color = half4(inputValue.pos.xyz, 1.0f);
        return outputValue;
    }
};

#endif
