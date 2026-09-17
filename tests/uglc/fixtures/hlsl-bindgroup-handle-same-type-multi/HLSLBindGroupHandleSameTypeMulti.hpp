#ifndef UGLC_TEST_HLSL_BINDGROUP_HANDLE_SAME_TYPE_MULTI_HPP
#define UGLC_TEST_HLSL_BINDGROUP_HANDLE_SAME_TYPE_MULTI_HPP

#include "UGL.h"

using namespace UGL;

/** Provides one storage buffer layout that is intentionally bound through two shader slots. */
struct SameTypeMultiBindGroup final : public IBindGroup
{
    /** Creates the bind group with one read-write storage buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Stores a BindGroup handle and exposes ordinary member methods over the selected binding. */
template <class BindGroupType>
struct SameTypeBindGroupHandleAccess
{
    /** Stores the bind group selected by the caller. */
    BindGroup<BindGroupType> storedBindGroup;

    /** Initializes the behavior object with a caller-selected bind group handle. */
    void init(BindGroup<BindGroupType> bindGroup)
    {
        storedBindGroup = bindGroup;
    }

    /** Reads a value through the stored bind group handle. */
    uint read(uint index)
    {
        return storedBindGroup->values[index];
    }

    /** Writes a value through the stored bind group handle. */
    void write(uint index, uint value)
    {
        storedBindGroup->values[index] = value;
    }
};

/** Verifies that two shader slots with the same BindGroup type remain distinguishable in HLSL helper code. */
class [[LocalWorkGroupSize(1, 1, 1)]] HLSLBindGroupHandleSameTypeMultiPass final : public IComputeClass
{
public:
    /** Captures two bind groups with the same concrete layout on different slots. */
    constructor(BindGroup<SameTypeMultiBindGroup> sourceBindGroup [[Slot0]],
                BindGroup<SameTypeMultiBindGroup> destinationBindGroup [[Slot1]])
    {
    }

private:
    /** Copies and increments values through two independent handle-backed behavior objects. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;

        SameTypeBindGroupHandleAccess<SameTypeMultiBindGroup> sourceHandleAccess;
        sourceHandleAccess.init(sourceBindGroup);

        SameTypeBindGroupHandleAccess<SameTypeMultiBindGroup> destinationHandleAccess;
        destinationHandleAccess.init(destinationBindGroup);

        const uint sourceValue = sourceHandleAccess.read(index);
        destinationHandleAccess.write(index, sourceValue + 1u);
    }
};

#endif
