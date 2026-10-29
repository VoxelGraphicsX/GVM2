#pragma once

#include "VKCommon.hpp"

namespace GVM::RHI::Vulkan
{
    [[nodiscard]]
    vk::PipelineStageFlags2 normalizeStageMask(vk::PipelineStageFlags2 stageMask);

    [[nodiscard]]
    vk::AccessFlags2 extractWriteAccessMask(vk::AccessFlags2 accessMask);

    [[nodiscard]]
    bool isReadOnlyAccess(vk::AccessFlags2 accessMask);

    [[nodiscard]]
    vk::PipelineStageFlags2 resolveStageMaskForLayout(vk::ImageLayout layout);

    [[nodiscard]]
    vk::AccessFlags2 resolveAccessMaskForLayout(vk::ImageLayout layout);
} // namespace GVM::RHI::Vulkan
