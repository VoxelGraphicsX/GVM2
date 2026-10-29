#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_LAMBDA_COMPUTE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_LAMBDA_COMPUTE_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer written by the lambda compute fixture. */
struct ExperimentalUGLIRLambdaComputeBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises non-escaping C++20 lambda authoring through the experimental UGLIR compute pipeline. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRLambdaComputePass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRLambdaComputeBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Uses immediate invocation, a local lambda variable, value capture, and resource use from a lambda body. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint base = threadID.x;
        const uint doubled = [base](uint scale) {
            return base * scale;
        }(2u);

        auto addCaptured = [base](uint offset) {
            return base + offset;
        };
        const uint result = addCaptured(5u) + doubled;

        auto writeValue = [this, result](uint index) {
            bindGroup->values[index] = result + 1u;
        };
        writeValue(threadID.x);
    }
};

#endif
