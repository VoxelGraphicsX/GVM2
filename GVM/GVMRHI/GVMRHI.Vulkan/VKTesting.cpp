#include "VKBuffer.hpp"
#include "VKDevice.hpp"
#include "VKInstance.hpp"
#include "VKQueue.hpp"
#include "VKRenderPipeline.hpp"
#include "VKSwapchain.hpp"
#include "VKTexture.hpp"
#include "../Private/VulkanTestHooks.hpp"

namespace GVM::RHI::Vulkan::Testing
{
    bool isVulkanInstance(Instance instance)
    {
        return dynamic_cast<GVM::RHI::Vulkan::VKInstance *>(instance) != nullptr;
    }

    bool isVulkanDevice(Device device)
    {
        return dynamic_cast<GVM::RHI::Vulkan::VKDevice *>(device) != nullptr;
    }

    bool isVulkanSwapchain(Swapchain swapchain)
    {
        return dynamic_cast<GVM::RHI::Vulkan::VKSwapchain *>(swapchain) != nullptr;
    }

    uint64_t getQueueUserSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getQueueUserSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getUserSubmitCount();
    }

    uint64_t getQueueBackendSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getQueueBackendSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getBackendSubmitCount();
    }

    uint64_t getQueuePresentInternalSubmitCount(Queue queue)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        if (vkQueue == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getQueuePresentInternalSubmitCount requires a valid Vulkan queue.");
        }
        return vkQueue->getPresentInternalSubmitCount();
    }

    size_t getFramebufferCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getFramebufferCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getFramebufferCacheSizeForTesting();
    }

    size_t getRenderPassCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getRenderPassCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getRenderPassCacheSizeForTesting();
    }

    size_t getPipelineCompatibleRenderPassCacheSize(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getPipelineCompatibleRenderPassCacheSize requires a valid Vulkan device.");
        }
        return vkDevice->getPipelineCompatibleRenderPassCacheSizeForTesting();
    }

    size_t getNativePipelineCacheSize(RenderPipeline pipeline)
    {
        auto *vkPipeline = static_cast<GVM::RHI::Vulkan::VKRenderPipeline *>(pipeline.get());
        if (vkPipeline == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getNativePipelineCacheSize requires a valid Vulkan render pipeline.");
        }
        return vkPipeline->getNativePipelineCacheSizeForTesting();
    }

    size_t getPendingDestroyBufferCount(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getPendingDestroyBufferCount requires a valid Vulkan device.");
        }
        return vkDevice->getPendingDestroyBufferCountForTesting();
    }

    uint32_t getBufferTotalReferenceCount(Device device, Buffer buffer)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getBufferTotalReferenceCount requires a valid Vulkan device.");
        }
        return vkDevice->getBufferTotalReferenceCountForTesting(buffer);
    }

    uint32_t getBufferBindGroupReferenceCount(Device device, Buffer buffer)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getBufferBindGroupReferenceCount requires a valid Vulkan device.");
        }
        return vkDevice->getBufferBindGroupReferenceCountForTesting(buffer);
    }

    bool isBufferHandleDestroyed(Buffer buffer)
    {
        auto *vkBuffer = static_cast<GVM::RHI::Vulkan::VKBuffer *>(buffer.get());
        return vkBuffer == nullptr || vkBuffer->isDestroyed();
    }

    BufferStateSnapshot getCurrentBufferState(Queue queue, Buffer buffer)
    {
        auto *vkQueue = static_cast<GVM::RHI::Vulkan::VKQueue *>(queue);
        auto *vkBuffer = static_cast<GVM::RHI::Vulkan::VKBuffer *>(buffer.get());
        if (vkQueue == nullptr || vkBuffer == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::getCurrentBufferState requires a valid Vulkan queue and buffer.");
        }

        GVM::RHI::Vulkan::VKResourceBufferState state = {};
        if (!vkQueue->getResourceStateDB().tryGetCurrentBufferState(*vkBuffer, state))
        {
            throw makeInvalidArgument("Vulkan::Testing::getCurrentBufferState failed to resolve the current Vulkan buffer state.");
        }

        BufferStateSnapshot snapshot = {};
        snapshot.stageMaskBits = static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(state.stageMask));
        snapshot.accessMaskBits = static_cast<uint64_t>(static_cast<VkAccessFlags>(state.accessMask));
        snapshot.lastWriteStageMaskBits = static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(state.lastWriteStageMask));
        snapshot.lastWriteAccessMaskBits = static_cast<uint64_t>(static_cast<VkAccessFlags>(state.lastWriteAccessMask));
        snapshot.initialized = state.initialized;
        snapshot.lastWriteInitialized = state.lastWriteInitialized;
        return snapshot;
    }

    void collectReleasedResources(Device device)
    {
        auto *vkDevice = static_cast<GVM::RHI::Vulkan::VKDevice *>(device);
        if (vkDevice == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::collectReleasedResources requires a valid Vulkan device.");
        }
        vkDevice->collectReleasedResources();
    }

    bool isTextureInSteadyStateLayout(Texture texture)
    {
        auto *vkTexture = static_cast<GVM::RHI::Vulkan::VKTexture *>(texture.get());
        if (vkTexture == nullptr)
        {
            throw makeInvalidArgument("Vulkan::Testing::isTextureInSteadyStateLayout requires a valid Vulkan texture.");
        }
        return vkTexture->isInSteadyStateLayout();
    }
} // namespace GVM::RHI::Vulkan::Testing
