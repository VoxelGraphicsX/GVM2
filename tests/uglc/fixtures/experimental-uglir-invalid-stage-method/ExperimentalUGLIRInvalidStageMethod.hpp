#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STAGE_METHOD_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STAGE_METHOD_HPP

#include "UGL.h"

using namespace UGL;

struct ExperimentalUGLIRInvalidStageMethodBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidStageMethodPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRInvalidStageMethodBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void vertex()
    {
    }

    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
