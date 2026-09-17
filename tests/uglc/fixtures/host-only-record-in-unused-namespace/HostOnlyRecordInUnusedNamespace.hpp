#ifndef UGLC_TEST_HOST_ONLY_RECORD_IN_UNUSED_NAMESPACE_HPP
#define UGLC_TEST_HOST_ONLY_RECORD_IN_UNUSED_NAMESPACE_HPP

#include "UGL.h"

#include <EASTL/array.h>

using namespace UGL;

/** Provides the output buffer used by the valid compute pass. */
struct HostOnlyRecordInUnusedNamespaceBindGroup final : public IBindGroup
{
    /** Creates the shader-visible output buffer. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace HostOnlyRecordInUnusedNamespaceHelpers
{
    /** Stores CPU-side texture views that should stay in host output only. */
    struct UnusedHostOnlyRecord
    {
        TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> textureView;
        eastl::array<TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D>, 2> textureViews;
    };

    /** Initializes host-only state and must not be emitted into shader source when unused. */
    inline void initializeHostOnlyRecord(UnusedHostOnlyRecord record)
    {
        record.textureViews[0] = record.textureView;
    }
} // namespace HostOnlyRecordInUnusedNamespaceHelpers

/** Verifies unused namespace host-only records do not leak into shader artifacts. */
class [[LocalWorkGroupSize(8, 1, 1)]] HostOnlyRecordInUnusedNamespacePass final : public IComputeClass
{
public:
    /** Captures the shader-visible output bind group. */
    constructor(BindGroup<HostOnlyRecordInUnusedNamespaceBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes a value while the same translation unit contains unused host-only namespace records. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = threadID.x + 11u;
    }
};

#endif
