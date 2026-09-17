#include "VKBufferRangeStateUtils.hpp"

#include "VKBuffer.hpp"

#include <EASTL/string.h>

namespace GVM::RHI::Vulkan
{
    uint64_t resolveBufferRangeSize(const char *context, const VKBuffer *buffer, uint64_t offset, uint64_t size)
    {
        if (buffer == nullptr)
        {
            return 0u;
        }

        const uint64_t storageSize = buffer->getStorageSize();
        if (offset > storageSize)
        {
            throw makeOutOfRange(eastl::string(context) + " received an offset beyond the Vulkan buffer size.");
        }

        if (size == WholeSize)
        {
            return storageSize - offset;
        }

        if (size > storageSize - offset)
        {
            throw makeOutOfRange(eastl::string(context) + " received a Vulkan buffer range that exceeds the buffer size.");
        }

        return size;
    }

    bool bufferRangesOverlap(uint64_t lhsOffset, uint64_t lhsSize, uint64_t rhsOffset, uint64_t rhsSize)
    {
        if (lhsSize == 0u || rhsSize == 0u)
        {
            return false;
        }

        const uint64_t lhsEnd = lhsOffset + lhsSize;
        const uint64_t rhsEnd = rhsOffset + rhsSize;
        return lhsOffset < rhsEnd && rhsOffset < lhsEnd;
    }
} // namespace GVM::RHI::Vulkan
