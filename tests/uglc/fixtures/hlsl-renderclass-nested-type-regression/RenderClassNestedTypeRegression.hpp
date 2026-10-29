#ifndef UGLC_TEST_HLSL_RENDERCLASS_NESTED_TYPE_REGRESSION_HPP
#define UGLC_TEST_HLSL_RENDERCLASS_NESTED_TYPE_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace RenderClassNestedType
{
    struct VertexInput
    {
        float4 pos [[Attribute0]];
    };

    struct FrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    class ScopedPass final : public IRenderClass
    {
    public:
        constructor()
        {
        }

    private:
        struct VertexOutput
        {
            float4 pos [[Position]];
            float value [[Attribute0]];
        };

        VertexOutput vertex(uint vid [[VertexID]], VertexInput inputValue [[VertexInput0]])
        {
            VertexOutput outputValue;
            outputValue.pos = inputValue.pos;
            outputValue.value = inputValue.pos.x + float(vid);
            return outputValue;
        }

        FrameBuffer fragment(VertexOutput inputValue)
        {
            FrameBuffer frameBuffer;
            frameBuffer.color = half4(inputValue.value, 0.0f, 0.0f, 1.0f);
            return frameBuffer;
        }
    };
} // namespace RenderClassNestedType

#endif
