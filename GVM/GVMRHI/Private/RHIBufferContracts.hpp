#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>

namespace GVM::RHI::Detail
{
    struct ResolvedMapRange
    {
        bool valid = false;
        uint64_t resolvedSize = 0;
    };

    constexpr bool isBufferMappable(BufferUsageFlags usage)
    {
        return (usage & (BufferUsage::MapRead | BufferUsage::MapWrite)) != 0u;
    }

    constexpr ResolvedMapRange resolveMappedRange(uint64_t storageSize, uint64_t offset, uint64_t requestedSize)
    {
        if (offset > storageSize)
        {
            return {};
        }

        const uint64_t remainingBytes = storageSize - offset;
        const uint64_t resolvedSize = requestedSize == WholeMapSize ? remainingBytes : requestedSize;
        if (resolvedSize > remainingBytes)
        {
            return {};
        }

        return {.valid = true, .resolvedSize = resolvedSize};
    }
} // namespace GVM::RHI::Detail
