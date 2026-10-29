#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_THREAD_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_THREAD_HPP

#include "UGL.h"

#include <thread>

using namespace UGL;

struct ExperimentalUGLIRInvalidThreadBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidThreadPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidThreadBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        std::thread worker;
        (void)worker;
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
