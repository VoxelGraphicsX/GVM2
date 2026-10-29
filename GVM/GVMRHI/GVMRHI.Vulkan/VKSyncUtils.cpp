#include "VKSyncUtils.hpp"

namespace GVM::RHI::Vulkan
{
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
} // namespace GVM::RHI::Vulkan
