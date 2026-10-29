#ifndef UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_FIELD_HPP
#define UGLC_TEST_INVALID_DSL_RESERVED_VERTEX_FIELD_HPP

#include "UGL.h"

using namespace UGL;

/** Provides a shader-visible payload with an invalid reserved field name. */
struct InvalidDSLReservedVertexFieldPayload
{
    uint vertex;
};

/** Provides a structured buffer whose element type exposes the invalid field to shader code. */
struct InvalidDSLReservedVertexFieldBindGroup final : public IBindGroup
{
    /** Binds the payload buffer used by the invalid reserved-field fixture. */
    constructor(RWStructuredBuffer<InvalidDSLReservedVertexFieldPayload> values [[Binding0]])
    {
    }
};

/** Declares a compute pass that makes the invalid payload record shader-visible. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDSLReservedVertexFieldPass final : public IComputeClass
{
public:
    /** Binds resources needed by the compute pass. */
    constructor(BindGroup<InvalidDSLReservedVertexFieldBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads the invalid payload record so the reserved field name enters the shader artifact. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        InvalidDSLReservedVertexFieldPayload payload = bindGroup->values[threadID.x];
        bindGroup->values[threadID.x] = payload;
    }
};

#endif
