#ifndef UGLC_TEST_COMPUTE_OVERLOAD_ENTRY_SELECTION_HPP
#define UGLC_TEST_COMPUTE_OVERLOAD_ENTRY_SELECTION_HPP

#include "UGL.h"

using namespace UGL;

struct ComputeOverloadBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace ComputeOverloadEntrySelectionHelpers
{
    inline void writeValue(BindGroup<ComputeOverloadBindGroup> bindGroup, uint index)
    {
        bindGroup->values[index] = bindGroup->values[index] + 41u;
    }
} // namespace ComputeOverloadEntrySelectionHelpers

class [[LocalWorkGroupSize(4, 1, 1)]] ComputeOverloadEntrySelectionPass final : public IComputeClass
{
public:
    constructor(BindGroup<ComputeOverloadBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        ComputeOverloadEntrySelectionHelpers::writeValue(bindGroup, threadID.x);
    }
};

#endif
