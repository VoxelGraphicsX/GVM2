#include "VKSyncUtils.hpp"

namespace GVM::RHI::Vulkan
{
    vk::PipelineStageFlags normalizeStageMask(vk::PipelineStageFlags stageMask)
    {
        return stageMask == vk::PipelineStageFlags{} ? vk::PipelineStageFlagBits::eTopOfPipe : stageMask;
    }

    vk::AccessFlags extractWriteAccessMask(vk::AccessFlags accessMask)
    {
        static constexpr vk::AccessFlags WriteAccessMask =
            vk::AccessFlagBits::eShaderWrite |
            vk::AccessFlagBits::eTransferWrite |
            vk::AccessFlagBits::eColorAttachmentWrite |
            vk::AccessFlagBits::eDepthStencilAttachmentWrite |
            vk::AccessFlagBits::eMemoryWrite |
            vk::AccessFlagBits::eHostWrite;
        return accessMask & WriteAccessMask;
    }

    bool isReadOnlyAccess(vk::AccessFlags accessMask)
    {
        return extractWriteAccessMask(accessMask) == vk::AccessFlags{};
    }

    vk::PipelineStageFlags resolveStageMaskForLayout(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined: return {};
        case vk::ImageLayout::eTransferSrcOptimal:
        case vk::ImageLayout::eTransferDstOptimal: return vk::PipelineStageFlagBits::eTransfer;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            return vk::PipelineStageFlagBits::eVertexShader |
                vk::PipelineStageFlagBits::eFragmentShader |
                vk::PipelineStageFlagBits::eComputeShader;
        case vk::ImageLayout::eColorAttachmentOptimal: return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        case vk::ImageLayout::ePresentSrcKHR: return vk::PipelineStageFlagBits::eBottomOfPipe;
        default:
            throw makeLogicError("resolveStageMaskForLayout requires an explicit stage mask for layouts without a single precise pipeline-stage meaning.");
        }
    }

    vk::AccessFlags resolveAccessMaskForLayout(vk::ImageLayout layout)
    {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined: return {};
        case vk::ImageLayout::eTransferSrcOptimal: return vk::AccessFlagBits::eTransferRead;
        case vk::ImageLayout::eTransferDstOptimal: return vk::AccessFlagBits::eTransferWrite;
        case vk::ImageLayout::eShaderReadOnlyOptimal: return vk::AccessFlagBits::eShaderRead;
        case vk::ImageLayout::eColorAttachmentOptimal:
            return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        case vk::ImageLayout::ePresentSrcKHR: return {};
        default:
            throw makeLogicError("resolveAccessMaskForLayout requires an explicit access mask for layouts without a single precise access meaning.");
        }
    }
} // namespace GVM::RHI::Vulkan
