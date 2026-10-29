#pragma once

#include "MDefines.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/atomic.h>
#include <Metal/Metal.hpp>

namespace GVM::RHI::Metal
{
    class MQueue;

    /// Owns one queued buffer or texture readback; becomes ready once the command buffer executing the copy completes.
    class MReadbackImpl final : public ReadbackImpl
    {
    public:
        /// Releases the retained command buffer when the readback is destroyed without a wait.
        virtual ~MReadbackImpl() override;
        /// Reports whether the executing command buffer has completed; callable from any thread.
        virtual bool isReady() const override;
        /// Blocks until the executing command buffer completes; throws std::logic_error if the queue has not submitted the read yet.
        virtual void wait() override;
        /// Returns the readback bytes; throws std::logic_error before isReady(). Direct reads alias the source buffer's mapped memory.
        virtual eastl::span<const uint8_t> getData() const override;

        /// Initializes a direct read over a shared source buffer's mapped memory (no copy).
        void initDirect(MQueue *queue, const Buffer &sourceBuffer, uint64_t offset, uint64_t size);
        /// Initializes a staging read: the queue copies the source into stagingBuffer during a later submission.
        void initStaging(MQueue *queue, const Buffer &stagingBuffer, uint64_t size);
        /// Assigns the command buffer executing the copy; called by the queue when the readback submission commits.
        void assignCommandBuffer(MTL::CommandBuffer *commandBuffer);
        /// Marks the readback ready; called once the executing command buffer completes.
        void markReady();
        /// Returns the command buffer executing the copy.
        MTL::CommandBuffer *getCommandBuffer() const;

    private:
        /** Returns true once the owning device is lost; readbacks expose completed-only behavior after loss. */
        [[nodiscard]] bool isDeviceLost() const;

        MQueue *mQueue = nullptr;
        Buffer mSourceBuffer;
        Buffer mStagingBuffer;
        MTL::CommandBuffer *mCommandBuffer = nullptr;
        uint64_t mOffset = 0u;
        uint64_t mSize = 0u;
        eastl::atomic<bool> mReady{false};
        bool mUsesDirectMapping = false;
        bool mHasCommandBuffer = false;
    };

    using MReadback = eastl::intrusive_ptr<MReadbackImpl>;

} // namespace GVM::RHI::Metal
