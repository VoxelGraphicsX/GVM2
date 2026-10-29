#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <Metal/Metal.hpp>
#include <EASTL/fixed_vector.h>
#include <GVMRHI/Private/ArgumentPagePool.hpp>
#include <GVMRHI/Private/RenderPassRecorder.hpp>

namespace GVM::RHI::Metal
{
    class MDevice;
    class MArgumentPagePool;

    /** Retains one native allocation and caches its actual shader residency requirements. */
    struct MArgumentResourceUse
    {
        NS::SharedPtr<NS::Object> allocation;
        MTL::Resource *resource = nullptr;
        MTL::ResourceUsage usage = 0;
        MTL::RenderStages stages = 0;
        bool compute = false;
    };

    /** Records one shader-visible texture view captured by a snapshot together with its declared storage access mode and stage visibility. */
    struct MArgumentTextureUse
    {
        TextureView view;
        StorageTextureAccess access = StorageTextureAccess::Undefined;
        ShaderStageFlags visibility = ShaderStage::None;
    };

    /** Owns encoded inputs and native allocations independently of resource-pool handles. */
    class MArguments final : public ArgumentsImpl
    {
    public:
        /** Captures values and typed resources using the pipeline's immutable argument layout. */
        void init(MDevice &device, const ArgumentsDescriptor &descriptor);
        /** Releases the CPU snapshot's memory reservation when its last task or submission reference ends. */
        ~MArguments() override;
        /** Returns the layout that must match the selected pipeline before any native command is recorded. */
        const PipelineLayout &getLayout() const { return mLayout; }
        /** Returns the already encoded values, addresses, and byte lengths. */
        eastl::span<const uint8_t> getBytes() const { return mBytes; }
        /** Declares the buffer and texture access and stage visibility prepared for this snapshot. */
        void trackUsage(MTL::RenderCommandEncoder &encoder) const;
        /** Declares the indirect buffer and texture residency and access modes for a compute dispatch. */
        void trackUsage(MTL::ComputeCommandEncoder &encoder) const;
        /** Returns the shader-visible texture views captured by this snapshot for render-pass image-conflict checks. */
        eastl::span<const MArgumentTextureUse> getTextureUses() const { return mTextureUses; }
        /** Returns the render-stage texture uses of this snapshot in backend-neutral form for render-pass validation. */
        eastl::span<const Private::PassTextureUse> getRenderTextureUses() const { return mRenderTextureUses; }
        /** Returns the texture subresource uses this snapshot exposes to compute shaders. */
        eastl::span<const Private::PassTextureUse> getComputeTextureUses() const { return mComputeTextureUses; }
    private:
        /** Retains a captured GPU object and prepares its immutable stage and access requirements once. */
        void retainResource(NS::Object &allocation, MTL::Resource *resource, MTL::ResourceUsage usage, ShaderStageFlags visibility);
        PipelineLayout mLayout;
        eastl::fixed_vector<uint8_t, 128> mBytes;
        eastl::fixed_vector<MArgumentResourceUse, 8> mResources;
        eastl::fixed_vector<MArgumentTextureUse, 4, true> mTextureUses;
        eastl::fixed_vector<Private::PassTextureUse, 4, true> mRenderTextureUses;
        eastl::fixed_vector<Private::PassTextureUse, 4, true> mComputeTextureUses;
        eastl::intrusive_ptr<MArgumentPagePool> mPool;
        uint64_t mReserved = 0;
    };

    /** Identifies one immutable interval within a submission-owned Metal parameter page. */
    struct MArgumentAllocation
    {
        MTL::Buffer *buffer = nullptr;
        uint64_t offset = 0;
        Arguments arguments;
    };

    /** Owns one Metal parameter page tracked by the device-wide Args page pool. */
    struct MArgumentPage
    {
        NS::SharedPtr<MTL::Buffer> buffer;
        uint64_t size = 0;
    };

    /** Pools and bounds Metal parameter pages for one device; waits on asynchronous submission completion. */
    class MArgumentPagePool final : public Private::ArgumentPagePool<MArgumentPage>
    {
    public:
        /** Creates the device-wide pool; the device must outlive the pool. */
        explicit MArgumentPagePool(MDevice &device) : mDevice(device) {}
        /** Destroys pooled free pages while the device is still alive. */
        ~MArgumentPagePool() override { destroyFreePages(); }
    protected:
        /** Allocates one shared-mode Metal buffer for the page. */
        MArgumentPage createPage(uint64_t size) override;
        /** Releases the page's Metal buffer. */
        void destroyPage(MArgumentPage &page) override;
        /** Waits until an in-flight submission's completion handler releases pages. */
        bool waitForSubmittedWork() override
        {
            waitForInFlightDecrease();
            return true;
        }
    private:
        MDevice &mDevice;
    };

    /** Owns bounded parameter pages and snapshots until abandonment or native GPU completion. */
    class MArgumentStorage final : public RefCountedObject
    {
    public:
        /** Creates storage that draws pages from the device's argument page pool. */
        explicit MArgumentStorage(eastl::intrusive_ptr<MArgumentPagePool> pool);
        /** Releases page reservations when an unsubmitted encoder is discarded. */
        ~MArgumentStorage();
        /** Copies a snapshot into a new aligned interval; earlier intervals are never overwritten. */
        MArgumentAllocation allocate(MDevice &device, Arguments arguments);
        /** Moves this storage's page bytes from recording to in-flight once its encoder reaches the GPU. */
        void markSubmitted();
        /** Releases submission-owned pages and snapshots after GPU completion; recording must have ended. */
        void complete();
    private:
        eastl::intrusive_ptr<MArgumentPagePool> mPool;
        eastl::vector<MArgumentPage> mPages;
        eastl::vector<Arguments> mArguments;
        uint64_t mCursor = 0;
        uint64_t mReserved = 0;
        bool mSubmitted = false;
    };
}
