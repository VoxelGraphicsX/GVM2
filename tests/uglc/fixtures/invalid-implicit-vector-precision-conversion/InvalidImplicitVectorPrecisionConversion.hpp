#ifndef UGLC_TEST_INVALID_IMPLICIT_VECTOR_PRECISION_CONVERSION_HPP
#define UGLC_TEST_INVALID_IMPLICIT_VECTOR_PRECISION_CONVERSION_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidImplicitVectorPrecisionConversionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidImplicitVectorPrecisionConversionPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidImplicitVectorPrecisionConversionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        float4 source = float4(1.0f);
        half4 value = source;
        bindGroup->values[threadID.x] = uint(float(value.x));
    }
};

#endif
