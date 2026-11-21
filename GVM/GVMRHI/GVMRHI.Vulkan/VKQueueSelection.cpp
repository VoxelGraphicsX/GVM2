#include "VKQueueSelection.hpp"
#include <EASTL/vector.h>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    eastl::array<VKQueueSelection, MaxDeviceQueues> selectDeviceQueues(
        eastl::span<const vk::QueueFamilyProperties> families, uint32_t graphicsFamily)
    {
        if (graphicsFamily >= families.size() || families[graphicsFamily].queueCount == 0)
        {
            throw std::invalid_argument("Device queue selection requires an available graphics queue family.");
        }
        eastl::array<VKQueueSelection, MaxDeviceQueues> selections{};
        eastl::vector<uint32_t> used(families.size(), 0);
        selections[0] = {graphicsFamily, used[graphicsFamily]++};
        if (families[graphicsFamily].queueCount > used[graphicsFamily] &&
            (families[graphicsFamily].queueFlags & vk::QueueFlagBits::eCompute))
        {
            selections[1] = {graphicsFamily, used[graphicsFamily]++};
        }
        else
        {
            for (uint32_t family = 0; family < families.size(); ++family)
            {
                if (families[family].queueCount > used[family] &&
                    (families[family].queueFlags & vk::QueueFlagBits::eCompute))
                {
                    selections[1] = {family, used[family]++};
                    break;
                }
            }
        }
        for (uint32_t search = 0; search < 2; ++search)
        {
            for (uint32_t family = 0; family < families.size(); ++family)
            {
                const auto flags = families[family].queueFlags;
                const bool graphicsOrCompute = bool(flags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute));
                const bool transfer = graphicsOrCompute || bool(flags & vk::QueueFlagBits::eTransfer);
                if (!transfer || (search == 0 && graphicsOrCompute) || families[family].queueCount <= used[family])
                {
                    continue;
                }
                selections[2] = {family, used[family]++};
                return selections;
            }
        }
        return selections;
    }
}
