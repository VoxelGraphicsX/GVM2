#pragma once

#include "GGPUVector.hpp"

#include <EASTL/utility.h>
#include <cstring>
#include <stdexcept>

namespace GVM::Core
{
    /**
     * Stores typed GPUVector data in a CPU-visible backing buffer for unified-memory devices.
     * The backing buffer stays persistently mapped so dirty commits can write directly into GPU-visible memory.
     */
    template <class T>
    class UnifiedMemoryGPUVector final : public GPUVectorBase<T>
    {
    public:
        /** Creates unified-memory GPU storage with the requested label, usage, initial capacity, and growth alignment. */
        UnifiedMemoryGPUVector(GVM::RHI::Device device, eastl::string label, GVM::RHI::BufferUsageFlags usage, uint64_t initialCapacityBytes, uint64_t growthAlignmentBytes)
            : GPUVectorBase<T>(device, eastl::move(label), usage, initialCapacityBytes, growthAlignmentBytes)
        {
        }

        /** Releases the persistent mapping and backing buffer owned by this storage object. */
        ~UnifiedMemoryGPUVector() override
        {
            destroy();
        }

        /** Creates or grows GPU storage and writes dirty CPU elements into the mapped backing buffer. */
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
                throw std::invalid_argument("UnifiedMemoryGPUVector::commitData requires non-null data for non-empty uploads.");
            }
            if (mMappedData == nullptr)
            {
                throw std::logic_error("UnifiedMemoryGPUVector::commitData requires a mapped backing buffer.");
            }
            std::memcpy(mMappedData, data, static_cast<size_t>(byteSize));
        }

        /** Releases the persistent mapping and backing GPU buffer. */
        void destroy() override
        {
            GPUVectorBase<T>::destroy();
            mMappedData = nullptr;
        }

    private:
        /** Returns MapWrite-capable usage for the unified-memory backing buffer. */
        [[nodiscard]] GVM::RHI::BufferUsageFlags getBackingBufferUsage() const override
        {
            return GVM::RHI::BufferUsage::Storage |
                GVM::RHI::BufferUsage::CopyDst |
                GVM::RHI::BufferUsage::CopySrc |
                GVM::RHI::BufferUsage::MapWrite |
                this->mUsage;
        }

        /** Unmaps the current persistent mapping before replacing the backing buffer. */
        void beforeReplacingBackingBuffer() override
        {
            if (!this->mGPUBuffer.isNull() && mMappedData != nullptr)
            {
                this->mGPUBuffer->unmap();
                mMappedData = nullptr;
            }
        }

        /** Maps the replacement backing buffer so future commits can write through it. */
        void afterBackingBufferCreated(uint64_t allocationBytes) override
        {
            this->mGPUBuffer->map();
            mMappedData = this->mGPUBuffer->getMappedRange(0u, allocationBytes);
            if (mMappedData == nullptr)
            {
                throw std::logic_error("UnifiedMemoryGPUVector failed to map its backing buffer.");
            }
        }

        /** Unmaps the current backing buffer before it is destroyed. */
        void beforeDestroyingBackingBuffer() override
        {
            if (!this->mGPUBuffer.isNull() && mMappedData != nullptr)
            {
                this->mGPUBuffer->unmap();
                mMappedData = nullptr;
            }
        }

        void *mMappedData = nullptr;
    };
} // namespace GVM::Core
