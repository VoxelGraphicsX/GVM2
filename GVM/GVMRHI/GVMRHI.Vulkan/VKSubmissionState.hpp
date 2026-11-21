#pragma once
#include "VKSubmissionTimeline.hpp"
#include <GVMRHI/Private/SubmissionState.hpp>

namespace GVM::RHI::Vulkan
{
    /// Associates one accepted submission value with a retained queue timeline.
    class VKSubmissionState final : public Private::SubmissionState
    {
    public:
        /// Retains the timeline and value of work that the queue accepted.
        VKSubmissionState(eastl::shared_ptr<VKSubmissionTimeline> timeline, uint64_t value);
        /// Returns the monotonic timeline value signaled by this submission.
        uint64_t timelineValue() const noexcept override;
        /// Queries or waits without borrowing the queue or device wrapper.
        bool wait(uint64_t timeoutNanoseconds) const override;
        /// Deduplicates timeline targets and executes a single Vulkan 1.3 waitSemaphores(eAny) call.
        bool waitAny(
            eastl::span<const Submission> submissions,
            const Private::HostTimelineState *hostTimelineState,
            uint64_t hostTargetValue,
            uint64_t timeoutNanoseconds) const override;
        /// Returns the native GPU wait for a submission on the same retained device domain.
        vk::SemaphoreSubmitInfo dependency(const eastl::shared_ptr<DeviceLostState> &device) const;

    private:
        eastl::shared_ptr<VKSubmissionTimeline> mTimeline;
        uint64_t mValue;
    };
}
