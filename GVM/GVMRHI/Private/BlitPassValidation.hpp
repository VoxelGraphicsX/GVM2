#pragma once

#include <GVMRHI/GVMRHI.hpp>

#include <cstdint>

namespace GVM::RHI::Private
{
    /** Side of a blit transfer a buffer range is on; selects the usage bits the buffer must declare. */
    enum class TransferDirection
    {
        Source,
        Destination,
    };

    /** Validates one buffer range of a blit copy or fill on either backend: buffer is non-null, usage (the buffer's declared usage) permits the direction
        (CopySrc or MapWrite for sources; CopyDst, MapRead or QueryResolve for destinations), and [offset, offset + size) lies inside the buffer.
        apiName prefixes every message. */
    void validateTransferBufferRange(const char *apiName, const BufferImpl *buffer, BufferUsageFlags usage, uint64_t offset, uint64_t size, TransferDirection direction);

    /** Validates the shape of a fill on either backend: the destination buffer is non-null and its offset and size are multiples of four bytes. */
    void validateFillBufferRange(const BufferRange &destination);
} // namespace GVM::RHI::Private
