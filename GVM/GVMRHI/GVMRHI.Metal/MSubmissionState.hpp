#pragma once
#include <GVMRHI/Private/SubmissionState.hpp>
#include <GVMRHI/Private/DeviceLostState.hpp>
#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <Metal/Metal.hpp>
#include <condition_variable>
#include <mutex>

namespace GVM::RHI::Metal
{
    /// Retains native completion and GPU dependency events independently of queue and device wrappers.
    class MSubmissionState final : public Private::SubmissionState, public eastl::enable_shared_from_this<MSubmissionState>
    {
    public:
        /// Captures every command buffer in one batch before any native commit occurs.
        MSubmissionState(eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> buffers,
            NS::SharedPtr<MTL::Event> event, eastl::shared_ptr<DeviceLostState> device);
        /// Registers weak completion notifications before committing the captured command buffers.
        void arm();
        /// Queries or waits for all captured native command buffers, including pending transfers.
        bool wait(uint64_t timeoutNanoseconds) const override;
        /// Validates the retained device domain and returns the event signaled by this submission.
        MTL::Event *dependency(const eastl::shared_ptr<DeviceLostState> &device) const;

    private:
        /// Reports native completion and promotes command-buffer failures to device loss.
        bool checkComplete() const;
        /// Wakes host waiters when Metal reports completion of any captured command buffer.
        void notifyCompletion();
        eastl::vector<NS::SharedPtr<MTL::CommandBuffer>> mBuffers;
        NS::SharedPtr<MTL::Event> mEvent;
        eastl::shared_ptr<DeviceLostState> mLostState;
        mutable std::mutex mMutex;
        mutable std::condition_variable mCompleted;
        uint64_t mNotifications = 0;
    };
}
