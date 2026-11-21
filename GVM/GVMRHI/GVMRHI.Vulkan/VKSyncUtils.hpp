#pragma once

#include "VKCommon.hpp"
#include <EASTL/vector.h>

namespace GVM::RHI::Vulkan
{
    /// Combines waits on one native timeline while preserving its largest value and all required stages.
    void mergeTimelineWait(eastl::vector<vk::SemaphoreSubmitInfo> &waits, const vk::SemaphoreSubmitInfo &wait);

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

    [[nodiscard]]
    vk::PipelineStageFlags2 supportedStageMaskForQueueFlags(vk::QueueFlags queueFlags);

    [[nodiscard]]
    vk::AccessFlags2 supportedAccessMaskForQueueFlags(vk::QueueFlags queueFlags);
} // namespace GVM::RHI::Vulkan
