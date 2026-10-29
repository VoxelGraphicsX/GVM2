#pragma once

#include "VKCommon.hpp"
#include "VKTaskDependencyResolver.hpp"
#include <GVMRHI/Private/ArgumentPagePool.hpp>
#include <GVMRHI/Private/RenderPassRecorder.hpp>
#include <EASTL/fixed_vector.h>

namespace GVM::RHI::Vulkan
{
    class VKDevice;
    class VKArgumentPagePool;
    /** Captures immutable Args bytes and keeps every shader-reachable native allocation alive. */
    class VKArguments final : public ArgumentsImpl
    {
    public:
        /** Encodes ordinary values, buffer addresses, and cached resource slots using one immutable layout. */
        void init(VKDevice &device, const ArgumentsDescriptor &descriptor);
        /** Releases native allocation references and the CPU snapshot reservation. */
        ~VKArguments() override;
        /** Returns the device whose allocations and layout this snapshot owns. */
        VKDevice &getDevice() const { return *mDevice; }
        /** Returns the immutable layout selected when the task was created. */
        const PipelineLayout &getLayout() const { return mLayout; }
        /** Returns the encoded ordinary values, device addresses, byte lengths, and resource slots. */
        eastl::span<const uint8_t> getBytes() const { return mBytes; }
        /** Returns exact captured image-view access scopes for ordered synchronization. */
        eastl::span<const VKTaskDependencyResolver::TextureSubresourceSyncRequest> getTextureUses() const { return mTextureUses; }
        /** Returns the persistent resource table, or null for buffer-only Args. */
        vk::DescriptorSet getResourceTable() const;
        /** Returns captured buffer uses without advancing queue state or splitting overlapping ranges. */
        eastl::span<const VKTaskDependencyResolver::BufferRangeSyncRequest> getBufferUses() const { return mBufferUses; }
        /** Returns the render-stage texture uses of this snapshot in backend-neutral form for render-pass validation. */
        eastl::span<const Private::PassTextureUse> getRenderTextureUses() const { return mRenderTextureUses; }
        /** Returns the texture subresource uses this snapshot exposes to compute shaders. */
        eastl::span<const Private::PassTextureUse> getComputeTextureUses() const { return mComputeTextureUses; }
    private:
        /** Records an unmodified buffer use, grouping references so each native buffer is retained once. */
        void captureBufferUse(VKTaskDependencyResolver::BufferRangeSyncRequest use);
        VKDevice *mDevice = nullptr;
        PipelineLayout mLayout;
        eastl::fixed_vector<uint8_t, 128, true> mBytes;
        eastl::fixed_vector<VKTaskDependencyResolver::BufferRangeSyncRequest, 4, true> mBufferUses;
        eastl::fixed_vector<VKTaskDependencyResolver::TextureSubresourceSyncRequest, 4, true> mTextureUses;
        /// Binding's storage access, index-parallel to mTextureUses, merged bitwise when entries alias one view and layout.
        eastl::fixed_vector<StorageTextureAccess, 4, true> mTextureUseAccesses;
        eastl::fixed_vector<Private::PassTextureUse, 4, true> mRenderTextureUses;
        eastl::fixed_vector<Private::PassTextureUse, 4, true> mComputeTextureUses;
        eastl::fixed_vector<VKSampler *, 4, true> mSamplers;
        eastl::intrusive_ptr<VKArgumentPagePool> mPool;
        uint64_t mReserved = 0;
    };

    /** Identifies an immutable interval and borrows its snapshot from encoder-owned submission storage. */
    struct VKArgumentAllocation
    {
        vk::DeviceAddress address = 0;
        VKArguments *arguments = nullptr;
    };

    /** Owns one persistently mapped, device-addressable parameter page. */
    struct VKArgumentPage
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void *data = nullptr;
        vk::DeviceAddress address = 0;
        uint64_t size = 0;
        bool coherent = false;
    };

    /** Pools and bounds Vulkan parameter pages for one device; waits on the main queue's oldest submission. */
    class VKArgumentPagePool final : public Private::ArgumentPagePool<VKArgumentPage>
    {
    public:
        /** Creates the device-wide pool; the device must outlive the pool. */
        explicit VKArgumentPagePool(VKDevice &device) : mDevice(device) {}
        /** Destroys pooled free pages while the VMA allocator is still alive. */
        ~VKArgumentPagePool() override { destroyFreePages(); }
    protected:
        /** Allocates one persistently mapped device-address page through VMA. */
        VKArgumentPage createPage(uint64_t size) override;
        /** Destroys the page's VMA buffer. */
        void destroyPage(VKArgumentPage &page) override;
        /** Waits for the oldest pending submission on the main queue so its pages are released. */
        bool waitForSubmittedWork() override;
    private:
        VKDevice &mDevice;
    };

    /** Holds bounded parameter pages until encoder abandonment or timeline completion. */
    class VKArgumentStorage final
    {
    public:
        /** Creates submission storage drawing pages from the owning device's Args page pool. */
        explicit VKArgumentStorage(VKDevice &device);
        /** Prevents copying ownership of native parameter-page allocations. */
        VKArgumentStorage(const VKArgumentStorage &) = delete;
        /** Prevents assigning ownership of native parameter-page allocations. */
        VKArgumentStorage &operator=(const VKArgumentStorage &) = delete;
        /** Releases pages when an unsubmitted encoder is discarded. */
        ~VKArgumentStorage();
        /** Copies a validated snapshot to a fresh aligned interval and flushes non-coherent writes. */
        VKArgumentAllocation allocate(Arguments arguments);
        /** Moves this storage's page bytes from recording to in-flight once its encoder reaches the GPU. */
        void markSubmitted();
        /** Releases pages and captured allocations after native GPU execution has completed. */
        void complete();
    private:
        VKDevice &mDevice;
        eastl::intrusive_ptr<VKArgumentPagePool> mPool;
        eastl::vector<VKArgumentPage> mPages;
        eastl::vector<Arguments> mArguments;
        uint64_t mCursor = 0;
        uint64_t mReserved = 0;
        bool mSubmitted = false;
    };

    /** Keeps the internal eight-byte root compatible across all directly supported shader stages. */
    inline constexpr vk::ShaderStageFlags ArgumentShaderStages = vk::ShaderStageFlagBits::eVertex |
        vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;
}
