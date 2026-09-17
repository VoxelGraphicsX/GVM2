#pragma once

#include "VKUploadAllocator.hpp"

namespace GVM::RHI::Vulkan
{
    /// Owns one persistently mapped Vulkan upload staging slab used by VKUploadAllocator.
    class VKUploadAllocatorSlab final
    {
    public:
        /// Creates a staging slab with the requested capacity on the provided Vulkan device.
        VKUploadAllocatorSlab(VKDevice *device, uint64_t size);

        /// Destroys the staging buffer and its VMA allocation.
        ~VKUploadAllocatorSlab();

        /// Returns whether the slab can satisfy an allocation of the requested size and alignment.
        [[nodiscard]]
        bool hasSpaceFor(uint64_t size, uint64_t alignment) const;

        /// Reserves a slice from the slab and returns the mapped write location for that range.
        [[nodiscard]]
        VKUploadSlice allocate(uint64_t size, uint64_t alignment);

        /// Flushes the requested mapped byte range so GPU-visible uploads observe the written data.
        void flush(uint64_t offset, uint64_t size) const;

        /// Marks the slab as awaiting submission tracking and returns true only on the first mark.
        [[nodiscard]]
        bool tryMarkPendingSubmission();

        /// Associates the slab with a recorded submission completion and clears its pending flag.
        void markSubmitted(const VKSubmissionCompletion &completion);

        /// Returns whether the slab is no longer pending and its last tracked submission has completed.
        [[nodiscard]]
        bool isReadyForReuse() const;

        /// Clears transient usage state so the slab can be reused by a later upload.
        void reset();

    private:
        VKDevice *mDevice = nullptr;
        vk::Buffer mBuffer = nullptr;
        VmaAllocation mAllocation = nullptr;
        VmaAllocationInfo mAllocationInfo = {};
        void *mMappedData = nullptr;
        uint64_t mCapacity = 0;
        uint64_t mBytesUsed = 0;
        VKSubmissionCompletion mLastSubmissionCompletion;
        bool mPendingSubmission = false;
    };
} // namespace GVM::RHI::Vulkan
