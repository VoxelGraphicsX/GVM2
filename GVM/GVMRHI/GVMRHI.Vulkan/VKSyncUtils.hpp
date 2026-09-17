#pragma once

#include "VKCommon.hpp"

namespace GVM::RHI::Vulkan
{
    [[nodiscard]]
    vk::PipelineStageFlags normalizeStageMask(vk::PipelineStageFlags stageMask);

    [[nodiscard]]
    vk::AccessFlags extractWriteAccessMask(vk::AccessFlags accessMask);

    [[nodiscard]]
    bool isReadOnlyAccess(vk::AccessFlags accessMask);

    [[nodiscard]]
    vk::PipelineStageFlags resolveStageMaskForLayout(vk::ImageLayout layout);

    [[nodiscard]]
    vk::AccessFlags resolveAccessMaskForLayout(vk::ImageLayout layout);
} // namespace GVM::RHI::Vulkan
