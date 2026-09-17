#ifndef UGLC_TEST_STRUCTURED_BUFFER_ATOMIC_LOAD_READONLY_HPP
#define UGLC_TEST_STRUCTURED_BUFFER_ATOMIC_LOAD_READONLY_HPP

#include "UGL.h"

using namespace UGL;

struct StructuredBufferAtomicLoadInputCounter
{
    uint keyCounter;
};

struct StructuredBufferAtomicLoadReadOnlyBindGroup final : public IBindGroup
{
    constructor(StructuredBuffer<StructuredBufferAtomicLoadInputCounter> inputCounter [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] StructuredBufferAtomicLoadReadOnlyPass final : public IComputeClass
{
public:
    constructor(BindGroup<StructuredBufferAtomicLoadReadOnlyBindGroup> tessBindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint inputKeyOffset = atomicLoad(tessBindGroup->inputCounter[0].keyCounter);
        if (threadID.x == inputKeyOffset)
        {
            return;
        }
    }
};

#endif
