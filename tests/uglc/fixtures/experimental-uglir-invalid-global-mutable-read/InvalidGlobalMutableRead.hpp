#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_GLOBAL_MUTABLE_READ_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_GLOBAL_MUTABLE_READ_HPP

#include "UGL.h"

using namespace UGL;

/** Defines mutable namespace storage that shader code must not read implicitly. */
uint experimentalUGLIRInvalidGlobalMutableReadValue = 7u;

/** Provides the output storage used by the global-storage diagnostic fixture. */
struct ExperimentalUGLIRInvalidGlobalMutableReadBindings final : public IBindGroup
{
    /** Binds the output buffer written by the shader entry. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]])
    {
    }
};

/** Exercises a shader read from mutable namespace storage. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidGlobalMutableReadPass final : public IComputeClass
{
public:
    /** Binds the output resource used by the diagnostic fixture. */
    constructor(BindGroup<ExperimentalUGLIRInvalidGlobalMutableReadBindings> data [[Slot0]])
    {
    }

private:
    /** Reads mutable global storage so the experimental verifier must reject the shader. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            data->output[0u] = experimentalUGLIRInvalidGlobalMutableReadValue;
        }
    }
};

#endif
