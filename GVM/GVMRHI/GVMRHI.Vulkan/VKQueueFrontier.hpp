#pragma once
#include "VKCommon.hpp"

namespace GVM::RHI::Vulkan
{
    /// Records the last writer and outstanding readers on the two device timelines for one range or image cell.
    struct VKQueueFrontier
    {
        uint64_t writer[2]{};
        uint64_t readers[2]{};
        uint32_t family = VK_QUEUE_FAMILY_IGNORED;
        /// Advances one of two queue timelines and accumulates the foreign wait needed for the access.
        bool advance(uint32_t lane, uint32_t queueFamily, uint64_t value, bool exclusive, uint64_t &foreignWait);
        /// Compares all accepted uses before two adjacent buffer ranges may be coalesced.
        bool operator==(const VKQueueFrontier &) const;
    };
}
