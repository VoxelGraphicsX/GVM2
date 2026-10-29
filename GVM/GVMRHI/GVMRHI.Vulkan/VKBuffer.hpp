#pragma once

#include "VKCommon.hpp"
#include "VKResourceLifetime.hpp"
#include "VKDefines.hpp"

namespace GVM::RHI::Vulkan
{
    /** Resolves a live buffer interval and checks device, usage, alignment, and bounds without retaining it. */
    VKBuffer &resolveBufferRange(VKDevice &device, BufferRange &range, BufferUsageFlags usage, uint32_t alignment);

    class VKBuffer final : public BufferImpl
    {
    public:
        VKBuffer() = default;

        void init(VKDevice &device, const BufferDescriptor &descriptor);
        /** Returns the owning logical device for argument capture. */
        VKDevice &getDevice() const { return *mDevice; }
        /** Returns the device address cached when shader-visible storage was allocated. */
        vk::DeviceAddress getDeviceAddress() const { return mDeviceAddress; }

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
        /** Stores the pool handle issued by the device right after allocation; used to enqueue this resource for destruction when its last reference is released. */
        void setHandle(Buffer handle);
        /** Returns the pool handle issued at creation. */
        [[nodiscard]] Buffer getHandle() const;
        /** Adds one holder reference keeping this resource alive past free*() until GPU work completes. */
        void retainReference();
        /** Drops one holder reference; enqueues the resource for destruction when the last one is released after free*(). */
        void releaseReference();
        /** Releases the user handle's reference exactly once; reports whether destruction may proceed. */
        [[nodiscard]] VKUserReleaseResult releaseUserReference();
        /** Reports whether free*() has been called on this resource. */
        [[nodiscard]] bool isDestroyRequested() const;
        /** Reports whether the user handle was released and no holder references remain. */
        [[nodiscard]] bool isDestroyable() const;
        /** Returns the live reference count, including the user handle's while it is held. */
        [[nodiscard]] uint32_t getReferenceCount() const;

    private:
        VKDevice *mDevice = nullptr;
        BufferDescriptor mDescriptor = {};
        vk::Buffer mBuffer;
        vk::DeviceAddress mDeviceAddress = 0;
        vk::BufferUsageFlags mNativeUsageFlags = {};
        VmaAllocation mAllocation = nullptr;
        VmaAllocationInfo mAllocationInfo = {};
        void *mPersistentMappedData = nullptr;
        VkMemoryPropertyFlags mMemoryProperties = 0;
        VKResourceLifetime mLifetime = {};
        Buffer mHandle = {};
        bool mIsMapped = false;
        bool mDestroyed = false;
    };
} // namespace GVM::RHI::Vulkan
