#ifndef UGLC_TEST_EXPERIMENTAL_UGLIR_CONCEPTS_REQUIRES_POLICY_HPP
#define UGLC_TEST_EXPERIMENTAL_UGLIR_CONCEPTS_REQUIRES_POLICY_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the storage buffer written by the concepts/requires policy fixture. */
struct ExperimentalUGLIRConceptsRequiresPolicyBindGroup final : public IBindGroup
{
    /** Creates the bind group with the writable values buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace ExperimentalUGLIRConceptsRequiresPolicyHelpers
{
    /** Accepts policy types that expose the transform method used by the shader helper. */
    template <class Policy>
    concept TransformPolicy = requires(Policy policy, uint value) {
        { policy.transform(value) };
    };

    /** Applies one stateless transform policy after Clang has accepted the concept constraint. */
    template <class Policy>
        requires TransformPolicy<Policy>
    uint applyPolicy(uint value)
    {
        Policy policy;
        return policy.transform(value);
    }

    /** Adds a stable increment so the instantiated concept helper has observable shader output. */
    struct AddFivePolicy
    {
        /** Returns the transformed value used by the compute shader. */
        uint transform(uint value)
        {
            return value + 5u;
        }
    };
} // namespace ExperimentalUGLIRConceptsRequiresPolicyHelpers

/** Exercises successful C++20 concepts/requires authoring through the experimental UGLIR compute pipeline. */
class [[LocalWorkGroupSize(8, 1, 1)]] ExperimentalUGLIRConceptsRequiresPolicyPass final : public IComputeClass
{
public:
    /** Binds the writable output values buffer. */
    constructor(BindGroup<ExperimentalUGLIRConceptsRequiresPolicyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Invokes a constrained helper template and stores the transformed value. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint index = threadID.x;
        bindGroup->values[index] =
            ExperimentalUGLIRConceptsRequiresPolicyHelpers::applyPolicy<ExperimentalUGLIRConceptsRequiresPolicyHelpers::AddFivePolicy>(index);
    }
};

#endif
