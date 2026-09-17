#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_ESCAPING_LAMBDA_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_INVALID_ESCAPING_LAMBDA_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer used by the invalid escaping-lambda fixture. */
struct ExperimentalUGLIRInvalidEscapingLambdaBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace ExperimentalUGLIRInvalidEscapingLambdaHelpers
{
    /** Accepts an arbitrary callable, which intentionally makes a lambda escape the supported lowering pattern. */
    template <class Callable>
    uint consumeCallable(Callable callable, uint value)
    {
        return callable(value);
    }
} // namespace ExperimentalUGLIRInvalidEscapingLambdaHelpers

/** Exercises verifier rejection when a lambda closure is passed through a generic helper parameter. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRInvalidEscapingLambdaPass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRInvalidEscapingLambdaBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Passes a local lambda to another function, which is outside Phase 7's non-escaping lambda subset. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint base = threadID.x;
        auto invalidLambda = [base](uint value) {
            return base + value;
        };
        bindGroup->values[threadID.x] =
            ExperimentalUGLIRInvalidEscapingLambdaHelpers::consumeCallable(invalidLambda, 1u);
    }
};

#endif
