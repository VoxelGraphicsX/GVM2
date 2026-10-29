#ifndef UGLC_TEST_NAMESPACE_REACHABLE_HELPER_ONLY_HPP
#define UGLC_TEST_NAMESPACE_REACHABLE_HELPER_ONLY_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the output buffer used by the reachable namespace helper pass. */
struct NamespaceReachableHelperOnlyBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace NamespaceReachableHelperOnlyHelpers
{
    /** Stores host-only resources that must not enter shader backend emission when unused. */
    struct UnusedHostOnlyPack
    {
        TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> textureView;
    };

    /** Exists in the same namespace but is intentionally unreachable from the shader entry. */
    inline uint unusedHostOnlyPoison(UnusedHostOnlyPack pack)
    {
        return 7u;
    }

    /** Computes the value used by the shader entry and should be the only namespace helper emitted. */
    inline uint reachableValue(uint value)
    {
        return value + 5u;
    }
} // namespace NamespaceReachableHelperOnlyHelpers

/** Verifies namespace emission only includes helpers reachable from the compute entry. */
class [[LocalWorkGroupSize(8, 1, 1)]] NamespaceReachableHelperOnlyPass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<NamespaceReachableHelperOnlyBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a value produced by the reachable namespace helper. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = NamespaceReachableHelperOnlyHelpers::reachableValue(threadID.x);
    }
};

#endif
