#ifndef UGLC_TEST_HLSL_CONST_CONDITIONAL_AGGREGATE_REGRESSION_HPP
#define UGLC_TEST_HLSL_CONST_CONDITIONAL_AGGREGATE_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

/** Provides vertex inputs that create distinct true and false branch aggregate values. */
struct HLSLConstConditionalAggregateRegressionVertexInput
{
    float4 pos [[Attribute0]];
    float4 fallback [[Attribute1]];
};

/** Carries an aggregate value selected through conditional initialization. */
struct HLSLConstConditionalAggregateRegressionVertexOutput
{
    float4 pos [[Position]];
    float4 debugValue [[Attribute0]];
};

/** Defines the framebuffer used by the HLSL const conditional aggregate regression pass. */
struct HLSLConstConditionalAggregateRegressionFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

namespace HLSLConstConditionalAggregateRegressionHelpers
{
    /** Selects scalar and aggregate values so HLSL can keep scalar ternaries and lower aggregate ternaries safely. */
    inline HLSLConstConditionalAggregateRegressionVertexOutput chooseOutput(bool useFirst, HLSLConstConditionalAggregateRegressionVertexOutput firstValue, HLSLConstConditionalAggregateRegressionVertexOutput secondValue)
    {
        const float scalarValue = useFirst ? firstValue.pos.x : secondValue.pos.x;
        const HLSLConstConditionalAggregateRegressionVertexOutput chosen = useFirst ? firstValue : secondValue;
        HLSLConstConditionalAggregateRegressionVertexOutput outputValue = chosen;
        outputValue.debugValue.x = scalarValue;
        return outputValue;
    }
} // namespace HLSLConstConditionalAggregateRegressionHelpers

/** Exercises HLSL conditional initialization lowering for const aggregate locals. */
class HLSLConstConditionalAggregateRegressionPass final : public IRenderClass
{
public:
    /** Creates a pass without external bind groups so the fixture focuses only on expression lowering. */
    constructor()
    {
    }

private:
    /** Produces two aggregate candidates and selects one through a helper-local conditional expression. */
    HLSLConstConditionalAggregateRegressionVertexOutput vertex(uint vid [[VertexID]], HLSLConstConditionalAggregateRegressionVertexInput inputValue [[VertexInput0]])
    {
        HLSLConstConditionalAggregateRegressionVertexOutput firstValue;
        firstValue.pos = inputValue.pos;
        firstValue.debugValue = float4(1.0, 0.0, 0.0, 1.0);

        HLSLConstConditionalAggregateRegressionVertexOutput secondValue;
        secondValue.pos = inputValue.fallback;
        secondValue.debugValue = float4(0.0, 1.0, 0.0, 1.0);

        return HLSLConstConditionalAggregateRegressionHelpers::chooseOutput((vid & 1u) == 0u, firstValue, secondValue);
    }

    /** Writes the selected debug value into the framebuffer. */
    HLSLConstConditionalAggregateRegressionFrameBuffer fragment(HLSLConstConditionalAggregateRegressionVertexOutput inputValue)
    {
        HLSLConstConditionalAggregateRegressionFrameBuffer frameBuffer;
        frameBuffer.color = half4(inputValue.debugValue);
        return frameBuffer;
    }
};

#endif
