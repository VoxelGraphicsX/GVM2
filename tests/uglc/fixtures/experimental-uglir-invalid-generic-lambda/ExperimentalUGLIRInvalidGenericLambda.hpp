#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_GENERIC_LAMBDA_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_GENERIC_LAMBDA_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer used by the invalid generic-lambda fixture. */
struct ExperimentalUGLIRInvalidGenericLambdaBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Exercises verifier rejection of generic lambda syntax inside shader-reachable code. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRInvalidGenericLambdaPass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRInvalidGenericLambdaBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Uses an auto parameter on a lambda, which Phase 7 intentionally does not lower. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        auto invalidLambda = [](auto value) {
            return value;
        };
        bindGroup->values[threadID.x] = invalidLambda(threadID.x);
    }
};

#endif
