#include "VKTextureViewCacheUtils.hpp"

#include "Private/RHIHashXXH64.hpp"

namespace GVM::RHI::Vulkan::CacheDetail
{
    uint64_t hashTextureViewDescriptor(const TextureViewDescriptor &descriptor)
    {
        GVM::RHI::Detail::XXH64State state;
        state.updateEnum(descriptor.format);
        state.updateEnum(descriptor.dimension);
        state.updatePod(descriptor.baseMipLevel);
        state.updatePod(descriptor.mipLevelCount);
        state.updatePod(descriptor.baseArrayLayer);
        state.updatePod(descriptor.arrayLayerCount);
        state.updatePod(descriptor.aspect);
        return state.digest();
    }

    bool equalTextureViewDescriptor(const TextureViewDescriptor &lhs, const TextureViewDescriptor &rhs)
    {
        return lhs.format == rhs.format &&
            lhs.dimension == rhs.dimension &&
            lhs.baseMipLevel == rhs.baseMipLevel &&
            lhs.mipLevelCount == rhs.mipLevelCount &&
            lhs.baseArrayLayer == rhs.baseArrayLayer &&
            lhs.arrayLayerCount == rhs.arrayLayerCount &&
            lhs.aspect == rhs.aspect;
    }
} // namespace GVM::RHI::Vulkan::CacheDetail
