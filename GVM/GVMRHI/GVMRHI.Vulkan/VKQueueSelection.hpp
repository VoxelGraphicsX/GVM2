#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"
#include <EASTL/array.h>
#include <EASTL/span.h>

namespace GVM::RHI::Vulkan
{
    /// Identifies one distinct native queue; an ignored family denotes an unavailable auxiliary selection.
    struct VKQueueSelection
    {
        uint32_t family = VK_QUEUE_FAMILY_IGNORED;
        uint32_t index = 0;
    };

    /// Selects main, compute, and transfer queues without assigning a native queue twice; transfer-only families take priority.
    eastl::array<VKQueueSelection, MaxDeviceQueues> selectDeviceQueues(
        eastl::span<const vk::QueueFamilyProperties> families, uint32_t graphicsFamily);
}
