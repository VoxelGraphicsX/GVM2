#ifndef UGLC_TEST_TEMPLATE_HELPER_OPERATOR_REGRESSION_HPP
#define UGLC_TEST_TEMPLATE_HELPER_OPERATOR_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

namespace TemplateHelperOperatorRegressionHelpers
{
    template <class T>
    T berp(T a, T b, T c, float2 u)
    {
        return a + u.x * (b - a) + u.y * (c - a);
    }
} // namespace TemplateHelperOperatorRegressionHelpers

struct TemplateHelperOperatorRegressionVertexInput
{
    float4 pos [[Attribute0]];
};

struct TemplateHelperOperatorRegressionVertexOutput
{
    float4 pos [[Position]];
};

struct TemplateHelperOperatorRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

class TemplateHelperOperatorRegressionPass final : public IRenderClass
{
public:
    constructor()
    {
    }

private:
    TemplateHelperOperatorRegressionVertexOutput vertex(uint vid [[VertexID]],
                                                        TemplateHelperOperatorRegressionVertexInput inputValue [[VertexInput0]])
    {
        float4 a = inputValue.pos;
        float4 b = inputValue.pos + float4(0.25, 0.0, 0.0, 0.0);
        float4 c = inputValue.pos + float4(0.0, 0.25, 0.0, 0.0);

        TemplateHelperOperatorRegressionVertexOutput outputValue;
        outputValue.pos = TemplateHelperOperatorRegressionHelpers::berp(a, b, c, float2(0.25, 0.25));
        return outputValue;
    }

    TemplateHelperOperatorRegressionFrameBuffer fragment(TemplateHelperOperatorRegressionVertexOutput inputValue)
    {
        TemplateHelperOperatorRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
