#ifndef UGLC_TEST_STRUCTURED_BUFFER_READONLY_HPP
#define UGLC_TEST_STRUCTURED_BUFFER_READONLY_HPP

#include "UGL.h"

using namespace UGL;

struct StructuredBufferReadOnlyBindGroup final : public IBindGroup
{
    constructor(StructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(8, 1, 1)]] StructuredBufferReadOnlyPass final : public IComputeClass
{
public:
    constructor(BindGroup<StructuredBufferReadOnlyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint value = bindGroup->values[threadID.x];
        if (value == 0u)
        {
            return;
        }
    }
};

#endif
