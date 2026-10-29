#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>
#include <stdexcept>

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

    /** Validates usage, alignment and bounds for one encoder buffer input and resolves WholeSize in place; liveness and device ownership stay with the backend. */
    inline void validateEncoderBufferRange(BufferUsageFlags bufferUsage, uint64_t storageSize, BufferRange &range,
        BufferUsageFlags requiredUsage, uint32_t alignment)
    {
        if ((bufferUsage & requiredUsage) != requiredUsage)
        {
            throw std::invalid_argument("Buffer usage does not match the operation.");
        }
        if (alignment == 0 || range.offset % alignment != 0 || range.offset >= storageSize)
        {
            throw std::out_of_range("Buffer offset is outside its allocation or misaligned.");
        }
        const uint64_t length = range.size == WholeSize ? storageSize - range.offset : range.size;
        if (length == 0 || length > storageSize - range.offset)
        {
            throw std::out_of_range("Buffer range is empty or exceeds its allocation.");
        }
        range.size = length;
    }
} // namespace GVM::RHI::Detail
