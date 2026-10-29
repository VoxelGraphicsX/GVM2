#pragma once

#include "VKCommon.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/atomic.h>
#include <EASTL/span.h>

namespace GVM::RHI::Vulkan
{
    class VKQueue;
    class VKBuffer;

    /// Owns one queued buffer or texture readback; becomes ready once the submission executing the copy completes.
    class VKReadbackImpl final : public ReadbackImpl
    {
    public:
        /// Reports whether the copy has completed; callable from any thread.
        virtual bool isReady() const override;
        /// Blocks until the executing submission completes; throws std::logic_error if the queue has not submitted the read yet.
        virtual void wait() override;
        /// Returns the readback bytes; throws std::logic_error before isReady(). Direct reads alias the source buffer's mapped memory.
        virtual eastl::span<const uint8_t> getData() const override;

        /// Initializes a direct read over a host-visible source buffer's mapped memory (no copy, no submission dependency for data access).
        void initDirect(VKQueue *queue, const Buffer &sourceBuffer, uint64_t offset, uint64_t size);
        /// Initializes a staging read: the queue copies the source into stagingBuffer during a later submission and marks this readback ready.
        void initStaging(VKQueue *queue, const Buffer &stagingBuffer, uint64_t size);
        /// Assigns the submission that executes the copy; called by the queue's tracker when the read is submitted.
        void assignSubmissionSerial(VKSubmissionSerial serial);
        /// Marks the copy complete; called by the queue's tracker once the executing submission retires.
        void markReady();
        /// Returns the submission serial assigned to this readback.
        VKSubmissionSerial getSubmissionSerial() const;

    private:
        VKQueue *mQueue = nullptr;
        Buffer mSourceBuffer;
        Buffer mStagingBuffer;
        uint64_t mOffset = 0u;
        uint64_t mSize = 0u;
        VKSubmissionSerial mSubmissionSerial = 0u;
        /** Returns true once the owning device is lost; readbacks expose completed-only behavior after loss. */
        [[nodiscard]] bool isDeviceLost() const;
        eastl::atomic<bool> mReady{false};
        bool mUsesDirectMapping = false;
        bool mHasSubmissionSerial = false;
    };

    using VKReadback = eastl::intrusive_ptr<VKReadbackImpl>;

} // namespace GVM::RHI::Vulkan
