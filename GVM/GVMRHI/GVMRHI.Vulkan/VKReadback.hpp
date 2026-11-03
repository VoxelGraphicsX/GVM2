#pragma once

#include "VKCommon.hpp"
#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/atomic.h>
#include <EASTL/span.h>

namespace GVM::RHI::Vulkan
{
    class VKQueue;
    class VKResourceRegistry;

    /// Owns one queued buffer or texture readback; becomes ready once the submission executing the copy completes.
    class VKReadbackImpl final : public ReadbackImpl
    {
    public:
        /// Captures the queue and device pool lifetime before adopting or retaining a mapped allocation.
        explicit VKReadbackImpl(VKQueue &queue);
        /// Drops the source reference or retires private staging storage on the final readback release.
        ~VKReadbackImpl() override;
        /// Reports whether the copy has completed; callable from any thread.
        virtual bool isReady() const override;
        /// Blocks until the executing submission completes; throws std::logic_error if the queue has not submitted the read yet.
        virtual void wait() override;
        /// Returns the readback bytes; throws std::logic_error before isReady(). Direct reads alias the source buffer's mapped memory.
        virtual eastl::span<const uint8_t> getData() const override;

        /// Initializes a direct read over a host-visible source buffer's mapped memory without copying.
        void initDirect(const Buffer &sourceBuffer, uint64_t offset, uint64_t size);
        /// Initializes a staging read: the queue copies the source into stagingBuffer during a later submission and marks this readback ready.
        void initStaging(const Buffer &stagingBuffer, uint64_t size);
        /// Assigns the submission that executes the copy; called by the queue's tracker when the read is submitted.
        void assignSubmissionSerial(VKSubmissionSerial serial);
        /// Marks the copy complete; called by the queue's tracker once the executing submission retires.
        void markReady();

    private:
        VKQueue &mQueue;
        eastl::weak_ptr<VKResourceRegistry> mRegistry;
        Buffer mBuffer;
        uint64_t mOffset = 0u;
        uint64_t mSize = 0u;
        eastl::atomic<VKSubmissionSerial> mSubmissionSerial{UINT64_MAX};
        eastl::atomic<bool> mReady{false};
        bool mUsesDirectMapping = false;
    };

    using VKReadback = eastl::intrusive_ptr<VKReadbackImpl>;

} // namespace GVM::RHI::Vulkan
