#ifndef UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_LOCAL_HPP
#define UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_LOCAL_HPP

#include "UGL.h"

using namespace UGL;

/** Provides a writable buffer so the invalid compute entry has a real shader side effect. */
struct InvalidDSLReservedVertexLocalBindGroup final : public IBindGroup
{
    /** Binds the output buffer used by the invalid reserved-name fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Declares a compute pass whose shader body uses the reserved local variable name `vertex`. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDSLReservedVertexLocalPass final : public IComputeClass
{
public:
    /** Binds resources needed by the compute pass. */
    constructor(BindGroup<InvalidDSLReservedVertexLocalBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Attempts to use `vertex` as a shader local variable, which the DSL reserves. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint vertex = threadID.x;
        bindGroup->values[threadID.x] = vertex;
    }
};

#endif
