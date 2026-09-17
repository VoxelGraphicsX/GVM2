#ifndef UGLC_TEST_NAMESPACE_GLOBAL_HELPER_DEPENDENCY_HPP
#define UGLC_TEST_NAMESPACE_GLOBAL_HELPER_DEPENDENCY_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output buffer used by the global helper dependency pass. */
struct NamespaceGlobalHelperDependencyBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Carries the vector payload forwarded from a namespace helper to a global helper. */
struct NamespaceGlobalHelperDependencyData
{
    float3 value;
};

/** Declares the global helper so a namespace helper can call it before its definition appears. */
inline float3 namespaceGlobalScale(NamespaceGlobalHelperDependencyData data, float scale);

namespace NamespaceGlobalHelperDependencyHelpers
{
    /** Builds a payload through a namespace helper and forwards it to a global helper. */
    inline float3 buildScaledValue(float baseValue)
    {
        NamespaceGlobalHelperDependencyData data;
        data.value = float3(baseValue, baseValue + 1.0f, baseValue + 2.0f);
        return namespaceGlobalScale(data, 2.0f);
    }
} // namespace NamespaceGlobalHelperDependencyHelpers

/** Defines the global helper after the namespace helper to exercise prototype ordering. */
inline float3 namespaceGlobalScale(NamespaceGlobalHelperDependencyData data, float scale)
{
    return data.value * scale;
}

/** Verifies namespace helpers can depend on global helpers regardless of source definition order. */
class [[LocalWorkGroupSize(8, 1, 1)]] NamespaceGlobalHelperDependencyPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<NamespaceGlobalHelperDependencyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a value produced by a namespace helper that calls a later global helper definition. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const float3 scaledValue = NamespaceGlobalHelperDependencyHelpers::buildScaledValue(float(threadID.x));
        bindGroup->values[threadID.x] = uint(scaledValue.z);
    }
};

#endif
