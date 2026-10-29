#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_RAW_POINTER_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_RAW_POINTER_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidRawPointerBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidRawPointerPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidRawPointerBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint *rawValue;
        (void)rawValue;
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
