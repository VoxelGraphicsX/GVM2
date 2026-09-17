#ifndef UGLC_TEST_STORAGE_TEXTURE_VULKAN_FORMAT_LOWERING_HPP
#define UGLC_TEST_STORAGE_TEXTURE_VULKAN_FORMAT_LOWERING_HPP

#include "UGL.h"

using namespace UGL;

struct StorageTextureVulkanFormatBindGroup final : public IBindGroup
{
    constructor(RWTexture2D<TextureFormat::RGBA8Unorm> rgbaTexture [[Binding0]],
                RWTexture2D<TextureFormat::R32Float> scalarTexture [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(1, 1, 1)]] StorageTextureVulkanFormatPass final : public IComputeClass
{
public:
    constructor(BindGroup<StorageTextureVulkanFormatBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint2 coord = uint2(threadID.x, 0u);
        bindGroup->rgbaTexture->write(coord, float4(1.0f, 0.25f, 0.5f, 1.0f));
        bindGroup->scalarTexture->write(coord, 1.0f);
    }
};

#endif
