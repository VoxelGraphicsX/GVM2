#include "VKSyncUtils.hpp"
#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    void mergeTimelineWait(eastl::vector<vk::SemaphoreSubmitInfo> &waits, const vk::SemaphoreSubmitInfo &wait)
    {
        for (auto &entry : waits)
        {
            if (entry.semaphore == wait.semaphore)
            {
                entry.value = eastl::max(entry.value, wait.value);
                entry.stageMask |= wait.stageMask;
                return;
            }
        }
        waits.push_back(wait);
    }

    vk::PipelineStageFlags2 normalizeStageMask(vk::PipelineStageFlags2 stageMask)
    {
        return stageMask == vk::PipelineStageFlags2{} ? vk::PipelineStageFlagBits2::eTopOfPipe : stageMask;
    }

    vk::AccessFlags2 extractWriteAccessMask(vk::AccessFlags2 accessMask)
    {
        static constexpr vk::AccessFlags2 WriteAccessMask =
            vk::AccessFlagBits2::eShaderWrite |
            vk::AccessFlagBits2::eShaderStorageWrite |
            vk::AccessFlagBits2::eTransferWrite |
            vk::AccessFlagBits2::eColorAttachmentWrite |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
            vk::AccessFlagBits2::eMemoryWrite |
            vk::AccessFlagBits2::eHostWrite;
        return accessMask & WriteAccessMask;
    }

    bool isReadOnlyAccess(vk::AccessFlags2 accessMask)
    {
        return extractWriteAccessMask(accessMask) == vk::AccessFlags2{};
    }

    vk::PipelineStageFlags2 resolveStageMaskForLayout(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined: return {};
        case vk::ImageLayout::eTransferSrcOptimal:
        case vk::ImageLayout::eTransferDstOptimal: return vk::PipelineStageFlagBits2::eTransfer;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eComputeShader;
        case vk::ImageLayout::eColorAttachmentOptimal: return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
        case vk::ImageLayout::ePresentSrcKHR: return vk::PipelineStageFlagBits2::eBottomOfPipe;
        default:
            throw std::logic_error("resolveStageMaskForLayout requires an explicit stage mask for layouts without a single precise pipeline-stage meaning.");
        }
    }

    vk::AccessFlags2 resolveAccessMaskForLayout(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined: return {};
        case vk::ImageLayout::eTransferSrcOptimal: return vk::AccessFlagBits2::eTransferRead;
        case vk::ImageLayout::eTransferDstOptimal: return vk::AccessFlagBits2::eTransferWrite;
        case vk::ImageLayout::eShaderReadOnlyOptimal: return vk::AccessFlagBits2::eShaderRead;
        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        case vk::ImageLayout::ePresentSrcKHR: return {};
        default:
            throw std::logic_error("resolveAccessMaskForLayout requires an explicit access mask for layouts without a single precise access meaning.");
        }
    }

    vk::PipelineStageFlags2 supportedStageMaskForQueueFlags(vk::QueueFlags queueFlags)
    {
        vk::PipelineStageFlags2 stages =
            vk::PipelineStageFlagBits2::eTopOfPipe |
            vk::PipelineStageFlagBits2::eBottomOfPipe |
            vk::PipelineStageFlagBits2::eHost |
            vk::PipelineStageFlagBits2::eAllCommands;

        const bool hasGraphics = static_cast<bool>(queueFlags & vk::QueueFlagBits::eGraphics);
        const bool hasCompute = static_cast<bool>(queueFlags & vk::QueueFlagBits::eCompute);
        const bool hasTransfer = hasGraphics || hasCompute || static_cast<bool>(queueFlags & vk::QueueFlagBits::eTransfer);

        if (hasGraphics && hasCompute)
        {
            return ~vk::PipelineStageFlags2{};
        }
        if (hasTransfer)
        {
            stages |=
                vk::PipelineStageFlagBits2::eTransfer |
                vk::PipelineStageFlagBits2::eCopy |
                vk::PipelineStageFlagBits2::eClear |
                vk::PipelineStageFlagBits2::eAllTransfer;
        }
        if (hasCompute)
        {
            stages |=
                vk::PipelineStageFlagBits2::eDrawIndirect |
                vk::PipelineStageFlagBits2::eComputeShader |
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        }
        if (hasGraphics)
        {
            stages |=
                vk::PipelineStageFlagBits2::eDrawIndirect |
                vk::PipelineStageFlagBits2::eVertexInput |
                vk::PipelineStageFlagBits2::eVertexAttributeInput |
                vk::PipelineStageFlagBits2::eIndexInput |
                vk::PipelineStageFlagBits2::eVertexShader |
                vk::PipelineStageFlagBits2::eTessellationControlShader |
                vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
                vk::PipelineStageFlagBits2::eGeometryShader |
                vk::PipelineStageFlagBits2::eFragmentShader |
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                vk::PipelineStageFlagBits2::eLateFragmentTests |
                vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                vk::PipelineStageFlagBits2::eAllGraphics |
                vk::PipelineStageFlagBits2::ePreRasterizationShaders |
                vk::PipelineStageFlagBits2::eBlit |
                vk::PipelineStageFlagBits2::eResolve;
        }
        return stages;
    }

    vk::AccessFlags2 supportedAccessMaskForQueueFlags(vk::QueueFlags queueFlags)
    {
        vk::AccessFlags2 accesses =
            vk::AccessFlagBits2::eHostRead |
            vk::AccessFlagBits2::eHostWrite |
            vk::AccessFlagBits2::eMemoryRead |
            vk::AccessFlagBits2::eMemoryWrite;

        const bool hasGraphics = static_cast<bool>(queueFlags & vk::QueueFlagBits::eGraphics);
        const bool hasCompute = static_cast<bool>(queueFlags & vk::QueueFlagBits::eCompute);
        const bool hasTransfer = hasGraphics || hasCompute || static_cast<bool>(queueFlags & vk::QueueFlagBits::eTransfer);

        if (hasGraphics && hasCompute)
        {
            return ~vk::AccessFlags2{};
        }
        if (hasTransfer)
        {
            accesses |=
                vk::AccessFlagBits2::eTransferRead |
                vk::AccessFlagBits2::eTransferWrite;
        }
        if (hasCompute)
        {
            accesses |=
                vk::AccessFlagBits2::eIndirectCommandRead |
                vk::AccessFlagBits2::eUniformRead |
                vk::AccessFlagBits2::eShaderRead |
                vk::AccessFlagBits2::eShaderWrite |
                vk::AccessFlagBits2::eShaderSampledRead |
                vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite |
                vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        }
        if (hasGraphics)
        {
            accesses |=
                vk::AccessFlagBits2::eIndirectCommandRead |
                vk::AccessFlagBits2::eIndexRead |
                vk::AccessFlagBits2::eVertexAttributeRead |
                vk::AccessFlagBits2::eUniformRead |
                vk::AccessFlagBits2::eInputAttachmentRead |
                vk::AccessFlagBits2::eShaderRead |
                vk::AccessFlagBits2::eShaderWrite |
                vk::AccessFlagBits2::eShaderSampledRead |
                vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite |
                vk::AccessFlagBits2::eColorAttachmentRead |
                vk::AccessFlagBits2::eColorAttachmentWrite |
                vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        }
        return accesses;
    }
} // namespace GVM::RHI::Vulkan
