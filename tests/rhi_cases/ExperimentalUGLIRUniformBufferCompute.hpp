#ifndef GVM_TEST_RHI_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_COMPUTE_HPP
#define GVM_TEST_RHI_EXPERIMENTAL_UGLIR_UNIFORM_BUFFER_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

/** Stores the runtime uniform parameters consumed by the experimental UGLIR readback shader. */
struct ExperimentalUGLIRRuntimeUniformParams
{
    uint addend;
};

/** Binds the uniform parameter buffer and writable output storage for the runtime smoke test. */
struct ExperimentalUGLIRRuntimeUniformBindGroup final : public IBindGroup
{
    /** Creates the bind group with a read-only uniform buffer and writable output storage. */
    constructor(UniformBuffer<ExperimentalUGLIRRuntimeUniformParams> params [[Binding0]],
                RWStructuredBuffer<ExperimentalUGLIRRuntimeUniformParams> values [[Binding1]])
    {
    }
};

/** Dispatches one compute thread per output value and writes a uniform-derived result. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRRuntimeUniformPass final : public IComputeClass
{
public:
    /** Creates the compute pass with the runtime uniform bind group. */
    constructor(BindGroup<ExperimentalUGLIRRuntimeUniformBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Reads the uniform addend and writes a deterministic output sequence. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x].addend = bindGroup->params->addend + threadID.x;
    }
};

/** Carries a nonsymmetric matrix through uniform storage and nested local values. */
struct ExperimentalUGLIRUniformMatrix
{
    float4x4 matrix;
    uint marker;
    uint padding0;
    uint padding1;
    uint padding2;
};

/** Exercises aggregate reconstruction after loading a row-major uniform matrix. */
struct ExperimentalUGLIRUniformMatrixContext
{
    ExperimentalUGLIRUniformMatrix camera;
    uint marker;
};

/** Binds matrix input and scalar observations for aggregate matrix regression coverage. */
struct ExperimentalUGLIRUniformMatrixBindGroup final : public IBindGroup
{
    /** Creates the resources used by the aggregate matrix regression. */
    constructor(UniformBuffer<ExperimentalUGLIRUniformMatrix> params [[Binding0]],
                RWStructuredBuffer<uint> output [[Binding1]])
    {
    }
};

/** Returns the context through a branch that requires aggregate value reconstruction. */
inline ExperimentalUGLIRUniformMatrixContext experimentalUGLIRMakeMatrixContext(
    ExperimentalUGLIRUniformMatrix camera, uint index)
{
    ExperimentalUGLIRUniformMatrixContext context;
    context.camera = camera;
    context.marker = index;
    if (index == 0u)
        context.camera.marker = 3u;
    else
        context.camera.marker = 5u;
    return context;
}

/** Copies a returned nested matrix aggregate through an output parameter. */
inline void experimentalUGLIRWriteMatrixContext(
    ExperimentalUGLIRUniformMatrix camera, uint index,
    OUT ExperimentalUGLIRUniformMatrixContext &context)
{
    context = experimentalUGLIRMakeMatrixContext(camera, index);
}

/** Reads a nested matrix through a loop-return function that remains an aggregate call boundary. */
inline float4 experimentalUGLIRMultiplyMatrixContext(ExperimentalUGLIRUniformMatrixContext context)
{
    for (uint index = 0u; index < 2u; ++index)
    {
        if (index == context.marker)
            return mul(context.camera.matrix, float4(1.0f, 2.0f, 3.0f, 4.0f));
    }
    return float4(0.0f);
}

/** Verifies matrix semantics after aggregate return, branching, and output-parameter copyback. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRUniformMatrixPass final : public IComputeClass
{
public:
    /** Creates the uniform matrix regression pass. */
    constructor(BindGroup<ExperimentalUGLIRUniformMatrixBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes independent row and multiplication observations for both branch paths. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        ExperimentalUGLIRUniformMatrixContext context;
        experimentalUGLIRWriteMatrixContext(bindGroup->params->read(), threadID.x, context);
        uint base = threadID.x * 5u;
        bindGroup->output[base] = uint(context.camera.matrix[0][1]);
        bindGroup->output[base + 1u] = uint(context.camera.matrix[1][0]);
        float4 product = experimentalUGLIRMultiplyMatrixContext(context);
        bindGroup->output[base + 2u] = uint(product.x);
        bindGroup->output[base + 3u] = uint(product.y);
        bindGroup->output[base + 4u] = context.camera.marker;
    }
};

#endif
