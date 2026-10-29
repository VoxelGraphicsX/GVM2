#ifndef UGLC_TEST_HLSL_NAMESPACE_SOURCE_ORDER_HPP
#define UGLC_TEST_HLSL_NAMESPACE_SOURCE_ORDER_HPP

#include "UGL.h"

using namespace UGL;

struct HlslNamespaceSourceOrderBindGroup final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace HlslNamespaceSourceOrder
{
    inline uint AddOne(uint value)
    {
        return value + 1;
    }

    struct Accumulator
    {
        uint evaluate(uint value) const
        {
            return AddOne(value);
        }
    };
} // namespace HlslNamespaceSourceOrder

class [[LocalWorkGroupSize(1, 1, 1)]] HlslNamespaceSourceOrderPass final : public IComputeClass
{
public:
    constructor(BindGroup<HlslNamespaceSourceOrderBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        HlslNamespaceSourceOrder::Accumulator accumulator;
        bindGroup->values[threadID.x] = accumulator.evaluate(threadID.x);
    }
};

#endif
