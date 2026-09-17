#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_COMPUTE_CONTROL_FLOW_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_COMPUTE_CONTROL_FLOW_HPP

#include "UGL.h"

using namespace UGL;

inline uint experimentalUGLIRControlFlowHelper(uint value)
{
    if (value > 8u)
    {
        return value - 1u;
    }
    return value + 1u;
}

struct ExperimentalUGLIRComputeControlFlowBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(4, 1, 1)]] ExperimentalUGLIRComputeControlFlowPass final : public IComputeClass
{
public:
    constructor(BindGroup<ExperimentalUGLIRComputeControlFlowBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint value = experimentalUGLIRControlFlowHelper(threadID.x);
        if (value > 0u)
        {
            for (uint i = 0u; i < 2u; i = i + 1u)
            {
                value = value + i;
            }
        }
        while (value < 8u)
        {
            value = value + 1u;
        }
        bindGroup->values[threadID.x] = value;
    }
};

#endif
