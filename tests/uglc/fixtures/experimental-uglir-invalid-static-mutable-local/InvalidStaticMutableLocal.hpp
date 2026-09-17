#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STATIC_MUTABLE_LOCAL_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_STATIC_MUTABLE_LOCAL_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output storage used by the static-storage diagnostic fixture. */
struct ExperimentalUGLIRInvalidStaticMutableLocalBindings final : public IBindGroup
{
    /** Binds the output buffer written by the shader entry. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]])
    {
    }
};

/** Uses mutable function-local static storage that cannot be represented in shader code. */
inline uint experimentalUGLIRInvalidStaticMutableLocalNextValue()
{
    static uint counter = 0u;
    return ++counter;
}

/** Exercises a reachable helper with mutable static local storage. */
class [[LocalWorkGroupSize(1, 1, 1)]] ExperimentalUGLIRInvalidStaticMutableLocalPass final : public IComputeClass
{
public:
    /** Binds the output resource used by the diagnostic fixture. */
    constructor(BindGroup<ExperimentalUGLIRInvalidStaticMutableLocalBindings> data [[Slot0]])
    {
    }

private:
    /** Calls the mutable-static helper so the experimental verifier must reject the shader. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        if (threadID.x == 0u)
        {
            data->output[0u] = experimentalUGLIRInvalidStaticMutableLocalNextValue();
        }
    }
};

#endif
