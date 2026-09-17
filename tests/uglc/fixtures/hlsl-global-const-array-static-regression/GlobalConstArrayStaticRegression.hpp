#ifndef UGLC_TEST_HLSL_GLOBAL_CONST_ARRAY_STATIC_REGRESSION_HPP
#define UGLC_TEST_HLSL_GLOBAL_CONST_ARRAY_STATIC_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace GlobalConstArrayStaticRegressionNs
{
    struct TableEntry
    {
        float4 pos;
        float value;
    };

    const TableEntry VertexTable[] = {
        {float4(-0.5f, -0.5f, 0.0f, 1.0f), 0.25f},
        {float4(0.0f, 0.5f, 0.0f, 1.0f), 0.50f},
        {float4(0.5f, -0.5f, 0.0f, 1.0f), 0.75f},
    };

    struct VertexOutput
    {
        float4 pos [[Position]];
        float value [[Attribute0]];
    };

    struct FrameBuffer final : public IFrameBuffer
    {
        ColorAttachment<TextureFormat::RGBA8Unorm> color;
    };

    class GlobalConstArrayStaticPass final : public IRenderClass
    {
    public:
        constructor()
        {
        }

    private:
        VertexOutput vertex(uint vid [[VertexID]])
        {
            VertexOutput outputValue;
            const TableEntry entry = VertexTable[vid];
            outputValue.pos = entry.pos;
            outputValue.value = entry.value;
            return outputValue;
        }

        FrameBuffer fragment(VertexOutput inputValue)
        {
            FrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.value, 0.0f, 0.0f, 1.0f);
            return frameBuffer;
        }
    };
} // namespace GlobalConstArrayStaticRegressionNs

#endif
