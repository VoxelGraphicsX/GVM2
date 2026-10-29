#ifndef UGLC_TEST_DSL_RESERVED_VERTEX_HOST_ONLY_HPP
#define UGLC_TEST_DSL_RESERVED_VERTEX_HOST_ONLY_HPP

#include "UGL.h"

using namespace UGL;

/** Uses the name `vertex` only in host-visible code that is not part of a shader artifact. */
inline uint makeHostOnlyVertexValue()
{
    uint vertex = 7u;
    return vertex;
}

/** Provides a writable buffer for the positive reserved-name control fixture. */
struct DSLReservedVertexHostOnlyBindGroup final : public IBindGroup
{
    /** Binds the output buffer used by the positive reserved-name fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Declares a valid compute pass while a host-only helper uses the reserved local name. */
class [[LocalWorkGroupSize(1, 1, 1)]] DSLReservedVertexHostOnlyPass final : public IComputeClass
{
public:
    /** Binds resources needed by the compute pass. */
    constructor(BindGroup<DSLReservedVertexHostOnlyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a simple value without referencing the host-only helper. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x;
    }
};

#endif
