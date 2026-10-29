#include "VKBuffer.hpp"
#include "VKDevice.hpp"
#include "VKRenderPassFramebufferCache.hpp"
#include "VKInstance.hpp"
#include "VKQueue.hpp"
#include "VKRenderPipeline.hpp"
#include "VKTexture.hpp"
#include "VKTesting.hpp"

namespace GVM::RHI::Vulkan::Testing
{
    void enableSynchronizationValidation()
    {
        VKInstance::enableSynchronizationValidationForTesting();
    }

    uint32_t getValidationErrorCount(Instance instance)
    {
        auto *vulkan = dynamic_cast<VKInstance *>(instance);
        return vulkan != nullptr ? vulkan->getValidationErrorCount() : 0;
    }

    bool isVulkanDevice(Device device)
    {
        return dynamic_cast<GVM::RHI::Vulkan::VKDevice *>(device) != nullptr;
    }

    uint64_t getQueueUserSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getQueueUserSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getUserSubmitCount();
    }

    uint64_t getQueueBackendSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getQueueBackendSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getBackendSubmitCount();
    }

    uint64_t getQueuePresentInternalSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getQueuePresentInternalSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getPresentInternalSubmitCount();
    }

    size_t getFramebufferCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getFramebufferCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getRenderPassFramebufferCache().getFramebufferCacheSize();
    }

    size_t getRenderPassCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getRenderPassCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getRenderPassFramebufferCache().getRenderPassCacheSize();
    }

    size_t getPipelineCompatibleRenderPassCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getRenderPassFramebufferCache().getPipelineCompatibleRenderPassCacheSize();
    }

    size_t getNativePipelineCacheSize(RenderPipeline pipeline)
    {
        auto *vkPipeline = static_cast<GVM::RHI::Vulkan::VKRenderPipeline *>(pipeline.get());
        if (vkPipeline == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getNativePipelineCacheSize requires a valid Vulkan render pipeline.");
        }
        return vkPipeline->getNativePipelineCacheSizeForTesting();
    }

    size_t getNativePipelineCount(RenderPipeline pipeline)
    {
        auto *vkPipeline = static_cast<VKRenderPipeline *>(pipeline.get());
        if (vkPipeline == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getNativePipelineCount requires a Vulkan pipeline.");
        }
        return vkPipeline->getNativePipelineCountForTesting();
    }

    uint32_t getBufferTotalReferenceCount(Device device, Buffer buffer)
    {
        auto *vkBuffer = static_cast<GVM::RHI::Vulkan::VKBuffer *>(buffer.get());
        return vkBuffer != nullptr ? vkBuffer->getReferenceCount() : 0u;
    }

    BufferStateSnapshot getCurrentBufferState(Queue queue, Buffer buffer)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        auto *vkBuffer = static_cast<GVM::RHI::Vulkan::VKBuffer *>(buffer.get());
        if (vkQueue == nullptr || vkBuffer == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::getCurrentBufferState requires a valid Vulkan queue and buffer.");
        }

        GVM::RHI::Vulkan::VKResourceBufferState state = {};
        if (!vkQueue->getResourceStateDB().tryGetCurrentBufferState(*vkBuffer, state))
        {
            throw std::invalid_argument("Vulkan::Testing::getCurrentBufferState failed to resolve the current Vulkan buffer state.");
        }

        BufferStateSnapshot snapshot = {};
        snapshot.stageMaskBits = static_cast<uint64_t>(static_cast<VkPipelineStageFlags2>(state.access.current.stages));
        snapshot.accessMaskBits = static_cast<uint64_t>(static_cast<VkAccessFlags2>(state.access.current.accesses));
        snapshot.lastWriteStageMaskBits = static_cast<uint64_t>(static_cast<VkPipelineStageFlags2>(state.access.producer.stages));
        snapshot.lastWriteAccessMaskBits = static_cast<uint64_t>(static_cast<VkAccessFlags2>(state.access.producer.accesses));
        snapshot.initialized = state.access.initialized;
        snapshot.lastWriteInitialized = state.access.hasProducer;
        return snapshot;
    }

    void collectReleasedResources(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::collectReleasedResources requires a valid Vulkan device.");
        }
        vkDevice->getResourceRegistry().collectReleasedResources();
    }

    void setArgumentTableCapacityForTesting(Device device, uint32_t sampledImages, uint32_t storageImages, uint32_t samplers)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::setArgumentTableCapacityForTesting requires a valid Vulkan device.");
        }
        vkDevice->getArgumentResources().setTableCapacitiesForTesting(sampledImages, storageImages, samplers);
    }

    bool isTextureInSteadyStateLayout(Texture texture)
    {
        auto *vkTexture = static_cast<GVM::RHI::Vulkan::VKTexture *>(texture.get());
        if (vkTexture == nullptr)
        {
            throw std::invalid_argument("Vulkan::Testing::isTextureInSteadyStateLayout requires a valid Vulkan texture.");
        }
        return vkTexture->isInSteadyStateLayout();
    }

    namespace
    {
        bool hasStageFlag(vk::PipelineStageFlags stages, vk::PipelineStageFlagBits flag)
        {
            return (stages & flag) != vk::PipelineStageFlags{};
        }

        bool hasAccessFlag(vk::AccessFlags accessFlags, vk::AccessFlagBits flag)
        {
            return (accessFlags & flag) != vk::AccessFlags{};
        }

        PixelLocalSubpassDependencySnapshot makeSnapshot(const vk::SubpassDependency &dependency)
        {
            PixelLocalSubpassDependencySnapshot snapshot = {};
            snapshot.sourceSubpass = dependency.srcSubpass;
            snapshot.destinationSubpass = dependency.dstSubpass;
            snapshot.sourceFragmentShaderStage =
                hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eFragmentShader);
            snapshot.sourceColorAttachmentOutputStage =
                hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eColorAttachmentOutput);
            snapshot.sourceEarlyFragmentTestsStage =
                hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eEarlyFragmentTests);
            snapshot.sourceLateFragmentTestsStage =
                hasStageFlag(dependency.srcStageMask, vk::PipelineStageFlagBits::eLateFragmentTests);
            snapshot.destinationFragmentShaderStage =
                hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eFragmentShader);
            snapshot.destinationColorAttachmentOutputStage =
                hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eColorAttachmentOutput);
            snapshot.destinationEarlyFragmentTestsStage =
                hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eEarlyFragmentTests);
            snapshot.destinationLateFragmentTestsStage =
                hasStageFlag(dependency.dstStageMask, vk::PipelineStageFlagBits::eLateFragmentTests);
            snapshot.sourceInputAttachmentReadAccess =
                hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eInputAttachmentRead);
            snapshot.sourceColorAttachmentWriteAccess =
                hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eColorAttachmentWrite);
            snapshot.sourceDepthStencilAttachmentWriteAccess =
                hasAccessFlag(dependency.srcAccessMask, vk::AccessFlagBits::eDepthStencilAttachmentWrite);
            snapshot.destinationInputAttachmentReadAccess =
                hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eInputAttachmentRead);
            snapshot.destinationColorAttachmentReadAccess =
                hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eColorAttachmentRead);
            snapshot.destinationColorAttachmentWriteAccess =
                hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eColorAttachmentWrite);
            snapshot.destinationDepthStencilAttachmentWriteAccess =
                hasAccessFlag(dependency.dstAccessMask, vk::AccessFlagBits::eDepthStencilAttachmentWrite);
            return snapshot;
        }
    }

    eastl::vector<PixelLocalSubpassDependencySnapshot> buildPixelLocalSubpassDependenciesForTesting(
        const eastl::vector<PixelLocalDependencyColorAttachment> &colorAttachments,
        const PixelLocalDependencyDepthAttachment &depthAttachment,
        uint32_t pixelLocalPassCount,
        const eastl::vector<PixelLocalPassAttachmentAccess> &attachmentAccesses)
    {
        eastl::vector<CacheDetail::RenderPassColorAttachmentSignature> colorSignatures;
        colorSignatures.reserve(colorAttachments.size());
        for (const PixelLocalDependencyColorAttachment &colorAttachment : colorAttachments)
        {
            colorSignatures.push_back(CacheDetail::RenderPassColorAttachmentSignature{
                .format = colorAttachment.format,
                .loadOp = colorAttachment.loadOp,
                .storeOp = colorAttachment.storeOp,
                .pixelLocal = colorAttachment.pixelLocal,
            });
        }

        CacheDetail::RenderPassDepthStencilAttachmentSignature depthSignature = {};
        depthSignature.format = depthAttachment.format;
        depthSignature.loadOp = depthAttachment.loadOp;
        depthSignature.storeOp = depthAttachment.storeOp;
        depthSignature.pixelLocal = depthAttachment.pixelLocal;

        const uint32_t renderPassCount = pixelLocalPassCount == 0u ? 1u : pixelLocalPassCount;
        const eastl::vector<PixelLocalPassAttachmentAccess> normalizedAccesses =
            CacheDetail::normalizeVulkanPixelLocalAttachmentAccesses(
                colorSignatures,
                depthSignature,
                true,
                renderPassCount,
                attachmentAccesses);
        const eastl::vector<vk::SubpassDependency> dependencies =
            CacheDetail::buildPixelLocalSubpassDependencies(
                colorSignatures,
                depthSignature,
                normalizedAccesses);

        eastl::vector<PixelLocalSubpassDependencySnapshot> snapshots;
        snapshots.reserve(dependencies.size());
        for (const vk::SubpassDependency &dependency : dependencies)
        {
            snapshots.push_back(makeSnapshot(dependency));
        }
        return snapshots;
    }
} // namespace GVM::RHI::Vulkan::Testing
