#pragma once
#include "UGL.h"

using namespace UGL;

/** Stores ordinary vector and scalar fields used for atomic addressing and cross-workgroup publication tests. */
struct AtomicStorageState
{
    uint4 counters;
    int4 signedCounters;
    uint scalar;
    uint ordinary;
    uint winner;
    uint successfulClaims;
    uint4 payload;
    uint ready;
    uint acknowledged;
    uint readError;
    uint padding;
};

/** Binds ordinary structured storage and deterministic per-invocation observations. */
struct AtomicStorageBindings final : public IBindGroup
{
    /** Connects writable states and observation words without atomic-qualified DSL resource types. */
    constructor(RWStructuredBuffer<AtomicStorageState> states [[Binding0]],
                RWStructuredBuffer<uint> observations [[Binding1]])
    {
    }
};

/** Reads and modifies invocation-local copies of the same fields targeted by storage atomics. */
uint inspectAtomicStorageCopy(AtomicStorageState snapshot, uint4 configuration)
{
    AtomicStorageState copy = snapshot;
    copy.counters.y += 2u;
    return snapshot.counters.y + configuration.y + copy.counters.y + snapshot.ordinary;
}

/** Tests exact scalar addresses inside buffer vectors alongside ordinary copies, scalar fields, and workgroup arrays. */
class [[LocalWorkGroupSize(32, 1, 1)]] AtomicStorageVectorPass final : public IComputeClass
{
public:
    /** Creates the vector addressing pass using the existing Slot binding API. */
    constructor(BindGroup<AtomicStorageBindings> bindings [[Slot0]])
    {
    }

private:
    /** Accumulates deterministic values in one independent record per workgroup and observes local copies. */
    void compute(uint3 threadID [[DispatchThreadID]], uint3 groupID [[GroupID]], uint lane [[GroupIndex]])
    {
        GroupShared<uint> shared[4];
        if (lane < 4u)
        {
            bindings->states[groupID.x].counters[lane] = 1u;
            atomicStore(shared[lane], 0u);
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane < 4u)
        {
            const uint saved = bindings->states[groupID.x].counters[lane];
            bindings->states[groupID.x].counters[lane] += saved;
            ++bindings->states[groupID.x].counters[lane];
        }
        GroupMemoryBarrierWithGroupSync();

        atomicAdd(bindings->states[groupID.x].counters.x, 1u);
        atomicAdd(bindings->states[groupID.x].counters[lane & 3u], 1u);
        uint index = lane;
        atomicAdd(bindings->states[groupID.x].counters[(index++) & 3u], 1u);
        uint row = groupID.x;
        atomicAdd(bindings->states[row++].scalar, 1u);
        atomicAdd(shared[lane & 3u], 1u);
        atomicOr(bindings->states[groupID.x].counters.z, 0u);
        atomicAnd(bindings->states[groupID.x].counters.w, 0xffffffffu);
        atomicMax(bindings->states[groupID.x].counters.y, 0u);
        atomicMin(bindings->states[groupID.x].counters.y, 0xffffffffu);
        atomicAdd(bindings->states[groupID.x].signedCounters.x, -1);
        atomicOr(bindings->states[groupID.x].signedCounters.y, int(1u << lane));
        atomicMin(bindings->states[groupID.x].signedCounters.z, -int(lane));
        atomicMax(bindings->states[groupID.x].signedCounters.w, int(lane));

        uint original = 0u;
        atomicCompareExchange(bindings->states[groupID.x].winner, 0u, 1u, original);
        if (original == 0u) atomicAdd(bindings->states[groupID.x].successfulClaims, 1u);
        atomicCompareExchange(bindings->states[groupID.x].winner, 77u, 88u, original);
        AllMemoryBarrierWithGroupSync();
        if (lane == 0u)
        {
            bindings->states[groupID.x].ordinary = 9u;
            bindings->states[groupID.x].ordinary += 1u;
            ++bindings->states[groupID.x].ordinary;
        }
        AllMemoryBarrierWithGroupSync();

        const AtomicStorageState snapshot = bindings->states[groupID.x];
        const uint4 configuration = uint4(11u, 13u, 17u, 19u);
        const uint base = threadID.x * 4u;
        bindings->observations[base] = atomicLoad(shared[lane & 3u]);
        bindings->observations[base + 1u] = inspectAtomicStorageCopy(snapshot, configuration);
        bindings->observations[base + 2u] = index + row + uint4(bindings->states[groupID.x].counters).y;
        bindings->observations[base + 3u] = original;
    }
};

/** Counts independent producer/consumer pairs without requiring workgroup spin loops to make mutual progress. */
constexpr uint AtomicStoragePublicationPairCount = 128u;

/** Tests publication visibility between independent workgroups using immutable payloads and bounded observation. */
class [[LocalWorkGroupSize(32, 1, 1)]] AtomicStoragePublicationPass final : public IComputeClass
{
public:
    /** Creates the publication pass with one independent message per producer/consumer pair. */
    constructor(BindGroup<AtomicStorageBindings> bindings [[Slot0]])
    {
    }

private:
    /** Publishes immutable messages without waiting for another workgroup to release the producer. */
    void compute(uint3 groupID [[GroupID]], uint lane [[GroupIndex]])
    {
        const uint pair = groupID.x / 2u;
        if (lane != 0u || pair >= AtomicStoragePublicationPairCount) return;
        const uint value = pair + 1u;
        if ((groupID.x & 1u) == 0u)
        {
            bindings->states[pair].payload = uint4(value, value ^ 0x55aau, value * 3u, ~value);
            if ((pair & 1u) == 0u)
                atomicStore(bindings->states[pair].ready, 1u);
            else
            {
                DeviceMemoryBarrier();
                atomicAdd(bindings->states[pair].ready, 1u);
            }
        }
        else
        {
            uint polls = 0u;
            while (atomicLoad(bindings->states[pair].ready) != 1u && polls < 100000u)
                ++polls;
            if (polls == 100000u) return;
            const uint4 payload = bindings->states[pair].payload;
            if (payload.x != value || payload.y != (value ^ 0x55aau) ||
                payload.z != value * 3u || payload.w != ~value)
                bindings->states[pair].readError = 1u;
            atomicStore(bindings->states[pair].acknowledged, 1u);
            bindings->observations[pair] = 1u;
        }
    }
};
