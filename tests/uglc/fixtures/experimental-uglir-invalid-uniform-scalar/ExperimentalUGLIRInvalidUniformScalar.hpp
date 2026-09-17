#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_UNIFORM_SCALAR_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_UNIFORM_SCALAR_HPP

#include "UGL.h"

using namespace UGL;

/** Binds a vector uniform payload that the direct SPIR-V backend wraps in a Uniform block. */
struct ExperimentalUGLIRInvalidUniformScalarBindGroup final : public IBindGroup
{
    /** Creates the bind group with a vector uniform payload and writable output storage. */
    constructor(UniformBuffer<float4> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

/** Exercises scalar/vector UniformBuffer lowering in the experimental SPIR-V backend. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidUniformScalarPass final : public IComputeClass
{
public:
    /** Creates the compute pass with the uniform bind group. */
    constructor(BindGroup<ExperimentalUGLIRInvalidUniformScalarBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads the vector uniform payload and writes a small scalar result. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            const float4 params = bindGroup->params->read();
            bindGroup->values[0] = params.x > 0.0f ? 1u : 0u;
        }
    }
};

#endif
