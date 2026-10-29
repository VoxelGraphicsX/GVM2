#include "BlitPassValidation.hpp"

#include <EASTL/string.h>

#include <stdexcept>

namespace GVM::RHI::Private
{
    namespace
    {
        /** Formats the label, size, and declared usage of a buffer for transfer diagnostics. */
        eastl::string describeTransferBuffer(const BufferImpl &buffer, BufferUsageFlags usage)
        {
            return eastl::string("buffer='") + (buffer.getLabelName().empty() ? "<unnamed>" : buffer.getLabelName().c_str()) +
                "' size=" + eastl::to_string(buffer.getStorageSize()) +
                " declared_usage=" + eastl::to_string(static_cast<uint64_t>(usage));
        }
    } // namespace

    void validateTransferBufferRange(const char *apiName, const BufferImpl *buffer, BufferUsageFlags usage, uint64_t offset, uint64_t size, TransferDirection direction)
    {
        if (buffer == nullptr)
        {
            throw std::invalid_argument((eastl::string(apiName) + " requires a valid buffer.").c_str());
        }
        const BufferUsageFlags permittedUsage = direction == TransferDirection::Source
            ? (BufferUsage::CopySrc | BufferUsage::MapWrite)
            : (BufferUsage::CopyDst | BufferUsage::MapRead | BufferUsage::QueryResolve);
        if ((usage & permittedUsage) == 0u)
        {
            throw std::invalid_argument((eastl::string(apiName) +
                " requires transfer-compatible buffer usage, but the destination/source buffer was created without the required usage. " +
                describeTransferBuffer(*buffer, usage)).c_str());
        }
        const uint64_t bufferSize = buffer->getStorageSize();
        if (offset > bufferSize || size > bufferSize - offset)
        {
            throw std::out_of_range((eastl::string(apiName) + " transfer range exceeds the buffer bounds. " +
                describeTransferBuffer(*buffer, usage) +
                " requested_offset=" + eastl::to_string(offset) +
                " requested_size=" + eastl::to_string(size)).c_str());
        }
    }

    void validateFillBufferRange(const BufferRange &destination)
    {
        if (destination.buffer.isNull() || destination.offset % 4u != 0u || destination.size % 4u != 0u)
        {
            throw std::invalid_argument("Buffer fill requires a resource and aligned range.");
        }
    }
} // namespace GVM::RHI::Private
