#include "VKQueueFrontier.hpp"
#include <EASTL/algorithm.h>

namespace GVM::RHI::Vulkan
{
    bool VKQueueFrontier::operator==(const VKQueueFrontier &) const = default;

    bool VKQueueFrontier::advance(uint32_t lane, uint32_t queueFamily, uint64_t value, bool exclusive, uint64_t &foreignWait)
    {
        const bool transfer = family != VK_QUEUE_FAMILY_IGNORED && family != queueFamily;
        family = queueFamily;
        const uint32_t other = 1 - lane;
        foreignWait = eastl::max(foreignWait, writer[other]);
        if (exclusive || transfer)
        {
            foreignWait = eastl::max(foreignWait, readers[other]);
            writer[other] = 0;
            writer[lane] = value;
            readers[0] = readers[1] = 0;
        }
        else
        {
            readers[lane] = value;
        }
        return transfer;
    }
}
