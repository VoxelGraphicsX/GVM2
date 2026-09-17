#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STL_CONTAINER_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STL_CONTAINER_HPP

#include "UGL.h"

#include <vector>

using namespace UGL;

struct ExperimentalUGLIRInvalidSTLContainerBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidSTLContainerPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidSTLContainerBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        std::vector<uint> values;
        (void)values;
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
