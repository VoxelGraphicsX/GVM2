#ifndef UGLC_TEST_INVALID_STRUCTURED_BUFFER_WRITE_HPP
#define UGLC_TEST_INVALID_STRUCTURED_BUFFER_WRITE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidStructuredBufferWriteBindGroup final : public IBindGroup
{
    constructor(StructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidStructuredBufferWritePass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidStructuredBufferWriteBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = 1u;
    }
};

#endif
