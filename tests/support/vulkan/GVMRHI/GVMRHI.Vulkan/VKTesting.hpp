#pragma once

// Vulkan-only testing helpers compiled into the gvm_vulkan_test_support static library; they are not part of the public RHI API and are linked only by test executables.

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

    /** Enables synchronization validation for all test instances; call once before creating any Vulkan instance. */
    void enableSynchronizationValidation();
    /** Returns native validation errors accumulated by an explicitly validated Vulkan instance. */
    uint32_t getValidationErrorCount(Instance instance);
    bool isVulkanDevice(Device device);
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
    /** Counts actual driver pipelines rather than render-pass aliases. Requires a Vulkan pipeline. */
    size_t getNativePipelineCount(RenderPipeline pipeline);
    uint32_t getBufferTotalReferenceCount(Device device, Buffer buffer);
    BufferStateSnapshot getCurrentBufferState(Queue queue, Buffer buffer);
    void collectReleasedResources(Device device);
    /** Overrides the shared Args descriptor table slot capacities; must run before the device's table exists. */
    void setArgumentTableCapacityForTesting(Device device, uint32_t sampledImages, uint32_t storageImages, uint32_t samplers);
    bool isTextureInSteadyStateLayout(Texture texture);
} // namespace GVM::RHI::Vulkan::Testing
