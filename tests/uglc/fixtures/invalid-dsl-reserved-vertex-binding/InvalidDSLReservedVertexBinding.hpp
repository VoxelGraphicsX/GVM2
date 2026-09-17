#ifndef UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_BINDING_HPP
#define UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_BINDING_HPP

#include "UGL.h"

using namespace UGL;

/** Provides a writable buffer so the invalid shader binding variable is used by shader code. */
struct InvalidDSLReservedVertexBindingBindGroup final : public IBindGroup
{
    /** Binds the output buffer used by the invalid reserved-binding fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Declares a compute pass whose shader binding variable uses the reserved name `vertex`. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDSLReservedVertexBindingPass final : public IComputeClass
{
public:
    /** Binds a resource variable named `vertex`, which the shader DSL reserves. */
    constructor(BindGroup<InvalidDSLReservedVertexBindingBindGroup> vertex [[Slot0]])
    {
    }

private:
    /** Uses the invalid binding variable so the pass has a real shader dependency. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        vertex->values[threadID.x] = threadID.x;
    }
};

#endif
