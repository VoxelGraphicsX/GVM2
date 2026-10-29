#pragma once

#include "VKCommon.hpp"
#include "VKHashBucketCache.hpp"
#include "VKRenderPassHashUtils.hpp"

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    class VKRenderPassFramebufferCache final
    {
    public:
        struct FramebufferOwner
        {
            vk::Device device = nullptr;
            vk::Framebuffer framebuffer = nullptr;

            ~FramebufferOwner();
            void destroy();
        };

        struct FramebufferHandle
        {
            vk::Framebuffer framebuffer = nullptr;
            eastl::shared_ptr<FramebufferOwner> owner;
        };

        /** Initializes the cache with its native device and logger. */
        void init(vk::Device device, Logger logger);
        void destroy();

        [[nodiscard]]
        vk::RenderPass getOrCreateRenderPass(const RenderPassDescriptor &descriptor);

        [[nodiscard]]
        vk::RenderPass getOrCreatePipelineRenderPass(const RenderPipelineDescriptor &descriptor);

        /** Tests full compatibility with the pipeline's single-subpass pass; multi-subpass passes use their own pipeline. */
        bool isPipelineRenderPassCompatible(vk::RenderPass renderPass, const RenderPipelineDescriptor &descriptor) const;

        [[nodiscard]]
        FramebufferHandle getOrCreateFramebuffer(
            vk::RenderPass renderPass,
            const eastl::vector<vk::ImageView> &attachments,
            uint32_t width,
            uint32_t height,
            uint32_t layers);

        void invalidateFramebufferCacheForImageView(vk::ImageView imageView);

        [[nodiscard]]
        size_t getFramebufferCacheSize() const;

        [[nodiscard]]
        size_t getRenderPassCacheSize() const;

        [[nodiscard]]
        size_t getPipelineCompatibleRenderPassCacheSize() const;

    private:
        struct CachedPipelineRenderPass
        {
            CacheDetail::PipelineCompatibleRenderPassSignature signature;
            vk::UniqueRenderPass renderPass;
        };

        struct CachedRenderPass
        {
            eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> colorAttachments;
            CacheDetail::RenderPassDepthStencilAttachmentSignature depthStencilAttachment;
            uint32_t pixelLocalPassCount = 1u;
            eastl::vector<PixelLocalPassAttachmentAccess> pixelLocalAttachmentAccesses;
            vk::UniqueRenderPass renderPass;
        };

        struct FramebufferEntry
        {
            vk::RenderPass renderPass = nullptr;
            eastl::vector<vk::ImageView> attachments;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t layers = 0;
            uint64_t lastUsedSerial = 0u;
            eastl::shared_ptr<FramebufferOwner> owner;
        };

        void trimFramebufferCache(eastl::vector<eastl::shared_ptr<FramebufferOwner>> &evictedFramebuffers);

        static constexpr size_t MaxFramebufferCacheEntries = 2048u;

        /** Throws when the cache was destroyed or has no native device. */
        void ensureAlive(const char *apiName) const;
        /** Unlocked body of getOrCreateRenderPass; the public method locks and logs. */
        vk::RenderPass getOrCreateRenderPassImpl(const RenderPassDescriptor &descriptor);
        /** Unlocked body of getOrCreateFramebuffer; the public method locks and logs. */
        FramebufferHandle getOrCreateFramebufferImpl(
            vk::RenderPass renderPass,
            const eastl::vector<vk::ImageView> &attachments,
            uint32_t width,
            uint32_t height,
            uint32_t layers);

        mutable Mutex mCacheMutex;
        vk::Device mDevice = nullptr;
        Logger mLogger = nullptr;
        bool mDestroyed = false;
        uint64_t mFramebufferAccessSerial = 0u;
        CacheDetail::VKHashBucketCache<CachedPipelineRenderPass> mPipelineRenderPassCache;
        CacheDetail::VKHashBucketCache<CachedRenderPass> mRenderPassCache;
        CacheDetail::VKHashBucketCache<FramebufferEntry> mFramebufferCache;
    };

    namespace CacheDetail
    {
        /** Expands or clamps the recorded pixel-local attachment accesses to one entry per render pass in the signature. */
        eastl::vector<PixelLocalPassAttachmentAccess> normalizeVulkanPixelLocalAttachmentAccesses(
            const eastl::vector<RenderPassColorAttachmentSignature> &colorAttachments,
            const RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            bool pixelLocalEnabled,
            uint32_t renderPassCount,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses);

        /** Builds the inter-subpass dependencies for a pixel-local render pass signature. */
        eastl::vector<vk::SubpassDependency> buildPixelLocalSubpassDependencies(
            const eastl::vector<RenderPassColorAttachmentSignature> &colorAttachments,
            const RenderPassDepthStencilAttachmentSignature &depthStencilAttachment,
            const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses);
    } // namespace CacheDetail
} // namespace GVM::RHI::Vulkan
