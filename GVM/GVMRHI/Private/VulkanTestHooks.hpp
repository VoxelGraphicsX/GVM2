#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstddef>
#include <cstdint>

namespace GVM::RHI::Vulkan::Testing
{
    static constexpr uint32_t ExternalSubpass = 0xFFFFFFFFu;

    struct BufferStateSnapshot
    {
        uint64_t stageMaskBits = 0u;
        uint64_t accessMaskBits = 0u;
        uint64_t lastWriteStageMaskBits = 0u;
        uint64_t lastWriteAccessMaskBits = 0u;
        bool initialized = false;
        bool lastWriteInitialized = false;
    };

    struct PixelLocalDependencyColorAttachment
    {
        TextureFormat format = TextureFormat::RGBA8Unorm;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        bool pixelLocal = true;
    };

    struct PixelLocalDependencyDepthAttachment
    {
        TextureFormat format = TextureFormat::Undefined;
        LoadOp loadOp = LoadOp::Undefined;
        StoreOp storeOp = StoreOp::Undefined;
        bool pixelLocal = false;
    };

    struct PixelLocalSubpassDependencySnapshot
    {
        uint32_t sourceSubpass = 0u;
        uint32_t destinationSubpass = 0u;
        bool sourceFragmentShaderStage = false;
        bool sourceColorAttachmentOutputStage = false;
        bool sourceEarlyFragmentTestsStage = false;
        bool sourceLateFragmentTestsStage = false;
        bool destinationFragmentShaderStage = false;
        bool destinationColorAttachmentOutputStage = false;
        bool destinationEarlyFragmentTestsStage = false;
        bool destinationLateFragmentTestsStage = false;
        bool sourceInputAttachmentReadAccess = false;
        bool sourceColorAttachmentWriteAccess = false;
        bool sourceDepthStencilAttachmentWriteAccess = false;
        bool destinationInputAttachmentReadAccess = false;
        bool destinationColorAttachmentReadAccess = false;
        bool destinationColorAttachmentWriteAccess = false;
        bool destinationDepthStencilAttachmentWriteAccess = false;
    };

    bool isVulkanInstance(Instance instance);
    bool isVulkanDevice(Device device);
    bool isVulkanSwapchain(Swapchain swapchain);
    eastl::vector<PixelLocalSubpassDependencySnapshot> buildPixelLocalSubpassDependenciesForTesting(
        const eastl::vector<PixelLocalDependencyColorAttachment> &colorAttachments,
        const PixelLocalDependencyDepthAttachment &depthAttachment,
        uint32_t pixelLocalPassCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses);
    uint64_t getQueueUserSubmitCount(Queue queue);
    uint64_t getQueueBackendSubmitCount(Queue queue);
    uint64_t getQueuePresentInternalSubmitCount(Queue queue);
    size_t getFramebufferCacheSize(Device device);
    size_t getRenderPassCacheSize(Device device);
    size_t getPipelineCompatibleRenderPassCacheSize(Device device);
    size_t getNativePipelineCacheSize(RenderPipeline pipeline);
    size_t getPendingDestroyBufferCount(Device device);
    uint32_t getBufferTotalReferenceCount(Device device, Buffer buffer);
    uint32_t getBufferBindGroupReferenceCount(Device device, Buffer buffer);
    bool isBufferHandleDestroyed(Buffer buffer);
    BufferStateSnapshot getCurrentBufferState(Queue queue, Buffer buffer);
    void collectReleasedResources(Device device);
    bool isTextureInSteadyStateLayout(Texture texture);
} // namespace GVM::RHI::Vulkan::Testing
