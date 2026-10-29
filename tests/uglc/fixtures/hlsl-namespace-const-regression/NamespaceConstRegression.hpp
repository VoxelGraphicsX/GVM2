#ifndef UGLC_TEST_HLSL_NAMESPACE_CONST_REGRESSION_HPP
#define UGLC_TEST_HLSL_NAMESPACE_CONST_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace NamespaceConstPassNs
{
    static const float VertexScale = 0.5f;

    struct VertexInput
    {
        float4 pos [[Attribute0]];
    };

    struct VertexOutput
    {
        float4 pos [[Position]];
        float scaled [[Attribute0]];
    };

    struct FrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    class NamespaceConstPass final : public IRenderClass
    {
    public:
        constructor()
        {
        }

    private:
        VertexOutput vertex(uint vid [[VertexID]], VertexInput inputValue [[VertexInput0]])
        {
            VertexOutput outputValue;
            outputValue.pos = inputValue.pos;
            outputValue.scaled = inputValue.pos.x * VertexScale;
            return outputValue;
        }

        FrameBuffer fragment(VertexOutput inputValue)
        {
            FrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.scaled, 0.0f, 0.0f, 1.0f);
            return frameBuffer;
        }
    };
} // namespace NamespaceConstPassNs

#endif
