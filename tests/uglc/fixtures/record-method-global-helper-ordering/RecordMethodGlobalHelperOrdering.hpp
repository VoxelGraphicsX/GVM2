#ifndef UGLC_TEST_RECORD_METHOD_GLOBAL_HELPER_ORDERING_HPP
#define UGLC_TEST_RECORD_METHOD_GLOBAL_HELPER_ORDERING_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output buffer used by the cross-backend record-method helper ordering regression. */
struct RecordMethodGlobalHelperOrderingBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Carries a vector payload passed from a namespace record method to a global helper. */
struct RecordMethodGlobalHelperOrderingPayload
{
    float3 value;
};

/** Scales a payload for namespace record methods that are emitted before full helper definitions. */
inline float3 recordMethodGlobalScale(RecordMethodGlobalHelperOrderingPayload payload, float scale)
{
    return payload.value * scale;
}

namespace RecordMethodGlobalHelperOrderingTypes
{
    /** Provides a namespace-scoped constant that must stay visible before record method bodies are emitted. */
    static const float AccumulatorScale = 2.0f;

    /** Verifies namespace record methods can call a reachable global helper after prototype emission is reordered. */
    struct Accumulator
    {
        /** Builds a payload from a scalar value. */
        RecordMethodGlobalHelperOrderingPayload makePayload(uint value) const
        {
            RecordMethodGlobalHelperOrderingPayload payload;
            payload.value = float3(float(value), float(value) + 1.0f, float(value) + 2.0f);
            return payload;
        }

        /** Evaluates the global helper from inside a record method body. */
        float3 evaluate(uint value) const
        {
            RecordMethodGlobalHelperOrderingPayload payload = makePayload(value);
            return recordMethodGlobalScale(payload, AccumulatorScale);
        }
    };
} // namespace RecordMethodGlobalHelperOrderingTypes

/** Runs a compute shader that forces a namespace record method to call a global helper. */
class [[LocalWorkGroupSize(1, 1, 1)]] RecordMethodGlobalHelperOrderingPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<RecordMethodGlobalHelperOrderingBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a value produced by a namespace record method that calls a global helper. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        RecordMethodGlobalHelperOrderingTypes::Accumulator accumulator;
        const float3 value = accumulator.evaluate(threadID.x);
        bindGroup->values[threadID.x] = uint(value.z);
    }
};

#endif
