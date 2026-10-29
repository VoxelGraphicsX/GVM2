#ifndef UGLC_TEST_SHADER_EXPLICIT_PRECISION_CONVERSION_HPP
#define UGLC_TEST_SHADER_EXPLICIT_PRECISION_CONVERSION_HPP

#include "UGL.h"

using namespace UGL;

struct ShaderExplicitPrecisionConversionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ShaderExplicitPrecisionConversionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ShaderExplicitPrecisionConversionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        half scalar = half(1.0f);
        float4 wideValue = float4(1.0f);
        half4 narrowValue = half4(wideValue);
        half4 splatValue = half4(1.0f);
        float4 sameTypeB = wideValue + wideValue;
        float2 swizzled = sameTypeB.xy;
        bindGroup->values[threadID.x] = uint(float(scalar)) + uint(float(narrowValue.y)) + uint(float(splatValue.x)) + uint(swizzled.x);
    }
};

#endif
