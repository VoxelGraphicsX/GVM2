#pragma once

#include "VKCommon.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    class VKBuffer final : public BufferImpl
    {
    public:
        VKBuffer() = default;

        void init(VKDevice &device, const BufferDescriptor &descriptor);

        uint64_t getStorageSize() const override;
        void map() override;
        void const *getConstMappedRange(uint64_t offset, uint64_t size) const override;
        void *getMappedRange(uint64_t offset, uint64_t size) const override;
        void unmap() override;
        void destroy() override;

        [[nodiscard]]
        vk::Buffer getNativeBuffer() const;

        [[nodiscard]]
        VmaAllocation getNativeAllocation() const;

        [[nodiscard]]
        VkDeviceMemory getNativeDeviceMemory() const;

        [[nodiscard]]
        VkDeviceSize getNativeAllocationOffset() const;

        [[nodiscard]]
        BufferUsageFlags getUsage() const;

        [[nodiscard]]
        vk::BufferUsageFlags getNativeUsageFlags() const;

        [[nodiscard]]
        bool supportsUniformBinding() const;

        [[nodiscard]]
        bool supportsStorageBinding() const;

        [[nodiscard]]
        bool isMapped() const;

        [[nodiscard]]
        bool isDestroyed() const;

        [[nodiscard]]
        bool isHostVisible() const;

        [[nodiscard]]
        bool isHostCoherent() const;

        [[nodiscard]]
        bool supportsCpuRead() const;

        [[nodiscard]]
        bool supportsCpuWrite() const;

        [[nodiscard]]
        void *getPersistentMappedData() const;

        void recordHostWrite();
        void flushAllocation(uint64_t offset, uint64_t size) const;
        void invalidateAllocation(uint64_t offset, uint64_t size) const;
        void retainBindGroupReference();
        void releaseBindGroupReference();
        void retainCommandReference();
        void releaseCommandReference();

        [[nodiscard]]
        bool requestUserDestroy();

        [[nodiscard]]
        uint32_t getTotalReferenceCount() const;

        [[nodiscard]]
        uint32_t getBindGroupReferenceCount() const;

        [[nodiscard]]
        uint32_t getCommandReferenceCount() const;

        [[nodiscard]]
        bool isDestroyRequested() const;

        [[nodiscard]]
        bool isReadyForDestroy() const;

        [[nodiscard]]
        bool markPendingDestroyQueued();

        void clearPendingDestroyQueued();

    private:
        VKDevice *mDevice = nullptr;
        BufferDescriptor mDescriptor = {};
        vk::Buffer mBuffer;
        vk::BufferUsageFlags mNativeUsageFlags = {};
        VmaAllocation mAllocation = nullptr;
        VmaAllocationInfo mAllocationInfo = {};
        void *mPersistentMappedData = nullptr;
        VkMemoryPropertyFlags mMemoryProperties = 0;
        VKAtomicResourceLifetimeState mLifetime = {};
        bool mIsMapped = false;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
