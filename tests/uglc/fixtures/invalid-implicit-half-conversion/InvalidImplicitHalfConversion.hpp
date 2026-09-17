#ifndef UGLC_TEST_INVALID_IMPLICIT_HALF_CONVERSION_HPP
#define UGLC_TEST_INVALID_IMPLICIT_HALF_CONVERSION_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidImplicitHalfConversionBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidImplicitHalfConversionPass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidImplicitHalfConversionBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        half value = 1.0f;
        bindGroup->values[threadID.x] = uint(float(value));
    }
};

#endif
