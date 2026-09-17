#ifndef UGLC_TEST_INVALID_RENDER_CLASS_HELPER_METHOD_HPP
#define UGLC_TEST_INVALID_RENDER_CLASS_HELPER_METHOD_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidRenderClassHelperVertexOutput
{
    float4 pos [[Position]];
};

struct InvalidRenderClassHelperFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class InvalidRenderClassHelperPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    float helper()
    {
        return 1.0f;
    }

    InvalidRenderClassHelperVertexOutput vertex(uint vertexID [[VertexID]])
    {
        InvalidRenderClassHelperVertexOutput outputValue;
        outputValue.pos = float4(float(vertexID), 0.0f, 0.0f, 1.0f);
        return outputValue;
    }

    InvalidRenderClassHelperFrameBuffer fragment(InvalidRenderClassHelperVertexOutput inputValue)
    {
        InvalidRenderClassHelperFrameBuffer outputValue;
        outputValue.color = half4(inputValue.pos.xyz, 1.0f);
        return outputValue;
    }
};

#endif
