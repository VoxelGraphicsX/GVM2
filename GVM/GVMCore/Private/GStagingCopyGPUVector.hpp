#pragma once

#include "GGPUVector.hpp"

#include <EASTL/utility.h>
#include <cstring>
#include <stdexcept>

namespace GVM::Core
{
    /**
     * Stores typed GPUVector data in device-local storage and uploads dirty CPU elements through a staging buffer.
     * Use this storage on non-unified-memory devices where the GPU backing buffer should not be CPU mapped.
     */
    template <class T>
    class StagingCopyGPUVector final : public GPUVectorBase<T>
    {
    public:
        /** Creates staging-copy GPU storage with the requested label, usage, initial capacity, and growth alignment. */
        StagingCopyGPUVector(GVM::RHI::Device device, eastl::string label, GVM::RHI::BufferUsageFlags usage, uint64_t initialCapacityBytes, uint64_t growthAlignmentBytes)
            : GPUVectorBase<T>(device, eastl::move(label), usage, initialCapacityBytes, growthAlignmentBytes)
        {
        }

        /** Releases the staging buffer and backing buffer owned by this storage object. */
        ~StagingCopyGPUVector() override
        {
            destroy();
        }

        /** Creates or grows GPU storage and uploads dirty CPU elements through a MapWrite staging buffer. */
        void commitData(const T *data, uint64_t elementCount, bool dataDirty) override
        {
            const uint64_t byteSize = GPUVectorBase<T>::getByteCountForElements(elementCount);
            this->reserveGPUBytes(byteSize);
            this->ensureGPUBuffer();
            if (!dataDirty || byteSize == 0u)
            {
                return;
            }
            if (data == nullptr)
            {
                throw std::invalid_argument("StagingCopyGPUVector::commitData requires non-null data for non-empty uploads.");
            }
            ensureStagingBuffer(byteSize);
            mStagingBuffer->map();
            void *mappedData = mStagingBuffer->getMappedRange(0u, byteSize);
            if (mappedData == nullptr)
            {
                mStagingBuffer->unmap();
                throw std::logic_error("StagingCopyGPUVector::commitData failed to map the staging buffer.");
            }
            std::memcpy(mappedData, data, static_cast<size_t>(byteSize));
            mStagingBuffer->unmap();
            this->mDevice->getMainQueue()->copyBufferToBuffer(
                GVM::RHI::BufferRange(mStagingBuffer, 0u, byteSize),
                GVM::RHI::BufferRange(this->mGPUBuffer, 0u, byteSize));
        }

        /** Releases both device-local and staging buffers owned by this storage object. */
        void destroy() override
        {
            GPUVectorBase<T>::destroy();
            destroyStagingBuffer();
        }

    private:
        /** Returns device-local usage for the GPU backing buffer. */
        [[nodiscard]] GVM::RHI::BufferUsageFlags getBackingBufferUsage() const override
        {
            return GVM::RHI::BufferUsage::Storage |
                GVM::RHI::BufferUsage::CopyDst |
                GVM::RHI::BufferUsage::CopySrc |
                this->mUsage;
        }

        /** Ensures the staging buffer can hold the next upload payload. */
        void ensureStagingBuffer(uint64_t requiredBytes)
        {
            if (!mStagingBuffer.isNull() && mStagingBuffer->getStorageSize() >= requiredBytes)
            {
                return;
            }
            destroyStagingBuffer();
            mStagingBuffer = this->mDevice->createBuffer({
                .label = this->mLabel + "_Staging",
                .usage = GVM::RHI::BufferUsage::MapWrite | GVM::RHI::BufferUsage::CopySrc,
                .size = requiredBytes,
            });
        }

        /** Releases the current staging buffer when it exists. */
        void destroyStagingBuffer()
        {
            if (!mStagingBuffer.isNull() && this->mDevice != nullptr)
            {
                this->mDevice->freeBuffer(mStagingBuffer);
            }
            mStagingBuffer.reset();
        }

        GVM::RHI::Buffer mStagingBuffer;
    };
} // namespace GVM::Core
