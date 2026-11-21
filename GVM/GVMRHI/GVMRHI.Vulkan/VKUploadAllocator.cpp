#include "VKUploadAllocator.hpp"

#include "VKDevice.hpp"

#include <cstring>
#include <stdexcept>

namespace GVM::RHI::Vulkan
{
    namespace
    {
        /** Creates one persistently mapped upload staging page through VMA. */
        VKUploadPage createUploadPage(VKDevice *device, uint64_t size)
        {
            if (device == nullptr)
            {
                throw std::invalid_argument("VKUploadAllocator requires a valid Vulkan device.");
            }
            if (size == 0u)
            {
                throw std::invalid_argument("VKUploadAllocator requires a non-zero page size.");
            }

            VkBufferCreateInfo bufferCreateInfo = {};
            bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreateInfo.size = size;
            bufferCreateInfo.usage = static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eTransferSrc);
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocationCreateInfo = {};
            allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VKUploadPage page = {};
            page.size = size;
            VmaAllocationInfo allocationInfo = {};
            VkBuffer nativeBuffer = VK_NULL_HANDLE;
            if (vmaCreateBuffer(device->getAllocator(), &bufferCreateInfo, &allocationCreateInfo, &nativeBuffer, &page.allocation, &allocationInfo) != VK_SUCCESS)
            {
                throw std::runtime_error("VKUploadAllocator failed to allocate upload staging memory.");
            }
            page.buffer = nativeBuffer;
            device->setDebugObjectName(vk::ObjectType::eBuffer, reinterpret_cast<uint64_t>(static_cast<VkBuffer>(nativeBuffer)), "GVM upload page");
            page.mappedData = static_cast<uint8_t *>(allocationInfo.pMappedData);
            if (page.mappedData == nullptr)
            {
                vmaDestroyBuffer(device->getAllocator(), nativeBuffer, page.allocation);
                page.buffer = nullptr;
                page.allocation = nullptr;
                throw std::runtime_error("VKUploadAllocator expected persistently mapped host memory.");
            }
            return page;
        }

        /** Destroys one upload staging page's VMA buffer and allocation. */
        void destroyUploadPage(VKDevice *device, VKUploadPage &page)
        {
            if (device != nullptr && page.buffer && page.allocation != nullptr)
            {
                vmaDestroyBuffer(device->getAllocator(), static_cast<VkBuffer>(page.buffer), page.allocation);
            }
            page = {};
        }
    } // namespace

    VKUploadAllocator::~VKUploadAllocator() = default;

    void VKUploadAllocator::init(VKDevice *device)
    {
        if (device == nullptr)
        {
            throw std::invalid_argument("VKUploadAllocator::init requires a valid Vulkan device.");
        }

        mDevice = device;
        eastl::vector<VKUploadPage> pages = mPages.releaseAll();
        for (VKUploadPage &page : pages)
        {
            destroyUploadPage(mDevice, page);
        }
    }

    eastl::optional<VKUploadSlice> VKUploadAllocator::tryAllocateAndWrite(
        const void *data,
        uint64_t size,
        uint64_t alignment)
    {
        if (mDevice == nullptr)
        {
            throw std::runtime_error("VKUploadAllocator::allocateAndWrite was called before init.");
        }
        if (data == nullptr)
        {
            throw std::invalid_argument("VKUploadAllocator::allocateAndWrite requires non-null source data.");
        }
        if (size == 0)
        {
            throw std::invalid_argument("VKUploadAllocator::allocateAndWrite requires a non-zero allocation size.");
        }

        eastl::optional<Private::UploadSlice<VKUploadPage>> slice = mPages.tryAllocate(size, alignment);
        if (!slice.has_value())
        {
            return eastl::nullopt;
        }

        memcpy(slice->mappedData, data, size);
        if (vmaFlushAllocation(mDevice->getAllocator(), slice->page.allocation, slice->offset, size) != VK_SUCCESS)
        {
            throw std::runtime_error("VKUploadAllocator failed to flush upload memory.");
        }

        return VKUploadSlice{slice->page.buffer, slice->offset, slice->mappedData};
    }

    VKUploadSlice VKUploadAllocator::allocateAndWrite(const void *data, uint64_t size, uint64_t alignment)
    {
        if (auto existing = tryAllocateAndWrite(data, size, alignment))
        {
            return *existing;
        }
        mPages.addPage(createUploadPage(mDevice, Private::UploadPageAllocator<VKUploadPage>::pageSizeFor(size)));
        auto created = tryAllocateAndWrite(data, size, alignment);
        if (!created.has_value())
        {
            throw std::runtime_error("VKUploadAllocator failed to sub-allocate from a new upload page.");
        }
        return *created;
    }

    void VKUploadAllocator::onSubmission(VKSubmissionSerial serial)
    {
        mPages.markSubmitted(serial);
    }

    void VKUploadAllocator::onCompleted(uint64_t lastRetiredSubmissionId)
    {
        eastl::vector<VKUploadPage> evicted = mPages.reclaim(lastRetiredSubmissionId);
        for (VKUploadPage &page : evicted)
        {
            destroyUploadPage(mDevice, page);
        }
    }

    void VKUploadAllocator::destroy()
    {
        eastl::vector<VKUploadPage> pages = mPages.releaseAll();
        for (VKUploadPage &page : pages)
        {
            destroyUploadPage(mDevice, page);
        }
        mDevice = nullptr;
    }
} // namespace GVM::RHI::Vulkan
