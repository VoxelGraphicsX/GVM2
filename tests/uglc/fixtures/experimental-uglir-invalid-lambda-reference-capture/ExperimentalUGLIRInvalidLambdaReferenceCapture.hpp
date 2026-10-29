#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_LAMBDA_REFERENCE_CAPTURE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_LAMBDA_REFERENCE_CAPTURE_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer used by the invalid lambda reference-capture fixture. */
struct ExperimentalUGLIRInvalidLambdaReferenceCaptureBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises verifier rejection of lambda reference capture in shader-reachable code. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRInvalidLambdaReferenceCapturePass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRInvalidLambdaReferenceCaptureBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Captures a local value by reference, which the experimental UGLIR verifier must reject. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint base = threadID.x;
        auto invalidLambda = [&base](uint offset) {
            return base + offset;
        };
        bindGroup->values[threadID.x] = invalidLambda(1u);
    }
};

#endif
