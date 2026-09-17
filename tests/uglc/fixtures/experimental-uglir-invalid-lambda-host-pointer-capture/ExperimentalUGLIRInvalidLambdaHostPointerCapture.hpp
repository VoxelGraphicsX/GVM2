#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_LAMBDA_HOST_POINTER_CAPTURE_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_LAMBDA_HOST_POINTER_CAPTURE_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer used by the invalid lambda pointer-capture fixture. */
struct ExperimentalUGLIRInvalidLambdaHostPointerCaptureBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises verifier rejection of raw pointer state captured by a lambda. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRInvalidLambdaHostPointerCapturePass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRInvalidLambdaHostPointerCaptureBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Captures a raw pointer, which is not part of the shader-visible authoring surface. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        uint *rawValue = nullptr;
        auto invalidLambda = [rawValue]() {
            return 0u;
        };
        bindGroup->values[threadID.x] = invalidLambda();
    }
};

#endif
