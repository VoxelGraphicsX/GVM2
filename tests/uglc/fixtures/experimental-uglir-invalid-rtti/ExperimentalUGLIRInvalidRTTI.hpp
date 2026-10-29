#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_RTTI_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_RTTI_HPP

#include "UGL.h"

#include <typeinfo>

using namespace UGL;

struct ExperimentalUGLIRInvalidRTTIBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidRTTIPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidRTTIBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)typeid(threadID);
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
