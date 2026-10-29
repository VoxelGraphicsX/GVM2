#ifndef UGLC_TEST_HOST_EXPRESSION_PRECEDENCE_REGRESSION_HPP
#define UGLC_TEST_HOST_EXPRESSION_PRECEDENCE_REGRESSION_HPP

#include "UGL.h"
#include <EASTL/vector.h>

using namespace UGL;

struct HostExpressionPrecedenceVertexInput
{
    float4 pos [[Attribute0]];
};

struct HostExpressionPrecedenceVertexOutput
{
    float4 pos [[Position]];
};

struct HostExpressionPrecedenceFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

struct HostExpressionPrecedenceScalar
{
    float value;
};

inline HostExpressionPrecedenceScalar operator-(HostExpressionPrecedenceScalar lhs,
                                                HostExpressionPrecedenceScalar rhs)
{
    return HostExpressionPrecedenceScalar{lhs.value - rhs.value};
}

inline HostExpressionPrecedenceScalar operator*(HostExpressionPrecedenceScalar lhs, float rhs)
{
    return HostExpressionPrecedenceScalar{lhs.value * rhs};
}

class HostExpressionPrecedencePass final : public IRenderClass
{
public:
    constructor()
    {
        eastl::vector<float> heightSamples;
        heightSamples.resize(4);

        float sampleSpacing = (heightSamples.size() - 1u) * 0.05f;
        HostExpressionPrecedenceScalar base{sampleSpacing};
        HostExpressionPrecedenceScalar offset{0.10f};
        HostExpressionPrecedenceScalar adjusted = (base - offset) * 0.50f;
        mScale = sampleSpacing + adjusted.value;
    }

private:
    float mScale = 0.0f;

    HostExpressionPrecedenceVertexOutput vertex(uint vid [[VertexID]],
                                                HostExpressionPrecedenceVertexInput inputValue [[VertexInput0]])
    {
        HostExpressionPrecedenceVertexOutput outputValue;
        outputValue.pos = inputValue.pos;
        return outputValue;
    }

    HostExpressionPrecedenceFrameBuffer fragment(HostExpressionPrecedenceVertexOutput inputValue)
    {
        HostExpressionPrecedenceFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.pos.xy, 0.0, 1.0);
        return frameBuffer;
    }
};

#endif
