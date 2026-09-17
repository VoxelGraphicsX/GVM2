#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include "VKHashBucketCache.hpp"

#include "Private/GVMRHIDefines.hpp"

#include <EASTL/vector.h>


namespace GVM::RHI::Vulkan
{
    class VKTexture final : public TextureImpl
    {
    public:
        VKTexture() = default;

        void init(VKDevice *device, const TextureDescriptor &descriptor);
        void initExternalImage(VKDevice *device, const TextureDescriptor &descriptor, vk::Image image);
        const eastl::shared_ptr<Internal::LogContext> &getLogContext() const;

        TextureView createView(const TextureViewDescriptor &descriptor) override;
        TextureView createView() override;
        uint32_t getWidth() const override;
        uint32_t getHeight() const override;
        uint32_t getDepth() const override;
        uint32_t getMipLevelCount() const override;
        uint32_t getArrayLayerCount() const override;
        TextureFormat getFormat() const override;
        void destroy() override;

        [[nodiscard]] vk::Image getNativeImage() const;
        [[nodiscard]] TextureUsageFlags getUsage() const;
        [[nodiscard]] TextureDimension getDimension() const;
        [[nodiscard]] vk::ImageLayout getSteadyStateLayout() const;
        [[nodiscard]] bool hasSteadyStateLayout() const;

        [[nodiscard]]
        vk::ImageSubresourceRange buildSubresourceRange(TextureAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t mipLevelCount, uint32_t baseArrayLayer, uint32_t arrayLayerCount) const;

        [[nodiscard]] vk::ImageSubresourceRange buildWholeTextureRange(TextureAspectFlags aspectMask = TextureAspect::All) const;
        [[nodiscard]] uint32_t getCachedArrayLayerCount() const;

        [[nodiscard]]
        bool isInSteadyStateLayout() const;

        [[nodiscard]] bool is3D() const;
        [[nodiscard]] const TextureDescriptor &getDescriptor() const;

        void setSteadyStateLayout(vk::ImageLayout layout);

        void retainBindGroupReference();
        void releaseBindGroupReference();
        void retainCommandReference();
        void releaseCommandReference();

        [[nodiscard]]
        bool requestUserDestroy();

        [[nodiscard]] uint32_t getTotalReferenceCount() const;
        [[nodiscard]] uint32_t getBindGroupReferenceCount() const;
        [[nodiscard]] uint32_t getCommandReferenceCount() const;
        [[nodiscard]] bool isDestroyRequested() const;
        [[nodiscard]] bool isReadyForDestroy() const;
        [[nodiscard]] bool markPendingDestroyQueued();
        void clearPendingDestroyQueued();

    private:
        struct CachedTextureViewEntry
        {
            TextureViewDescriptor descriptor = {};
            TextureView handle = {};
        };

        TextureView createViewInternal(const TextureViewDescriptor &descriptor);
        void destroyOwnedViews();

        VKDevice *mDevice = nullptr;
        eastl::shared_ptr<Internal::LogContext> mLogContext;
        TextureDescriptor mDescriptor = {};
        vk::Image mImage;
        VmaAllocation mAllocation = nullptr;
        VmaAllocationInfo mAllocationInfo = {};
        vk::DeviceMemory mDedicatedMemory = nullptr;
        vk::ImageLayout mSteadyStateLayout = vk::ImageLayout::eUndefined;
        mutable Mutex mCachedStateMutex;
        uint32_t mCachedArrayLayerCount = 0u;
        Mutex mViewCacheMutex;
        TextureView mDefaultViewHandle = {};
        eastl::vector<TextureView> mOwnedViews;
        CacheDetail::VKHashBucketCache<CachedTextureViewEntry> mCachedViews;
        VKAtomicResourceLifetimeState mLifetime = {};
        bool mOwnsAllocation = true;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
