#ifndef UGLC_TEST_INVALID_COMPUTE_SLOT8_HPP
#define UGLC_TEST_INVALID_COMPUTE_SLOT8_HPP

#include "UGL.h"

using namespace UGL;

struct InvalidComputeSlot8BindGroup0 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup1 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup2 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup3 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup4 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup5 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup6 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup7 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

struct InvalidComputeSlot8BindGroup8 final : public IBindGroup
{
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] InvalidComputeSlot8Pass final : public IComputeClass
{
public:
    constructor(BindGroup<InvalidComputeSlot8BindGroup0> bindGroup0 [[Slot0]],
                BindGroup<InvalidComputeSlot8BindGroup1> bindGroup1 [[Slot1]],
                BindGroup<InvalidComputeSlot8BindGroup2> bindGroup2 [[Slot2]],
                BindGroup<InvalidComputeSlot8BindGroup3> bindGroup3 [[Slot3]],
                BindGroup<InvalidComputeSlot8BindGroup4> bindGroup4 [[Slot4]],
                BindGroup<InvalidComputeSlot8BindGroup5> bindGroup5 [[Slot5]],
                BindGroup<InvalidComputeSlot8BindGroup6> bindGroup6 [[Slot6]],
                BindGroup<InvalidComputeSlot8BindGroup7> bindGroup7 [[Slot7]],
                BindGroup<InvalidComputeSlot8BindGroup8> bindGroup8 [[Slot8]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup8->values[threadID.x] = bindGroup8->values[threadID.x] + 1u;
    }
};

#endif
