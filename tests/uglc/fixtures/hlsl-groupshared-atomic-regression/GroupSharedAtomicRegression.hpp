#ifndef UGLC_TEST_HLSL_GROUPSHARED_ATOMIC_REGRESSION_HPP
#define UGLC_TEST_HLSL_GROUPSHARED_ATOMIC_REGRESSION_HPP

#include "UGL.h"

using namespace UGL;

class [[LocalWorkGroupSize(32, 1, 1)]] GroupSharedAtomicRegressionPass final : public IComputeClass
{
public:
    constructor()
    {
    }

private:
    void compute(uint GI [[GroupIndex]])
    {
        GroupShared<uint> scalarCounter;
        GroupShared<uint> scratch[1];

        if (GI == 0u)
        {
            atomicStore(scalarCounter, 0u);
            atomicStore(scratch[0], 0u);
        }

        GroupMemoryBarrierWithGroupSync();

        const uint scalarBase = atomicAdd(scalarCounter, 1u);
        uint assignedBase = 0u;
        assignedBase = atomicAdd(scalarCounter, 1u);
        atomicOr(scratch[0], 1u << (GI & 31u));
        atomicOr(scalarCounter, (scalarBase | assignedBase) & 31u);
        const uint andBase = atomicAnd(scalarCounter, 0xfffffff0u);
        uint assignedMask = 0u;
        assignedMask = atomicAnd(scalarCounter, 0xffffffefu);
        atomicAnd(scratch[0], andBase | assignedMask);
        const uint loadedCounter = atomicLoad(scalarCounter);
        if ((loadedCounter & 1u) == 0u)
        {
            atomicAdd(scalarCounter, 1u);
        }
    }
};

/**
 * Provides a small device-memory producer/consumer surface for atomic publication regression tests.
 */
struct DeviceAtomicPublishBindGroup final : public IBindGroup
{
    /**
     * Binds the payload buffer and the device-visible atomic counter buffer.
     */
    constructor(RWStructuredBuffer<uint> payload [[Binding0]],
                RWStructuredBuffer<uint> counter [[Binding1]])
    {
    }
};

/**
 * Verifies that payload writes can be fenced before a device counter publishes the payload to other workgroups.
 */
class [[LocalWorkGroupSize(64, 1, 1)]] DeviceAtomicPublishRegressionPass final : public IComputeClass
{
public:
    /**
     * Binds the device atomic regression buffers.
     */
    constructor(BindGroup<DeviceAtomicPublishBindGroup> data [[Slot0]])
    {
    }

private:
    /**
     * Publishes one payload value through a fenced device counter and consumes it in the same dispatch.
     */
    void compute(uint GI [[GroupIndex]])
    {
        if (GI == 0u)
        {
            data->payload[0] = 42u;
            // Metal device atomics only compile portably with relaxed ordering here, so the DSL must preserve the
            // explicit fence shape: payload write -> DeviceMemoryBarrier() -> relaxed counter publish.
            DeviceMemoryBarrier();
            atomicStore(data->counter[0], 1u);
        }
        DeviceMemoryBarrierWithGroupSync();
        if (atomicLoad(data->counter[0]) != 0u)
        {
            const uint value = data->payload[0];
            if (value == 42u)
            {
                atomicAdd(data->counter[0], 1u);
            }
        }
    }
};

#endif
