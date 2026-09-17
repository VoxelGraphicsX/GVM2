#ifndef UGLC_TEST_ENTRY_LOCAL_BINDGROUP_LEAK_NEGATIVE_HPP
#define UGLC_TEST_ENTRY_LOCAL_BINDGROUP_LEAK_NEGATIVE_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output buffer used by the invalid bind-group leak pass. */
struct EntryLocalBindGroupLeakNegativeBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace EntryLocalBindGroupLeakNegativeHelpers
{
    /** Represents an invalid namespace-scope shader resource handle. */
    BindGroup<EntryLocalBindGroupLeakNegativeBindGroup> leakedBindGroup;

    /** Attempts to use the invalid global bind-group handle from shader helper code. */
    inline void writeThroughLeakedBindGroup(uint index)
    {
        leakedBindGroup->values[index] = 3u;
    }
} // namespace EntryLocalBindGroupLeakNegativeHelpers

/** Calls a helper that reaches a namespace-scope bind group instead of receiving one as a parameter. */
class [[LocalWorkGroupSize(1, 1, 1)]] EntryLocalBindGroupLeakNegativePass final : public IComputeClass
{
public:
    /** Captures the valid shader entry bind group. */
    constructor(BindGroup<EntryLocalBindGroupLeakNegativeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Calls the invalid helper so the shared reference collector reports the resource-scope error. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        EntryLocalBindGroupLeakNegativeHelpers::writeThroughLeakedBindGroup(threadID.x);
    }
};

#endif
