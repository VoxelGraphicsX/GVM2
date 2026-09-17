#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_ADDRESS_OF_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_ADDRESS_OF_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidAddressOfBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidAddressOfPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidAddressOfBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint value = threadID.x;
        bindGroup->values[threadID.x] = *(&value);
    }
};

#endif
