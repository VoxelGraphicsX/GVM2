#pragma once

#include "MTextureCopyUtils.hpp"

#include <cstdint>

namespace GVM::RHI::Metal::Detail
{
    constexpr uint64_t SharedStagingAllocationAlignment = MetalTextureBytesPerRowAlignment;

    inline uint64_t alignStagingWriteSize(uint64_t size)
    {
        return alignUp(size, SharedStagingAllocationAlignment);
    }

    inline uint64_t selectWriteBufferBlockSize(uint64_t requiredBytes)
    {
        return alignStagingWriteSize(requiredBytes == 0u ? SharedStagingAllocationAlignment : requiredBytes);
    }

    inline bool hasSpaceForWrite(uint64_t offsetInUse, uint64_t storageSize, uint64_t requiredBytes)
    {
        return offsetInUse + alignStagingWriteSize(requiredBytes) <= storageSize;
    }

    inline bool isWriteBufferBlockExpired(uint64_t completedSubmitSerial, uint64_t lastUsedSubmitSerial, uint64_t retentionSubmitCount)
    {
        return completedSubmitSerial > lastUsedSubmitSerial + retentionSubmitCount;
    }
} // namespace GVM::RHI::Metal::Detail
