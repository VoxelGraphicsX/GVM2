#include "VKUploadAllocatorSlab.hpp"

#include "VKDevice.hpp"

namespace GVM::RHI::Vulkan
{
    namespace
    {
        uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            if (alignment <= 1u)
            {
                return value;
            }

            const uint64_t remainder = value % alignment;
            return remainder == 0u ? value : (value + (alignment - remainder));
        }
    } // namespace

    VKUploadAllocatorSlab::VKUploadAllocatorSlab(VKDevice *device, uint64_t size)
        : mDevice(device)
        , mCapacity(size)
    {
        if (mDevice == nullptr)
        {
            throw makeInvalidArgument("VKUploadAllocatorSlab requires a valid Vulkan device.");
        }
        if (mCapacity == 0)
        {
            throw makeInvalidArgument("VKUploadAllocatorSlab requires a non-zero capacity.");
        }

        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = mCapacity;
        bufferCreateInfo.usage = static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc);
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo = {};
        allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VkBuffer nativeBuffer = VK_NULL_HANDLE;
        if (vmaCreateBuffer(mDevice->getAllocator(), &bufferCreateInfo, &allocationCreateInfo, &nativeBuffer, &mAllocation, &mAllocationInfo) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKUploadAllocatorSlab failed to allocate upload staging memory.");
        }

        mBuffer = nativeBuffer;
        mMappedData = mAllocationInfo.pMappedData;
        if (mMappedData == nullptr)
        {
            vmaDestroyBuffer(mDevice->getAllocator(), nativeBuffer, mAllocation);
            mAllocation = nullptr;
            mBuffer = nullptr;
            throw makeRuntimeError("VKUploadAllocatorSlab expected persistently mapped host memory.");
        }
    }

    VKUploadAllocatorSlab::~VKUploadAllocatorSlab()
    {
        if (mBuffer && mAllocation != nullptr && mDevice != nullptr)
        {
            vmaDestroyBuffer(mDevice->getAllocator(), static_cast<VkBuffer>(mBuffer), mAllocation);
        }
    }

    bool VKUploadAllocatorSlab::hasSpaceFor(uint64_t size, uint64_t alignment) const
    {
        const uint64_t alignedOffset = alignUp(mBytesUsed, alignment);
        return alignedOffset <= mCapacity && size <= (mCapacity - alignedOffset);
    }

    VKUploadSlice VKUploadAllocatorSlab::allocate(uint64_t size, uint64_t alignment)
    {
        if (!hasSpaceFor(size, alignment))
        {
            throw makeOutOfRange("VKUploadAllocatorSlab ran out of staging space.");
        }

        const uint64_t alignedOffset = alignUp(mBytesUsed, alignment);
        mBytesUsed = alignedOffset + size;

        VKUploadSlice slice = {};
        slice.buffer = mBuffer;
        slice.offset = alignedOffset;
        slice.mappedData = static_cast<uint8_t *>(mMappedData) + alignedOffset;
        return slice;
    }

    void VKUploadAllocatorSlab::flush(uint64_t offset, uint64_t size) const
    {
        if (size == 0 || mAllocation == nullptr)
        {
            return;
        }

        if (vmaFlushAllocation(mDevice->getAllocator(), mAllocation, offset, size) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKUploadAllocatorSlab failed to flush upload memory.");
        }
    }

    bool VKUploadAllocatorSlab::tryMarkPendingSubmission()
    {
        if (mPendingSubmission)
        {
            return false;
        }

        mPendingSubmission = true;
        return true;
    }

    void VKUploadAllocatorSlab::markSubmitted(const VKSubmissionCompletion &completion)
    {
        mLastSubmissionCompletion = completion;
        mPendingSubmission = false;
    }

    bool VKUploadAllocatorSlab::isReadyForReuse() const
    {
        if (mPendingSubmission || !mLastSubmissionCompletion)
        {
            return false;
        }

        return mLastSubmissionCompletion->completed.load(std::memory_order_acquire);
    }

    void VKUploadAllocatorSlab::reset()
    {
        mBytesUsed = 0;
        mLastSubmissionCompletion.reset();
        mPendingSubmission = false;
    }
} // namespace GVM::RHI::Vulkan
