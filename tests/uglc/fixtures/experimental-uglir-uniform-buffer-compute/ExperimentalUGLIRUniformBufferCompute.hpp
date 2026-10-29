#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_COMPUTE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores uniform parameters read by the experimental direct SPIR-V uniform-buffer fixture. */
struct ExperimentalUGLIRUniformBufferComputeParams
{
    uint addend;
};

/** Binds one uniform buffer and one writable storage buffer for direct SPIR-V validation. */
struct ExperimentalUGLIRUniformBufferComputeBindGroup final : public IBindGroup
{
    /** Creates the bind group with a uniform payload and writable output storage. */
    constructor(UniformBuffer<ExperimentalUGLIRUniformBufferComputeParams> params [[Binding0]],
                RWStructuredBuffer<uint> values [[Binding1]])
    {
    }
};

/** Verifies that UniformBuffer fields lower to direct SPIR-V Uniform block loads. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRUniformBufferComputePass final : public IComputeClass
{
public:
    /** Creates the compute pass with the uniform-buffer bind group. */
    constructor(BindGroup<ExperimentalUGLIRUniformBufferComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads a uniform value and writes it into the storage output. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            bindGroup->values[0] = bindGroup->params->addend + 1u;
        }
    }
};

#endif
