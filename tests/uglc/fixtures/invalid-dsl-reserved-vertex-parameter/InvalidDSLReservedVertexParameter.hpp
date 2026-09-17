#ifndef UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_PARAMETER_HPP
#define UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_PARAMETER_HPP

#include "UGL.h"

using namespace UGL;

/** Groups the invalid shader helper that uses a reserved parameter name. */
namespace InvalidDSLReservedVertexParameterHelpers
{
    /** Attempts to use `vertex` as a shader helper parameter, which the DSL reserves. */
    inline uint addOne(uint vertex)
    {
        return vertex + 1u;
    }
} // namespace InvalidDSLReservedVertexParameterHelpers

/** Provides a writable buffer so the invalid helper call has a real shader side effect. */
struct InvalidDSLReservedVertexParameterBindGroup final : public IBindGroup
{
    /** Binds the output buffer used by the invalid reserved-name fixture. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Declares a compute pass that references a free helper with a reserved parameter name. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDSLReservedVertexParameterPass final : public IComputeClass
{
public:
    /** Binds resources needed by the compute pass. */
    constructor(BindGroup<InvalidDSLReservedVertexParameterBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Calls the invalid helper so its parameter surface enters the shader artifact. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = InvalidDSLReservedVertexParameterHelpers::addOne(threadID.x);
    }
};

#endif
