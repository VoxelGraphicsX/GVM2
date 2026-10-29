#include "VKBuffer.hpp"

#include "VKDevice.hpp"
#include "VKEnumUtils.hpp"
#include "VKQueue.hpp"
#include <EASTL/atomic.h>

#include <GVMRHI/Private/RHIBufferContracts.hpp>

#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    VKBuffer &resolveBufferRange(VKDevice &device, BufferRange &range, BufferUsageFlags usage, uint32_t alignment)
    {
        auto *source = dynamic_cast<VKBuffer *>(range.buffer.get());
        if (source == nullptr || source->isDestroyed() || source->isDestroyRequested() || &source->getDevice() != &device)
        {
            throw std::invalid_argument("Buffer operation requires a live buffer from the same device.");
        }
        Detail::validateEncoderBufferRange(source->getUsage(), source->getStorageSize(), range, usage, alignment);
        return *source;
    }

    namespace
    {
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
            throw std::invalid_argument("VKBuffer::init requires descriptor.size > 0.");
        }

        mDevice = &device;
        mDescriptor = descriptor;
        mLabelName = descriptor.label;

        vk::BufferUsageFlags usageFlags = translateBufferUsage(descriptor.usage);
        if (descriptor.usage & (BufferUsage::Storage | BufferUsage::Uniform))
        {
            usageFlags |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
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
            throw std::runtime_error("VKBuffer::init failed to allocate buffer memory.");
        }

        mBuffer = nativeBuffer;
        mDevice->setDebugObjectName(vk::ObjectType::eBuffer, reinterpret_cast<uint64_t>(static_cast<VkBuffer>(nativeBuffer)), mLabelName.c_str());
        mDeviceAddress = 0;
        if (usageFlags & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        {
            mDeviceAddress = device.getNativeDevice().getBufferAddress(vk::BufferDeviceAddressInfo{mBuffer});
        }
        mPersistentMappedData = mAllocationInfo.pMappedData;
        vmaGetAllocationMemoryProperties(device.getAllocator(), mAllocation, &mMemoryProperties);
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
            throw std::runtime_error("VKBuffer::map was called on a non-mappable buffer.");
        }
        if (!isHostVisible() || mPersistentMappedData == nullptr)
        {
            throw std::runtime_error("VKBuffer::map could not expose host-visible memory for this buffer.");
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
            throw std::logic_error("VKBuffer::getConstMappedRange requires the buffer to be mapped.");
        }

        const Detail::ResolvedMapRange range = Detail::resolveMappedRange(mDescriptor.size, offset, size);
        if (!range.valid)
        {
            throw std::out_of_range("VKBuffer::getConstMappedRange received an invalid mapped range.");
        }

        const auto *basePointer = static_cast<const uint8_t *>(mPersistentMappedData);
        return basePointer + offset;
    }

    void *VKBuffer::getMappedRange(uint64_t offset, uint64_t size) const
    {
        if (!mIsMapped || mPersistentMappedData == nullptr)
        {
            throw std::logic_error("VKBuffer::getMappedRange requires the buffer to be mapped.");
        }

        const Detail::ResolvedMapRange range = Detail::resolveMappedRange(mDescriptor.size, offset, size);
        if (!range.valid)
        {
            throw std::out_of_range("VKBuffer::getMappedRange received an invalid mapped range.");
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
        if (mDevice == nullptr)
        {
            throw std::runtime_error("VKBuffer::recordHostWrite requires a live owning Vulkan device.");
        }

        VKQueue *queue = mDevice->getMainQueueImpl();
        if (queue == nullptr)
        {
            throw std::runtime_error("VKBuffer::recordHostWrite requires a live owning Vulkan queue.");
        }

        queue->recordHostWrite(*this);
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
            throw std::runtime_error("VKBuffer::flushAllocation failed.");
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
            throw std::runtime_error("VKBuffer::invalidateAllocation failed.");
        }
    }

    void VKBuffer::setHandle(Buffer handle)
    {
        mHandle = handle;
    }

    Buffer VKBuffer::getHandle() const
    {
        return mHandle;
    }

    void VKBuffer::retainReference()
    {
        mLifetime.retain();
    }

    void VKBuffer::releaseReference()
    {
        if (mLifetime.release("VKBuffer::releaseReference"))
        {
            mDevice->getResourceRegistry().enqueueRetiredBuffer(mHandle);
        }
    }

    VKUserReleaseResult VKBuffer::releaseUserReference()
    {
        return mLifetime.releaseUser();
    }

    bool VKBuffer::isDestroyRequested() const
    {
        return mLifetime.isUserReleased();
    }

    bool VKBuffer::isDestroyable() const
    {
        return mLifetime.isDestroyable();
    }

    uint32_t VKBuffer::getReferenceCount() const
    {
        return mLifetime.getReferenceCount();
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
        mDeviceAddress = 0;
        mAllocation = nullptr;
        mAllocationInfo = {};
        mPersistentMappedData = nullptr;
        mMemoryProperties = 0;
        mDestroyed = true;
    }
} // namespace GVM::RHI::Vulkan
