#ifndef UGLC_TEST_INVALID_LEGACY_ATOMIC_TEMPLATE_HPP
#define UGLC_TEST_INVALID_LEGACY_ATOMIC_TEMPLATE_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidLegacyAtomicTemplateBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<Atomic<uint>> counters [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidLegacyAtomicTemplatePass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidLegacyAtomicTemplateBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        (void)threadID;
    }
};

#endif
