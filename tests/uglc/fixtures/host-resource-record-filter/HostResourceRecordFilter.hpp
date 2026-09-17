#ifndef UGLC_TEST_HOST_RESOURCE_RECORD_FILTER_HPP
#define UGLC_TEST_HOST_RESOURCE_RECORD_FILTER_HPP

#include "UGL.h"

#include <EASTL/array.h>

using namespace UGL;

/** Provides the shader-visible storage buffer used by the valid compute pass. */
struct HostResourceRecordFilterBindGroup final : public IBindGroup
{
    /** Creates the bind group used by the shader pass. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

/** Stores host-only resource handles that must remain out of shader backend records. */
struct HostOnlyBufferPack
{
    Buffer<uint, BufferUsage<Storage, CopyDst>> values;
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> texture;
    TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D> textureView;
    eastl::array<TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding, CopyDst>, TextureDimension::e2D>, 2> textureViews;

    /** Allocates host-only resources and derives texture views for CPU-side renderer code. */
    void create(UGL::Device device)
    {
        values = device->createBuffer("HostOnlyBufferPackValues", 1);
        texture = device->createTexture("HostOnlyBufferPackTexture", 4, 4, 1);
        textureView = texture->createView();
        textureViews[0] = texture->createView();
        textureViews[1] = texture->createView();
    }
};

/** Verifies shader generation succeeds while unrelated host-only resource records exist in the same TU. */
class [[LocalWorkGroupSize(8, 1, 1)]] HostResourceRecordFilterPass final : public IComputeClass
{
public:
    /** Captures the shader-visible bind group. */
    constructor(BindGroup<HostResourceRecordFilterBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes one value to prove the shader pass itself remains valid. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        bindGroup->values[threadID.x] = 1u;
    }
};

#endif
