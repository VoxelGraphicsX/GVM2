#include "VKBuffer.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKQueue.hpp"

#include <GVMRHI/Private/RHIBufferContracts.hpp>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        bool isReadbackOnlyTransferBuffer(BufferUsageFlags usage)
        {
            const bool hasMapRead = (usage & BufferUsage::MapRead) != 0u;
            const bool hasMapWrite = (usage & BufferUsage::MapWrite) != 0u;
            const bool hasCopySrc = (usage & BufferUsage::CopySrc) != 0u;
            const bool hasCopyDst = (usage & BufferUsage::CopyDst) != 0u;
            const bool hasGpuBindingUsage =
                (usage & (BufferUsage::Storage |
                          BufferUsage::Uniform |
                          BufferUsage::Vertex |
                          BufferUsage::Index |
                          BufferUsage::Indirect)) != 0u;
            return hasMapRead && !hasMapWrite && !hasCopySrc && hasCopyDst && !hasGpuBindingUsage;
        }

        bool shouldProvisionInternalStorageBinding(BufferUsageFlags usage)
        {
            if ((usage & BufferUsage::Storage) != 0u)
            {
                return true;
            }
            if (isReadbackOnlyTransferBuffer(usage))
            {
                return false;
            }
            return (usage & (BufferUsage::CopySrc |
                             BufferUsage::CopyDst |
                             BufferUsage::MapWrite)) != 0u;
        }

        VmaAllocationCreateInfo buildBufferAllocationInfo(BufferUsageFlags usage)
        {
            VmaAllocationCreateInfo allocationInfo = {};
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if ((usage & BufferUsage::MapRead) != 0u)
            {
                allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }
            else if ((usage & BufferUsage::MapWrite) != 0u)
            {
                allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }

            return allocationInfo;
        }
    } // namespace

    void VKBuffer::init(VKDevice &device, const BufferDescriptor &descriptor)
    {
        if (descriptor.size == 0)
        {
            throw makeInvalidArgument("VKBuffer::init requires descriptor.size > 0.");
        }

        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;

        vk::BufferUsageFlags usageFlags = translateBufferUsage(descriptor.usage);
        if (shouldProvisionInternalStorageBinding(descriptor.usage))
        {
            // Vulkan multi-region copies stay on the GPU compute path, so upload/copy
            // participating buffers still need storage capability even when callers
            // only requested transfer/map usage. Keep that widening targeted and skip
            // pure readback-only buffers, which never participate in the compute path.
            usageFlags |= vk::BufferUsageFlagBits::eStorageBuffer;
        }
        mNativeUsageFlags = usageFlags;

        VkBufferCreateInfo bufferCreateInfo = {};
        bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferCreateInfo.size = descriptor.size;
        bufferCreateInfo.usage = static_cast<VkBufferUsageFlags>(usageFlags);
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationCreateInfo = buildBufferAllocationInfo(descriptor.usage);

        VkBuffer nativeBuffer = VK_NULL_HANDLE;
        if (vmaCreateBuffer(
                device.getAllocator(),
                &bufferCreateInfo,
                &allocationCreateInfo,
                &nativeBuffer,
                &mAllocation,
                &mAllocationInfo) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKBuffer::init failed to allocate buffer memory.");
        }

        mBuffer = nativeBuffer;
        mPersistentMappedData = mAllocationInfo.pMappedData;
        vmaGetAllocationMemoryProperties(device.getAllocator(), mAllocation, &mMemoryProperties);
        mLifetime.ownerReferences.store(1u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(false, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mDestroyed = false;
    }

    vk::Buffer VKBuffer::getNativeBuffer() const
    {
        return mBuffer;
    }

    VmaAllocation VKBuffer::getNativeAllocation() const
    {
        return mAllocation;
    }

    VkDeviceMemory VKBuffer::getNativeDeviceMemory() const
    {
        return mAllocationInfo.deviceMemory;
    }

    VkDeviceSize VKBuffer::getNativeAllocationOffset() const
    {
        return mAllocationInfo.offset;
    }

    BufferUsageFlags VKBuffer::getUsage() const
    {
        return mDescriptor.usage;
    }

    vk::BufferUsageFlags VKBuffer::getNativeUsageFlags() const
    {
        return mNativeUsageFlags;
    }

    bool VKBuffer::supportsUniformBinding() const
    {
        return (mNativeUsageFlags & vk::BufferUsageFlagBits::eUniformBuffer) != vk::BufferUsageFlags{};
    }

    bool VKBuffer::supportsStorageBinding() const
    {
        return (mNativeUsageFlags & vk::BufferUsageFlagBits::eStorageBuffer) != vk::BufferUsageFlags{};
    }

    bool VKBuffer::isMapped() const
    {
        return mIsMapped;
    }

    bool VKBuffer::isDestroyed() const
    {
        return mDestroyed;
    }

    bool VKBuffer::isHostVisible() const
    {
        return (mMemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }

    bool VKBuffer::isHostCoherent() const
    {
        return (mMemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }

    bool VKBuffer::supportsCpuRead() const
    {
        return (mDescriptor.usage & BufferUsage::MapRead) != 0u;
    }

    bool VKBuffer::supportsCpuWrite() const
    {
        return (mDescriptor.usage & BufferUsage::MapWrite) != 0u;
    }

    void *VKBuffer::getPersistentMappedData() const
    {
        return mPersistentMappedData;
    }

    uint64_t VKBuffer::getStorageSize() const
    {
        return mDescriptor.size;
    }

    void VKBuffer::map()
    {
        if (!Detail::isBufferMappable(mDescriptor.usage))
        {
            throw makeRuntimeError("VKBuffer::map was called on a non-mappable buffer.");
        }
        if (!isHostVisible() || mPersistentMappedData == nullptr)
        {
            throw makeRuntimeError("VKBuffer::map could not expose host-visible memory for this buffer.");
        }

        if (supportsCpuRead())
        {
            invalidateAllocation(0, WholeMapSize);
        }
        mIsMapped = true;
    }

    void const *VKBuffer::getConstMappedRange(uint64_t offset, uint64_t size) const
    {
        if (!mIsMapped || mPersistentMappedData == nullptr)
        {
            throw makeLogicError("VKBuffer::getConstMappedRange requires the buffer to be mapped.");
        }

        const Detail::ResolvedMapRange range = Detail::resolveMappedRange(mDescriptor.size, offset, size);
        if (!range.valid)
        {
            throw makeOutOfRange("VKBuffer::getConstMappedRange received an invalid mapped range.");
        }

        const auto *basePointer = static_cast<const uint8_t *>(mPersistentMappedData);
        return basePointer + offset;
    }

    void *VKBuffer::getMappedRange(uint64_t offset, uint64_t size) const
    {
        if (!mIsMapped || mPersistentMappedData == nullptr)
        {
            throw makeLogicError("VKBuffer::getMappedRange requires the buffer to be mapped.");
        }

        const Detail::ResolvedMapRange range = Detail::resolveMappedRange(mDescriptor.size, offset, size);
        if (!range.valid)
        {
            throw makeOutOfRange("VKBuffer::getMappedRange received an invalid mapped range.");
        }

        auto *basePointer = static_cast<uint8_t *>(mPersistentMappedData);
        return basePointer + offset;
    }

    void VKBuffer::unmap()
    {
        if (!mIsMapped)
        {
            return;
        }

        if (supportsCpuWrite())
        {
            flushAllocation(0, WholeMapSize);
            recordHostWrite();
        }
        mIsMapped = false;
    }

    void VKBuffer::recordHostWrite()
    {
        const vk::PipelineStageFlags stageMask = vk::PipelineStageFlagBits::eHost;
        const vk::AccessFlags accessMask = vk::AccessFlagBits::eHostWrite;
        if (mDevice == nullptr)
        {
            throw makeRuntimeError("VKBuffer::recordHostWrite requires a live owning Vulkan device.");
        }

        VKQueue *queue = mDevice->getMainQueueImpl();
        if (queue == nullptr)
        {
            throw makeRuntimeError("VKBuffer::recordHostWrite requires a live owning Vulkan queue.");
        }

        queue->getResourceStateDB().recordExternalBufferUsage(*this, stageMask, accessMask);
    }

    void VKBuffer::flushAllocation(uint64_t offset, uint64_t size) const
    {
        if (mAllocation == nullptr || isHostCoherent())
        {
            return;
        }

        const VkDeviceSize flushSize = size == WholeMapSize ? VK_WHOLE_SIZE : size;
        if (vmaFlushAllocation(mDevice->getAllocator(), mAllocation, offset, flushSize) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKBuffer::flushAllocation failed.");
        }
    }

    void VKBuffer::invalidateAllocation(uint64_t offset, uint64_t size) const
    {
        if (mAllocation == nullptr || isHostCoherent())
        {
            return;
        }

        const VkDeviceSize invalidateSize = size == WholeMapSize ? VK_WHOLE_SIZE : size;
        if (vmaInvalidateAllocation(mDevice->getAllocator(), mAllocation, offset, invalidateSize) != VK_SUCCESS)
        {
            throw makeRuntimeError("VKBuffer::invalidateAllocation failed.");
        }
    }

    void VKBuffer::retainBindGroupReference()
    {
        incrementAtomicReference(mLifetime.bindGroupReferences);
        incrementAtomicReference(mLifetime.ownerReferences);
    }

    void VKBuffer::releaseBindGroupReference()
    {
        decrementAtomicReference(mLifetime.bindGroupReferences, "VKBuffer::releaseBindGroupReference");
        decrementAtomicReference(mLifetime.ownerReferences, "VKBuffer::releaseBindGroupReference");
    }

    void VKBuffer::retainCommandReference()
    {
        incrementAtomicReference(mLifetime.commandReferences);
        incrementAtomicReference(mLifetime.ownerReferences);
    }

    void VKBuffer::releaseCommandReference()
    {
        decrementAtomicReference(mLifetime.commandReferences, "VKBuffer::releaseCommandReference");
        decrementAtomicReference(mLifetime.ownerReferences, "VKBuffer::releaseCommandReference");
    }

    bool VKBuffer::requestUserDestroy()
    {
        bool expected = false;
        if (!mLifetime.destroyRequested.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
        {
            return false;
        }
        decrementAtomicReference(mLifetime.ownerReferences, "VKBuffer::requestUserDestroy");
        return true;
    }

    uint32_t VKBuffer::getTotalReferenceCount() const
    {
        return mLifetime.ownerReferences.load(std::memory_order_acquire);
    }

    uint32_t VKBuffer::getBindGroupReferenceCount() const
    {
        return mLifetime.bindGroupReferences.load(std::memory_order_acquire);
    }

    uint32_t VKBuffer::getCommandReferenceCount() const
    {
        return mLifetime.commandReferences.load(std::memory_order_acquire);
    }

    bool VKBuffer::isDestroyRequested() const
    {
        return mLifetime.destroyRequested.load(std::memory_order_acquire);
    }

    bool VKBuffer::isReadyForDestroy() const
    {
        return isDestroyRequested() && getTotalReferenceCount() == 0u;
    }

    bool VKBuffer::markPendingDestroyQueued()
    {
        bool expected = false;
        return mLifetime.pendingDestroyQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void VKBuffer::clearPendingDestroyQueued()
    {
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
    }

    void VKBuffer::destroy()
    {
        if (mDestroyed)
        {
            return;
        }

        mIsMapped = false;
        if (mBuffer && mAllocation != nullptr)
        {
            vmaDestroyBuffer(mDevice->getAllocator(), static_cast<VkBuffer>(mBuffer), mAllocation);
        }

        mBuffer = nullptr;
        mAllocation = nullptr;
        mAllocationInfo = {};
        mPersistentMappedData = nullptr;
        mMemoryProperties = 0;
        mLifetime.ownerReferences.store(0u, std::memory_order_release);
        mLifetime.bindGroupReferences.store(0u, std::memory_order_release);
        mLifetime.commandReferences.store(0u, std::memory_order_release);
        mLifetime.destroyRequested.store(true, std::memory_order_release);
        mLifetime.pendingDestroyQueued.store(false, std::memory_order_release);
        mDestroyed = true;
    }
} // namespace GVM::RHI::Vulkan
