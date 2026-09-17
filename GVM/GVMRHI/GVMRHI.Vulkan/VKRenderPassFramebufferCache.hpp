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

        void init(vk::Device device);
        void destroy();

        [[nodiscard]]
        vk::RenderPass getOrCreateRenderPass(const RenderPassDescriptor &descriptor);

        [[nodiscard]]
        vk::RenderPass getOrCreatePipelineRenderPass(const RenderPipelineDescriptor &descriptor);

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

        vk::Device mDevice = nullptr;
        uint64_t mFramebufferAccessSerial = 0u;
        CacheDetail::VKHashBucketCache<CachedPipelineRenderPass> mPipelineRenderPassCache;
        CacheDetail::VKHashBucketCache<CachedRenderPass> mRenderPassCache;
        CacheDetail::VKHashBucketCache<FramebufferEntry> mFramebufferCache;
    };
} // namespace GVM::RHI::Vulkan
