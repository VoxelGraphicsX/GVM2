#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

#include "Private/GVMRHIDefines.hpp"
#include <GVMRHI/Private/TextureViewResolution.hpp>
#include "VKResourceLifetime.hpp"

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

        /** Returns the logical device that owns this image allocation. */
        VKDevice &getDevice() const { return *mDevice; }
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

        /** Stores the pool handle issued by the device right after allocation; used to enqueue this resource for destruction when its last reference is released. */
        void setHandle(Texture handle);
        /** Returns the pool handle issued at creation. */
        [[nodiscard]] Texture getHandle() const;
        /** Adds one holder reference keeping this resource alive past free*() until GPU work completes. */
        void retainReference();
        /** Drops one holder reference; enqueues the resource for destruction when the last one is released after free*(). */
        void releaseReference();
        /** Releases the user handle's reference exactly once; reports whether destruction may proceed. */
        [[nodiscard]] VKUserReleaseResult releaseUserReference();
        /** Reports whether free*() has been called on this resource. */
        [[nodiscard]] bool isDestroyRequested() const;
        /** Reports whether the user handle was released and no holder references remain. */
        [[nodiscard]] bool isDestroyable() const;
        /** Returns the live reference count, including the user handle's while it is held. */
        [[nodiscard]] uint32_t getReferenceCount() const;

    private:
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
        Private::TextureViewCache mCachedViews;
        VKResourceLifetime mLifetime = {};
        Texture mHandle = {};
        bool mOwnsAllocation = true;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
