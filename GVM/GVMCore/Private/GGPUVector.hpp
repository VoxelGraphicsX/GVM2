#pragma once

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <GVMRHI/GVMRHI.hpp>
#include <cstdint>
#include <stdexcept>

namespace GVM::Core
{
    /**
     * Owns typed GPU storage for RenderSet streaming data.
     * The template parameter names the element type used by typed uploads while storage growth remains byte-addressed.
     */
    template <class T>
    class GPUVectorBase
    {
    public:
        /** Creates a GPU storage object with a device, label, usage flags, initial capacity, and growth alignment. */
        GPUVectorBase(GVM::RHI::Device device, eastl::string label, GVM::RHI::BufferUsageFlags usage, uint64_t initialCapacityBytes, uint64_t growthAlignmentBytes)
            : mDevice(device)
            , mLabel(eastl::move(label))
            , mUsage(usage)
            , mRequiredGPUBytes(initialCapacityBytes)
            , mGrowthAlignmentBytes(growthAlignmentBytes)
        {
            if (mDevice == nullptr)
            {
                throw std::invalid_argument("GPUVectorBase requires a valid device.");
            }
            if (mLabel.empty())
            {
                mLabel = "GPUVector";
            }
        }

        /** Releases subclass-owned resources and the backing GPU buffer when the concrete storage is deleted. */
        virtual ~GPUVectorBase()
        {
            destroyBackingBuffer();
        }

        /** Ensures the backing GPU buffer can store at least the requested byte count. */
        void reserveGPUBytes(uint64_t requestedBytes)
        {
            if (requestedBytes <= mRequiredGPUBytes)
            {
                return;
            }
            mRequiredGPUBytes = requestedBytes;
        }

        /** Ensures the backing GPU buffer can store at least the requested element count. */
        void reserveElements(uint64_t requestedElements)
        {
            reserveGPUBytes(getByteCountForElements(requestedElements));
        }

        /** Creates or grows GPU storage without uploading CPU bytes. */
        void commitStorage()
        {
            ensureGPUBuffer();
        }

        /** Creates or grows GPU storage and uploads the supplied typed CPU elements when dataDirty is true. */
        virtual void commitData(const T *data, uint64_t elementCount, bool dataDirty) = 0;

        /** Returns the GPU buffer that should be bound by RenderSet and shader-generated code. */
        [[nodiscard]] GVM::RHI::Buffer gpuBuffer() const
        {
            return mGPUBuffer;
        }

        /** Returns the byte count currently allocated by the GPU backing buffer. */
        [[nodiscard]] uint64_t gpuCapacityBytes() const
        {
            return mGPUBuffer.isNull() ? 0u : mGPUBuffer->getStorageSize();
        }

        /** Returns how many times the GPU backing buffer has been recreated. */
        [[nodiscard]] uint64_t generation() const
        {
            return mGeneration;
        }

        /** Releases all GPU resources owned by this storage object. */
        virtual void destroy()
        {
            destroyBackingBuffer();
            mRequiredGPUBytes = 0u;
            mGeneration = 0u;
        }

    protected:
        /** Returns the RHI usage flags required for the concrete GPU backing buffer. */
        [[nodiscard]] virtual GVM::RHI::BufferUsageFlags getBackingBufferUsage() const = 0;

        /** Runs before an existing backing buffer is copied into a replacement buffer. */
        virtual void beforeReplacingBackingBuffer()
        {
        }

        /** Runs after a replacement backing buffer has been created. */
        virtual void afterBackingBufferCreated(uint64_t)
        {
        }

        /** Runs before the current backing buffer is freed. */
        virtual void beforeDestroyingBackingBuffer()
        {
        }

        /** Returns the byte count for a typed element count and reports overflow explicitly. */
        [[nodiscard]] static uint64_t getByteCountForElements(uint64_t elementCount)
        {
            if (elementCount > (eastl::numeric_limits<uint64_t>::max() / sizeof(T)))
            {
                throw std::overflow_error("GPUVectorBase element count overflowed byte capacity.");
            }
            return elementCount * sizeof(T);
        }

        /** Creates or grows the GPU backing buffer while preserving previous GPU contents. */
        void ensureGPUBuffer()
        {
            const uint64_t requiredBytes = eastl::max<uint64_t>(mRequiredGPUBytes, 1u);
            if (!mGPUBuffer.isNull() && mGPUBuffer->getStorageSize() >= requiredBytes)
            {
                return;
            }

            beforeReplacingBackingBuffer();

            const GVM::RHI::Buffer oldBuffer = mGPUBuffer;
            const uint64_t oldBufferBytes = oldBuffer.isNull() ? 0u : oldBuffer->getStorageSize();
            const uint64_t allocationBytes = computeExpandedGPUSize(requiredBytes);
            mGPUBuffer = mDevice->createBuffer({
                .label = mLabel + "_GEN_" + eastl::to_string(mGeneration + 1u),
                .usage = getBackingBufferUsage(),
                .size = allocationBytes,
            });
            afterBackingBufferCreated(allocationBytes);

            if (!oldBuffer.isNull() && oldBufferBytes > 0u)
            {
                const uint64_t copyBytes = eastl::min(oldBufferBytes, allocationBytes);
                mDevice->getMainQueue()->copyBufferToBuffer(
                    GVM::RHI::BufferRange(oldBuffer, 0u, copyBytes),
                    GVM::RHI::BufferRange(mGPUBuffer, 0u, copyBytes));
                mDevice->freeBuffer(oldBuffer);
            }
            ++mGeneration;
        }

        GVM::RHI::Device mDevice = nullptr;
        eastl::string mLabel;
        GVM::RHI::BufferUsageFlags mUsage = GVM::RHI::BufferUsage::Storage;
        GVM::RHI::Buffer mGPUBuffer;
        uint64_t mRequiredGPUBytes = 0u;
        uint64_t mGrowthAlignmentBytes = 0u;
        uint64_t mGeneration = 0u;

    private:
        /** Releases the current backing GPU buffer after concrete storage has handled strategy-specific state. */
        void destroyBackingBuffer()
        {
            beforeDestroyingBackingBuffer();
            if (!mGPUBuffer.isNull() && mDevice != nullptr)
            {
                mDevice->freeBuffer(mGPUBuffer);
            }
            mGPUBuffer.reset();
        }

        /** Computes the next backing allocation size using byte growth settings for GPU-only storage. */
        [[nodiscard]] uint64_t computeExpandedGPUSize(uint64_t requiredBytes) const
        {
            const uint64_t minimumBytes = eastl::max<uint64_t>(requiredBytes, 1u);
            if (mGrowthAlignmentBytes == 0u)
            {
                return minimumBytes;
            }

            uint64_t targetBytes = mGPUBuffer.isNull() ? mGrowthAlignmentBytes : alignUp(mGPUBuffer->getStorageSize(), mGrowthAlignmentBytes);
            while (targetBytes < minimumBytes)
            {
                const uint64_t growthStep = eastl::max<uint64_t>(targetBytes / 2u, mGrowthAlignmentBytes);
                if (growthStep > (eastl::numeric_limits<uint64_t>::max() - targetBytes))
                {
                    targetBytes = minimumBytes;
                    break;
                }
                targetBytes += growthStep;
            }
            return alignUp(targetBytes, mGrowthAlignmentBytes);
        }

        /** Rounds a byte count up to the next multiple of the supplied alignment. */
        [[nodiscard]] static uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            if (alignment == 0u)
            {
                return value;
            }
            const uint64_t remainder = value % alignment;
            return remainder == 0u ? value : value + (alignment - remainder);
        }
    };
} // namespace GVM::Core
